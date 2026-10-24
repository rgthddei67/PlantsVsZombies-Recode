#include "OpenGLRenderer.h"

#include "../FileManager.h"
#include "../Logger.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>

namespace pvz {
	namespace {
		constexpr std::size_t kInitialBufferBytes = 256 * 1024; // 兼容路径初始动态缓冲，按峰值自动增长
		constexpr unsigned int kMatrixSsboBinding = 0; // batch 矩阵 SSBO 固定 binding，与 GLSL layout 一致

		std::string GlString(OpenGLApi& api, unsigned int name) {
			const auto* value = api.GetString(name);
			return value ? reinterpret_cast<const char*>(value) : "unavailable";
		}

		std::size_t GrowCapacity(std::size_t current, std::size_t required) {
			std::size_t capacity = std::max(current, kInitialBufferBytes);
			while (capacity < required) capacity *= 2;
			return capacity;
		}
	}

	OpenGLRenderer::~OpenGLRenderer() {
		Shutdown();
	}

	bool OpenGLRenderer::Initialize(SDL_Window* window, bool vsync, std::string& error) {
		if (!window) {
			error = "OpenGL 窗口为空";
			return false;
		}
		mWindow = window;
		mContext = SDL_GL_CreateContext(window);
		if (!mContext) {
			error = std::string("SDL_GL_CreateContext 失败: ") + SDL_GetError();
			Shutdown();
			return false;
		}
		if (SDL_GL_MakeCurrent(window, mContext) != 0) {
			error = std::string("SDL_GL_MakeCurrent 失败: ") + SDL_GetError();
			Shutdown();
			return false;
		}
		if (!mApi.Load(error)) {
			Shutdown();
			return false;
		}

		mApi.GetIntegerv(GL_MAJOR_VERSION, &mContextMajor);
		mApi.GetIntegerv(GL_MINOR_VERSION, &mContextMinor);
		mVendor = GlString(mApi, GL_VENDOR);
		mRendererName = GlString(mApi, GL_RENDERER);
		mVersion = GlString(mApi, GL_VERSION);
		mShadingLanguageVersion = GlString(mApi, GL_SHADING_LANGUAGE_VERSION);
#if defined(__ANDROID__)
		// GLES 与桌面 GL 的版本数字不能直接比较；移动端最低为 ES 3.0。
		if (mContextMajor < 3 || !mVersion.contains("OpenGL ES")) {
			error = "最低要求 OpenGL ES 3.0，设备报告: " + mVersion;
			Shutdown();
			return false;
		}
#else
		if (mContextMajor < 3 || (mContextMajor == 3 && mContextMinor < 3)) {
			error = "检测到 OpenGL " + std::to_string(mContextMajor) + "." + std::to_string(mContextMinor)
				+ "；最低要求是 OpenGL 3.3 Core";
			Shutdown();
			return false;
		}
		int profileMask = 0;
		mApi.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
		if ((profileMask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
			error = "驱动没有创建 OpenGL 3.3 Core profile Context";
			Shutdown();
			return false;
		}

#endif
		if (!CreatePrograms(error) || !CreateBuffers(error) || !ApplyVsync(vsync, error)) {
			Shutdown();
			return false;
		}
		// SSBO 是 OpenGL 4.3 Core 能力；此 Context 的快路初始化失败时，
		// 让 GameApp 销毁整个窗口并重建真正的 3.3 Context，避免能力状态含混。
#if !defined(__ANDROID__)
		if (mContextMajor > 4 || (mContextMajor == 4 && mContextMinor >= 3)) {
			std::string ssboError;
			if (!TryCreateSsboBatch(ssboError)) {
				error = "OpenGL 4.3 SSBO batch: " + ssboError;
				Shutdown();
				return false;
			}
		}
#endif
		mApi.Disable(GL_DEPTH_TEST);
		mApi.Disable(GL_CULL_FACE);
		mApi.Disable(GL_SCISSOR_TEST);
		mApi.Enable(GL_BLEND);
		mApi.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		RefreshDrawableSize();

		LOG_WARN("OpenGL") << "OpenGL Core ready: vendor=" << mVendor
			<< " renderer=" << mRendererName << " version=" << mVersion
			<< " GLSL=" << mShadingLanguageVersion
			<< " framebuffer=" << mDrawableWidth << "x" << mDrawableHeight
			<< " vsync=" << (mVsync ? "on" : "off")
			<< " batchPath=" << BatchPathName();
		return true;
	}

	void OpenGLRenderer::RefreshDrawableSize() {
		if (mWindow) SDL_GL_GetDrawableSize(mWindow, &mDrawableWidth, &mDrawableHeight);
	}

	void OpenGLRenderer::Shutdown() {
		if (mContext && mWindow) SDL_GL_MakeCurrent(mWindow, mContext);
		DestroySsboBatch();
		if (mApi.DeleteBuffers) {
			if (mVbo) mApi.DeleteBuffers(1, &mVbo);
			if (mIbo) mApi.DeleteBuffers(1, &mIbo);
		}
		if (mApi.DeleteVertexArrays && mVao) mApi.DeleteVertexArrays(1, &mVao);
		if (mApi.DeleteProgram) {
			if (mBatchProgram.id) mApi.DeleteProgram(mBatchProgram.id);
			if (mBatchColorizeProgram.id) mApi.DeleteProgram(mBatchColorizeProgram.id);
			if (mBatchLessColorizeProgram.id) mApi.DeleteProgram(mBatchLessColorizeProgram.id);
			if (mPoolProgram.id) mApi.DeleteProgram(mPoolProgram.id);
		}
		mVbo = mIbo = mVao = 0;
		mBatchProgram = {};
		mBatchColorizeProgram = {};
		mBatchLessColorizeProgram = {};
		mPoolProgram = {};
		mVboCapacity = mIboCapacity = 0;
		mContextMajor = mContextMinor = 0;
		mSequentialIndices.clear();
		mFrameOpen = false;
		if (mContext) {
			SDL_GL_DeleteContext(mContext);
			mContext = nullptr;
		}
		mWindow = nullptr;
	}

	/** 编译共享批次 shader；Android 仅转换语言前导，保持绘制和颜色算法一致。 */
	unsigned int OpenGLRenderer::CompileShader(const char* programName, unsigned int type,
		const std::string& source, std::string& error) {
		const unsigned int shader = mApi.CreateShader(type);
		std::string platformSource = source;
#if defined(__ANDROID__)
		const auto versionEnd = platformSource.find('\n');
		if (!platformSource.starts_with("#version 330 core") || versionEnd == std::string::npos) {
			error = std::string("GLES 仅接受共享 330 基线 shader: ") + programName;
			mApi.DeleteShader(shader);
			return 0;
		}
		platformSource.replace(0, versionEnd + 1,
			"#version 300 es\nprecision highp float;\nprecision highp int;\n");
#endif
		const char* text = platformSource.c_str();
		mApi.ShaderSource(shader, 1, &text, nullptr);
		mApi.CompileShader(shader);
		int compiled = 0;
		mApi.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled) return shader;

		int length = 0;
		mApi.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
		mApi.GetShaderInfoLog(shader, length, nullptr, &log[0]);
		error = std::string("OpenGL shader 编译失败 [") + programName + "]: " + log;
		LOG_ERROR("OpenGL") << error;
		mApi.DeleteShader(shader);
		return 0;
	}

	bool OpenGLRenderer::CreateProgram(const char* name, const char* vertexPath,
		const char* fragmentPath, Program& output, std::string& error) {
		const std::string vertexSource = FileManager::LoadFileAsString(vertexPath);
		std::string fragmentSource = FileManager::LoadFileAsString(fragmentPath);
		// GLSL 330 无 include 扩展；仅展开这两份由项目维护的共用片元函数。
		for (const char* file : { "water_effects.glsl", "primitive_coverage.glsl" }) {
			const std::string directive = std::string("#include \"") + file + "\"";
			if (const auto at = fragmentSource.find(directive); at != std::string::npos) {
				const std::string shared = FileManager::LoadFileAsString(std::string("Shader/opengl/") + file);
				if (shared.empty()) { error = std::string("shared shader missing: ") + file; return false; }
				fragmentSource.replace(at, directive.size(), shared);
			}
		}

		if (vertexSource.empty() || fragmentSource.empty()) {
			error = std::string("OpenGL shader 文件缺失 [") + name + "]: "
				+ vertexPath + " / " + fragmentPath;
			return false;
		}
		const unsigned int vertex = CompileShader(name, GL_VERTEX_SHADER, vertexSource, error);
		if (!vertex) return false;
		const unsigned int fragment = CompileShader(name, GL_FRAGMENT_SHADER, fragmentSource, error);
		if (!fragment) {
			mApi.DeleteShader(vertex);
			return false;
		}

		const unsigned int program = mApi.CreateProgram();
		mApi.AttachShader(program, vertex);
		mApi.AttachShader(program, fragment);
		mApi.LinkProgram(program);
		mApi.DeleteShader(vertex);
		mApi.DeleteShader(fragment);
		int linked = 0;
		mApi.GetProgramiv(program, GL_LINK_STATUS, &linked);
		if (!linked) {
			int length = 0;
			mApi.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
			std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
			mApi.GetProgramInfoLog(program, length, nullptr, &log[0]);
			error = std::string("OpenGL shader 链接失败 [") + name + "]: " + log;
			LOG_ERROR("OpenGL") << error;
			mApi.DeleteProgram(program);
			return false;
		}

		output.id = program;
		output.projectionView = mApi.GetUniformLocation(program, "uProjectionView");
		output.framebufferHeight = mApi.GetUniformLocation(program, "uFramebufferHeight");
		output.texture = mApi.GetUniformLocation(program, "uTexture");
		output.poolLayer = mApi.GetUniformLocation(program, "uPoolLayer");
		output.poolCounter = mApi.GetUniformLocation(program, "uPoolCounter");
		return true;
	}

	bool OpenGLRenderer::CreatePrograms(std::string& error) {
		return CreateProgram("batch", "Shader/opengl/batch.vert.glsl",
			"Shader/opengl/batch.frag.glsl", mBatchProgram, error)
			&& CreateProgram("batch_colorize", "Shader/opengl/batch.vert.glsl",
				"Shader/opengl/batch_colorize.frag.glsl", mBatchColorizeProgram, error)
			&& CreateProgram("batch_less_colorize", "Shader/opengl/batch.vert.glsl",
				"Shader/opengl/batch_less_colorize.frag.glsl", mBatchLessColorizeProgram, error)
			&& CreateProgram("pool", "Shader/opengl/pool.vert.glsl",
				"Shader/opengl/pool.frag.glsl", mPoolProgram, error);
	}

	bool OpenGLRenderer::CreateBuffers(std::string& error) {
		mApi.GenVertexArrays(1, &mVao);
		mApi.GenBuffers(1, &mVbo);
		mApi.GenBuffers(1, &mIbo);
		if (!mVao || !mVbo || !mIbo) {
			error = "OpenGL 动态 VAO/VBO/IBO 创建失败";
			return false;
		}
		mApi.BindVertexArray(mVao);
		mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
		mApi.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
		const int stride = static_cast<int>(sizeof(OpenGLVertex));
		auto attribute = [&](unsigned int index, int count, std::size_t offset) {
			mApi.EnableVertexAttribArray(index);
			mApi.VertexAttribPointer(index, count, GL_FLOAT, GL_FALSE, stride,
				reinterpret_cast<const void*>(offset));
		};
		attribute(0, 2, offsetof(OpenGLVertex, x));
		attribute(1, 2, offsetof(OpenGLVertex, u));
		attribute(2, 4, offsetof(OpenGLVertex, r));
		attribute(3, 4, offsetof(OpenGLVertex, clipLeft));
		attribute(4, 1, offsetof(OpenGLVertex, drawMode));
		mApi.BindVertexArray(0);
		return EnsureBufferCapacity(kInitialBufferBytes, kInitialBufferBytes);
	}

	/** 为 4.3 Context 创建矩阵 SSBO 与独立 VAO；失败不影响 3.3 CPU 后备路径。 */
	bool OpenGLRenderer::TryCreateSsboBatch(std::string& error) {
		if (!CreateProgram("batch_ssbo", "Shader/opengl/batch_ssbo.vert.glsl",
			"Shader/opengl/batch.frag.glsl", mSsboBatchProgram, error)
			|| !CreateProgram("batch_ssbo_colorize", "Shader/opengl/batch_ssbo.vert.glsl",
				"Shader/opengl/batch_colorize.frag.glsl", mSsboBatchColorizeProgram, error)
			|| !CreateProgram("batch_ssbo_less_colorize", "Shader/opengl/batch_ssbo.vert.glsl",
				"Shader/opengl/batch_less_colorize.frag.glsl", mSsboBatchLessColorizeProgram, error)) {
			return false;
		}

		mApi.GenVertexArrays(1, &mSsboVao);
		mApi.GenBuffers(1, &mMatrixSsbo);
		if (!mSsboVao || !mMatrixSsbo) {
			error = "OpenGL SSBO batch VAO/矩阵缓冲创建失败";
			return false;
		}

		mApi.BindVertexArray(mSsboVao);
		mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
		const int stride = static_cast<int>(sizeof(OpenGLSsboBatchVertex));
		auto floatAttribute = [&](unsigned int index, int count, std::size_t offset) {
			mApi.EnableVertexAttribArray(index);
			mApi.VertexAttribPointer(index, count, GL_FLOAT, GL_FALSE, stride,
				reinterpret_cast<const void*>(offset));
		};
		auto uintAttribute = [&](unsigned int index, int count, std::size_t offset) {
			mApi.EnableVertexAttribArray(index);
			mApi.VertexAttribIPointer(index, count, GL_UNSIGNED_INT, stride,
				reinterpret_cast<const void*>(offset));
		};
		floatAttribute(0, 2, offsetof(OpenGLSsboBatchVertex, x));
		floatAttribute(1, 2, offsetof(OpenGLSsboBatchVertex, u));
		floatAttribute(2, 4, offsetof(OpenGLSsboBatchVertex, r));
		uintAttribute(3, 2, offsetof(OpenGLSsboBatchVertex, clipMinXY));
		uintAttribute(4, 1, offsetof(OpenGLSsboBatchVertex, matrixIndex));
		floatAttribute(5, 1, offsetof(OpenGLSsboBatchVertex, blendMode));
		mApi.BindVertexArray(0);
		if (!EnsureSsboCapacity(kInitialBufferBytes)) {
			error = "OpenGL 矩阵 SSBO 初始分配失败";
			return false;
		}
		mSsboBatchEnabled = true;
		return true;
	}

	/** 只销毁可选 SSBO 快路资源，保留基线 OpenGL Context 与 CPU Batch。 */
	void OpenGLRenderer::DestroySsboBatch() {
		mSsboBatchEnabled = false;
		if (mApi.DeleteBuffers && mMatrixSsbo) mApi.DeleteBuffers(1, &mMatrixSsbo);
		if (mApi.DeleteVertexArrays && mSsboVao) mApi.DeleteVertexArrays(1, &mSsboVao);
		if (mApi.DeleteProgram) {
			if (mSsboBatchProgram.id) mApi.DeleteProgram(mSsboBatchProgram.id);
			if (mSsboBatchColorizeProgram.id) mApi.DeleteProgram(mSsboBatchColorizeProgram.id);
			if (mSsboBatchLessColorizeProgram.id) mApi.DeleteProgram(mSsboBatchLessColorizeProgram.id);
		}
		mMatrixSsbo = mSsboVao = 0;
		mSsboCapacity = 0;
		mSsboBatchProgram = {};
		mSsboBatchColorizeProgram = {};
		mSsboBatchLessColorizeProgram = {};
	}

	bool OpenGLRenderer::EnsureBufferCapacity(std::size_t vertexBytes, std::size_t indexBytes) {
		if (vertexBytes > mVboCapacity) {
			mVboCapacity = GrowCapacity(mVboCapacity, vertexBytes);
			mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
			mApi.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mVboCapacity), nullptr, GL_DYNAMIC_DRAW);
		}
		if (indexBytes > mIboCapacity) {
			mIboCapacity = GrowCapacity(mIboCapacity, indexBytes);
			mApi.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
			mApi.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mIboCapacity), nullptr, GL_DYNAMIC_DRAW);
		}
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::EnsureSsboCapacity(std::size_t bytes) {
		if (bytes <= mSsboCapacity) return true;
		mSsboCapacity = GrowCapacity(mSsboCapacity, bytes);
		mApi.BindBuffer(GL_SHADER_STORAGE_BUFFER, mMatrixSsbo);
		mApi.BufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(mSsboCapacity),
			nullptr, GL_DYNAMIC_DRAW);
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::BeginFrame(float clearR, float clearG, float clearB, float clearA,
		int logicalWidth, int logicalHeight, float letterboxScale,
		float letterboxOffsetX, float letterboxOffsetY) {
		if (!mContext || mFrameOpen || logicalWidth <= 0 || logicalHeight <= 0) return false;
		RefreshDrawableSize();
		if (mDrawableWidth <= 0 || mDrawableHeight <= 0) return false;

		const int viewportX = static_cast<int>(letterboxOffsetX + 0.5f);
		const int viewportW = std::max(1, static_cast<int>(logicalWidth * letterboxScale + 0.5f));
		const int viewportH = std::max(1, static_cast<int>(logicalHeight * letterboxScale + 0.5f));
		const int top = static_cast<int>(letterboxOffsetY + 0.5f);
		const int viewportY = mDrawableHeight - top - viewportH;
		mApi.Viewport(viewportX, viewportY, viewportW, viewportH);
		mApi.Disable(GL_SCISSOR_TEST);
		mApi.ClearColor(clearR, clearG, clearB, clearA);
		mApi.Clear(GL_COLOR_BUFFER_BIT);
		mFrameStats = {};
		mFrameStart = std::chrono::steady_clock::now();
		mFrameOpen = true;
		return true;
	}

	void OpenGLRenderer::ApplyBlend(bool additive) {
		mApi.Enable(GL_BLEND);
		mApi.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		if (additive) {
			mApi.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
		}
		else {
			mApi.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
				GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}
	}

	bool OpenGLRenderer::UploadAndDraw(Program& program, std::uint32_t texture,
		bool additive, const OpenGLVertex* vertices, std::size_t vertexCount,
		const glm::mat4& projectionView) {
		if (!mFrameOpen || !vertices || vertexCount == 0 || vertexCount > 0x7FFFFFFFu) return false;
		const std::size_t vertexBytes = vertexCount * sizeof(OpenGLVertex);
		const std::size_t indexBytes = vertexCount * sizeof(std::uint32_t);
		if (!EnsureBufferCapacity(vertexBytes, indexBytes)) return false;
		if (mSequentialIndices.size() < vertexCount) {
			const std::size_t oldSize = mSequentialIndices.size();
			mSequentialIndices.resize(vertexCount);
			for (std::size_t i = oldSize; i < vertexCount; ++i) {
				mSequentialIndices[i] = static_cast<std::uint32_t>(i);
			}
		}

		mApi.BindVertexArray(mVao);
		mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
		mApi.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mVboCapacity), nullptr, GL_DYNAMIC_DRAW);
		mApi.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexBytes), vertices);
		mApi.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
		mApi.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mIboCapacity), nullptr, GL_DYNAMIC_DRAW);
		mApi.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indexBytes),
			mSequentialIndices.data());
		mApi.UseProgram(program.id);
		mApi.UniformMatrix4fv(program.projectionView, 1, GL_FALSE, glm::value_ptr(projectionView));
		mApi.Uniform1f(program.framebufferHeight, static_cast<float>(mDrawableHeight));
		mApi.ActiveTexture(GL_TEXTURE0);
		mApi.BindTexture(GL_TEXTURE_2D, texture);
		mApi.Uniform1i(program.texture, 0);
		ApplyBlend(additive);
		mApi.DrawElements(GL_TRIANGLES, static_cast<int>(vertexCount), GL_UNSIGNED_INT, nullptr);

		mFrameStats.quadCount += static_cast<std::uint32_t>(vertexCount / 6);
		++mFrameStats.batchCount;
		++mFrameStats.drawCallCount;
		mFrameStats.peakVboBytes = std::max(mFrameStats.peakVboBytes, vertexBytes);
		mFrameStats.peakIboBytes = std::max(mFrameStats.peakIboBytes, indexBytes);
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::UploadCpuBatch(const OpenGLVertex* vertices, std::size_t vertexCount) {
		mCpuBatchVertexCount = 0;
		if (!mFrameOpen || !vertices || vertexCount == 0 || vertexCount > 0x7FFFFFFFu) return false;
		const std::size_t bytes = vertexCount * sizeof(OpenGLVertex);
		mVboCapacity = GrowCapacity(mVboCapacity, bytes);
		mApi.BindVertexArray(mVao);
		mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
		// 每个完整 batch 仅 orphan 一次；纹理分段共享上传结果，不在小 draw 间反复申请大缓冲。
		mApi.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mVboCapacity), nullptr, GL_DYNAMIC_DRAW);
		mApi.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices);
		if (mApi.GetError() != GL_NO_ERROR) return false;
		mCpuBatchVertexCount = vertexCount;
		++mFrameStats.cpuBatchUploadCount;
		mFrameStats.peakVboBytes = std::max(mFrameStats.peakVboBytes, bytes);
		return true;
	}

	bool OpenGLRenderer::SubmitCpuBatchSegment(std::uint32_t texture, bool additive, bool washedOut,
		bool lessWashedOut,
		std::size_t firstVertex, std::size_t vertexCount,
		const glm::mat4& projectionView, bool textureBoundary, bool stateBoundary) {
		if (!mFrameOpen || vertexCount == 0 || firstVertex > mCpuBatchVertexCount
			|| vertexCount > mCpuBatchVertexCount - firstVertex) return false;
		if (textureBoundary) ++mFrameStats.textureFlushCount;
		if (stateBoundary) ++mFrameStats.stateFlushCount;
		Program& program = lessWashedOut ? mBatchLessColorizeProgram
			: washedOut ? mBatchColorizeProgram : mBatchProgram;
		mApi.UseProgram(program.id);
		mApi.UniformMatrix4fv(program.projectionView, 1, GL_FALSE, glm::value_ptr(projectionView));
		mApi.Uniform1f(program.framebufferHeight, static_cast<float>(mDrawableHeight));
		mApi.ActiveTexture(GL_TEXTURE0);
		mApi.BindTexture(GL_TEXTURE_2D, texture);
		mApi.Uniform1i(program.texture, 0);
		ApplyBlend(additive);
		// CPU 数据已经按三角形展开；旧索引始终是 0,1,2,...，DrawArrays 可免去等价索引上传。
		mApi.DrawArrays(GL_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
		mFrameStats.quadCount += static_cast<std::uint32_t>(vertexCount / 6);
		++mFrameStats.batchCount;
		++mFrameStats.drawCallCount;
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::UploadSsboBatch(const OpenGLSsboBatchVertex* vertices,
		std::size_t vertexCount, const glm::mat4* matrices, std::size_t matrixCount) {
		if (!mSsboBatchEnabled || !mFrameOpen || !vertices || !matrices
			|| vertexCount == 0 || matrixCount == 0) return false;
		const std::size_t vertexBytes = vertexCount * sizeof(OpenGLSsboBatchVertex);
		const std::size_t matrixBytes = matrixCount * sizeof(glm::mat4);
		if (!EnsureBufferCapacity(vertexBytes, 0) || !EnsureSsboCapacity(matrixBytes)) return false;

		mApi.BindVertexArray(mSsboVao);
		mApi.BindBuffer(GL_ARRAY_BUFFER, mVbo);
		mApi.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mVboCapacity), nullptr, GL_DYNAMIC_DRAW);
		mApi.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexBytes), vertices);
		mApi.BindBuffer(GL_SHADER_STORAGE_BUFFER, mMatrixSsbo);
		mApi.BufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(mSsboCapacity), nullptr, GL_DYNAMIC_DRAW);
		mApi.BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(matrixBytes), matrices);
		mApi.BindBufferBase(GL_SHADER_STORAGE_BUFFER, kMatrixSsboBinding, mMatrixSsbo);
		mFrameStats.peakVboBytes = std::max(mFrameStats.peakVboBytes, vertexBytes);
		mFrameStats.peakSsboBytes = std::max(mFrameStats.peakSsboBytes, matrixBytes);
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::SubmitSsboBatchSegment(std::uint32_t texture, bool additive,
		bool washedOut, bool lessWashedOut, std::size_t firstVertex, std::size_t vertexCount,
		const glm::mat4& projectionView, bool textureBoundary, bool stateBoundary) {
		if (!mSsboBatchEnabled || !mFrameOpen || vertexCount == 0
			|| firstVertex > 0x7FFFFFFFu || vertexCount > 0x7FFFFFFFu) return false;
		if (textureBoundary) ++mFrameStats.textureFlushCount;
		if (stateBoundary) ++mFrameStats.stateFlushCount;
		Program& program = lessWashedOut ? mSsboBatchLessColorizeProgram
			: washedOut ? mSsboBatchColorizeProgram : mSsboBatchProgram;
		mApi.BindVertexArray(mSsboVao);
		mApi.UseProgram(program.id);
		mApi.UniformMatrix4fv(program.projectionView, 1, GL_FALSE, glm::value_ptr(projectionView));
		mApi.Uniform1f(program.framebufferHeight, static_cast<float>(mDrawableHeight));
		mApi.ActiveTexture(GL_TEXTURE0);
		mApi.BindTexture(GL_TEXTURE_2D, texture);
		mApi.Uniform1i(program.texture, 0);
		ApplyBlend(additive);
		mApi.DrawArrays(GL_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
		mFrameStats.quadCount += static_cast<std::uint32_t>(vertexCount / 6);
		++mFrameStats.batchCount;
		++mFrameStats.drawCallCount;
		return mApi.GetError() == GL_NO_ERROR;
	}

	bool OpenGLRenderer::SubmitPoolLayer(std::uint32_t texture, int layer, float poolCounter,
		const OpenGLVertex* vertices, std::size_t vertexCount,
		const glm::mat4& projectionView) {
		mApi.UseProgram(mPoolProgram.id);
		mApi.Uniform1f(mPoolProgram.poolLayer, static_cast<float>(layer));
		mApi.Uniform1f(mPoolProgram.poolCounter, poolCounter);
		++mFrameStats.stateFlushCount;
		return UploadAndDraw(mPoolProgram, texture, false, vertices, vertexCount, projectionView);
	}

	bool OpenGLRenderer::ApplyVsync(bool vsync, std::string& error) {
		if (!mContext) {
			error = "OpenGL Context 未初始化";
			return false;
		}
		if (SDL_GL_SetSwapInterval(vsync ? 1 : 0) != 0) {
			error = std::string("SDL_GL_SetSwapInterval 失败: ") + SDL_GetError();
			return false;
		}
		mVsync = vsync;
		return true;
	}

	CaptureTicket OpenGLRenderer::RequestCapture(const std::string& pngPath) {
		const CaptureTicket ticket = mNextCaptureTicket++;
		auto& record = mCaptureRecords[ticket];
		if (pngPath.empty()) {
			record.status = CaptureStatus::Failed;
			record.error = "截图路径为空";
			return ticket;
		}
		if (mPendingCaptureTicket != 0) {
			record.status = CaptureStatus::Failed;
			record.error = "已有截图请求等待处理";
			return ticket;
		}
		mPendingCaptureTicket = ticket;
		mCapturePath = pngPath;
		return ticket;
	}

	CaptureStatus OpenGLRenderer::GetCaptureStatus(CaptureTicket ticket) const {
		const auto found = mCaptureRecords.find(ticket);
		return found == mCaptureRecords.end() ? CaptureStatus::Unknown : found->second.status;
	}

	std::string OpenGLRenderer::GetCaptureError(CaptureTicket ticket) const {
		const auto found = mCaptureRecords.find(ticket);
		return found == mCaptureRecords.end() ? "未知截图 ticket" : found->second.error;
	}

	void OpenGLRenderer::CompleteCapture(bool succeeded, const std::string& error) {
		if (!mPendingCaptureTicket) return;
		auto& record = mCaptureRecords[mPendingCaptureTicket];
		record.status = succeeded ? CaptureStatus::Succeeded : CaptureStatus::Failed;
		record.error = error;
		mPendingCaptureTicket = 0;
		mCapturePath.clear();
	}

	bool OpenGLRenderer::ProcessCapture() {
		if (!mPendingCaptureTicket) return true;
		if (mDrawableWidth <= 0 || mDrawableHeight <= 0) {
			CompleteCapture(false, "OpenGL framebuffer 尺寸无效");
			return false;
		}
		const std::size_t rowBytes = static_cast<std::size_t>(mDrawableWidth) * 4;
		std::vector<std::uint8_t> bottomUp(rowBytes * static_cast<std::size_t>(mDrawableHeight));
		std::vector<std::uint8_t> topDown(bottomUp.size());
		mApi.ReadBuffer(GL_BACK);
		mApi.PixelStorei(GL_PACK_ALIGNMENT, 1);
		mApi.ReadPixels(0, 0, mDrawableWidth, mDrawableHeight,
			GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
		if (mApi.GetError() != GL_NO_ERROR) {
			CompleteCapture(false, "glReadPixels 失败");
			return false;
		}
		for (int y = 0; y < mDrawableHeight; ++y) {
			std::memcpy(topDown.data() + static_cast<std::size_t>(y) * rowBytes,
				bottomUp.data() + static_cast<std::size_t>(mDrawableHeight - 1 - y) * rowBytes,
				rowBytes);
		}
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(topDown.data(),
			mDrawableWidth, mDrawableHeight, 32, static_cast<int>(rowBytes), SDL_PIXELFORMAT_ABGR8888);
		if (!surface) {
			CompleteCapture(false, std::string("SDL 截图 surface 创建失败: ") + SDL_GetError());
			return false;
		}
		const int saved = IMG_SavePNG(surface, mCapturePath.c_str());
		SDL_FreeSurface(surface);
		if (saved != 0) {
			CompleteCapture(false, std::string("IMG_SavePNG 失败: ") + IMG_GetError());
			return false;
		}
		CompleteCapture(true);
		return true;
	}

	bool OpenGLRenderer::EndFrame() {
		if (!mFrameOpen) return false;
		ProcessCapture();
		SDL_GL_SwapWindow(mWindow);
		mFrameStats.frameMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - mFrameStart).count();
		mLastStats = mFrameStats;
		mFrameOpen = false;
		return true;
	}

} // namespace pvz
