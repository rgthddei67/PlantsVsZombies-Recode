#pragma once

#include "OpenGLApi.h"
#include "RenderBackend.h"

#include <SDL2/SDL.h>
#ifdef DrawText
#undef DrawText
#endif
#include <glm/glm.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pvz {

	/** CPU 已完成对象矩阵展开的普通 OpenGL 顶点。 */
	struct OpenGLVertex {
		float x = 0.0f;
		float y = 0.0f;
		float u = 0.0f;
		float v = 0.0f;
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
		float clipLeft = 0.0f;
		float clipTop = 0.0f;
		float clipRight = 65535.0f;
		float clipBottom = 65535.0f;
		float drawMode = 0.0f; // 与 BatchVertex.blendMode 相同的混合/解析图元编码
	};
	static_assert(sizeof(OpenGLVertex) == 52, "OpenGLVertex must be 52 bytes");

	/** 与 Graphics::BatchVertex 字节对齐的 SSBO 快路顶点；未使用字段仍保留以共享 CPU 提交数组。 */
	struct OpenGLSsboBatchVertex {
		float x = 0.0f;
		float y = 0.0f;
		float u = 0.0f;
		float v = 0.0f;
		std::uint32_t texture = 0;
		std::uint32_t matrixIndex = 0;
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
		float blendMode = 0.0f;
		std::uint32_t clipMinXY = 0;
		std::uint32_t clipMaxXY = 0xFFFFFFFFu;
	};
	static_assert(sizeof(OpenGLSsboBatchVertex) == 52,
		"OpenGLSsboBatchVertex must be 52 bytes");

	struct OpenGLFrameStats {
		std::uint32_t quadCount = 0;
		std::uint32_t batchCount = 0;
		std::uint32_t drawCallCount = 0;
		std::uint32_t cpuBatchUploadCount = 0;
		std::uint32_t textureFlushCount = 0;
		std::uint32_t stateFlushCount = 0;
		std::size_t peakVboBytes = 0;
		std::size_t peakIboBytes = 0;
		std::size_t peakSsboBytes = 0;
		double frameMilliseconds = 0.0;
	};

	/** OpenGL Core 帧、可选 4.3 SSBO Batch、3.3 CPU Batch、截图和交换控制。 */
	class OpenGLRenderer final : public CaptureBackend {
	public:
		OpenGLRenderer() = default;
		~OpenGLRenderer() override;

		OpenGLRenderer(const OpenGLRenderer&) = delete;
		OpenGLRenderer& operator=(const OpenGLRenderer&) = delete;

		bool Initialize(SDL_Window* window, bool vsync, std::string& error);
		void Shutdown();

		bool BeginFrame(float clearR, float clearG, float clearB, float clearA,
			int logicalWidth, int logicalHeight, float letterboxScale,
			float letterboxOffsetX, float letterboxOffsetY);
		bool EndFrame();
		bool ApplyVsync(bool vsync, std::string& error);
		/** 重新查询高 DPI drawable 尺寸；全屏切换后必须先刷新再计算 letterbox。 */
		void RefreshDrawableSize();

		/** 一次上传 CPU 展开的完整 batch；后续分段绘制前不得插入 Pool 或其他缓冲上传。 */
		bool UploadCpuBatch(const OpenGLVertex* vertices, std::size_t vertexCount);
		/** 按原顺序绘制已上传的连续三角形段，纹理切换不再重分配/上传缓冲。 */
		bool SubmitCpuBatchSegment(std::uint32_t texture, bool additive, bool washedOut,
			bool lessWashedOut,
			std::size_t firstVertex, std::size_t vertexCount,
			const glm::mat4& projectionView,
			bool textureBoundary, bool stateBoundary);

		/** 一次上传完整顶点/矩阵数组；后续按原顺序仅分段 draw，避免重复上传 SSBO。 */
		bool UploadSsboBatch(const OpenGLSsboBatchVertex* vertices, std::size_t vertexCount,
			const glm::mat4* matrices, std::size_t matrixCount);
		/** 绘制已上传 SSBO batch 的连续三角形段。 */
		bool SubmitSsboBatchSegment(std::uint32_t texture, bool additive, bool washedOut,
			bool lessWashedOut, std::size_t firstVertex, std::size_t vertexCount,
			const glm::mat4& projectionView, bool textureBoundary, bool stateBoundary);

		/** Pool 专用 GLSL 330 路径；顶点中的 UV 同时是规则网格坐标。 */
		bool SubmitPoolLayer(std::uint32_t texture, int layer, float poolCounter,
			const OpenGLVertex* vertices, std::size_t vertexCount,
			const glm::mat4& projectionView);

		CaptureTicket RequestCapture(const std::string& pngPath) override;
		CaptureStatus GetCaptureStatus(CaptureTicket ticket) const override;
		std::string GetCaptureError(CaptureTicket ticket) const override;

		OpenGLApi* Api() { return &mApi; }
		const OpenGLFrameStats& LastFrameStats() const { return mLastStats; }
		int DrawableWidth() const { return mDrawableWidth; }
		int DrawableHeight() const { return mDrawableHeight; }
		bool IsFrameOpen() const { return mFrameOpen; }
		bool IsVsyncEnabled() const { return mVsync; }
		bool SupportsSsboBatch() const { return mSsboBatchEnabled; }
		const char* BatchPathName() const { return mSsboBatchEnabled ? "ssbo" : "cpu"; }
		int ContextMajorVersion() const { return mContextMajor; }
		int ContextMinorVersion() const { return mContextMinor; }
		const std::string& Vendor() const { return mVendor; }
		const std::string& RendererName() const { return mRendererName; }
		const std::string& Version() const { return mVersion; }
		const std::string& ShadingLanguageVersion() const { return mShadingLanguageVersion; }

	private:
		struct Program {
			unsigned int id = 0;
			int projectionView = -1;
			int framebufferHeight = -1;
			int texture = -1;
			int poolLayer = -1;
			int poolCounter = -1;
		};

		bool CreatePrograms(std::string& error);
		bool CreateProgram(const char* name, const char* vertexPath, const char* fragmentPath,
			Program& output, std::string& error);
		unsigned int CompileShader(const char* programName, unsigned int type,
			const std::string& source, std::string& error);
		bool CreateBuffers(std::string& error);
		bool TryCreateSsboBatch(std::string& error);
		void DestroySsboBatch();
		bool EnsureBufferCapacity(std::size_t vertexBytes, std::size_t indexBytes);
		bool EnsureSsboCapacity(std::size_t bytes);
		bool UploadAndDraw(Program& program, std::uint32_t texture, bool additive,
			const OpenGLVertex* vertices, std::size_t vertexCount,
			const glm::mat4& projectionView);
		void ApplyBlend(bool additive);
		bool ProcessCapture();
		void CompleteCapture(bool succeeded, const std::string& error = {});

		SDL_Window* mWindow = nullptr;
		SDL_GLContext mContext = nullptr;
		OpenGLApi mApi;
		Program mBatchProgram;
		Program mBatchColorizeProgram;
		Program mBatchLessColorizeProgram;
		Program mPoolProgram;
		Program mSsboBatchProgram;
		Program mSsboBatchColorizeProgram;
		Program mSsboBatchLessColorizeProgram;
		unsigned int mVao = 0;
		unsigned int mSsboVao = 0;
		unsigned int mVbo = 0;
		unsigned int mIbo = 0;
		unsigned int mMatrixSsbo = 0;
		std::size_t mVboCapacity = 0;
		std::size_t mIboCapacity = 0;
		std::size_t mSsboCapacity = 0;
		std::size_t mCpuBatchVertexCount = 0;
		std::vector<std::uint32_t> mSequentialIndices;
		bool mFrameOpen = false;
		bool mVsync = false;
		int mDrawableWidth = 0;
		int mDrawableHeight = 0;
		int mContextMajor = 0;
		int mContextMinor = 0;
		bool mSsboBatchEnabled = false;
		std::string mVendor;
		std::string mRendererName;
		std::string mVersion;
		std::string mShadingLanguageVersion;
		OpenGLFrameStats mFrameStats;
		OpenGLFrameStats mLastStats;
		std::chrono::steady_clock::time_point mFrameStart{};

		struct CaptureRecord {
			CaptureStatus status = CaptureStatus::Pending;
			std::string error;
		};
		CaptureTicket mNextCaptureTicket = 1;
		CaptureTicket mPendingCaptureTicket = 0;
		std::unordered_map<CaptureTicket, CaptureRecord> mCaptureRecords;
		std::string mCapturePath;
	};

} // namespace pvz
