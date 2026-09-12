#include "Graphics.h"
#include "Logger.h"
#include "Profiler.h"

#include "./Renderer/VulkanContext.h"
#include "./Renderer/VulkanRenderer.h"
#include "./Renderer/VulkanTexturePool.h"
#include "./Renderer/VulkanBuffer.h"
#include "./Renderer/VulkanPipeline.h"
#include "./Renderer/OpenGLRenderer.h"

#include <cstring>
#include <array>
#include <algorithm>
#include <cmath>

namespace {
	// Phase 3b — BatchVertex 顶点输入描述（与 Graphics.h struct BatchVertex 对齐）
	// stride=52B：vec2 pos(8) + vec2 uv(8) + uvec2 indices(8) + vec4 color(16)
	//             + float blendMode(4) + uvec2 packedClip(8)。
	// blendMode 仅 CPU 侧分段使用；packedClip 作为 location=4 送入 shader。
	constexpr VkVertexInputBindingDescription kBatchVertexBinding = {
		/*binding*/0, /*stride*/sizeof(BatchVertex), VK_VERTEX_INPUT_RATE_VERTEX
	};
	constexpr VkVertexInputAttributeDescription kBatchVertexAttrs[5] = {
		{ /*location*/0, /*binding*/0, VK_FORMAT_R32G32_SFLOAT,        offsetof(BatchVertex, x)         },
		{ /*location*/1, /*binding*/0, VK_FORMAT_R32G32_SFLOAT,        offsetof(BatchVertex, u)         },
		{ /*location*/2, /*binding*/0, VK_FORMAT_R32G32_UINT,          offsetof(BatchVertex, texIndex)  },
		{ /*location*/3, /*binding*/0, VK_FORMAT_R32G32B32A32_SFLOAT,  offsetof(BatchVertex, r)         },
		{ /*location*/4, /*binding*/0, VK_FORMAT_R32G32_UINT,           offsetof(BatchVertex, clipMinXY) },
	};

	struct PoolPushConstants {
		glm::mat4 projView;
		float poolLayer;
		float poolCounter;
		float padding[2];
	};
	static_assert(sizeof(PoolPushConstants) == 80, "PoolPushConstants must match pool shaders");

	constexpr int kPoolGridColumns = 15;                   // 原版水面横向网格数
	constexpr int kPoolGridRows = 5;                       // 原版水面纵向网格数
	constexpr int kPoolVerticesPerLayer = 450;             // 15x5 格、每格两个三角形的顶点数
	constexpr int kPoolLayerCount = 3;                     // 底图、阴影、焦散三层
	constexpr float kPoolBaseWidth = 720.0f;               // 底图/阴影网格世界宽度（逻辑像素）
	constexpr float kPoolBaseHeight = 159.0f;              // 底图/阴影网格世界高度（逻辑像素）
	constexpr float kPoolCausticWidth = 704.0f;            // 焦散层世界宽度（逻辑像素）
	constexpr float kPoolCausticCellHeight = 30.0f;        // 焦散层单格高度（逻辑像素）

	// 每帧持久映射 host-visible 缓冲容量 —— grow-on-demand（见 ResizeBufferIfNeeded / Graphics::BeginFrame）。
	// 启动只分配 *_INIT 的小容量；某帧写入溢出（fr.overflowWarned）时 EndFrame 把目标容量翻倍（无上限），
	// 下一次该帧 BeginFrame（fence 已等过、GPU 不再引用本帧缓冲）create-then-swap 重建到位。
	// 无封顶：每帧绘制需求由玩法天然有界，×2 收敛到真实需求的 ≤2 倍；真 OOM 由 create-then-swap 兜底降级为 drop。
	// 收益：常驻 host-visible 从旧固定 (128+32+64)×2帧=448MB 降到 (16+4+8)×2帧=56MB（普通游玩省 ~392MB），
	// 重场景再按需长到够用（旧固定容量参考：VBO 128MB≈3M 顶点 / SSBO 32MB≈2×262144 mat4 /
	// INST 64MB≈1.39M（48 B/实例），11000 僵尸 ×~15 track ×3 ≈ 500k 实例）。
	constexpr VkDeviceSize VBO_BYTES_INIT  = 16u * 1024u * 1024u;
	constexpr VkDeviceSize SSBO_BYTES_INIT =  4u * 1024u * 1024u;
	constexpr VkDeviceSize INST_BYTES_INIT =  8u * 1024u * 1024u;
} // anonymous

// Phase 3b — Vulkan 端持有的全部渲染资源。PIMPL 在 Graphics.cpp 内部定义，
// 避免把 vulkan.h 暴露给整个工程。
struct Graphics::VulkanGraphicsState {
	pvz::VulkanContext* ctx = nullptr;
	pvz::VulkanRenderer* renderer = nullptr;
	pvz::VulkanTexturePool* texPool = nullptr;

	// set=0 描述：matrix SSBO（binding=0），逐帧一个 set 绑定到自家 SSBO
	VkDescriptorSetLayout matrixSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool      matrixPool = VK_NULL_HANDLE;

	struct PerFrame {
		std::unique_ptr<pvz::VulkanBuffer> vbo;          // host-visible 持久映射
		std::unique_ptr<pvz::VulkanBuffer> ssbo;         // host-visible 持久映射
		std::unique_ptr<pvz::VulkanBuffer> instBuf;      // host-visible 持久映射的逐实例顶点缓冲
		VkDescriptorSet                    matrixSet = VK_NULL_HANDLE;
		VkDeviceSize                       vboCursor = 0;   // 当前写入字节偏移
		VkDeviceSize                       ssboCursor = 0;
		VkDeviceSize                       instCursor = 0;   // Task 3
		VkDeviceSize                       vboCap = 0;   // 本帧缓冲实际容量（grow-on-demand）
		VkDeviceSize                       ssboCap = 0;
		VkDeviceSize                       instCap = 0;
		bool                               overflowWarned = false;  // 本帧已打过 overflow 警告？（仅日志节流）
		bool                               vboOverflow = false;     // 本帧各缓冲是否溢出 —— 按缓冲归因，
		bool                               ssboOverflow = false;    // 驱动 EndFrame 只翻倍真正溢出的那个缓冲，
		bool                               instOverflow = false;    // 不再因共用一旗而把三缓冲全部 ×2（过度分配）。
	};
	static constexpr uint32_t FRAMES = pvz::VulkanRenderer::FRAMES_IN_FLIGHT;
	std::array<PerFrame, FRAMES> frames;
	uint32_t frameIdx = 0;  // 与 VulkanRenderer 帧索引同步

	// grow-on-demand 目标容量（全局高水位）。EndFrame 溢出时增长、持续空闲时回收；
	// 各帧 BeginFrame 把自己的缓冲 resize 到该值；普通场景从不溢出 → 恒为 *_INIT。
	VkDeviceSize desiredVboCap  = VBO_BYTES_INIT;
	VkDeviceSize desiredSsboCap = SSBO_BYTES_INIT;
	VkDeviceSize desiredInstCap = INST_BYTES_INIT;

	// 本帧各缓冲"真实需求"字节（含被容量拒绝的写入；并行区聚合 + 串行溢出点更新）。
	// EndFrame 据此一步扩容到位，避免 ×2 逐帧爬升导致连续多帧丢绘制。每帧 BeginFrame 清零。
	uint64_t frameVboDemand  = 0;
	uint64_t frameSsboDemand = 0;
	uint64_t frameInstDemand = 0;

	// 空闲回收（#8）：连续 kShrinkIdleFrames 帧占用 < desired/4 且无溢出 → 缩到窗口峰值×2（地板 *_INIT）。
	// 防止一次重场景把 host-visible 顶到几百 MB 后整个会话不回收，吞掉本次 grow-on-demand 的常驻收益。
	static constexpr int kShrinkIdleFrames = 600;   // ~10s @60fps，迟滞防抖
	VkDeviceSize idleVboPeak  = 0, idleSsboPeak  = 0, idleInstPeak  = 0;
	int          idleVboFrames = 0, idleSsboFrames = 0, idleInstFrames = 0;

	// OOM 节流（#9）：grow/shrink 分配失败只在"进入失败态"时报一次，成功后复位 —— 避免
	// 持续 OOM 时每帧对 3 个缓冲各刷一条 WARN（Release 不裁 WARN）卡死主线程。
	bool growFailWarned = false;

	std::unique_ptr<pvz::VulkanPipeline> pipeBatchAlpha;
	std::unique_ptr<pvz::VulkanPipeline> pipeBatchAdd;
	std::unique_ptr<pvz::VulkanPipeline> pipeBatchColorize;
	std::unique_ptr<pvz::VulkanPipeline> pipeBatchLessColorize;
	std::unique_ptr<pvz::VulkanPipeline> pipePool;

	// Instance pipelines for reanim sprites; records arrive through a per-instance vertex binding.
	std::unique_ptr<pvz::VulkanPipeline>  pipeInstAlpha;
	std::unique_ptr<pvz::VulkanPipeline>  pipeInstAdd;
	std::unique_ptr<pvz::VulkanPipeline>  pipeInstColorize;
	std::unique_ptr<pvz::VulkanPipeline>  pipeInstLessColorize;

	// 1x1 白色纹理：FillRect / DrawText 等"无纹理但走 batch 路径"的画法用它的 bindless 槽。
	pvz::VulkanTexture* whiteTex = nullptr;

	bool frameOpen = false;
};

// 与 glm::mat4(1.0f) 精确比较：仅用于快路判定，假阴性只会多做一次乘法（结果仍正确），不会出错。
static inline bool IsIdentityMat(const glm::mat4& m) {
	return m == glm::mat4(1.0f);
}

// ==================== 多线程录制状态（thread_local） ====================
//
// 这些指针在主线程上始终为 nullptr，所以所有公开 DrawXxx 路径在主线程上保持原行为
// 不变；只有 SetWorkerSlot() 在 worker 线程把它们置位后，DrawXxx 才走 Record 路径。
// 注意：thread_local 是按线程而非按 slot 的。Dispatch 每轮把同一个 worker 线程绑到
// 同一个 slot 上跑完一整段（slot = idx），所以指针在该线程内稳定。
namespace {
	thread_local WorkerRecord* tl_record = nullptr;
	thread_local std::vector<glm::mat4>* tl_transformStack = nullptr;
	thread_local std::vector<char>* tl_transformIsIdentity = nullptr;
	thread_local std::vector<ClipRect>* tl_clipStack = nullptr;
	thread_local std::vector<PackedClipRect>* tl_packedClipStack = nullptr;
	thread_local BlendMode               tl_blend = BlendMode::None;

	// 同一 worker 常连续绘制相同 HUD 文本（如成批僵尸血量）。缓存只持有
	// 当前图集的只读指针；主线程重建图集时通过 generation 使其失效。
	struct WorkerGlyphRunCache {
		const Graphics* owner = nullptr;
		uint32_t atlasGeneration = 0;
		std::string fontKey;
		int fontSize = 0;
		const GlyphAtlas* atlas = nullptr;
		std::string text;
		std::array<const GlyphInfo*, 64> glyphs{};
		int glyphCount = 0;
	};
	static_assert(sizeof(BatchVertex) == sizeof(pvz::OpenGLSsboBatchVertex),
		"OpenGL SSBO batch must share BatchVertex bytes");
	static_assert(offsetof(BatchVertex, matrixIndex)
		== offsetof(pvz::OpenGLSsboBatchVertex, matrixIndex));
	static_assert(offsetof(BatchVertex, r) == offsetof(pvz::OpenGLSsboBatchVertex, r));
	static_assert(offsetof(BatchVertex, clipMinXY)
		== offsetof(pvz::OpenGLSsboBatchVertex, clipMinXY));

	// InstanceRecord 走 Vulkan 的 per-instance 顶点取数器；同一份记录由四个 quad 顶点
	// 复用，避免 vertex shader 通过 gl_InstanceIndex 对 host-visible SSBO 重复动态读取。
	constexpr VkVertexInputBindingDescription kInstanceBinding = {
		/*binding*/0, /*stride*/sizeof(InstanceRecord), VK_VERTEX_INPUT_RATE_INSTANCE
	};
	constexpr VkVertexInputAttributeDescription kInstanceAttrs[5] = {
		{ /*location*/0, /*binding*/0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceRecord, tA)        },
		{ /*location*/1, /*binding*/0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceRecord, tx)        },
		{ /*location*/2, /*binding*/0, VK_FORMAT_R32G32_SFLOAT,       offsetof(InstanceRecord, u1)        },
		{ /*location*/3, /*binding*/0, VK_FORMAT_R32_UINT,            offsetof(InstanceRecord, texSlot)   },
		{ /*location*/4, /*binding*/0, VK_FORMAT_R8G8B8A8_UNORM,      offsetof(InstanceRecord, colorRGBA8)},
	};
	thread_local WorkerGlyphRunCache tl_glyphRunCache;

	/** 把公开绘制模式编码到 BatchVertex 的 CPU 分段字段。 */
	constexpr float EncodeBatchBlendMode(BlendMode mode) {
		if (mode == BlendMode::Add) return 1.0f;
		if (mode == BlendMode::WashedOut) return 2.0f;
		if (mode == BlendMode::LessWashedOut) return 3.0f;
		return 0.0f;
	}

	/** 解码 BatchVertex 的稳定枚举值；未知值保守回落到普通 Alpha。 */
	constexpr BlendMode DecodeBatchBlendMode(float mode) {
		if (mode == 1.0f) return BlendMode::Add;
		if (mode == 2.0f) return BlendMode::WashedOut;
		if (mode == 3.0f) return BlendMode::LessWashedOut;
		return BlendMode::Alpha;
	}

	// Phase 5：worker 直接把绝对 bindless 槽位写进 BatchVertex.texIndex、把
	// (slice.ssboBaseMat + slice.ssboCount) 写进 BatchVertex.matrixIndex，replay 不再
	// 做任何索引重映射，所以 InternTex / InternMat 已删除。

	// Phase 5：从 FlushBatch（CPU 顶点源）和 ReplayAndEndParallel（每个 worker slot 的
	// mapped VBO 切片）共用的"分段 + vkCmdDraw"。
	// 前置条件：调用方已经做过 vkCmdBindVertexBuffers，cb 处于 record 状态。
	// 参数：
	//   firstVert : 这段顶点在已绑定 VBO 中的绝对起始下标（vkCmdDraw 的 firstVertex 用它）
	//   vertCount : 要 emit 的顶点数（按 6 顶点对齐，否则尾部余数被裁掉）
	//   scan      : scan[0..vertCount) 应当反映 [firstVert..firstVert+vertCount) 的 blendMode 字段。
	//               FlushBatch 路径下 scan = m_batchVertices.data()（CPU 顺序内存，cache 友好）；
	//               Replay 路径下 scan = (BatchVertex*)mapped + firstVert（host-coherent map，按顺序读）。
	void EmitDrawRange(VkCommandBuffer cb,
		uint32_t firstVert, uint32_t vertCount,
		const BatchVertex* scan,
		pvz::VulkanPipeline* pipeAlpha, pvz::VulkanPipeline* pipeAdd,
		pvz::VulkanPipeline* pipeColorize, pvz::VulkanPipeline* pipeLessColorize,
		const VkDescriptorSet sets[2],
		const glm::mat4& projView,
		uint32_t* drawCallCount)
	{
		if (vertCount == 0 || !scan) return;
		// 严格按 6 顶点 stride 分段；尾部余数（不可能正常出现）丢弃，避免越界。
		const uint32_t aligned = (vertCount / 6) * 6;
		if (aligned == 0) return;

		auto emit = [&](uint32_t segStart, uint32_t segEnd, float bm) {
			if (segEnd <= segStart) return;
			const BlendMode mode = DecodeBatchBlendMode(bm);
			pvz::VulkanPipeline* pipe = mode == BlendMode::Add ? pipeAdd
				: mode == BlendMode::WashedOut ? pipeColorize
				: mode == BlendMode::LessWashedOut ? pipeLessColorize : pipeAlpha;
			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Handle());
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Layout(),
				0, 2, sets, 0, nullptr);
			vkCmdPushConstants(cb, pipe->Layout(), VK_SHADER_STAGE_VERTEX_BIT,
				0, sizeof(glm::mat4), &projView);
			vkCmdDraw(cb, segEnd - segStart, 1, firstVert + segStart, 0);
			if (drawCallCount) ++(*drawCallCount);
			};

		uint32_t segStart = 0;
		float curBm = scan[0].blendMode;
		for (uint32_t i = 6; i < aligned; i += 6) {
			const float bm = scan[i].blendMode;
			if (bm != curBm) {
				emit(segStart, i, curBm);
				segStart = i;
				curBm = bm;
			}
		}
		emit(segStart, aligned, curBm);
	}
}

Graphics::Graphics() {
	m_transformStack.push_back(glm::mat4(1.0f));
	m_transformIsIdentity.push_back(1);
	this->SetBlendMode(BlendMode::Alpha);
}

Graphics::~Graphics() {
	// 文字与白色纹理必须在对应 TextureBackend/Context 仍存活时归还。
	ClearTextCache();
	ClearPinnedTextCache();
	ShutdownOpenGL();
	ShutdownVulkan();
	m_batchVertices.clear();
	m_batchMatrices.clear();
}

bool Graphics::Initialize(int windowWidth, int windowHeight) {
	// 共享初始化只设置窗口尺寸与投影矩阵；GPU 资源由各后端初始化函数建立。
	SetWindowSize(windowWidth, windowHeight);
	return true;
}

void Graphics::SetWindowSize(int width, int height) {
	m_windowWidth = width;
	m_windowHeight = height;
	m_projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
	// 实际视口由当前后端处理，CPU 侧只维护逻辑投影矩阵。
}

bool Graphics::IsWorldPointVisible(float worldX, float worldY, float marginPx) const {
	// 用当前 projView 把世界点投到裁剪空间。ortho 下 w==1，无需透视除法，clip.xy 即 NDC。
	const glm::vec4 clip = (m_projection * m_viewMatrix) * glm::vec4(worldX, worldY, 0.0f, 1.0f);
	// 像素 margin → NDC：NDC 全宽 2 对应窗口像素全宽。zoom 已包含在 m_viewMatrix 里，margin 是屏幕空间量。
	const float mx = (m_windowWidth  > 0) ? (2.0f * marginPx / (float)m_windowWidth)  : 0.0f;
	const float my = (m_windowHeight > 0) ? (2.0f * marginPx / (float)m_windowHeight) : 0.0f;
	return clip.x >= -1.0f - mx && clip.x <= 1.0f + mx
		&& clip.y >= -1.0f - my && clip.y <= 1.0f + my;
}

// ==================== Phase 3b — Vulkan 接入 ====================

bool Graphics::InitializeVulkan(pvz::VulkanContext* ctx,
	pvz::VulkanRenderer* renderer,
	pvz::VulkanTexturePool* pool) {
	if (!ctx || !renderer || !pool) {
		LOG_ERROR("Graphics") << "InitializeVulkan 参数为空";
		return false;
	}
	if (m_vk) {
		LOG_WARN("Graphics") << "InitializeVulkan 已初始化";
		return false;
	}
	m_backend = pvz::RendererBackend::Vulkan;
	m_textureBackend = pool;

	auto state = std::make_unique<VulkanGraphicsState>();
	state->ctx = ctx;
	state->renderer = renderer;
	state->texPool = pool;

	VkDevice dev = ctx->Device();

	// 1) set=0 layout：binding=0 SSBO（matrix），VERTEX 阶段
	{
		VkDescriptorSetLayoutBinding b{};
		b.binding = 0;
		b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b.descriptorCount = 1;
		b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		lci.bindingCount = 1;
		lci.pBindings = &b;
		if (vkCreateDescriptorSetLayout(dev, &lci, nullptr, &state->matrixSetLayout) != VK_SUCCESS) {
			LOG_ERROR("Graphics") << "matrix SSBO descriptor set layout 创建失败";
			return false;
		}
	}

	// 2) descriptor pool：能装 FRAMES 个 set，每个 set 一个 SSBO
	{
		VkDescriptorPoolSize ps{};
		ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ps.descriptorCount = VulkanGraphicsState::FRAMES;

		VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pci.maxSets = VulkanGraphicsState::FRAMES;
		pci.poolSizeCount = 1;
		pci.pPoolSizes = &ps;
		if (vkCreateDescriptorPool(dev, &pci, nullptr, &state->matrixPool) != VK_SUCCESS) {
			LOG_ERROR("Graphics") << "matrix descriptor pool 创建失败";
			return false;
		}
	}

	// 3) 逐帧资源：VBO + SSBO + descriptor set
	for (uint32_t i = 0; i < VulkanGraphicsState::FRAMES; ++i) {
		auto& fr = state->frames[i];

		fr.vbo = std::make_unique<pvz::VulkanBuffer>();
		fr.ssbo = std::make_unique<pvz::VulkanBuffer>();
		if (!fr.vbo->Create(ctx, VBO_BYTES_INIT,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, /*hostVisible*/true)) return false;
		if (!fr.ssbo->Create(ctx, SSBO_BYTES_INIT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, /*hostVisible*/true)) return false;
		fr.instBuf = std::make_unique<pvz::VulkanBuffer>();
		if (!fr.instBuf->Create(ctx, INST_BYTES_INIT,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			/*hostVisible*/true)) return false;
		fr.vboCap = VBO_BYTES_INIT;
		fr.ssboCap = SSBO_BYTES_INIT;
		fr.instCap = INST_BYTES_INIT;

		VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		ai.descriptorPool = state->matrixPool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &state->matrixSetLayout;
		if (vkAllocateDescriptorSets(dev, &ai, &fr.matrixSet) != VK_SUCCESS) {
			LOG_ERROR("Graphics") << "matrix descriptor set 分配失败";
			return false;
		}

		VkDescriptorBufferInfo bi{};
		bi.buffer = fr.ssbo->Handle();
		bi.offset = 0;
		bi.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		w.dstSet = fr.matrixSet;
		w.dstBinding = 0;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w.pBufferInfo = &bi;
		vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);

	}

	// 4) 四条 batch pipeline（alpha / additive / 两档 WashedOut），共享 layout 形状。
	{
		const VkDescriptorSetLayout sets[2] = {
			state->matrixSetLayout,        // set=0 matrix SSBO
			pool->DescriptorSetLayout(),   // set=1 bindless textures
		};

		pvz::VulkanPipeline::Desc desc{};
		desc.vertSpvPath = "Shader/spv/batch.vert.spv";
		desc.fragSpvPath = "Shader/spv/batch.frag.spv";
		desc.colorFormat = ctx->SwapchainFormat();
		desc.setLayouts = sets;
		desc.setLayoutCount = 2;
		desc.pushConstantSize = sizeof(glm::mat4);
		desc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
		desc.vertexBindings = &kBatchVertexBinding;
		desc.vertexBindingCount = 1;
		desc.vertexAttributes = kBatchVertexAttrs;
		desc.vertexAttributeCount = (uint32_t)(sizeof(kBatchVertexAttrs) / sizeof(kBatchVertexAttrs[0]));

		desc.alphaBlend = true;
		desc.additiveBlend = false;
		state->pipeBatchAlpha = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeBatchAlpha->Initialize(ctx, desc)) return false;

		desc.alphaBlend = false;
		desc.additiveBlend = true;
		state->pipeBatchAdd = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeBatchAdd->Initialize(ctx, desc)) return false;

		// 两档 WashedOut 与 Alpha 共用预乘混合方程，只替换片元着色器。
		desc.fragSpvPath = "Shader/spv/batch_colorize.frag.spv";
		desc.alphaBlend = true;
		desc.additiveBlend = false;
		state->pipeBatchColorize = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeBatchColorize->Initialize(ctx, desc)) return false;

		desc.fragSpvPath = "Shader/spv/batch_less_colorize.frag.spv";
		state->pipeBatchLessColorize = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeBatchLessColorize->Initialize(ctx, desc)) return false;

		// PoolEffect 复用 BatchVertex 与同一组 matrix/bindless descriptors，但由专用
		// shader 在 GPU 上计算三层水波和焦散，避免逐帧生成、上传动态纹理。
		desc.vertSpvPath = "Shader/spv/pool.vert.spv";
		desc.fragSpvPath = "Shader/spv/pool.frag.spv";
		desc.pushConstantSize = sizeof(PoolPushConstants);
		desc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		desc.alphaBlend = true;
		desc.additiveBlend = false;
		state->pipePool = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipePool->Initialize(ctx, desc)) {
			LOG_ERROR("Graphics") << "pipePool 创建失败";
			return false;
		}
	}

	// 4b) 四条 instance pipeline（alpha / additive / 两档 WashedOut）。InstanceRecord 由
	//     per-instance vertex input 读取；set=0 沿用 matrix layout 以保持 batch/instance layout
	//     兼容，但实例 shader 不访问它，只需要绑定 set=1 的 bindless textures。
	{
		const VkDescriptorSetLayout instSets[2] = {
			state->matrixSetLayout,        // set=0 layout-compatible placeholder（实例 shader 未使用）
			pool->DescriptorSetLayout(),   // set=1 bindless textures
		};

		pvz::VulkanPipeline::Desc desc{};
		desc.vertSpvPath = "Shader/spv/reanim_inst.vert.spv";
		desc.fragSpvPath = "Shader/spv/reanim_inst.frag.spv";
		desc.colorFormat = ctx->SwapchainFormat();
		desc.setLayouts = instSets;
		desc.setLayoutCount = 2;
		desc.pushConstantSize = sizeof(glm::mat4);
		desc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
		desc.vertexBindings = &kInstanceBinding;
		desc.vertexBindingCount = 1;
		desc.vertexAttributes = kInstanceAttrs;
		desc.vertexAttributeCount = (uint32_t)(sizeof(kInstanceAttrs) / sizeof(kInstanceAttrs[0]));
		desc.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

		desc.alphaBlend = true;
		desc.additiveBlend = false;
		state->pipeInstAlpha = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeInstAlpha->Initialize(ctx, desc)) {
			LOG_ERROR("Graphics") << "pipeInstAlpha 创建失败";
			return false;
		}

		desc.alphaBlend = false;
		desc.additiveBlend = true;
		state->pipeInstAdd = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeInstAdd->Initialize(ctx, desc)) {
			LOG_ERROR("Graphics") << "pipeInstAdd 创建失败";
			return false;
		}

		desc.fragSpvPath = "Shader/spv/reanim_inst_colorize.frag.spv";
		desc.alphaBlend = true;
		desc.additiveBlend = false;
		state->pipeInstColorize = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeInstColorize->Initialize(ctx, desc)) {
			LOG_ERROR("Graphics") << "pipeInstColorize 创建失败";
			return false;
		}

		desc.fragSpvPath = "Shader/spv/reanim_inst_less_colorize.frag.spv";
		state->pipeInstLessColorize = std::make_unique<pvz::VulkanPipeline>();
		if (!state->pipeInstLessColorize->Initialize(ctx, desc)) {
			LOG_ERROR("Graphics") << "pipeInstLessColorize 创建失败";
			return false;
		}
	}

	// 5) 1x1 白色纹理：FillRect / DrawText 等画法用它的 bindless 槽
	{
		const uint8_t white[4] = { 255, 255, 255, 255 };
		state->whiteTex = pool->CreateTextureRGBA8(1, 1, white);
		if (!state->whiteTex) {
			LOG_ERROR("Graphics") << "白色纹理上传失败";
			return false;
		}
		m_whiteTexture = state->whiteTex->bindlessIndex;
		m_whiteTextureHandle = state->whiteTex;
	}

	m_vk = std::move(state);
	LOG_INFO("Graphics") << "Vulkan 接入完成。host-visible 缓冲 grow-on-demand 初始(MB/frame，无上限按需增长): VBO="
		<< (VBO_BYTES_INIT / 1024 / 1024)
		<< ", SSBO=" << (SSBO_BYTES_INIT / 1024 / 1024)
		<< ", INST=" << (INST_BYTES_INIT / 1024 / 1024)
		<< ", white tex bindless=" << m_whiteTexture;
	return true;
}

void Graphics::ShutdownVulkan() {
	if (!m_vk) return;
	VkDevice dev = m_vk->ctx ? m_vk->ctx->Device() : VK_NULL_HANDLE;
	if (dev) vkDeviceWaitIdle(dev);

	// 白色纹理交还给 pool
	if (m_vk->whiteTex && m_vk->texPool) {
		m_vk->texPool->DestroyTexture(m_vk->whiteTex);
		m_vk->whiteTex = nullptr;
		m_whiteTextureHandle = nullptr;
		m_whiteTexture = 0;
	}

	m_vk->pipeBatchAlpha.reset();
	m_vk->pipeBatchAdd.reset();
	m_vk->pipeBatchColorize.reset();
	m_vk->pipeBatchLessColorize.reset();
	m_vk->pipePool.reset();
	m_vk->pipeInstAlpha.reset();
	m_vk->pipeInstAdd.reset();
	m_vk->pipeInstColorize.reset();
	m_vk->pipeInstLessColorize.reset();

	for (auto& fr : m_vk->frames) {
		fr.vbo.reset();
		fr.ssbo.reset();
		fr.instBuf.reset();
		fr.matrixSet = VK_NULL_HANDLE;  // pool 销毁时一起回收
		fr.vboCursor = fr.ssboCursor = fr.instCursor = 0;
	}

	if (dev) {
		if (m_vk->matrixPool)      vkDestroyDescriptorPool(dev, m_vk->matrixPool, nullptr);
		if (m_vk->matrixSetLayout) vkDestroyDescriptorSetLayout(dev, m_vk->matrixSetLayout, nullptr);
	}
	m_vk->matrixPool = VK_NULL_HANDLE;
	m_vk->matrixSetLayout = VK_NULL_HANDLE;

	m_vk.reset();
	if (m_backend == pvz::RendererBackend::Vulkan) m_textureBackend = nullptr;
}

void Graphics::ShutdownOpenGL() {
	if (!m_gl) return;
	if (m_textureBackend && m_whiteTextureHandle) {
		m_textureBackend->DestroyTexture(m_whiteTextureHandle);
	}
	m_whiteTextureHandle = nullptr;
	m_whiteTexture = 0;
	m_gl = nullptr;
	m_textureBackend = nullptr;
}

pvz::CaptureBackend* Graphics::GetCaptureBackend() const {
	if (m_gl) return m_gl;
	if (m_vk) return m_vk->renderer;
	return nullptr;
}

namespace {
	// grow-on-demand：把单个持久映射 host-visible 缓冲增长到 desiredCap。
	// 把缓冲 resize 到 desiredCap（grow 或 shrink 均走此路）：销毁旧缓冲、按新容量重建、
	// （若 dstSet 非空）把 storage 描述符重写到新句柄。
	// 调用前提：该帧 fence 已被 renderer->BeginFrame 等过，GPU 不再引用此缓冲 → 销毁重建安全。
	// vbo 走 dstSet=VK_NULL_HANDLE（顶点缓冲每帧 replay 现读 Handle()，无需描述符）。
	// failWarned：sticky OOM 节流旗（成功复位）——持续失败时只在进入失败态报一次。
	void ResizeBufferIfNeeded(pvz::VulkanContext* ctx,
		std::unique_ptr<pvz::VulkanBuffer>& buf, VkDeviceSize& cap,
		VkDeviceSize desiredCap, VkBufferUsageFlags usage, VkDescriptorSet dstSet,
		bool& failWarned) {
		if (cap == desiredCap) return;   // 既处理 grow（cap<desired）也处理 shrink（cap>desired）
		// 先建新的、成功后再换下旧的（create-then-swap）：grow 无上限，desiredCap 极大时
		// vmaCreateBuffer 可能 OOM 失败——此时必须保留旧缓冲，让本帧走既有 drop 路径而不是崩。
		auto fresh = std::make_unique<pvz::VulkanBuffer>();
		if (!fresh->Create(ctx, desiredCap, usage, /*hostVisible*/true)) {
			if (!failWarned) {   // 仅"进入失败态"报一次，避免每帧×3缓冲刷屏卡主线程
				LOG_WARN("Graphics") << "缓冲 resize 分配失败，保留旧容量 " << cap
					<< "（desired=" << desiredCap << "）— 本帧按 drop 处理";
				failWarned = true;
			}
			return;  // 旧 buf 完好，drop 路径兜底
		}
		failWarned = false;   // 成功 → 复位，下次再失败可再报
		buf = std::move(fresh);   // 旧缓冲随 unique_ptr 析构 → vmaDestroyBuffer（fence 已等过，安全）
		cap = desiredCap;
		if (dstSet != VK_NULL_HANDLE) {
			VkDescriptorBufferInfo bi{ buf->Handle(), 0, VK_WHOLE_SIZE };
			VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			w.dstSet = dstSet;
			w.dstBinding = 0;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &bi;
			vkUpdateDescriptorSets(ctx->Device(), 1, &w, 0, nullptr);
		}
	}
} // namespace

bool Graphics::BeginFrame() {
	if (m_gl) {
		m_frameDrawCallCount = 0;
		m_frameScissorChangeCount = 0;
		return m_gl->BeginFrame(m_clearR, m_clearG, m_clearB, m_clearA,
			m_windowWidth, m_windowHeight, m_letterboxScale,
			m_letterboxOffsetX, m_letterboxOffsetY);
	}
	if (!m_vk || !m_vk->renderer) return false;
	if (m_vk->frameOpen) {
		LOG_WARN("Graphics") << "BeginFrame 上一帧未 EndFrame";
		return false;
	}

	if (!m_vk->renderer->BeginFrame(m_clearR, m_clearG, m_clearB, m_clearA)) {
		return false;
	}

	// 延迟纹理删除：此刻本帧 slot 的 fence 已被 renderer->BeginFrame 等过，回收"已过
	// FRAMES_IN_FLIGHT 帧"的待删纹理最安全。取代旧 DestroyTexture 里的 vkDeviceWaitIdle。
	if (m_vk->texPool) m_vk->texPool->BeginFrameTick();

	// Letterbox：renderer 已设铺满交换链的默认 viewport，这里覆盖成等比居中的矩形。
	// 视口变换把所有几何体约束进该矩形，黑边区域无顶点 → 保持清屏色（黑）。
	// 保留负高度 Y 翻转（沿用 GL 风格 top=0 正交矩阵）。窗口模式 scale=1/offset=0 时退化为原 viewport。
	{
		VkCommandBuffer cb = m_vk->renderer->CurrentCmdBuffer();
		if (cb != VK_NULL_HANDLE) {
			float vpW = m_windowWidth * m_letterboxScale;
			float vpH = m_windowHeight * m_letterboxScale;
			VkViewport vp{};
			vp.x = m_letterboxOffsetX;
			vp.y = m_letterboxOffsetY + vpH;   // 底边，配合负高度翻转
			vp.width = vpW;
			vp.height = -vpH;
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
			vkCmdSetViewport(cb, 0, 1, &vp);
		}
	}

	m_vk->frameIdx = m_vk->renderer->CurrentFrameIdx();
	auto& fr = m_vk->frames[m_vk->frameIdx];
	m_frameDrawCallCount = 0;
	m_frameScissorChangeCount = 0;

	// grow-on-demand：此刻本帧 fence 已被 renderer->BeginFrame 等过，GPU 不再引用本帧缓冲，
	// 可安全销毁重建到目标容量（grow 或 shrink）。ssbo 是描述符绑定，重建后重写描述符；
	// vbo/inst 是顶点缓冲，每帧 replay 现读 Handle()，无需重写。
	{
		PROFILE_SCOPE("C1d.BeginFrame_bufferResize");
		ResizeBufferIfNeeded(m_vk->ctx, fr.vbo, fr.vboCap, m_vk->desiredVboCap,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_NULL_HANDLE, m_vk->growFailWarned);
		ResizeBufferIfNeeded(m_vk->ctx, fr.ssbo, fr.ssboCap, m_vk->desiredSsboCap,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, fr.matrixSet, m_vk->growFailWarned);
		ResizeBufferIfNeeded(m_vk->ctx, fr.instBuf, fr.instCap, m_vk->desiredInstCap,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_NULL_HANDLE, m_vk->growFailWarned);
	}

	fr.vboCursor = 0;
	fr.ssboCursor = 0;
	fr.instCursor = 0;  // Task 3
	fr.overflowWarned = false;
	fr.vboOverflow = fr.ssboOverflow = fr.instOverflow = false;
	m_vk->frameVboDemand = m_vk->frameSsboDemand = m_vk->frameInstDemand = 0;
	m_vk->frameOpen = true;
	return true;
}

bool Graphics::EndFrame() {
	if (m_gl) {
		FlushBatch();
		FlushInstances();
		const bool succeeded = m_gl->EndFrame();
		m_lastFrameDrawCallCount = m_gl->LastFrameStats().drawCallCount;
		m_lastFrameScissorChangeCount = 0;
		return succeeded;
	}
	if (!m_vk || !m_vk->frameOpen) return false;
	FlushBatch();
	FlushInstances();

	// grow-on-demand 容量决策（按缓冲独立，#6/#7/#8）：
	//  · 扩容：本帧某缓冲溢出 → 目标增长到 max(cap×2, 真实需求×1.25)。真实需求来自 worker 切片的
	//    "想写"计数（含被拒绝的）+ 串行溢出点，故一个大尖峰能"一步到位"而非 ×2 逐帧爬升（避免连续丢帧）。
	//    ×2 为地板（需求测不全时兜底）；真 OOM 由 ResizeBufferIfNeeded 的 create-then-swap 降级为 drop。
	//  · 回收：连续 kShrinkIdleFrames 帧占用 < desired/4 且无溢出 → 缩到窗口峰值×2（地板 *_INIT），
	//    防一次重场景把 host-visible 顶高后整个会话不回收。
	// 普通关卡/主菜单从不溢出且占用恒低 → desired 恒为 *_INIT。
	{
		const auto& fr = m_vk->frames[m_vk->frameIdx];
		auto growTo = [](VkDeviceSize& desired, VkDeviceSize cap, uint64_t demand) {
			const VkDeviceSize target = std::max<VkDeviceSize>(cap * 2, (VkDeviceSize)(demand + demand / 4));
			desired = std::max(desired, target);
		};
		if (fr.vboOverflow)  growTo(m_vk->desiredVboCap,  fr.vboCap,  m_vk->frameVboDemand);
		if (fr.ssboOverflow) growTo(m_vk->desiredSsboCap, fr.ssboCap, m_vk->frameSsboDemand);
		if (fr.instOverflow) growTo(m_vk->desiredInstCap, fr.instCap, m_vk->frameInstDemand);

		auto considerShrink = [](VkDeviceSize& desired, VkDeviceSize init, VkDeviceSize usage,
			bool overflowed, VkDeviceSize& peak, int& frames) {
				if (desired <= init) { frames = 0; peak = 0; return; }      // 已到地板，无可缩
				if (overflowed || usage * 4 > desired) { frames = 0; peak = 0; return; }  // 忙 → 重置窗口
				if (usage > peak) peak = usage;
				if (++frames >= VulkanGraphicsState::kShrinkIdleFrames) {
					const VkDeviceSize tgt = std::max(init, peak * 2);     // 留 2× 余量再缩
					if (tgt < desired) desired = tgt;
					frames = 0; peak = 0;
				}
			};
		considerShrink(m_vk->desiredVboCap,  VBO_BYTES_INIT,  fr.vboCursor,  fr.vboOverflow,
			m_vk->idleVboPeak,  m_vk->idleVboFrames);
		considerShrink(m_vk->desiredSsboCap, SSBO_BYTES_INIT, fr.ssboCursor, fr.ssboOverflow,
			m_vk->idleSsboPeak, m_vk->idleSsboFrames);
		considerShrink(m_vk->desiredInstCap, INST_BYTES_INIT, fr.instCursor, fr.instOverflow,
			m_vk->idleInstPeak, m_vk->idleInstFrames);
	}

	m_lastFrameDrawCallCount = m_frameDrawCallCount;
	m_lastFrameScissorChangeCount = m_frameScissorChangeCount;
	m_vk->frameOpen = false;
	return m_vk->renderer->EndFrame();
}

void Graphics::PushTransform(const glm::mat4& transform) {
	if (tl_record) {
		// worker：只动 thread-local 栈，不进 record 流（每次 DrawXxx 已把 finalMatrix 烘到 record 里）
		const bool curId = tl_transformIsIdentity->back() != 0;
		const bool tId = IsIdentityMat(transform);
		glm::mat4 newTop = curId ? transform : (tl_transformStack->back() * transform);
		tl_transformStack->push_back(newTop);
		tl_transformIsIdentity->push_back((curId && tId) ? 1 : 0);
		return;
	}
	const bool curId = m_transformIsIdentity.back() != 0;
	const bool tId = IsIdentityMat(transform);
	// 栈顶为单位阵时无需相乘，直接取 transform（这本身也消除一次冗余乘法）
	glm::mat4 newTop = curId ? transform : (m_transformStack.back() * transform);
	m_transformStack.push_back(newTop);
	m_transformIsIdentity.push_back((curId && tId) ? 1 : 0);
}

void Graphics::PopTransform() {
	if (tl_record) {
		if (tl_transformStack->size() > 1) {
			tl_transformStack->pop_back();
			tl_transformIsIdentity->pop_back();
		}
		else {
			LOG_WARN("Graphics") << "PopTransform (worker) failed: stack underflow.";
		}
		return;
	}
	if (m_transformStack.size() > 1) {
		m_transformStack.pop_back();
		m_transformIsIdentity.pop_back();
	}
	else {
		LOG_WARN("Graphics") << "PopTransform failed: stack underflow.";
	}
}

void Graphics::SetIdentity() {
	if (tl_record) {
		if (!tl_transformStack->empty()) {
			tl_transformStack->back() = glm::mat4(1.0f);
			tl_transformIsIdentity->back() = 1;
		}
		return;
	}
	if (!m_transformStack.empty()) {
		m_transformStack.back() = glm::mat4(1.0f);
		m_transformIsIdentity.back() = 1;
	}
}

void Graphics::Translate(float x, float y, float z) {
	if (tl_record) {
		if (tl_transformStack->empty()) return;
		tl_transformStack->back() = glm::translate(tl_transformStack->back(), glm::vec3(x, y, z));
		tl_transformIsIdentity->back() = 0;
		return;
	}
	if (m_transformStack.empty()) return;
	m_transformStack.back() = glm::translate(m_transformStack.back(), glm::vec3(x, y, z));
	m_transformIsIdentity.back() = 0;
}

void Graphics::Rotate(float angleDegrees, float x, float y, float z) {
	if (tl_record) {
		if (tl_transformStack->empty()) return;
		tl_transformStack->back() = glm::rotate(tl_transformStack->back(), glm::radians(angleDegrees), glm::vec3(x, y, z));
		tl_transformIsIdentity->back() = 0;
		return;
	}
	if (m_transformStack.empty()) return;
	m_transformStack.back() = glm::rotate(m_transformStack.back(), glm::radians(angleDegrees), glm::vec3(x, y, z));
	m_transformIsIdentity.back() = 0;
}

void Graphics::Scale(float sx, float sy, float sz) {
	if (tl_record) {
		if (tl_transformStack->empty()) return;
		tl_transformStack->back() = glm::scale(tl_transformStack->back(), glm::vec3(sx, sy, sz));
		tl_transformIsIdentity->back() = 0;
		return;
	}
	if (m_transformStack.empty()) return;
	m_transformStack.back() = glm::scale(m_transformStack.back(), glm::vec3(sx, sy, sz));
	m_transformIsIdentity.back() = 0;
}

// ==================== 裁剪栈（PushClipRect / PopClipRect） ====================

ClipRect Graphics::IntersectClip(const ClipRect& a, const ClipRect& b) {
	int x1 = std::max(a.x, b.x);
	int y1 = std::max(a.y, b.y);
	int x2 = std::min(a.x + a.w, b.x + b.w);
	int y2 = std::min(a.y + a.h, b.y + b.h);
	ClipRect r;
	r.x = x1;
	r.y = y1;
	r.w = std::max(0, x2 - x1);
	r.h = std::max(0, y2 - y1);
	return r;
}

PackedClipRect Graphics::PackClipRect(const ClipRect& rect) const {
	// ClipRect 保持屏幕逻辑坐标语义；片元阶段改用 gl_FragCoord 后，须在 push 时一次性
	// 换算到帧缓冲像素。16-bit 打包覆盖 Vulkan 的常见最大视口尺寸，同时把逐实例开销压到 8 B。
	int32_t x = 0, y = 0;
	uint32_t w = 0, h = 0;
	LogicalRectToFramebuffer(rect.x, rect.y, rect.w, rect.h, x, y, w, h);

	uint32_t framebufferW = 0xFFFFu;
	uint32_t framebufferH = 0xFFFFu;
	if (m_vk && m_vk->ctx) {
		const VkExtent2D extent = m_vk->ctx->SwapchainExtent();
		framebufferW = std::min(extent.width, 0xFFFFu);
		framebufferH = std::min(extent.height, 0xFFFFu);
	}
	else if (m_gl) {
		framebufferW = static_cast<uint32_t>(std::clamp(m_gl->DrawableWidth(), 0, 0xFFFF));
		framebufferH = static_cast<uint32_t>(std::clamp(m_gl->DrawableHeight(), 0, 0xFFFF));
	}

	const int64_t rawRight = static_cast<int64_t>(x) + w;
	const int64_t rawBottom = static_cast<int64_t>(y) + h;
	const uint32_t left = static_cast<uint32_t>(
		std::clamp<int64_t>(x, 0, framebufferW));
	const uint32_t top = static_cast<uint32_t>(
		std::clamp<int64_t>(y, 0, framebufferH));
	const uint32_t right = static_cast<uint32_t>(
		std::clamp<int64_t>(rawRight, 0, framebufferW));
	const uint32_t bottom = static_cast<uint32_t>(
		std::clamp<int64_t>(rawBottom, 0, framebufferH));

	PackedClipRect packed;
	packed.minXY = left | (top << 16);
	packed.maxXY = right | (bottom << 16);
	return packed;
}

void Graphics::PushClipRect(int x, int y, int w, int h) {
	std::vector<ClipRect>& clipStack = tl_clipStack ? *tl_clipStack : m_clipStack;
	std::vector<PackedClipRect>& packedStack =
		tl_packedClipStack ? *tl_packedClipStack : m_packedClipStack;

	ClipRect rect{ x, y, w, h };
	if (!clipStack.empty()) {
		rect = IntersectClip(clipStack.back(), rect);  // 嵌套取交集
	}
	clipStack.push_back(rect);
	packedStack.push_back(PackClipRect(rect));
}

void Graphics::PopClipRect() {
	std::vector<ClipRect>& clipStack = tl_clipStack ? *tl_clipStack : m_clipStack;
	std::vector<PackedClipRect>& packedStack =
		tl_packedClipStack ? *tl_packedClipStack : m_packedClipStack;
	if (clipStack.empty() || packedStack.empty()) {
		LOG_WARN("Graphics") << "PopClipRect failed: stack underflow.";
		return;
	}
	clipStack.pop_back();
	packedStack.pop_back();
}

bool Graphics::IsClipActive() const {
	return tl_clipStack ? !tl_clipStack->empty() : !m_clipStack.empty();
}

void Graphics::PushClipBottom(float bottomY) {
	PushClipRect(0, 0, m_windowWidth, static_cast<int>(std::ceil(bottomY)));
}

void Graphics::PopClipBottom() {
	PopClipRect();
}

PackedClipRect Graphics::CurrentPackedClipRect() const {
	const std::vector<PackedClipRect>& stack =
		tl_packedClipStack ? *tl_packedClipStack : m_packedClipStack;
	return stack.empty() ? PackedClipRect{} : stack.back();
}

void Graphics::FlushBatch() {
	// Vulkan 把累积顶点/矩阵拷入当前帧 mapped VBO/SSBO；OpenGL 先在 CPU
	// 展开矩阵，再按原顺序的纹理/混合边界提交动态 VBO/IBO。没有活动帧时只清 CPU 缓冲。

	const size_t vertCount = m_batchVertices.size();
	const size_t matCount = m_batchMatrices.size();

	auto clearCpu = [&]() {
		m_batchVertices.clear();
		m_batchMatrices.clear();
		};

	if (m_gl) {
		if (!m_gl->IsFrameOpen() || vertCount == 0) {
			clearCpu();
			return;
		}

		// 4.3 快路一次上传原 BatchVertex 与矩阵 SSBO，之后只按纹理/混合边界 draw。
		// 矩阵索引若异常则保守回落原 CPU 单位阵语义，避免 shader 越界读。
		const bool matricesValid = matCount > 0 && std::all_of(
			m_batchVertices.begin(), m_batchVertices.end(),
			[matCount](const BatchVertex& vertex) { return vertex.matrixIndex < matCount; });
		if (m_gl->SupportsSsboBatch() && matricesValid
			&& m_gl->UploadSsboBatch(
				reinterpret_cast<const pvz::OpenGLSsboBatchVertex*>(m_batchVertices.data()),
				vertCount, m_batchMatrices.data(), matCount)) {
			const glm::mat4 projectionView = m_projection * m_viewMatrix;
			std::size_t segmentStart = 0;
			std::uint32_t segmentTexture = m_batchVertices[0].texIndex;
			float segmentBlend = m_batchVertices[0].blendMode;
			bool firstSegment = true;
			auto submit = [&](std::size_t end) {
				if (end <= segmentStart) return;
				const bool textureBoundary = !firstSegment
					&& segmentTexture != m_batchVertices[segmentStart - 1].texIndex;
				const bool stateBoundary = !firstSegment
					&& segmentBlend != m_batchVertices[segmentStart - 1].blendMode;
				const BlendMode mode = DecodeBatchBlendMode(segmentBlend);
				m_gl->SubmitSsboBatchSegment(segmentTexture,
					mode == BlendMode::Add, mode == BlendMode::WashedOut,
					mode == BlendMode::LessWashedOut, segmentStart, end - segmentStart,
					projectionView, textureBoundary, stateBoundary);
				firstSegment = false;
			};
			for (std::size_t i = 3; i + 2 < vertCount; i += 3) {
				const BatchVertex& next = m_batchVertices[i];
				if (next.texIndex != segmentTexture || next.blendMode != segmentBlend) {
					submit(i);
					segmentStart = i;
					segmentTexture = next.texIndex;
					segmentBlend = next.blendMode;
				}
			}
			submit(vertCount - (vertCount % 3));
			clearCpu();
			return;
		}

		std::vector<pvz::OpenGLVertex> expanded;
		expanded.reserve(vertCount);
		for (const BatchVertex& source : m_batchVertices) {
			const glm::mat4& matrix = source.matrixIndex < m_batchMatrices.size()
				? m_batchMatrices[source.matrixIndex] : glm::mat4(1.0f);
			const glm::vec4 transformed = matrix * glm::vec4(source.x, source.y, 0.0f, 1.0f);
			pvz::OpenGLVertex vertex{};
			vertex.x = transformed.x;
			vertex.y = transformed.y;
			vertex.u = source.u;
			vertex.v = source.v;
			vertex.r = source.r;
			vertex.g = source.g;
			vertex.b = source.b;
			vertex.a = source.a;
			vertex.clipLeft = static_cast<float>(source.clipMinXY & 0xFFFFu);
			vertex.clipTop = static_cast<float>((source.clipMinXY >> 16) & 0xFFFFu);
			vertex.clipRight = static_cast<float>(source.clipMaxXY & 0xFFFFu);
			vertex.clipBottom = static_cast<float>((source.clipMaxXY >> 16) & 0xFFFFu);
			expanded.push_back(vertex);
		}

		// 整组 CPU 几何只上传一次；卡面/雾片仍保序切纹理，但不为每个小段重复 orphan。
		if (!m_gl->UploadCpuBatch(expanded.data(), expanded.size())) {
			LOG_ERROR("Graphics") << "OpenGL CPU batch 上传失败";
			clearCpu();
			return;
		}
		// 上层几何由三角形组成；只在三角形边界按纹理/Blend 分段，不跨命令重排。
		const glm::mat4 projectionView = m_projection * m_viewMatrix;
		std::size_t segmentStart = 0;
		std::uint32_t segmentTexture = m_batchVertices[0].texIndex;
		float segmentBlend = m_batchVertices[0].blendMode;
		bool firstSegment = true;
		auto submit = [&](std::size_t end) {
			if (end <= segmentStart) return;
			const bool textureBoundary = !firstSegment
				&& segmentTexture != m_batchVertices[segmentStart - 1].texIndex;
			const bool stateBoundary = !firstSegment
				&& segmentBlend != m_batchVertices[segmentStart - 1].blendMode;
			const BlendMode mode = DecodeBatchBlendMode(segmentBlend);
			m_gl->SubmitCpuBatchSegment(segmentTexture,
				mode == BlendMode::Add, mode == BlendMode::WashedOut,
				mode == BlendMode::LessWashedOut,
				segmentStart, end - segmentStart,
				projectionView, textureBoundary, stateBoundary);
			firstSegment = false;
		};
		for (std::size_t i = 3; i + 2 < vertCount; i += 3) {
			const BatchVertex& next = m_batchVertices[i];
			if (next.texIndex != segmentTexture || next.blendMode != segmentBlend) {
				submit(i);
				segmentStart = i;
				segmentTexture = next.texIndex;
				segmentBlend = next.blendMode;
			}
		}
		submit(vertCount - (vertCount % 3));
		clearCpu();
		return;
	}

	if (!m_vk || !m_vk->frameOpen || vertCount == 0) {
		clearCpu();
		return;
	}

	VkCommandBuffer cb = m_vk->renderer->CurrentCmdBuffer();
	if (cb == VK_NULL_HANDLE) {
		clearCpu();
		return;
	}

	auto& fr = m_vk->frames[m_vk->frameIdx];

	const VkDeviceSize matBytes = matCount * sizeof(glm::mat4);
	const VkDeviceSize vertBytes = vertCount * sizeof(BatchVertex);

	const bool ssboOver = fr.ssboCursor + matBytes > fr.ssboCap;
	const bool vboOver  = fr.vboCursor + vertBytes > fr.vboCap;
	if (ssboOver || vboOver) {
		// 按缓冲归因（驱动 EndFrame 只翻倍溢出的缓冲）+ 记录串行真实需求（供一步扩容到位）。
		if (ssboOver) { fr.ssboOverflow = true; m_vk->frameSsboDemand = std::max<uint64_t>(m_vk->frameSsboDemand, (uint64_t)fr.ssboCursor + matBytes); }
		if (vboOver)  { fr.vboOverflow  = true; m_vk->frameVboDemand  = std::max<uint64_t>(m_vk->frameVboDemand,  (uint64_t)fr.vboCursor  + vertBytes); }
		// 每帧只打一次警告——overflow 之后几乎每次 DrawXxx 都会再次 hit 这条路径，
		// 同步写控制台会把主线程憋成死循环（PVZ 在大场景下能堆到 60k+ 次/帧）。
		// （grow-on-demand：本帧溢出会在 EndFrame 触发目标容量增长，下帧到位。）
		if (!fr.overflowWarned) {
			LOG_WARN("Graphics") << "Frame buffer overflow — ssbo " << fr.ssboCursor << "+" << matBytes
				<< "/" << fr.ssboCap << "  vbo " << fr.vboCursor << "+" << vertBytes
				<< "/" << fr.vboCap << " — dropping further draws this frame";
			fr.overflowWarned = true;
		}
		clearCpu();
		return;
	}

	// 1) 矩阵：本次 FlushBatch 的矩阵会从 ssboCursor 处开始写入；记下该位置对应的 mat4 下标，
	//    后面把它加到每条顶点的 matrixIndex 上，让 shader 看到的是 SSBO 内的绝对槽位。
	const uint32_t matrixBase = (uint32_t)(fr.ssboCursor / sizeof(glm::mat4));
	{
		PROFILE_SCOPE("FBa.copy");
		if (matCount > 0) {
			std::memcpy(static_cast<char*>(fr.ssbo->MappedPtr()) + fr.ssboCursor,
				m_batchMatrices.data(), (size_t)matBytes);
		}

		// 2) 顶点：matrixIndex += matrixBase，然后 memcpy 进 VBO
		for (auto& v : m_batchVertices) {
			v.matrixIndex += matrixBase;
		}
		std::memcpy(static_cast<char*>(fr.vbo->MappedPtr()) + fr.vboCursor,
			m_batchVertices.data(), (size_t)vertBytes);
	}
	const uint32_t firstVertex = (uint32_t)(fr.vboCursor / sizeof(BatchVertex));

	fr.ssboCursor += matBytes;
	fr.vboCursor += vertBytes;

	// 3) 按 6 顶点一组（一个 quad）按 blendMode 字段分段，emit vkCmdDraw。
	const glm::mat4 projView = m_projection * m_viewMatrix;
	const VkDescriptorSet sets[2] = { fr.matrixSet, m_vk->texPool->DescriptorSet() };
	VkBuffer vbuf = fr.vbo->Handle();
	VkDeviceSize vboOff = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &vboOff);

	// Phase 5：分段 + vkCmdDraw 抽进静态 helper EmitDrawRange（同文件末尾），并行回放路径复用。
	// 这里直接从主线程 CPU 缓冲 m_batchVertices 读 blendMode（顺序遍历，cache 友好）。
	{
		PROFILE_SCOPE("FBb.emit");
		EmitDrawRange(cb, firstVertex, (uint32_t)vertCount,
			m_batchVertices.data(),
			m_vk->pipeBatchAlpha.get(), m_vk->pipeBatchAdd.get(),
			m_vk->pipeBatchColorize.get(), m_vk->pipeBatchLessColorize.get(),
			sets, projView, &m_frameDrawCallCount);
	}

	// 诊断：统计一次真实提交（含 replay 里逐行血量文字各自的 FlushBatch）。
	// Profiler::Get().CountFlush(vertCount);

	clearCpu();
}

void Graphics::FlushInstances() {
	if (m_batchInstances.empty()) return;
	if (!m_vk || !m_vk->frameOpen) {
		m_batchInstances.clear();
		return;
	}

	auto& fr = m_vk->frames[m_vk->frameIdx];
	const size_t needBytes = m_batchInstances.size() * sizeof(InstanceRecord);
	if (fr.instCursor + needBytes > fr.instCap) {
		fr.instOverflow = true;   // 按缓冲归因 + 记录串行真实需求
		m_vk->frameInstDemand = std::max<uint64_t>(m_vk->frameInstDemand, (uint64_t)fr.instCursor + needBytes);
		if (!fr.overflowWarned) {
			LOG_WARN("Graphics") << "FlushInstances overflow ("
				<< fr.instCursor << "+" << needBytes
				<< ">" << fr.instCap << ")";
			fr.overflowWarned = true;
		}
		m_batchInstances.clear();
		return;
	}

	const uint32_t baseInstAbs = (uint32_t)(fr.instCursor / sizeof(InstanceRecord));
	std::memcpy(static_cast<char*>(fr.instBuf->MappedPtr()) + fr.instCursor,
		m_batchInstances.data(), needBytes);
	fr.instCursor += needBytes;

	VkCommandBuffer cb = m_vk->renderer->CurrentCmdBuffer();
	if (cb == VK_NULL_HANDLE) { m_batchInstances.clear(); return; }

	const glm::mat4 projView = m_projection * m_viewMatrix;
	const VkDescriptorSet textureSet = m_vk->texPool->DescriptorSet();
	// 用 m_queuedInstanceBlend（实例队列私有状态）而非 m_currentBlendMode（被
	// DrawTexture/DrawTextureMatrix 等读作 per-vert bm 默认值的全局态）。这两者必须
	// 分离，否则 Animator 的 glow Append 会污染同帧后续 shadow 的 bm，把影子打成 Add
	// 不可见——参见 AppendReanimInstance 主线程分支的同条注释。
	pvz::VulkanPipeline* pipe = m_queuedInstanceBlend == BlendMode::Add
		? m_vk->pipeInstAdd.get()
		: m_queuedInstanceBlend == BlendMode::WashedOut
			? m_vk->pipeInstColorize.get()
			: m_queuedInstanceBlend == BlendMode::LessWashedOut
				? m_vk->pipeInstLessColorize.get() : m_vk->pipeInstAlpha.get();

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Handle());
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Layout(),
		1, 1, &textureSet, 0, nullptr);
	vkCmdPushConstants(cb, pipe->Layout(), VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof(glm::mat4), &projView);
	VkBuffer instVertexBuffer = fr.instBuf->Handle();
	VkDeviceSize instVertexOffset = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &instVertexBuffer, &instVertexOffset);
	vkCmdDraw(cb, 4, (uint32_t)m_batchInstances.size(), 0, baseInstAbs);
	++m_frameDrawCallCount;

	m_batchInstances.clear();
}

int Graphics::BindTexture(uint32_t textureID) {
	// TextureBackend 已把 binding ID 编码为对应后端需要的值：Vulkan 为 bindless
	// 槽位，OpenGL 为 texture name。Graphics 保留该值，由 FlushBatch 按后端解释。
	return (int)textureID;
}

int Graphics::AddMatrix(const glm::mat4& matrix) {
	// 矩阵先保留在 CPU 列表中：Vulkan 提交到 SSBO，OpenGL 在 FlushBatch 时展开到顶点。
	m_batchMatrices.push_back(matrix);
	return (int)(m_batchMatrices.size() - 1);
}

void Graphics::AddVertices(const BatchVertex* vertices, int count) {
	// 主线程 cross-flush（worker 路径走 tl_record 直接进 slice 不经过这里）：
	// 如果 m_batchInstances 已积累记录，说明在本次 batch 写入之前 caller 调过
	// AppendReanimInstance。要保证 vkCmdDraw 顺序 == append 顺序（否则 instance
	// 会在 EndFrame 之前 mid-frame 被 blend-change 触发 flush，先于本批 batch 进入
	// cmd buffer，导致 lawn/UI 反盖住 plant body）——所以这里先 flush instance。
	// worker 模式不会走 main thread AddVertices；其 cmd-stream 由 ReplayAndEndParallel
	// 的 emitUpTo 按 SetBlend 段交错 emit，本来就保序。
	if (!m_batchInstances.empty()) {
		FlushInstances();
	}
	const PackedClipRect clip = CurrentPackedClipRect();
	for (int i = 0; i < count; ++i) {
		BatchVertex vertex = vertices[i];
		vertex.clipMinXY = clip.minXY;
		vertex.clipMaxXY = clip.maxXY;
		m_batchVertices.push_back(vertex);
	}
}

void Graphics::CheckBatch() {
	if (m_gl) {
		// 兼容路径使用可增长 GPU buffer，但限制单次 CPU 暂存，避免极端场景形成超大拷贝。
		if (m_batchVertices.size() + 6 >= std::max<std::size_t>(m_batchBufferCapacity, 65536)) {
			FlushBatch();
		}
		return;
	}
	// Phase 3b（+3c 修复）：主动 flush 在还能把当前批 fit 进剩余空间时。
	// 阈值要早于"已经填满"——因为 FlushBatch 是把整个 m_batchVertices append 到本帧 buffer，
	// 一旦 m_batchVertices ≥ 剩余空间，flush 就会 drop（参见 FlushBatch 里的 overflow 报错）。
	// 同时给下一次 DrawXxx 留一份 6 顶点 + 1 mat4 的安全余量。
	const size_t vertBytes = m_batchVertices.size() * sizeof(BatchVertex);
	const size_t matBytes = m_batchMatrices.size() * sizeof(glm::mat4);

	VkDeviceSize vboLeft = VBO_BYTES_INIT;
	VkDeviceSize ssboLeft = SSBO_BYTES_INIT;
	if (m_vk && m_vk->frameOpen) {
		const auto& fr = m_vk->frames[m_vk->frameIdx];
		vboLeft = (fr.vboCursor < fr.vboCap) ? (fr.vboCap - fr.vboCursor) : 0;
		ssboLeft = (fr.ssboCursor < fr.ssboCap) ? (fr.ssboCap - fr.ssboCursor) : 0;
	}
	constexpr VkDeviceSize kRoom = 6 * sizeof(BatchVertex) + sizeof(glm::mat4); // ~376 B
	if (vertBytes + kRoom >= vboLeft || matBytes + kRoom >= ssboLeft) {
		FlushBatch();
	}
}

void Graphics::DrawTexture(const Texture* tex, float x, float y, float width, float height,
	float rotation, const glm::vec4& tint) {
	if (!tex) {
		LOG_WARN("Graphics") << "DrawTexture: null texture pointer";
		return;
	}
	if (tl_record) { RecordDrawTexture(*tl_record, tex, x, y, width, height, rotation, tint); return; }

	int texIndex = BindTexture(tex->BindingId());

	// 构建局部变换矩阵：平移 -> 缩放 -> 旋转（绕中心）
	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	local = glm::scale(local, glm::vec3(width, height, 1.0f));
	if (rotation != 0.0f) {
		local = glm::translate(local, glm::vec3(0.5f, 0.5f, 0.0f));
		local = glm::rotate(local, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		local = glm::translate(local, glm::vec3(-0.5f, -0.5f, 0.0f));
	}
	glm::mat4 finalMatrix = m_transformStack.back() * local;
	int matrixIndex = AddMatrix(finalMatrix);

	// 转换颜色为 0-1 格式
	glm::vec4 normalizedTint = NormalizeColor(tint);

	// 构建两个三角形（6个顶点）的批处理顶点数据
	float bm = EncodeBatchBlendMode(m_currentBlendMode);
	BatchVertex vertices[6] = {
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r , normalizedTint.g , normalizedTint.b , normalizedTint.a, bm},
		{1.0f, 1.0f, 1.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g , normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r , normalizedTint.g , normalizedTint.b , normalizedTint.a, bm},
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r , normalizedTint.g, normalizedTint.b , normalizedTint.a, bm},
		{0.0f, 0.0f, 0.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r , normalizedTint.g , normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b , normalizedTint.a, bm}
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

/**
 * 把普通纹理矩形转换为 InstanceRecord，使跨对象的影子与 reanim 本体共享同一有序队列。
 */
void Graphics::DrawTextureInstanced(const Texture* tex, float x, float y, float width, float height,
	float rotation, const glm::vec4& tint) {
	if (!tex) {
		LOG_WARN("Graphics") << "DrawTextureInstanced: null texture pointer";
		return;
	}
	// 紧凑 InstanceRecord 不携带裁剪框；裁剪内容保留原 batch shader 路径。
	if (IsClipActive()) {
		DrawTexture(tex, x, y, width, height, rotation, tint);
		return;
	}

	// 与 DrawTexture 使用完全相同的局部变换，随后把最终 2D 仿射矩阵压缩进 InstanceRecord。
	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	local = glm::scale(local, glm::vec3(width, height, 1.0f));
	if (rotation != 0.0f) {
		local = glm::translate(local, glm::vec3(0.5f, 0.5f, 0.0f));
		local = glm::rotate(local, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		local = glm::translate(local, glm::vec3(-0.5f, -0.5f, 0.0f));
	}
	const glm::mat4& currentTransform = tl_record
		? tl_transformStack->back() : m_transformStack.back();
	const glm::mat4 finalMatrix = currentTransform * local;

	InstanceRecord rec{};
	rec.tA = finalMatrix[0][0];
	rec.tB = finalMatrix[0][1];
	rec.tC = finalMatrix[1][0];
	rec.tD = finalMatrix[1][1];
	rec.tx = finalMatrix[3][0];
	rec.ty = finalMatrix[3][1];

	const Texture* bindTex = tex->atlasPage ? tex->atlasPage : tex;
	rec.u0 = tex->aU0; rec.v0 = tex->aV0;
	rec.u1 = tex->aU1; rec.v1 = tex->aV1;
	rec.texSlot = bindTex->BindingId();

	// InstanceRecord 使用 RGBA8；按现有字形实例路径四舍五入并钳制 0..255 颜色约定。
	const auto pack8 = [](float value) {
		return static_cast<uint32_t>(std::clamp(static_cast<int>(value + 0.5f), 0, 255));
		};
	rec.colorRGBA8 = pack8(tint.r) | (pack8(tint.g) << 8)
		| (pack8(tint.b) << 16) | (pack8(tint.a) << 24);

	AppendReanimInstance(rec, BlendMode::Alpha);
}

/**
 * 直接向当前帧持久映射 VBO 写入三层规则网格，并用专用 pipeline 保序提交。
 * 水波和焦散都留在 shader 内计算，CPU 每帧只更新一个动画计数器。
 */
bool Graphics::DrawPoolEffect(const Texture* baseTex, const Texture* shadingTex,
	const Texture* causticTex, float offsetX, float offsetY,
	int poolCounter, bool isNight) {
	if (m_gl) {
		if (tl_record || !m_gl->IsFrameOpen()) return false;
		const std::array<const Texture*, kPoolLayerCount> textures = {
			baseTex, shadingTex, causticTex
		};
		for (const Texture* texture : textures) {
			// Pool Shader 的 UV 是规则网格坐标；当前三张 GameImage 均保持独立纹理。
			if (!texture || !texture->renderTexture || texture->atlasPage) return false;
		}

		FlushBatch();
		FlushInstances();
		const PackedClipRect clip = CurrentPackedClipRect();
		const float clipLeft = static_cast<float>(clip.minXY & 0xFFFFu);
		const float clipTop = static_cast<float>((clip.minXY >> 16) & 0xFFFFu);
		const float clipRight = static_cast<float>(clip.maxXY & 0xFFFFu);
		const float clipBottom = static_cast<float>((clip.maxXY >> 16) & 0xFFFFu);
		constexpr int kIndexOffsetX[6] = { 0, 0, 1, 0, 1, 1 };
		constexpr int kIndexOffsetY[6] = { 0, 1, 1, 0, 1, 0 };
		std::array<pvz::OpenGLVertex, kPoolVerticesPerLayer> vertices{};
		const glm::mat4 objectMatrix = m_transformStack.back();
		const glm::mat4 projectionView = m_projection * m_viewMatrix;

		for (int layer = 0; layer < kPoolLayerCount; ++layer) {
			std::size_t vertexIndex = 0;
			for (int x = 0; x < kPoolGridColumns; ++x) {
				for (int y = 0; y < kPoolGridRows; ++y) {
					for (int i = 0; i < 6; ++i) {
						const int gridX = x + kIndexOffsetX[i];
						const int gridY = y + kIndexOffsetY[i];
						const bool caustic = layer == 2;
						const float drawX = caustic
							? (kPoolCausticWidth / kPoolGridColumns) * gridX + 45.0f + offsetX
							: (kPoolBaseWidth / kPoolGridColumns) * gridX + 35.0f + offsetX;
						const float drawY = caustic
							? kPoolCausticCellHeight * gridY + 288.0f + offsetY
							: (kPoolBaseHeight / kPoolGridRows) * gridY + 279.0f + offsetY;
						float color = 1.0f;
						if (caustic) {
							if (gridX == 0 || gridX == kPoolGridColumns || gridY == 0) color = 32.0f / 255.0f;
							else if (isNight) color = 48.0f / 255.0f;
							else color = gridX <= 7 ? 192.0f / 255.0f : 128.0f / 255.0f;
						}
						const glm::vec4 transformed = objectMatrix * glm::vec4(drawX, drawY, 0.0f, 1.0f);
						auto& vertex = vertices[vertexIndex++];
						vertex.x = transformed.x;
						vertex.y = transformed.y;
						vertex.u = gridX / static_cast<float>(kPoolGridColumns);
						vertex.v = gridY / static_cast<float>(kPoolGridRows);
						vertex.r = vertex.g = vertex.b = vertex.a = color;
						vertex.clipLeft = clipLeft;
						vertex.clipTop = clipTop;
						vertex.clipRight = clipRight;
						vertex.clipBottom = clipBottom;
					}
				}
			}
			if (!m_gl->SubmitPoolLayer(textures[layer]->BindingId(), layer,
				static_cast<float>(poolCounter), vertices.data(), vertices.size(), projectionView)) {
				return false;
			}
		}
		return true;
	}
	if (tl_record || !m_vk || !m_vk->frameOpen || !m_vk->pipePool) return false;

	const std::array<const Texture*, kPoolLayerCount> textures = {
		baseTex, shadingTex, causticTex
	};
	for (const Texture* texture : textures) {
		// PoolEffect 资源是独立 GameImage；若以后被收入图集，必须同时增加 UV region 支持。
		if (!texture || !texture->renderTexture || texture->atlasPage) return false;
	}

	// 专用 draw 必须排在此前普通背景之后、随后游戏对象之前。
	FlushBatch();
	FlushInstances();

	auto& fr = m_vk->frames[m_vk->frameIdx];
	constexpr size_t kVertexCount = kPoolVerticesPerLayer * kPoolLayerCount;
	const VkDeviceSize vertexBytes = kVertexCount * sizeof(BatchVertex);
	const VkDeviceSize matrixBytes = sizeof(glm::mat4);
	const bool vboOver = fr.vboCursor + vertexBytes > fr.vboCap;
	const bool ssboOver = fr.ssboCursor + matrixBytes > fr.ssboCap;
	if (vboOver || ssboOver) {
		if (vboOver) {
			fr.vboOverflow = true;
			m_vk->frameVboDemand = std::max<uint64_t>(
				m_vk->frameVboDemand, static_cast<uint64_t>(fr.vboCursor + vertexBytes));
		}
		if (ssboOver) {
			fr.ssboOverflow = true;
			m_vk->frameSsboDemand = std::max<uint64_t>(
				m_vk->frameSsboDemand, static_cast<uint64_t>(fr.ssboCursor + matrixBytes));
		}
		if (!fr.overflowWarned) {
			LOG_WARN("Graphics") << "DrawPoolEffect frame buffer overflow — dropping pool layers";
			fr.overflowWarned = true;
		}
		return false;
	}

	const uint32_t matrixIndex =
		static_cast<uint32_t>(fr.ssboCursor / sizeof(glm::mat4));
	std::memcpy(static_cast<char*>(fr.ssbo->MappedPtr()) + fr.ssboCursor,
		&m_transformStack.back(), matrixBytes);

	const uint32_t firstVertex =
		static_cast<uint32_t>(fr.vboCursor / sizeof(BatchVertex));
	auto* vertices = reinterpret_cast<BatchVertex*>(
		static_cast<char*>(fr.vbo->MappedPtr()) + fr.vboCursor);
	const PackedClipRect clip = CurrentPackedClipRect();
	constexpr int kIndexOffsetX[6] = { 0, 0, 1, 0, 1, 1 };
	constexpr int kIndexOffsetY[6] = { 0, 1, 1, 0, 1, 0 };

	for (int layer = 0; layer < kPoolLayerCount; ++layer) {
		size_t layerVertex = 0;
		for (int x = 0; x < kPoolGridColumns; ++x) {
			for (int y = 0; y < kPoolGridRows; ++y) {
				for (int i = 0; i < 6; ++i) {
					const int gridX = x + kIndexOffsetX[i];
					const int gridY = y + kIndexOffsetY[i];
					const bool caustic = layer == 2;
					const float drawX = caustic
						? (kPoolCausticWidth / kPoolGridColumns) * gridX + 45.0f + offsetX
						: (kPoolBaseWidth / kPoolGridColumns) * gridX + 35.0f + offsetX;
					const float drawY = caustic
						? kPoolCausticCellHeight * gridY + 288.0f + offsetY
						: (kPoolBaseHeight / kPoolGridRows) * gridY + 279.0f + offsetY;

					float color = 1.0f;
					if (caustic) {
						if (gridX == 0 || gridX == kPoolGridColumns || gridY == 0) {
							color = 32.0f / 255.0f;
						}
						else if (isNight) {
							color = 48.0f / 255.0f;
						}
						else {
							color = gridX <= 7 ? 192.0f / 255.0f : 128.0f / 255.0f;
						}
					}

					vertices[layer * kPoolVerticesPerLayer + layerVertex++] = {
						drawX,
						drawY,
						gridX / static_cast<float>(kPoolGridColumns),
						gridY / static_cast<float>(kPoolGridRows),
						textures[layer]->BindingId(),
						matrixIndex,
						color, color, color, color,
						0.0f,
						clip.minXY,
						clip.maxXY
					};
				}
			}
		}
	}

	fr.ssboCursor += matrixBytes;
	fr.vboCursor += vertexBytes;

	VkCommandBuffer cb = m_vk->renderer->CurrentCmdBuffer();
	if (cb == VK_NULL_HANDLE) return false;

	VkBuffer vbo = fr.vbo->Handle();
	const VkDeviceSize vboOffset = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &vboOffset);
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vk->pipePool->Handle());
	const VkDescriptorSet sets[2] = {
		fr.matrixSet,
		m_vk->texPool->DescriptorSet()
	};
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_vk->pipePool->Layout(), 0, 2, sets, 0, nullptr);

	PoolPushConstants constants{};
	constants.projView = m_projection * m_viewMatrix;
	constants.poolCounter = static_cast<float>(poolCounter);
	for (int layer = 0; layer < kPoolLayerCount; ++layer) {
		constants.poolLayer = static_cast<float>(layer);
		vkCmdPushConstants(cb, m_vk->pipePool->Layout(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(constants), &constants);
		vkCmdDraw(cb, kPoolVerticesPerLayer, 1,
			firstVertex + layer * kPoolVerticesPerLayer, 0);
		++m_frameDrawCallCount;
	}
	return true;
}

bool Graphics::InitializeOpenGL(pvz::OpenGLRenderer* renderer,
	pvz::TextureBackend* textureBackend) {
	if (!renderer || !textureBackend
		|| textureBackend->Backend() != pvz::RendererBackend::OpenGL) {
		LOG_ERROR("Graphics") << "InitializeOpenGL 参数无效";
		return false;
	}
	if (m_gl || m_vk) {
		LOG_ERROR("Graphics") << "渲染后端已经初始化";
		return false;
	}
	const uint8_t white[4] = { 255, 255, 255, 255 };
	pvz::RenderTexture* whiteTexture = textureBackend->CreateTextureRGBA8(1, 1, white);
	if (!whiteTexture) {
		LOG_ERROR("Graphics") << "OpenGL 白色纹理上传失败";
		return false;
	}
	m_gl = renderer;
	m_textureBackend = textureBackend;
	m_backend = pvz::RendererBackend::OpenGL;
	m_whiteTextureHandle = whiteTexture;
	m_whiteTexture = whiteTexture->bindingId;
	m_useInstancePath = false;
	// Vulkan worker 直接写 mapped buffer；兼容后端保持串行 CPU 展开，所有 GL API
	// 只发生在持有 Context 的主线程，提交顺序与现有慢路径一致。
	m_parallelDrawEnabled = false;
	m_batchBufferCapacity = 65536;
	m_batchVertices.reserve(m_batchBufferCapacity);
	m_batchMatrices.reserve(m_batchBufferCapacity / 6);
	LOG_WARN("Graphics") << "OpenGL 兼容路径启用: batch=" << renderer->BatchPathName()
		<< ", GPU instancing=off, parallel record=off";
	return true;
}

void Graphics::DrawTextureMatrix(const Texture* tex, const glm::mat4& transform,
	float pivotX, float pivotY, const glm::vec4& tint, BlendMode blendMode) {
	if (!tex) return;
	if (tl_record) { RecordDrawTextureMatrix(*tl_record, tex, transform, pivotX, pivotY, tint, blendMode); return; }

	// 图集重映射：若该纹理已被打进图集页，则改绑图集页并把 UV 收窄到子矩形
	const Texture* bindTex = tex;
	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	if (tex->atlasPage) {
		bindTex = tex->atlasPage;
		u0 = tex->aU0; v0 = tex->aV0; u1 = tex->aU1; v1 = tex->aV1;
	}
	int texIndex = BindTexture(bindTex->BindingId());

	glm::mat4 pivotTransform;
	if (pivotX != 0.0f || pivotY != 0.0f) {
		glm::vec3 pivot(pivotX, pivotY, 0.0f);
		pivotTransform = glm::translate(glm::mat4(1.0f), pivot) *
			transform *
			glm::translate(glm::mat4(1.0f), -pivot);
	}
	else {
		pivotTransform = transform;
	}
	// 栈顶为单位阵时跳过 mat4×mat4（Animator::Draw 压入单位阵，~9万 sprite/帧的串行热点）
	glm::mat4 finalMatrix = m_transformIsIdentity.back()
		? pivotTransform
		: (m_transformStack.back() * pivotTransform);
	int matrixIndex = AddMatrix(finalMatrix);

	// 转换颜色为 0-1 格式
	glm::vec4 normalizedTint = NormalizeColor(tint);

	// 确定实际混合模式：None 表示使用当前全局混合模式
	BlendMode actualMode = (blendMode == BlendMode::None) ? m_currentBlendMode : blendMode;
	float bm = EncodeBatchBlendMode(actualMode);

	BatchVertex vertices[6] = {
		{0.0f, 1.0f, u0, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 1.0f, u1, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, u1, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{0.0f, 1.0f, u0, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{0.0f, 0.0f, u0, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, u1, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm}
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

void Graphics::AppendReanimInstance(const InstanceRecord& rec, BlendMode blendMode) {
	// Worker thread path: write into slot's slice
	if (tl_record) {
		WorkerRecord& r = *tl_record;
		VkWorkerSlice& sl = r.slice;

		sl.instDemand++;   // 计入"想写"的实例（含被容量拒绝的），供精确扩容 + 负载均衡权重
		if (sl.instCount >= sl.instCap) {
			sl.instOverflowed = true;
			return;
		}

		// Save/restore tl_blend，与 RecordDrawTextureMatrix(行 1897-1951) 的模式对齐。
		// 不恢复会污染同 slot 后续 Record* 调用——典型表现是 Animator 画完 glow（Add）后，
		// 同一 worker 的 ShadowComponent::Draw 把 bm 字段（行 1893 读 tl_blend）写成 1.0，
		// 影子被 Add 混合打成不可见。SetBlend cmd 必须先于 instance 数据写入，以便
		// instOffsetAtCmd 正确捕获"blend 切换时的 instance 计数"——回放按它分段切 pipeline。
		const BlendMode savedBlend = tl_blend;
		const bool needSwitch = (blendMode != tl_blend);
		if (needSwitch) {
			RecordSetBlendMode(r, blendMode);
		}

		sl.instPtr[sl.instCount++] = rec;

		if (needSwitch) {
			RecordSetBlendMode(r, savedBlend);
		}
		return;
	}

	// Main thread serial path——两个独立修复：
	//
	// (1) Cross-flush：如果 m_batchVertices 已积累 batch quads（典型是先画 lawn/shadow/UI
	//     再画 plant），先 FlushBatch 让那批 vkCmdDraw 进 cmd buffer，再让本次 instance
	//     append 接在后面。否则后续 mid-frame FlushInstances（由 blend 切换触发）会先于
	//     m_batchVertices 的 EndFrame flush 进 cmd buffer，造成 plant body 被 lawn 反盖
	//     不可见的 Z-order bug。worker 路径有 emitUpTo 按段交错回放，本身保序，无需此 fix。
	//
	// (2) Blend 状态用独立的 m_queuedInstanceBlend，**不再触碰** m_currentBlendMode。
	//     原始代码用 m_currentBlendMode 同时承担两个不变量：
	//       (a) 实例队列当前堆的 blend（FlushInstances 据此选 pipeline）
	//       (b) DrawTexture/DrawTextureMatrix 的 per-vert bm 默认值
	//     一旦 glow Append 把它改成 Add，下游所有 (b) 的消费者（典型是 ShadowComponent）
	//     就被污染——shadow 顶点 bm=1，整段走 pipeBatchAdd 被打成不可见。
	//     分离后 flush 计数与原始代码字节级一致，行为不变，只切断了污染路径。
	if (!m_batchVertices.empty()) {
		FlushBatch();
	}
	if (blendMode != m_queuedInstanceBlend) {
		FlushInstances();
		m_queuedInstanceBlend = blendMode;
	}
	if ((int)m_batchInstances.size() >= m_batchInstancesLimit) {
		FlushInstances();
	}
	m_batchInstances.push_back(rec);
}

bool Graphics::BeginReanimInstanceWrite(uint32_t maxCount, BlendMode blendMode,
	ReanimInstanceWriteSpan& outSpan) {
	outSpan = {};
	if (!tl_record || maxCount == 0 || IsClipActive()) return false;

	WorkerRecord& record = *tl_record;
	VkWorkerSlice& slice = record.slice;
	if (maxCount > slice.instCap - slice.instCount) return false;

	outSpan.records = slice.instPtr + slice.instCount;
	outSpan.owner = &record;
	outSpan.reserved = maxCount;
	outSpan.savedBlend = tl_blend;
	outSpan.switchedBlend = blendMode != tl_blend;
	if (outSpan.switchedBlend) RecordSetBlendMode(record, blendMode);
	return true;
}

void Graphics::EndReanimInstanceWrite(ReanimInstanceWriteSpan& span, uint32_t count) {
	if (!span.records || !span.owner || span.owner != tl_record) {
		span = {};
		return;
	}

	WorkerRecord& record = *span.owner;
	VkWorkerSlice& slice = record.slice;
	if (count > span.reserved) {
		LOG_WARN("Graphics") << "EndReanimInstanceWrite count exceeds reservation ("
			<< count << "/" << span.reserved << ")";
		count = span.reserved;
	}
	slice.instDemand += count;
	slice.instCount += count;
	if (span.switchedBlend) RecordSetBlendMode(record, span.savedBlend);
	span = {};
}

void Graphics::DrawTextureRegion(const Texture* tex,
	float srcX, float srcY, float srcW, float srcH,
	float dstX, float dstY, float dstW, float dstH,
	float rotation, const glm::vec4& tint)
{
	if (!tex || tex->BindingId() == 0) return;
	if (tl_record) { RecordDrawTextureRegion(*tl_record, tex, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, rotation, tint); return; }

	// 计算归一化 UV 坐标
	float u0 = srcX / tex->width;
	float v0 = srcY / tex->height;
	float u1 = (srcX + srcW) / tex->width;
	float v1 = (srcY + srcH) / tex->height;

	// 构建局部变换矩阵（平移 -> 缩放 -> 绕中心旋转）
	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(dstX, dstY, 0.0f));
	local = glm::scale(local, glm::vec3(dstW, dstH, 1.0f));
	if (rotation != 0.0f) {
		local = glm::translate(local, glm::vec3(0.5f, 0.5f, 0.0f));
		local = glm::rotate(local, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		local = glm::translate(local, glm::vec3(-0.5f, -0.5f, 0.0f));
	}

	int texIndex = BindTexture(tex->BindingId());
	glm::mat4 finalMatrix = m_transformStack.back() * local;
	int matrixIndex = AddMatrix(finalMatrix);

	// 转换颜色为 0-1 格式
	glm::vec4 normalizedTint = NormalizeColor(tint);

	float bm = EncodeBatchBlendMode(m_currentBlendMode);
	BatchVertex vertices[6] = {
		{0.0f, 1.0f, u0, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 1.0f, u1, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, u1, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{0.0f, 1.0f, u0, v1, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{0.0f, 0.0f, u0, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm},
		{1.0f, 0.0f, u1, v0, (uint32_t)texIndex, (uint32_t)matrixIndex, normalizedTint.r, normalizedTint.g, normalizedTint.b, normalizedTint.a, bm}
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

// UTF-8 解码：从 s[i] 起解一个码点，前进 i。非法字节返回 0xFFFD 并前进 1。
// HP 字符集（数字/标点/CJK 本体一类二类）全在 BMP，但仍按通用 UTF-8 正确解多字节。
static uint32_t DecodeUtf8(const std::string& s, size_t& i) {
	const unsigned char c = (unsigned char)s[i];
	uint32_t cp; int extra;
	if (c < 0x80)             { cp = c;        extra = 0; }
	else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
	else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
	else if ((c >> 3) == 0x1E){ cp = c & 0x07; extra = 3; }
	else { ++i; return 0xFFFD; }
	++i;
	for (int k = 0; k < extra && i < s.size(); ++k, ++i)
		cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
	return cp;
}

// 文字超采样：letterbox 把整幅逻辑画面放大 letterboxScale 倍铺满全屏。文字若按逻辑
// fontSize 光栅化再被 GPU 放大就会糊。改为按物理像素 physSize 光栅化，绘制时再按
// superSample 除回逻辑尺寸——屏幕布局不变、像素密度匹配屏幕。窗口模式 scale=1 → 恒等。
static int ComputeTextRasterSize(int fontSize, float letterboxScale, float& outSuperSample) {
	int phys = (int)std::lround(fontSize * (double)letterboxScale);
	if (phys < 1) phys = 1;
	outSuperSample = (float)phys / (float)fontSize;
	return phys;
}

glm::vec2 Graphics::MeasureTextSize(const std::string& text, const std::string& fontKey,
	int fontSize, float scale) const {
	if (text.empty() || fontSize <= 0 || scale <= 0.0f) return glm::vec2(0.0f);

	float superSample = 1.0f;
	const int physicalSize = ComputeTextRasterSize(fontSize, m_letterboxScale, superSample);
	TTF_Font* font = ResourceManager::GetInstance().GetFont(fontKey, physicalSize);
	int width = 0;
	int height = 0;
	if (!font || TTF_SizeUTF8(font, text.c_str(), &width, &height) != 0) {
		return glm::vec2(0.0f);
	}
	return glm::vec2(static_cast<float>(width), static_cast<float>(height))
		* scale / superSample;
}

float Graphics::MeasureTextWidth(const std::string& text, const std::string& fontKey,
	int fontSize, float scale) const {
	return MeasureTextSize(text, fontKey, fontSize, scale).x;
}

uint32_t Graphics::GetOrCreateTextTexture(const std::string& text, const std::string& fontKey,
	int fontSize, const glm::vec4& color, int& outWidth, int& outHeight, float& outSuperSample) {
	// 按物理像素超采样光栅化。physSize 进缓存键，避免窗口/全屏两种密度的同一句文字串号。
	float superSample;
	const int physSize = ComputeTextRasterSize(fontSize, m_letterboxScale, superSample);
	outSuperSample = superSample;

	// 生成缓存键
	std::stringstream ss;
	ss << text << "|" << fontKey << "|" << physSize << "|"
		<< (int)color.r << "," << (int)color.g << "," << (int)color.b << "," << (int)color.a;
	std::string key = ss.str();

	auto it = m_textCache.find(key);
	if (it != m_textCache.end()) {
		// 命中缓存：移到链表头部（最近使用）
		Profiler::Get().CountText(/*miss*/false);
		m_textCacheOrder.erase(it->second.second);
		m_textCacheOrder.push_front(key);
		it->second.second = m_textCacheOrder.begin();
		outWidth = it->second.first.width;
		outHeight = it->second.first.height;
		return it->second.first.BindingId();
	}

	// 未命中：TTF 光栅化 + 格式转换 + GPU 上传（+ 满载时淘汰旧纹理）。这段是串行 replay 里
	// 随"掉血节奏"放大的成本——单独计时(7a)+计数，验证血量数字 thrash 假设。
	Profiler::Get().CountText(/*miss*/true);
	PROFILE_SCOPE("7a.TextRaster_miss");

	CachedText entry;
	if (!RenderTextToBackendTexture(text, fontKey, physSize, color, entry)) {
		return 0;
	}
	entry.superSample = superSample;
	outWidth = entry.width;
	outHeight = entry.height;

	// 超出容量时淘汰最久未使用的条目
	if ((int)m_textCache.size() >= TEXT_CACHE_MAX_SIZE) {
		const std::string& lruKey = m_textCacheOrder.back();
		auto lruIt = m_textCache.find(lruKey);
		if (lruIt != m_textCache.end()) {
			if (m_textureBackend && lruIt->second.first.texture) {
				m_textureBackend->DestroyTexture(lruIt->second.first.texture);
			}
			m_textCache.erase(lruIt);
		}
		m_textCacheOrder.pop_back();
	}

	// 插入新条目到链表头部
	m_textCacheOrder.push_front(key);
	m_textCache[key] = { entry, m_textCacheOrder.begin() };
	return entry.BindingId();
}

bool Graphics::RenderTextToBackendTexture(const std::string& text, const std::string& fontKey,
	int fontSize, const glm::vec4& color, CachedText& out, bool trimTransparent) {
	// TTF → SDL_Surface → ABGR8888 → 当前 TextureBackend；失败保持空句柄。
	if (!m_textureBackend) {
		LOG_ERROR("Graphics") << "RenderTextToBackendTexture: 纹理后端尚未初始化";
		return false;
	}
	if (text.empty()) return false;

	TTF_Font* font = ResourceManager::GetInstance().GetFont(fontKey, fontSize);
	if (!font) {
		LOG_WARN("Graphics") << "RenderTextToBackendTexture: 找不到字体: " << fontKey
			<< " size=" << fontSize;
		return false;
	}

	const SDL_Color sdlColor = ToSDLColor(color);
	SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), sdlColor);
	if (!surface) {
		LOG_ERROR("Graphics") << "RenderTextToBackendTexture: TTF_RenderUTF8_Blended 失败: "
			<< TTF_GetError();
		return false;
	}

	// 小端 ABGR8888 内存布局即 RGBA 字节序，两个纹理后端共用该输入约定。
	SDL_Surface* conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
	SDL_FreeSurface(surface);
	if (!conv) {
		LOG_ERROR("Graphics") << "RenderTextToBackendTexture: SDL_ConvertSurfaceFormat 失败: "
			<< SDL_GetError();
		return false;
	}

	// pinned 整行文字会被高频重绘；裁掉 TTF 行面四周的全透明边缘，减少
	// GPU 对透明像素的无效 fragment invocation。offset 记住原 surface 原点，绘制位置不变。
	SDL_Surface* uploadSurface = conv;
	if (trimTransparent) {
		int minX = conv->w, minY = conv->h, maxX = -1, maxY = -1;
		const auto* pixels = static_cast<const uint8_t*>(conv->pixels);
		for (int y = 0; y < conv->h; ++y) {
			const auto* row = pixels + static_cast<size_t>(y) * conv->pitch;
			for (int x = 0; x < conv->w; ++x) {
				if (row[x * 4 + 3] == 0) continue;
				minX = std::min(minX, x); minY = std::min(minY, y);
				maxX = std::max(maxX, x); maxY = std::max(maxY, y);
			}
		}
		if (maxX >= minX && maxY >= minY
			&& (minX != 0 || minY != 0 || maxX + 1 != conv->w || maxY + 1 != conv->h)) {
			const int tightW = maxX - minX + 1;
			const int tightH = maxY - minY + 1;
			SDL_Surface* tight = SDL_CreateRGBSurfaceWithFormat(
				0, tightW, tightH, 32, SDL_PIXELFORMAT_ABGR8888);
			if (tight) {
				SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
				SDL_Rect src{ minX, minY, tightW, tightH };
				SDL_Rect dst{ 0, 0, tightW, tightH };
				if (SDL_BlitSurface(conv, &src, tight, &dst) == 0) {
					uploadSurface = tight;
					out.offsetX = minX;
					out.offsetY = minY;
				}
				else {
					SDL_FreeSurface(tight);
				}
			}
		}
	}

	pvz::RenderTexture* uploaded = m_textureBackend->CreateTextureRGBA8(
		uploadSurface->w, uploadSurface->h, uploadSurface->pixels);
	const int w = uploadSurface->w;
	const int h = uploadSurface->h;
	if (uploadSurface != conv) SDL_FreeSurface(uploadSurface);
	SDL_FreeSurface(conv);
	if (!uploaded) {
		LOG_ERROR("Graphics") << "RenderTextToBackendTexture: 纹理上传失败";
		return false;
	}

	out.texture = uploaded;
	out.width = w;
	out.height = h;
	return true;
}

void Graphics::BuildGlyphAtlas(const std::string& fontKey, int fontSize, GlyphAtlas& atlas) {
	// glyphs.clear()/重建会使 worker 缓存的 GlyphInfo 指针失效；并行录制只读此代际。
	++m_glyphAtlasGeneration;
	Profiler::Get().CountGlyphBuild();
	atlas.glyphs.clear();
	// 旧纹理先还给当前后端（重建场景：新码点 / letterbox 变化）。
	if (m_textureBackend && atlas.texture) {
		// GL 立即删除 texture name；前面排队的字形仍引用旧纹理及旧 UV。
		// 必须先提交 CPU batch，否则名称复用后会采样新图集的错误位置，或变黑/消失。
		// Vulkan 已延迟回收纹理和 descriptor 槽位，无需在这里打断其批次。
		if (m_gl) FlushBatch();
		m_textureBackend->DestroyTexture(atlas.texture);
		atlas.texture = nullptr;
	}

	if (!m_textureBackend || atlas.covered.empty()) return;

	float superSample;
	const int physSize = ComputeTextRasterSize(fontSize, m_letterboxScale, superSample);
	TTF_Font* font = ResourceManager::GetInstance().GetFont(fontKey, physSize);
	if (!font) {
		LOG_WARN("Graphics") << "BuildGlyphAtlas: 找不到字体 " << fontKey << " size=" << physSize;
		return;  // textureID 保持 0 → 上层 fallback
	}

	const SDL_Color white{ 255, 255, 255, 255 };
	const int pad = 1;

	// ⚠ SDL 的 TTF_RenderGlyph_Blended 返回的是「整行高(=FontHeight)、字形已按基线摆放」的 surface
	// （字形墨迹顶部留白 = ascent - maxy，所有字形基线统一落在第 ascent 行），并非紧致位图。
	// 若整张入图集、把 surf_h 当字形高存，DrawGlyphRun 再叠加 (ascent - bearingY) 偏移，基线对齐量
	// 就被算两遍（双重计算）：低 maxy 的字形（冒号）被多下推最多、汉字最少 → 同一行文字高低错落。
	// 修法：入图集时按真实墨迹框 [minx, ascent-maxy, sz_width, sz_rows] 裁成紧致位图，GlyphInfo 即回到
	// 紧致口径，DrawGlyphRun 那条为紧致字形写的基线公式无需改动、自动成立。裁剪框经探针逐字形核对。
	const int ascent = TTF_FontAscent(font);

	// 第一趟：渲染每个字形、裁紧致墨迹框、算单行布局。
	struct Pending { uint32_t cp; SDL_Surface* surf; SDL_Rect src; int advance, bearingX, bearingY; int x; };
	std::vector<Pending> pend;
	pend.reserve(atlas.covered.size());
	int atlasW = pad, atlasH = 1;
	for (uint32_t cp : atlas.covered) {
		// TTF_GlyphMetrics/RenderGlyph 是 Uint16 旧 API：非 BMP 码点（>0xFFFF）一旦 (Uint16) 截断会
		// 别名到错误的 BMP 字形（U+10041→'A'）。显式跳过 → 上层缺字形整串回退 DrawText（其
		// TTF_RenderUTF8 能正确渲染全 Unicode）。HUD 数字/CJK 全在 BMP，不触发。
		if (cp > 0xFFFF) continue;
		int minx, maxx, miny, maxy, adv;
		if (TTF_GlyphMetrics(font, (Uint16)cp, &minx, &maxx, &miny, &maxy, &adv) != 0)
			continue;  // 该字形不可用，跳过（缺字形会触发整串 fallback）
		SDL_Surface* gs = TTF_RenderGlyph_Blended(font, (Uint16)cp, white);  // 空白字形可能返回 null
		const int sw = gs ? gs->w : 0;
		const int sh = gs ? gs->h : 0;
		// 字形墨迹在整行 surface 内的位置：行 [ascent-maxy, ascent-miny)，宽高 sz_width=maxx-minx /
		// sz_rows=maxy-miny。clamp 防越界（重音符等 maxy>ascent、负 minx 等极端字形；HUD 数字不触发）。
		SDL_Rect src{ minx, ascent - maxy, maxx - minx, maxy - miny };
		if (src.x < 0) { src.w += src.x; src.x = 0; }
		if (src.y < 0) { src.h += src.y; src.y = 0; }
		if (src.x + src.w > sw) src.w = sw - src.x;
		if (src.y + src.h > sh) src.h = sh - src.y;
		if (src.w < 0) src.w = 0;
		if (src.h < 0) src.h = 0;
		Pending p{ cp, gs, src, adv, minx, maxy, atlasW };
		pend.push_back(p);
		atlasW += src.w + pad;
		if (src.h > atlasH) atlasH = src.h;
	}
	if (pend.empty()) return;

	// 目标图集 surface（ABGR8888，清零）。
	SDL_Surface* atlasSurf = SDL_CreateRGBSurfaceWithFormat(0, atlasW, atlasH, 32, SDL_PIXELFORMAT_ABGR8888);
	if (!atlasSurf) {
		for (auto& p : pend) if (p.surf) SDL_FreeSurface(p.surf);
		LOG_ERROR("Graphics") << "BuildGlyphAtlas: SDL_CreateRGBSurfaceWithFormat 失败: " << SDL_GetError();
		return;
	}
	SDL_FillRect(atlasSurf, nullptr, SDL_MapRGBA(atlasSurf->format, 0, 0, 0, 0));

	// 第二趟：blit 紧致墨迹框进图集（NONE 混合=直接覆盖含 alpha），记录 GlyphInfo（紧致口径）。
	for (auto& p : pend) {
		const int gw = p.src.w;
		const int gh = p.src.h;
		if (p.surf && gw > 0 && gh > 0) {
			SDL_SetSurfaceBlendMode(p.surf, SDL_BLENDMODE_NONE);
			SDL_Rect src = p.src;
			SDL_Rect dst{ p.x, 0, gw, gh };
			SDL_BlitSurface(p.surf, &src, atlasSurf, &dst);
		}
		GlyphInfo gi;
		gi.u0 = (float)p.x / (float)atlasW;
		gi.v0 = 0.0f;
		gi.u1 = (float)(p.x + gw) / (float)atlasW;
		gi.v1 = (float)gh / (float)atlasH;
		gi.pxW = gw;
		gi.pxH = gh;
		gi.advance = p.advance;
		gi.bearingX = p.bearingX;
		gi.bearingY = p.bearingY;
		atlas.glyphs[p.cp] = gi;
		if (p.surf) SDL_FreeSurface(p.surf);
	}

	pvz::RenderTexture* uploaded = m_textureBackend->CreateTextureRGBA8(atlasW, atlasH, atlasSurf->pixels);
	SDL_FreeSurface(atlasSurf);
	if (!uploaded) {
		LOG_ERROR("Graphics") << "BuildGlyphAtlas: CreateTextureRGBA8 失败";
		atlas.glyphs.clear();
		return;
	}
	atlas.texture = uploaded;
	atlas.texW = atlasW;
	atlas.texH = atlasH;
	atlas.ascent = ascent;
	atlas.physSize = physSize;
	atlas.superSample = superSample;
}

GlyphAtlas& Graphics::GetOrBuildGlyphAtlas(const std::string& fontKey, int fontSize,
	const std::vector<uint32_t>& needed) {
	const std::string key = fontKey + "|" + std::to_string(fontSize);
	GlyphAtlas& atlas = m_glyphAtlases[key];

	float ss;
	const int curPhys = ComputeTextRasterSize(fontSize, m_letterboxScale, ss);
	// physSize 变化（letterbox）或图集未建 → 需重建；有新码点 → 加入 covered 后重建。
	bool needRebuild = (atlas.BindingId() == 0) || (atlas.physSize != curPhys);
	for (uint32_t cp : needed)
		if (atlas.covered.insert(cp).second) needRebuild = true;

	if (needRebuild) BuildGlyphAtlas(fontKey, fontSize, atlas);
	return atlas;
}

void Graphics::ClearGlyphAtlases() {
	if (m_textureBackend) {
		for (auto& kv : m_glyphAtlases)
			if (kv.second.texture) m_textureBackend->DestroyTexture(kv.second.texture);
	}
	m_glyphAtlases.clear();
	++m_glyphAtlasGeneration;
}

CachedText Graphics::AcquireTextTexture(const std::string& text, const std::string& fontKey,
	int fontSize, const glm::vec4& color) {
	// 与 GetOrCreateTextTexture 同款超采样：按物理像素光栅化，physSize 进键。
	float superSample;
	const int physSize = ComputeTextRasterSize(fontSize, m_letterboxScale, superSample);

	std::stringstream ss;
	ss << text << "|" << fontKey << "|" << physSize << "|"
		<< (int)color.r << "," << (int)color.g << "," << (int)color.b << "," << (int)color.a;
	std::string key = ss.str();

	auto it = m_pinnedTextCache.find(key);
	if (it != m_pinnedTextCache.end()) {
		return it->second;
	}

	// RenderTextToBackendTexture 会上传 GPU 纹理且写共享的
	// m_pinnedTextCache——两者都只能在主线程做。worker 线程
	// （tl_record 非空，例如并行 Draw 录制阶段）上未命中时直接放弃本帧返回空
	// 句柄，由主线程预热缓存后即稳定命中。缺少此守卫会在 worker 线程拿到无效/
	// 串号的纹理 ID（表现为数字变成贴图或别的文字），并发写 map 还会损坏容器。
	if (tl_record) return {};

	CachedText entry;
	if (!RenderTextToBackendTexture(text, fontKey, physSize, color, entry, /*trimTransparent*/true)) {
		return {};
	}
	entry.superSample = superSample;
	entry.generation = m_textGeneration;
	m_pinnedTextCache.emplace(std::move(key), entry);
	return entry;
}

void Graphics::DrawCachedText(const CachedText& handle, float x, float y, float scale) {
	if (handle.BindingId() == 0) return;
	// 过期句柄（底层纹理已被 ClearPinnedTextCache 销毁）直接丢弃，避免读悬垂的 bindless 槽位。
	// 消费者应在主线程经 IsCachedTextStale 重新获取；这里只做防御。
	if (handle.generation != m_textGeneration) return;
	if (tl_record) { RecordDrawCachedText(*tl_record, handle, x, y, scale); return; }

	int texIndex = BindTexture(handle.BindingId());
	glm::mat4 local = glm::mat4(1.0f);
	// 超采样纹理：除回逻辑尺寸保证屏幕布局不变（handle.superSample=1 时等价）。
	const float inv = scale / handle.superSample;
	local = glm::translate(local, glm::vec3(
		x + handle.offsetX * inv, y + handle.offsetY * inv, 0.0f));
	local = glm::scale(local, glm::vec3(handle.width * inv, handle.height * inv, 1.0f));
	glm::mat4 finalMatrix = m_transformStack.back() * local;
	int matrixIndex = AddMatrix(finalMatrix);

	BatchVertex vertices[6] = {
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 1.0f, 1.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{0.0f, 0.0f, 0.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f}
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

void Graphics::ClearPinnedTextCache() {
	if (m_textureBackend) {
		for (auto& kv : m_pinnedTextCache) {
			if (kv.second.texture) m_textureBackend->DestroyTexture(kv.second.texture);
		}
	}
	m_pinnedTextCache.clear();
	// 代际号递增：所有在外部持有的旧句柄副本就此失效（IsCachedTextStale 会返回 true），
	// 防止消费者拿已销毁的 bindless 槽位去绘制。
	++m_textGeneration;
}

void Graphics::DrawText(const std::string& text, const std::string& fontKey, int fontSize,
	const glm::vec4& color, float x, float y, float scale) {
	// worker：defer 到主线程，onTop=false → 回放时就地交错渲染（与对象同 z-order）。
	if (tl_record) { RecordDrawText(*tl_record, text, fontKey, fontSize, color, x, y, scale, /*onTop*/false); return; }

	// 主线程串行：先 flush 已排队的 reanim instance（草坪/影子/前面对象 sprite），文字随后写入
	// batch，将在下一次 cross-flush / EndFrame 落在它们之上——即"当前层"（在已画对象之上、后续
	// 对象之下）。否则 AppendReanimInstance 的 cross-flush 会把 batch 压到后续 instance 下层 → 隐身。
	// 对象循环之外的调用（UI/菜单文字）此刻 instance 队列为空，FlushInstances 是 no-op，行为不变。
	FlushInstances();

	int w, h;
	float superSample;
	uint32_t texID = GetOrCreateTextTexture(text, fontKey, fontSize, color, w, h, superSample);
	if (texID == 0) return;

	int texIndex = BindTexture(texID);
	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	// 纹理按物理像素超采样，除回逻辑尺寸保证屏幕布局不变（superSample=1 时等价）。
	const float inv = scale / superSample;
	local = glm::scale(local, glm::vec3(w * inv, h * inv, 1.0f));
	glm::mat4 finalMatrix = m_transformStack.back() * local;
	int matrixIndex = AddMatrix(finalMatrix);

	BatchVertex vertices[6] = {
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 1.0f, 1.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{0.0f, 0.0f, 0.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, 1,1,1,1, 0.0f}
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

void Graphics::DrawTextOnTop(const std::string& text, const std::string& fontKey, int fontSize,
	const glm::vec4& color, float x, float y, float scale) {
	// worker 线程：onTop=true → ReplayAndEndParallel 在所有 slot 几何之后统一渲染 = 绝对顶层。
	if (tl_record) {
		RecordDrawText(*tl_record, text, fontKey, fontSize, color, x, y, scale, /*onTop*/true);
		return;
	}
	// 主线程串行：无并行回放阶段、无法真正延迟到所有对象之后；这里把已排队的 batch + instance
	// 全部 flush，再绘制，等价于"当前已绘制内容之上"。串行对象少，足够近似顶层。
	if (!m_batchVertices.empty()) FlushBatch();
	FlushInstances();
	DrawText(text, fontKey, fontSize, color, x, y, scale);
}

void Graphics::DrawGlyphRun(const std::string& text, const std::string& fontKey, int fontSize,
	const glm::vec4& color, float x, float y, float scale) {
	// worker：defer 到主线程 replay 就地发射字形 quad（零光栅化，无 TTF/上传/LRU）；与对象同 z-order。
	if (tl_record) { RecordDrawGlyphRun(*tl_record, text, fontKey, fontSize, color, x, y, scale); return; }
	if (text.empty()) return;

	// 1. 解码本串全部码点。
	std::vector<uint32_t> cps;
	for (size_t i = 0; i < text.size(); ) {
		uint32_t cp = DecodeUtf8(text, i);
		if (cp) cps.push_back(cp);
	}
	if (cps.empty()) return;

	// 2. 保 z-order（同 DrawText）：先 flush 已排队 instance，文字落在已画对象之上、后续对象之下。
	FlushInstances();

	// 3. 取/建图集（当帧把缺码点并入并重建 → 此后所有字形必在）。
	PROFILE_SCOPE("7b0.glyph_body");
	GlyphAtlas& atlas = GetOrBuildGlyphAtlas(fontKey, fontSize, cps);
	if (atlas.BindingId() == 0) {  // 建失败 → fallback 整串 DrawText，保证不消失
		Profiler::Get().CountGlyphFallback(true);
		DrawText(text, fontKey, fontSize, color, x, y, scale);
		return;
	}

	// 3b. 缺字形整串回退：某码点 TTF 度量失败（字体无此字形 / 非 BMP 被跳过）→ 它进了 covered 却
	//     不在 glyphs。逐字形发射会把它静默吞掉、且不推进 penX（后续字挤位）。改为整串走 DrawText
	//     （TTF_RenderUTF8 正确处理缺字形与全 Unicode），保证显示与间距正确。HUD 数字/CJK 不触发。
	for (uint32_t cp : cps) {
		if (atlas.glyphs.find(cp) == atlas.glyphs.end()) {
			Profiler::Get().CountGlyphFallback(false);
			DrawText(text, fontKey, fontSize, color, x, y, scale);
			return;
		}
	}

	// 4. 逐字形拼 quad。度量是物理像素，× invSS 转逻辑尺寸。烘白 + 顶点色 tint（NormalizeColor）。
	Profiler::Get().CountGlyphLine();
	const int texIndex = BindTexture(atlas.BindingId());
	const glm::vec4 nc = NormalizeColor(color);
	const float invSS = scale / atlas.superSample;
	const float ascent = (float)atlas.ascent;
	float penX = 0.0f;
	for (uint32_t cp : cps) {
		auto it = atlas.glyphs.find(cp);
		if (it == atlas.glyphs.end()) continue;  // 理论不达（当帧已建全）；防御
		const GlyphInfo& gi = it->second;
		if (gi.pxW > 0 && gi.pxH > 0) {
			const float gx = x + (penX + gi.bearingX) * invSS;
			const float gy = y + (ascent - gi.bearingY) * invSS;
			const float gw = gi.pxW * invSS;
			const float gh = gi.pxH * invSS;
			glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(gx, gy, 0.0f));
			local = glm::scale(local, glm::vec3(gw, gh, 1.0f));
			const glm::mat4 finalMatrix = m_transformStack.back() * local;
			const int matrixIndex = AddMatrix(finalMatrix);
			const BatchVertex verts[6] = {
				{0.0f, 1.0f, gi.u0, gi.v1, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f},
				{1.0f, 1.0f, gi.u1, gi.v1, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f},
				{1.0f, 0.0f, gi.u1, gi.v0, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f},
				{0.0f, 1.0f, gi.u0, gi.v1, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f},
				{0.0f, 0.0f, gi.u0, gi.v0, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f},
				{1.0f, 0.0f, gi.u1, gi.v0, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, 0.0f}
			};
			AddVertices(verts, 6);
			CheckBatch();
		}
		penX += gi.advance;
	}
}

void Graphics::ClearTextCache() {
	if (m_textureBackend) {
		for (auto& kv : m_textCache) {
			if (kv.second.first.texture) m_textureBackend->DestroyTexture(kv.second.first.texture);
		}
	}
	m_textCache.clear();
	m_textCacheOrder.clear();
	ClearGlyphAtlases();
}

void Graphics::SetClearColor(float r, float g, float b, float a) {
	// Phase 3a：保存归一化到 [0,1]，每帧由 VulkanRenderer::BeginFrame 读取。
	m_clearR = r / 255.0f;
	m_clearG = g / 255.0f;
	m_clearB = b / 255.0f;
	m_clearA = a / 255.0f;
}

void Graphics::SetBlendMode(BlendMode mode) {
	if (tl_record) { RecordSetBlendMode(*tl_record, mode); return; }

	if (m_currentBlendMode == mode) return;

	m_currentBlendMode = mode;

	// 纹理批次通过逐顶点 blendMode 字段在 FlushBatch 时分段处理，pipeline 切换在 emit 时根据
	// blendMode 决定，无需在这里做 GL 状态切换。
}

void Graphics::Clear() {
	// Phase 3a：真正的清屏由 VulkanRenderer::BeginFrame 完成；这里只做 CPU 侧的卫生检查。
	if (!m_clipStack.empty() || !m_packedClipStack.empty()) {
		LOG_WARN("Graphics") << "Unbalanced PushClipRect/PopClipRect across frames ("
			<< m_clipStack.size() << " entries left); resetting.";
		m_clipStack.clear();
		m_packedClipStack.clear();
	}
}

void Graphics::UpdateViewMatrix() {
	m_viewMatrix = glm::mat4(1.0f);
	m_viewMatrix = glm::scale(m_viewMatrix, glm::vec3(m_cameraZoom, m_cameraZoom, 1.0f));
	if (m_cameraRotation != 0.0f)
		m_viewMatrix = glm::rotate(m_viewMatrix, glm::radians(-m_cameraRotation), glm::vec3(0.0f, 0.0f, 1.0f));
	m_viewMatrix = glm::translate(m_viewMatrix, glm::vec3(-m_cameraPos.x, -m_cameraPos.y, 0.0f));
}

void Graphics::SetCameraPosition(float x, float y) {
	m_cameraPos = glm::vec2(x, y);
	UpdateViewMatrix();
}

void Graphics::MoveCamera(float dx, float dy) {
	m_cameraPos += glm::vec2(dx, dy);
	UpdateViewMatrix();
}

void Graphics::SetCameraZoom(float zoom) {
	m_cameraZoom = zoom;
	UpdateViewMatrix();
}

void Graphics::SetCameraRotation(float degrees) {
	m_cameraRotation = degrees;
	UpdateViewMatrix();
}

void Graphics::ResetCamera() {
	m_cameraPos = glm::vec2(0.0f);
	m_cameraZoom = 1.0f;
	m_cameraRotation = 0.0f;
	m_viewMatrix = glm::mat4(1.0f);
}

void Graphics::RecomputeLetterbox() {
	// 使用当前后端的真实 framebuffer 尺寸；构造期退化为逻辑尺寸。
	float realW = (float)m_windowWidth;
	float realH = (float)m_windowHeight;
	if (m_vk && m_vk->ctx) {
		VkExtent2D ext = m_vk->ctx->SwapchainExtent();
		if (ext.width > 0 && ext.height > 0) {
			realW = (float)ext.width;
			realH = (float)ext.height;
		}
	}
	else if (m_gl) {
		// BeginFrame 之前也可能发生全屏切换，不能沿用上一帧缓存的 drawable 尺寸。
		m_gl->RefreshDrawableSize();
		const int drawableWidth = m_gl->DrawableWidth();
		const int drawableHeight = m_gl->DrawableHeight();
		if (drawableWidth > 0 && drawableHeight > 0) {
			realW = static_cast<float>(drawableWidth);
			realH = static_cast<float>(drawableHeight);
		}
	}
	const float prevScale = m_letterboxScale;
	// 等比：取较小的轴缩放比，保证整幅逻辑画面装得下；剩余空间均分为两侧黑边。
	m_letterboxScale = std::min(realW / (float)m_windowWidth, realH / (float)m_windowHeight);
	m_letterboxOffsetX = (realW - m_windowWidth * m_letterboxScale) * 0.5f;
	m_letterboxOffsetY = (realH - m_windowHeight * m_letterboxScale) * 0.5f;

	// 缩放比变了（窗口⇄全屏切换）→ 已缓存的文字纹理是按旧密度光栅化的，立即失效让其按新
	// 密度重建，否则全屏后文字会沿用低密度纹理而发糊。一次性重光栅化（非每帧），代价可接受。
	if (std::abs(m_letterboxScale - prevScale) > 1e-4f) {
		ClearTextCache();
		ClearPinnedTextCache();
	}
}

void Graphics::LogicalRectToFramebuffer(int lx, int ly, int lw, int lh,
	int32_t& outX, int32_t& outY, uint32_t& outW, uint32_t& outH) const {
	// shader 用 gl_FragCoord 比较帧缓冲像素、不经 viewport 变换，必须手动套 letterbox 缩放+偏移。
	outX = (int32_t)std::lround(lx * m_letterboxScale + m_letterboxOffsetX);
	outY = (int32_t)std::lround(ly * m_letterboxScale + m_letterboxOffsetY);
	outW = (uint32_t)std::max(0L, std::lround(lw * m_letterboxScale));
	outH = (uint32_t)std::max(0L, std::lround(lh * m_letterboxScale));
}

glm::vec2 Graphics::LogicalToWorld(float logicalX, float logicalY) const {
	// 入参是「逻辑坐标」(0..1100/0..600)，不是帧缓冲像素——
	// 全场 15+ 调用点绝大多数传的是 UI 布局坐标用于绘制（Button/Slider/ShovelBank 等）。
	// 鼠标的 letterbox 逆变换在 InputHandler::ProcessEvent 入口已完成，故此处不做 letterbox，
	// 只保留 逻辑坐标 → 世界坐标 的相机逆变换。窗口/全屏行为一致。
	// 逻辑坐标 → NDC（注意 Y 轴翻转，因为正交投影 top=0）
	float ndcX = (logicalX / m_windowWidth) * 2.0f - 1.0f;
	float ndcY = 1.0f - (logicalY / m_windowHeight) * 2.0f;
	glm::vec4 worldPos = glm::inverse(m_projection * m_viewMatrix) * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
	return glm::vec2(worldPos.x, worldPos.y);
}

glm::vec2 Graphics::WorldToLogical(float worldX, float worldY) const {
	glm::vec4 clipPos = m_projection * m_viewMatrix * glm::vec4(worldX, worldY, 0.0f, 1.0f);
	// NDC → 逻辑坐标（Y 轴翻转）
	float logicalX = (clipPos.x + 1.0f) * 0.5f * m_windowWidth;
	float logicalY = (1.0f - clipPos.y) * 0.5f * m_windowHeight;
	return glm::vec2(logicalX, logicalY);
}

void Graphics::DrawLine(float x1, float y1, float x2, float y2, const glm::vec4& color) {
	if (tl_record) { RecordDrawLine(*tl_record, x1, y1, x2, y2, color); return; }

	glm::vec4 nc = NormalizeColor(color);
	float r = nc.r, g = nc.g, b = nc.b, a = nc.a;

	// 线段转为 1px 宽四边形，走纹理批次（1×1 白纹理）保证绘制顺序
	const glm::mat4& transform = m_transformStack.back();
	glm::vec4 p0 = transform * glm::vec4(x1, y1, 0.0f, 1.0f);
	glm::vec4 p1 = transform * glm::vec4(x2, y2, 0.0f, 1.0f);

	float dx = p1.x - p0.x, dy = p1.y - p0.y;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) return;
	float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;

	int texIndex = BindTexture(m_whiteTexture);
	int matIndex = AddMatrix(glm::mat4(1.0f));
	float bm = EncodeBatchBlendMode(m_currentBlendMode);

	BatchVertex verts[6] = {
		{p0.x + nx, p0.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		{p0.x - nx, p0.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		{p1.x - nx, p1.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		{p0.x + nx, p0.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		{p1.x - nx, p1.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		{p1.x + nx, p1.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
	};
	AddVertices(verts, 6);
	CheckBatch();
}

void Graphics::DrawRect(float x, float y, float width, float height, const glm::vec4& color) {
	if (tl_record) { RecordDrawRect(*tl_record, x, y, width, height, color); return; }

	glm::vec4 nc = NormalizeColor(color);
	float r = nc.r, g = nc.g, b = nc.b, a = nc.a;

	// 4 条边各转为 1px 宽四边形，走纹理批次
	const glm::mat4& transform = m_transformStack.back();
	glm::vec4 p[4] = {
		transform * glm::vec4(x,         y,          0.0f, 1.0f),
		transform * glm::vec4(x + width, y,          0.0f, 1.0f),
		transform * glm::vec4(x + width, y + height, 0.0f, 1.0f),
		transform * glm::vec4(x,         y + height, 0.0f, 1.0f),
	};

	int texIndex = BindTexture(m_whiteTexture);
	int matIndex = AddMatrix(glm::mat4(1.0f));
	float bm = EncodeBatchBlendMode(m_currentBlendMode);

	for (int i = 0; i < 4; ++i) {
		float ax = p[i].x, ay = p[i].y;
		float bx = p[(i + 1) % 4].x, by = p[(i + 1) % 4].y;
		float edx = bx - ax, edy = by - ay;
		float len = sqrtf(edx * edx + edy * edy);
		if (len < 0.001f) continue;
		float nx = -edy / len * 0.5f, ny = edx / len * 0.5f;

		BatchVertex verts[6] = {
			{ax + nx, ay + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{ax - nx, ay - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{bx - nx, by - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{ax + nx, ay + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{bx - nx, by - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{bx + nx, by + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		};
		AddVertices(verts, 6);
	}
	CheckBatch();
}

void Graphics::FillRect(float x, float y, float width, float height, const glm::vec4& color) {
	if (tl_record) { RecordFillRect(*tl_record, x, y, width, height, color); return; }

	// 使用 1×1 白色纹理加入纹理批次，保证与其他纹理的绘制顺序正确
	int texIndex = BindTexture(m_whiteTexture);

	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	local = glm::scale(local, glm::vec3(width, height, 1.0f));
	glm::mat4 finalMatrix = m_transformStack.back() * local;
	int matrixIndex = AddMatrix(finalMatrix);

	glm::vec4 nc = NormalizeColor(color);
	float bm = EncodeBatchBlendMode(m_currentBlendMode);
	BatchVertex vertices[6] = {
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
		{1.0f, 1.0f, 1.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
		{0.0f, 1.0f, 0.0f, 1.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
		{0.0f, 0.0f, 0.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
		{1.0f, 0.0f, 1.0f, 0.0f, (uint32_t)texIndex, (uint32_t)matrixIndex, nc.r, nc.g, nc.b, nc.a, bm},
	};
	AddVertices(vertices, 6);
	CheckBatch();
}

void Graphics::DrawCircle(float cx, float cy, float radius, const glm::vec4& color, int segments) {
	if (tl_record) { RecordDrawCircle(*tl_record, cx, cy, radius, color, segments); return; }

	glm::vec4 nc = NormalizeColor(color);
	float r = nc.r, g = nc.g, b = nc.b, a = nc.a;

	// 每段弧转为 1px 宽四边形，走纹理批次
	const glm::mat4& transform = m_transformStack.back();

	int texIndex = BindTexture(m_whiteTexture);
	int matIndex = AddMatrix(glm::mat4(1.0f));
	float bm = EncodeBatchBlendMode(m_currentBlendMode);

	for (int i = 0; i < segments; ++i) {
		float angle0 = 2.0f * glm::pi<float>() * i / segments;
		float angle1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
		glm::vec4 p0 = transform * glm::vec4(cx + radius * cosf(angle0), cy + radius * sinf(angle0), 0.0f, 1.0f);
		glm::vec4 p1 = transform * glm::vec4(cx + radius * cosf(angle1), cy + radius * sinf(angle1), 0.0f, 1.0f);

		float dx = p1.x - p0.x, dy = p1.y - p0.y;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) continue;
		float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;

		BatchVertex verts[6] = {
			{p0.x + nx, p0.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p0.x - nx, p0.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p1.x - nx, p1.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p0.x + nx, p0.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p1.x - nx, p1.y - ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p1.x + nx, p1.y + ny, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		};
		AddVertices(verts, 6);
	}
	CheckBatch();
}

void Graphics::FillCircle(float cx, float cy, float radius, const glm::vec4& color, int segments) {
	if (tl_record) { RecordFillCircle(*tl_record, cx, cy, radius, color, segments); return; }

	glm::vec4 nc = NormalizeColor(color);
	float r = nc.r, g = nc.g, b = nc.b, a = nc.a;

	// 展开三角扇，走纹理批次
	const glm::mat4& transform = m_transformStack.back();
	glm::vec4 center = transform * glm::vec4(cx, cy, 0.0f, 1.0f);

	int texIndex = BindTexture(m_whiteTexture);
	int matIndex = AddMatrix(glm::mat4(1.0f));
	float bm = EncodeBatchBlendMode(m_currentBlendMode);

	for (int i = 0; i < segments; ++i) {
		float angle0 = 2.0f * glm::pi<float>() * i / segments;
		float angle1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
		glm::vec4 p0 = transform * glm::vec4(cx + radius * cosf(angle0), cy + radius * sinf(angle0), 0.0f, 1.0f);
		glm::vec4 p1 = transform * glm::vec4(cx + radius * cosf(angle1), cy + radius * sinf(angle1), 0.0f, 1.0f);

		BatchVertex verts[3] = {
			{center.x, center.y, 0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p0.x, p0.y,         0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
			{p1.x, p1.y,         0.5f, 0.5f, (uint32_t)texIndex, (uint32_t)matIndex, r,g,b,a, bm},
		};
		AddVertices(verts, 3);
	}
	CheckBatch();
}

// ============================================================================
//  多线程录制 / 回放
// ============================================================================
//
// 关键不变量：
//   1. tl_record 只在 worker 线程上非空，主线程上始终为 nullptr。
//   2. 每个 slot 一份的 WorkerRecord / WorkerThreadState 同一时刻只由领取该 slot 的
//      一个 worker 独占写入，无 lock 必要；物理 worker 可依次处理多个 slot。
//   3. Replay 在主线程串行执行；该 mapped-buffer 路径只由 Vulkan 启用。
//   4. 顺序保证：每个 slot 对应 mGameObjects 的一个连续且不重叠区间；物理 worker 的领取
//      次序不影响结果。回放循环 slot = 0..N-1，整体绝对顺序与原串行 Draw 等价。

void Graphics::BeginParallelRecord(int numRecordSlots) {
	if (numRecordSlots <= 0) {
		m_numActiveWorkers = 0;
		return;
	}

	// Phase 5：先把主线程预先累积的 CPU 批排空到 GPU。这样切片区域起始游标干净，
	// 主线程 UI 既有数据落在并行区前面，回放后续的 text spill 落在后面。
	FlushBatch();
	FlushInstances();  // Task 4: 同步刷新主线程 instance 缓冲，保证 instCursor 在并行区起始干净

	// 确保 records 与 states 都有 numRecordSlots 份
	if ((int)m_workerRecords.size() < numRecordSlots) m_workerRecords.resize(numRecordSlots);
	if ((int)m_workerStates.size() < numRecordSlots) m_workerStates.resize(numRecordSlots);

	// 当前 Graphics 状态作为每个 worker 的初始基线
	const glm::mat4& curTop = m_transformStack.back();
	const bool       curId = m_transformIsIdentity.back() != 0;

	// 默认把所有 slot 的切片置为"零容量 / 空指针"——SliceHasRoom 会直接拒绝写入，
	// 工作线程不会 crash。下面如果检测到帧合法 + 容量充足，会重新填充实际指针。
	for (int i = 0; i < numRecordSlots; ++i) {
		WorkerRecord& r = m_workerRecords[i];
		r.Reset();
		if (r.cmds.capacity() == 0) {
			r.cmds.reserve(16 * 1024);
			r.blendModes.reserve(16);
			r.textCmds.reserve(64);
		}
		r.slice = VkWorkerSlice{};  // 全部归零
		r.initialTopTransform = curTop;
		r.initialTopIsIdentity = curId;
		r.initialClipStack = m_clipStack;
		r.initialPackedClipStack = m_packedClipStack;
		r.initialBlend = m_currentBlendMode;
	}
	m_parallelBaseVbo = m_parallelBaseSsbo = m_parallelBaseInst = 0;
	m_parallelVboBytes = m_parallelSsboBytes = m_parallelInstBytes = 0;
	m_numActiveWorkers = numRecordSlots;  // 永远 set；Replay 依赖此值遍历全部有序切片

	// 没活动帧（构造期 / shutdown 期 / 切场景瞬间）就跳过切片填充——
	// SliceHasRoom 在 worker 端会拒绝所有写入，replay 也会早退。
	if (!m_vk || !m_vk->frameOpen) return;
	auto& fr = m_vk->frames[m_vk->frameIdx];

	const uint64_t baseVbo = (uint64_t)fr.vboCursor;
	const uint64_t baseSsbo = (uint64_t)fr.ssboCursor;
	const uint64_t baseInst = (uint64_t)fr.instCursor;
	const uint64_t vboRemain = (fr.vboCap > baseVbo) ? (fr.vboCap - baseVbo) : 0;
	const uint64_t ssboRemain = (fr.ssboCap > baseSsbo) ? (fr.ssboCap - baseSsbo) : 0;
	const uint64_t instRemain = (fr.instCap > baseInst) ? (fr.instCap - baseInst) : 0;

	// 给并行区域之后的主线程绘制留余量：deferred text / DrawGlyphRun 血量 / post-parallel UI / overlay。
	// 关键：预留必须随 buffer 容量按比例增长，不能用固定字节。血量改字形图集（DrawGlyphRun）后，
	// replay 阶段每行血量从 1 个 quad 膨胀到逐字形十几个 quad，数百僵尸时 post-parallel 顶点可达
	// 十几 MB。固定 4MB 预留会被撑爆，而 grow 只翻倍切片区、不增固定预留 → 每帧溢出→cap 翻倍→直到
	// vmaCreateBuffer 分配 2GB 失败。改为按 remain 的 1/4 预留（带固定下限）：grow 时预留同步放大，
	// 溢出能自愈，正反馈被打破，一两帧内收敛。inst 不受影响（血量不写 instance），维持固定。
	const uint64_t POST_PARALLEL_RESERVE_VBO = std::max<uint64_t>(8u * 1024u * 1024u, vboRemain / 4);
	const uint64_t POST_PARALLEL_RESERVE_SSBO = std::max<uint64_t>(2u * 1024u * 1024u, ssboRemain / 4);
	const uint64_t POST_PARALLEL_RESERVE_INST = 1u * 1024u * 1024u;
	const uint64_t vboUsable = (vboRemain > POST_PARALLEL_RESERVE_VBO) ? (vboRemain - POST_PARALLEL_RESERVE_VBO) : 0;
	const uint64_t ssboUsable = (ssboRemain > POST_PARALLEL_RESERVE_SSBO) ? (ssboRemain - POST_PARALLEL_RESERVE_SSBO) : 0;
	const uint64_t instUsable = (instRemain > POST_PARALLEL_RESERVE_INST) ? (instRemain - POST_PARALLEL_RESERVE_INST) : 0;

	// 硬门槛：连"每 record slot 一个 stride 的等分"都放不下，说明本帧已被主线程画满。
	// 与旧逻辑语义一致——slot 切片保持零容量，worker 写入会被静默丢弃。
	const uint64_t equalVboBytes = (vboUsable / numRecordSlots / sizeof(BatchVertex)) * sizeof(BatchVertex);
	const uint64_t equalSsboBytes = (ssboUsable / numRecordSlots / sizeof(glm::mat4)) * sizeof(glm::mat4);
	const uint64_t equalInstBytes = (instUsable / numRecordSlots / sizeof(InstanceRecord)) * sizeof(InstanceRecord);
	if (equalVboBytes == 0 || equalSsboBytes == 0 || equalInstBytes == 0) {
		// 哪个缓冲连"每 slot 一个 stride 的等分"都放不下，就标哪个溢出（驱动 EndFrame 只翻倍它）。
		if (equalVboBytes == 0)  fr.vboOverflow = true;
		if (equalSsboBytes == 0) fr.ssboOverflow = true;
		if (equalInstBytes == 0) fr.instOverflow = true;
		if (!fr.overflowWarned) {   // 日志节流
			LOG_WARN("Graphics") << "BeginParallelRecord: insufficient frame buffer capacity ("
				<< "vbo " << baseVbo << "/" << fr.vboCap
				<< ", ssbo " << baseSsbo << "/" << fr.ssboCap
				<< ", inst " << baseInst << "/" << fr.instCap << ")";
			fr.overflowWarned = true;
		}
		return;
	}

	// 自适应加权切片：用上一并行帧各 slot 的占用作权重，按比例瓜分 usable。
	// 每个 slot 先保底一块地板（让上帧空、本帧突然变重的 slot 不至于 0 容量直接丢，
	// 撑过一帧后权重就会把它放大）；剩余弹性容量按权重分配。Σ ≤ usable 由构造保证。
	// 无历史（首并行帧 / numRecordSlots 变化）则退回等分，1~2 帧内自动收敛。
	auto floorTo = [](uint64_t v, uint64_t stride) { return (v / stride) * stride; };
	const uint64_t VSTRIDE = sizeof(BatchVertex);
	const uint64_t MSTRIDE = sizeof(glm::mat4);
	const uint64_t ISTRIDE = sizeof(InstanceRecord);
	const uint64_t minVbo = floorTo(2048ull * VSTRIDE, VSTRIDE);  // ~88 KB/slot 地板
	const uint64_t minSsbo = floorTo(512ull * MSTRIDE, MSTRIDE);  // ~32 KB/slot 地板
	const uint64_t minInst = floorTo(1024ull * ISTRIDE, ISTRIDE);  // ~48 KB/slot 地板

	std::vector<uint64_t> vboSlice(numRecordSlots), ssboSlice(numRecordSlots), instSlice(numRecordSlots);
	auto splitWeighted = [&](uint64_t usable, uint64_t stride, uint64_t minBytes,
		const std::vector<uint32_t>& hist,
		std::vector<uint64_t>& out) {
			const bool haveHist = (hist.size() == (size_t)numRecordSlots);
			const uint64_t totalMin = (uint64_t)numRecordSlots * minBytes;
			// 地板都放不下 → 退回纯等分（极端兜底，实测 56MB vs ~1MB 地板不会触发）。
			if (!haveHist || totalMin >= usable) {
				const uint64_t eq = floorTo(usable / numRecordSlots, stride);
				for (int i = 0; i < numRecordSlots; ++i) out[i] = eq;
				return;
			}
			const uint64_t elastic = usable - totalMin;
			double sumW = 0.0;
			std::vector<double> w(numRecordSlots);
			for (int i = 0; i < numRecordSlots; ++i) { w[i] = std::max<double>(1.0, (double)hist[i]); sumW += w[i]; }
			for (int i = 0; i < numRecordSlots; ++i)
				out[i] = minBytes + floorTo((uint64_t)((double)elastic * (w[i] / sumW)), stride);
		};
	splitWeighted(vboUsable, VSTRIDE, minVbo, m_prevSliceVboDemand, vboSlice);
	splitWeighted(ssboUsable, MSTRIDE, minSsbo, m_prevSliceSsboDemand, ssboSlice);
	splitWeighted(instUsable, ISTRIDE, minInst, m_prevSliceInstDemand, instSlice);

	char* const vboBaseMap = static_cast<char*>(fr.vbo->MappedPtr()) + baseVbo;
	char* const ssboBaseMap = static_cast<char*>(fr.ssbo->MappedPtr()) + baseSsbo;
	char* const instBaseMap = static_cast<char*>(fr.instBuf->MappedPtr()) + baseInst;
	const uint32_t baseVertGlobal = (uint32_t)(baseVbo / sizeof(BatchVertex));
	const uint32_t baseMatGlobal = (uint32_t)(baseSsbo / sizeof(glm::mat4));
	const uint32_t baseInstGlobal = (uint32_t)(baseInst / sizeof(InstanceRecord));

	// 切片在区域内连续排布（前缀和偏移），无空洞；vbo / ssbo / inst 各自独立排布。
	uint64_t vOff = 0, mOff = 0, iOff = 0;
	for (int i = 0; i < numRecordSlots; ++i) {
		VkWorkerSlice& sl = m_workerRecords[i].slice;
		sl.vboPtr = reinterpret_cast<BatchVertex*>(vboBaseMap + vOff);
		sl.ssboPtr = reinterpret_cast<glm::mat4*>(ssboBaseMap + mOff);
		sl.instPtr = reinterpret_cast<InstanceRecord*>(instBaseMap + iOff);   // Task 3
		sl.vboBaseVert = baseVertGlobal + (uint32_t)(vOff / VSTRIDE);
		sl.ssboBaseMat = baseMatGlobal + (uint32_t)(mOff / MSTRIDE);
		sl.instBaseIdx = baseInstGlobal + (uint32_t)(iOff / ISTRIDE);              // Task 3
		sl.vboCap = (uint32_t)(vboSlice[i] / VSTRIDE);
		sl.ssboCap = (uint32_t)(ssboSlice[i] / MSTRIDE);
		sl.instCap = (uint32_t)(instSlice[i] / ISTRIDE);                       // Task 3
		// vboCount/ssboCount/instCount、*Demand、*Overflowed 已经在上面 VkWorkerSlice{} 清零
		vOff += vboSlice[i];
		mOff += ssboSlice[i];
		iOff += instSlice[i];                                                       // Task 3
	}

	// 记录整段切片区域大小，ReplayAndEndParallel 收尾时一次性把游标推到末尾。
	m_parallelBaseVbo = baseVbo;
	m_parallelBaseSsbo = baseSsbo;
	m_parallelBaseInst = baseInst;
	m_parallelVboBytes = vOff;
	m_parallelSsboBytes = mOff;
	m_parallelInstBytes = iOff;
}

void Graphics::SetWorkerSlot(int slot) {
	if (slot < 0 || slot >= m_numActiveWorkers) {
		LOG_WARN("Graphics") << "SetWorkerSlot: invalid slot " << slot
			<< " (active=" << m_numActiveWorkers << ")";
		return;
	}
	WorkerRecord& r = m_workerRecords[slot];
	WorkerThreadState& s = m_workerStates[slot];

	// 用快照初始化 thread-local 变换栈 / 裁剪栈 / 混合模式。clear() 保留 capacity。
	s.transformStack.clear();
	s.transformIsIdentity.clear();
	s.clipStack.clear();
	s.packedClipStack.clear();
	s.transformStack.push_back(r.initialTopTransform);
	s.transformIsIdentity.push_back(r.initialTopIsIdentity ? 1 : 0);
	s.clipStack = r.initialClipStack;
	s.packedClipStack = r.initialPackedClipStack;

	tl_record = &r;
	tl_transformStack = &s.transformStack;
	tl_transformIsIdentity = &s.transformIsIdentity;
	tl_clipStack = &s.clipStack;
	tl_packedClipStack = &s.packedClipStack;
	tl_blend = r.initialBlend;
}

void Graphics::ClearWorkerSlot() {
	// 防御性检查：worker 块结束时栈应平衡（GameObjectManager 总是把 Push/PopClip 配
	// 对调用；component 的 Push/PopTransform 也配对）。不平衡只打印警告，不阻塞，
	// 回放时主线程会自己保证 m_clipStack 起止一致。
	if (tl_record && tl_clipStack &&
		tl_clipStack->size() != tl_record->initialClipStack.size()) {
		LOG_WARN("Graphics") << "ClearWorkerSlot: clip stack imbalanced ("
			<< tl_clipStack->size() << " vs initial "
			<< tl_record->initialClipStack.size() << ")";
	}
	if (tl_record && tl_packedClipStack &&
		tl_packedClipStack->size() != tl_record->initialPackedClipStack.size()) {
		LOG_WARN("Graphics") << "ClearWorkerSlot: packed clip stack imbalanced ("
			<< tl_packedClipStack->size() << " vs initial "
			<< tl_record->initialPackedClipStack.size() << ")";
	}
	if (tl_transformStack && tl_transformStack->size() != 1) {
		LOG_WARN("Graphics") << "ClearWorkerSlot: transform stack imbalanced ("
			<< tl_transformStack->size() << ", expected 1)";
	}

	tl_record = nullptr;
	tl_transformStack = nullptr;
	tl_transformIsIdentity = nullptr;
	tl_clipStack = nullptr;
	tl_packedClipStack = nullptr;
	tl_blend = BlendMode::None;
}

void Graphics::ReplayAndEndParallel() {
	// Phase 5（cmd-based blend）：worker 已经把顶点和矩阵直写进每帧 mapped VBO/SSBO 切片，
	// r.cmds 里只剩会切 draw 顺序的命令（SetBlend / DeferredText / DeferredGlyphRun）；
	// ClipRect 已逐顶点/逐实例携带，不进入命令流。
	//
	// 关键：blend 分段走 cmd 流而非扫 per-vertex blendMode 字段。
	// 原因：mapped host-coherent 内存读取比 cache 内存慢 ~100×（write-combined），4400 僵尸
	// 场景下 ~26k quad × 一次读取 ≈ 几 ms 浪费在 CPU 主线程扫 blendMode 上，导致 Phase 5
	// 比 Phase 4 还慢。worker 已经在 SetBlendMode 切换时记了 SetBlend cmd，回放直接顺着 cmd
	// 跟踪当前 blend 即可，零 mapped 内存读。
	//
	// 副作用：pipeline 切换严格按 worker 写入顺序，而非按"实际 vert 的 blendMode 字段"。
	// 二者本质等价，因为 RecordDrawTexture 写 per-vert blendMode 时就是读 tl_blend——
	// SetBlend cmd 必定先于受影响的 vert 出现。

	if (m_numActiveWorkers == 0) return;
	if (!m_vk || !m_vk->frameOpen) {
		m_numActiveWorkers = 0;
		return;
	}
	VkCommandBuffer cb = m_vk->renderer->CurrentCmdBuffer();
	if (cb == VK_NULL_HANDLE) {
		m_numActiveWorkers = 0;
		return;
	}

	auto& fr = m_vk->frames[m_vk->frameIdx];

	// VBO 整段切片只 bind 一次。pipeline / descriptor / push-constant 只在 blend 真正变化
	// 时重绑（首次绑定也算变化）。
	VkBuffer vbuf = fr.vbo->Handle();
	VkDeviceSize vboOff = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &vboOff);

	const glm::mat4 projView = m_projection * m_viewMatrix;
	const VkDescriptorSet sets[2] = { fr.matrixSet, m_vk->texPool->DescriptorSet() };

	// 跨 slot 复用的 pipeline binding 状态：用一个"还没绑过"哨兵值，第一次 emit 时强制绑。
	enum class BoundPipe : uint8_t {
		None, BatchAlpha, BatchAdd, BatchColorize, BatchLessColorize,
		InstAlpha, InstAdd, InstColorize, InstLessColorize
	};
	BoundPipe boundPipe = BoundPipe::None;

	auto bindBatchForBlend = [&](BlendMode bm) {
		const BoundPipe want = bm == BlendMode::Add ? BoundPipe::BatchAdd
			: bm == BlendMode::WashedOut ? BoundPipe::BatchColorize
			: bm == BlendMode::LessWashedOut ? BoundPipe::BatchLessColorize
			: BoundPipe::BatchAlpha;
		if (want == boundPipe) return;
		pvz::VulkanPipeline* pipe = want == BoundPipe::BatchAdd ? m_vk->pipeBatchAdd.get()
			: want == BoundPipe::BatchColorize ? m_vk->pipeBatchColorize.get()
			: want == BoundPipe::BatchLessColorize ? m_vk->pipeBatchLessColorize.get()
			: m_vk->pipeBatchAlpha.get();
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Handle());
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Layout(),
			0, 2, sets, 0, nullptr);
		vkCmdPushConstants(cb, pipe->Layout(), VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(glm::mat4), &projView);
		vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &vboOff);
		boundPipe = want;
		};

	// 6.2: Instance pipeline bind helper — records come from the vertex buffer; only set=1 textures are used.
	auto bindInstForBlend = [&](BlendMode bm) {
		const BoundPipe want = bm == BlendMode::Add ? BoundPipe::InstAdd
			: bm == BlendMode::WashedOut ? BoundPipe::InstColorize
			: bm == BlendMode::LessWashedOut ? BoundPipe::InstLessColorize
			: BoundPipe::InstAlpha;
		if (want == boundPipe) return;
		pvz::VulkanPipeline* pipe = want == BoundPipe::InstAdd ? m_vk->pipeInstAdd.get()
			: want == BoundPipe::InstColorize ? m_vk->pipeInstColorize.get()
			: want == BoundPipe::InstLessColorize ? m_vk->pipeInstLessColorize.get()
			: m_vk->pipeInstAlpha.get();
		const VkDescriptorSet textureSet = m_vk->texPool->DescriptorSet();
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Handle());
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->Layout(),
			1, 1, &textureSet, 0, nullptr);
		vkCmdPushConstants(cb, pipe->Layout(), VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(glm::mat4), &projView);
		VkBuffer instVertexBuffer = fr.instBuf->Handle();
		VkDeviceSize instVertexOffset = 0;
		vkCmdBindVertexBuffers(cb, 0, 1, &instVertexBuffer, &instVertexOffset);
		boundPipe = want;
		};

	// DeferredText 渲染策略（按 cmd 的 onTop 标志区分）：
	//   onTop=false → 在该 cmd 记录的几何位置就地交错渲染（与对象同 z-order，如血量显示）；
	//   onTop=true  → 收集到此，待所有 slot 几何 emit 完后统一渲染（绝对顶层）。
	std::vector<const DeferredTextCmd*> pendingTopText;

	// 把帧游标提前推到 post-parallel 预留区：下面 onTop=false 的内联渲染会走 FlushBatch 写顶点，
	// 必须落在 BeginParallelRecord 保留的余量内，不能与各 slot 切片数据撞车。
	// （原本在 slot 循环之后才推；内联渲染要求提前。其后的 overlay 串行绘制会从这里继续累加。）
	fr.vboCursor  = (VkDeviceSize)(m_parallelBaseVbo  + m_parallelVboBytes);
	fr.ssboCursor = (VkDeviceSize)(m_parallelBaseSsbo + m_parallelSsboBytes);
	fr.instCursor = (VkDeviceSize)(m_parallelBaseInst + m_parallelInstBytes);

	for (int slot = 0; slot < m_numActiveWorkers; ++slot) {
		WorkerRecord& r = m_workerRecords[slot];
		VkWorkerSlice& sl = r.slice;

		if (sl.vboOverflowed)  fr.vboOverflow  = true;   // 按缓冲归因聚合到帧级
		if (sl.ssboOverflowed) fr.ssboOverflow = true;
		if (sl.instOverflowed) fr.instOverflow = true;
		if ((sl.vboOverflowed || sl.ssboOverflowed || sl.instOverflowed) && !fr.overflowWarned) {
			LOG_WARN("Graphics") << "Worker slot " << slot
				<< " overflowed slice (vbo " << sl.vboCount << "/" << sl.vboCap
				<< ", ssbo " << sl.ssboCount << "/" << sl.ssboCap
				<< ", inst " << sl.instCount << "/" << sl.instCap << ")";
			fr.overflowWarned = true;
		}

		// 本 slot 起始 blend 来自 BeginParallelRecord 抓取的快照，replay 主线程也同步影子状态。
		BlendMode curBlend = r.initialBlend;
		m_currentBlendMode = curBlend;

		uint32_t drawStart = sl.vboBaseVert;
		uint32_t instStart = sl.instBaseIdx;

		// Emit batch verts AND instance records up to the given absolute cutoffs.
		// Order: batch first, then instance — matches PvZ z-order convention
		// (shadow → reanim) within a single BlendMode segment in a slot.
		auto emitUpTo = [&](uint32_t absVbo, uint32_t absInst) {
			if (absVbo > drawStart) {
				bindBatchForBlend(curBlend);
				vkCmdDraw(cb, absVbo - drawStart, 1, drawStart, 0);
				++m_frameDrawCallCount;
				drawStart = absVbo;
			}
			if (absInst > instStart) {
				bindInstForBlend(curBlend);
				// 4 vertices per instance, instanceCount = (absInst - instStart),
				// firstInstance = instStart (absolute index into fr.instBuf).
				vkCmdDraw(cb, 4, absInst - instStart, 0, instStart);
				++m_frameDrawCallCount;
				instStart = absInst;
			}
			};

		for (const RecordCmd& cmd : r.cmds) {
			const uint32_t cmdAbsVert = sl.vboBaseVert + cmd.vertOffsetAtCmd;
			const uint32_t cmdAbsInst = sl.instBaseIdx + cmd.instOffsetAtCmd;
			// 命令插入点之前的顶点 + 实例必须先 emit（用 cmd 应用 *之前* 的状态）
			emitUpTo(cmdAbsVert, cmdAbsInst);

			switch (cmd.type) {
			case RecCmdType::SetBlend: {
				curBlend = r.blendModes[cmd.payloadIdx];
				m_currentBlendMode = curBlend;
				// 不立刻 bind pipeline——等下一次 emitUpTo 真要画的时候再绑，
				// 避免连续 SetBlend 之间空跑 vkCmdBindPipeline。
				break;
			}
			case RecCmdType::DeferredText: {
				const DeferredTextCmd& t = r.textCmds[cmd.payloadIdx];
				if (t.onTop) {
					// 绝对顶层：留到所有几何 emit 完后统一画。
					pendingTopText.push_back(&t);
				}
				else {
					// 当前层：emitUpTo 已把本对象 sprite（记录在该 cmd 之前）emit 完，此处就地画文字，
					// 文字便夹在"本对象 sprite"与"后续对象"之间 = 与对象同 z-order。
					// DrawText/FlushBatch 会重绑 pipeline/descriptor/vbo，画完清空 boundPipe 哨兵，
					// 强制下一次 emitUpTo 重新绑定，避免后续几何沿用文字管线。
					PROFILE_SCOPE("7a.replay_inlineText");
					if (t.hasClipRect) {
						PushClipRect(t.clipRect.x, t.clipRect.y, t.clipRect.w, t.clipRect.h);
					}
					DrawText(t.text, t.fontKey, t.fontSize, t.color, t.x, t.y, t.scale);
					FlushBatch();
					if (t.hasClipRect) {
						PopClipRect();
					}
					boundPipe = BoundPipe::None;
				}
				break;
			}
			case RecCmdType::DeferredGlyphRun: {
				const DeferredGlyphRunCmd& t = r.glyphRunCmds[cmd.payloadIdx];
				// 就地发射：emitUpTo 已把本对象 sprite emit 完，此处画字形 quad，夹在本对象与
				// 后续对象之间 = 与对象同 z-order。DrawGlyphRun/FlushBatch 会重绑 pipeline/vbo，
				// 画完清空 boundPipe 哨兵，强制下次 emitUpTo 重绑。
				{
					PROFILE_SCOPE("7b.replay_inlineGlyph");
					if (t.hasClipRect) {
						PushClipRect(t.clipRect.x, t.clipRect.y, t.clipRect.w, t.clipRect.h);
					}
					DrawGlyphRun(t.text, t.fontKey, t.fontSize, t.color, t.x, t.y, t.scale);
				}
				{
					PROFILE_SCOPE("7c.replay_inlineGlyphFlush");
					FlushBatch();
				}
				if (t.hasClipRect) {
					PopClipRect();
				}
				boundPipe = BoundPipe::None;
				break;
			}
			}
		}

		// slot 末尾：剩余顶点 + 剩余实例 emit
		emitUpTo(sl.vboBaseVert + sl.vboCount, sl.instBaseIdx + sl.instCount);
	}

	// 自适应负载均衡：采样本帧各 slot 的"真实需求"（sl.*Demand，含被容量拒绝的写入），
	// 作为下一并行帧的切片权重 —— 比旧的"溢出→cap×1.5 估计"精确，溢出 slot 下帧直接拿到
	// 够用的切片（快 attack）。同时把各 buffer 需求聚合成帧级真实需求字节，供 EndFrame 一步
	// 扩容到位（避免 ×2 逐帧爬升时连续多帧丢绘制）。必须在 m_numActiveWorkers 清零前采。
	if ((int)m_prevSliceVboDemand.size() != m_numActiveWorkers) m_prevSliceVboDemand.assign(m_numActiveWorkers, 0);
	if ((int)m_prevSliceSsboDemand.size() != m_numActiveWorkers) m_prevSliceSsboDemand.assign(m_numActiveWorkers, 0);
	if ((int)m_prevSliceInstDemand.size() != m_numActiveWorkers) m_prevSliceInstDemand.assign(m_numActiveWorkers, 0);   // Task 3
	uint64_t vboDemandBytes = 0, ssboDemandBytes = 0, instDemandBytes = 0;
	size_t recordCommands = 0;
	for (int slot = 0; slot < m_numActiveWorkers; ++slot) {
		const WorkerRecord& record = m_workerRecords[slot];
		const VkWorkerSlice& sl = record.slice;
		m_prevSliceVboDemand[slot]  = sl.vboDemand;
		m_prevSliceSsboDemand[slot] = sl.ssboDemand;
		m_prevSliceInstDemand[slot] = sl.instDemand;                                                                  // Task 3
		vboDemandBytes  += (uint64_t)sl.vboDemand  * sizeof(BatchVertex);
		ssboDemandBytes += (uint64_t)sl.ssboDemand * sizeof(glm::mat4);
		instDemandBytes += (uint64_t)sl.instDemand * sizeof(InstanceRecord);
		recordCommands += record.cmds.size();
	}
	Profiler::Get().CountParallelRecord(
		static_cast<size_t>(vboDemandBytes / sizeof(BatchVertex)),
		static_cast<size_t>(ssboDemandBytes / sizeof(glm::mat4)),
		static_cast<size_t>(instDemandBytes / sizeof(InstanceRecord)),
		recordCommands);
	if (m_vk && m_vk->frameOpen) {
		// 并行区真实需求 = 区前主线程已占用(base) + 各 slot 想写的总量。
		m_vk->frameVboDemand  = std::max<uint64_t>(m_vk->frameVboDemand,  m_parallelBaseVbo  + vboDemandBytes);
		m_vk->frameSsboDemand = std::max<uint64_t>(m_vk->frameSsboDemand, m_parallelBaseSsbo + ssboDemandBytes);
		m_vk->frameInstDemand = std::max<uint64_t>(m_vk->frameInstDemand, m_parallelBaseInst + instDemandBytes);
	}

	// onTop=true 的 deferred text 在所有 slot 几何之后统一渲染 = 绝对顶层。
	// （帧游标已在 slot 循环前推到 post-parallel 预留区，onTop=false 的内联文字也已从该区累加写入；
	//   后续 overlay 串行绘制继续从当前游标累加。）
	if (!pendingTopText.empty()) {
		PROFILE_SCOPE("7d.replay_topText");
		for (const DeferredTextCmd* t : pendingTopText) {
			DrawText(t->text, t->fontKey, t->fontSize, t->color, t->x, t->y, t->scale);
		}
		FlushBatch();
	}

	m_numActiveWorkers = 0;
	// 不释放 record 存储；下帧 Reset 复用 capacity。
}

// ----------------------------------------------------------------------------
//  Phase 5：Record 路径直接把 BatchVertex / glm::mat4 写进 r.slice 指向的当前帧
//  mapped VBO/SSBO 切片。texIndex 直接是 bindless 槽位绝对值；matrixIndex 直接是
//  (slice.ssboBaseMat + slice.ssboCount)，即整帧 SSBO 中的绝对槽位。回放完全不再
//  做索引重映射。
//
//  公共写顶点的"6-vert quad / 1 mat4"模板在下面 EmitQuad 里抽出来；非 quad 的
//  RecordFillCircle 直接展开。
// ----------------------------------------------------------------------------

namespace {
	// 容量检查：6 顶点 + 1 mat4。返回 false 表示溢出（slot 标记 overflowed，调用方早退）。
	inline bool SliceHasRoom(VkWorkerSlice& sl, uint32_t addVerts, uint32_t addMats) {
		sl.vboDemand  += addVerts;   // 计入"想写"的量（含被容量拒绝的），供精确扩容 + 负载均衡权重
		sl.ssboDemand += addMats;
		bool fit = true;
		if (sl.vboCount + addVerts > sl.vboCap)  { sl.vboOverflowed  = true; fit = false; }
		if (sl.ssboCount + addMats > sl.ssboCap) { sl.ssboOverflowed = true; fit = false; }
		return fit;
	}

	// 把一个矩阵推进切片 SSBO，返回它在整帧 SSBO 中的绝对下标。
	// 调用方必须先 SliceHasRoom 通过。
	inline uint32_t PushSliceMatrix(VkWorkerSlice& sl, const glm::mat4& m) {
		const uint32_t abs = sl.ssboBaseMat + sl.ssboCount;
		sl.ssboPtr[sl.ssboCount++] = m;
		return abs;
	}

	// 写一个 6-vert quad 到切片。pos/uv 按 (x,y) 矩形铺。texIdx / matAbs 是绝对槽位。
	// quad 排布与公开 DrawTexture（Graphics.cpp 中 BatchVertex vertices[6]）保持一致。
	inline void EmitQuad(VkWorkerSlice& sl,
		float u0, float v0, float u1, float v1,
		uint32_t texIdx, uint32_t matAbs,
		float r, float g, float b, float a, float bm)
	{
		const PackedClipRect clip = (tl_packedClipStack && !tl_packedClipStack->empty())
			? tl_packedClipStack->back()
			: PackedClipRect{};
		BatchVertex* v = sl.vboPtr + sl.vboCount;
		v[0] = { 0.0f, 1.0f, u0, v1, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		v[1] = { 1.0f, 1.0f, u1, v1, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		v[2] = { 1.0f, 0.0f, u1, v0, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		v[3] = { 0.0f, 1.0f, u0, v1, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		v[4] = { 0.0f, 0.0f, u0, v0, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		v[5] = { 1.0f, 0.0f, u1, v0, texIdx, matAbs, r, g, b, a, bm, clip.minXY, clip.maxXY };
		sl.vboCount += 6;
	}

	// 把一条"已经在世界坐标里的"端点拼成 1 px 宽的世界坐标 quad，写进切片。
	// 与公开 DrawLine / DrawRect / DrawCircle 的 1px-wide quad 走法保持一致。
	// 调用方负责传 matAbs（通常对应 worker 端的单位矩阵）。
	inline void EmitEdgeQuad(VkWorkerSlice& sl,
		float ax, float ay, float bx, float by,
		uint32_t texIdx, uint32_t matAbs,
		float rr, float gg, float bb, float aa, float bm)
	{
		const float edx = bx - ax, edy = by - ay;
		const float len = std::sqrt(edx * edx + edy * edy);
		if (len < 0.001f) return;
		const float nx = -edy / len * 0.5f, ny = edx / len * 0.5f;

		const PackedClipRect clip = (tl_packedClipStack && !tl_packedClipStack->empty())
			? tl_packedClipStack->back()
			: PackedClipRect{};
		BatchVertex* v = sl.vboPtr + sl.vboCount;
		v[0] = { ax + nx, ay + ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		v[1] = { ax - nx, ay - ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		v[2] = { bx - nx, by - ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		v[3] = { ax + nx, ay + ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		v[4] = { bx - nx, by - ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		v[5] = { bx + nx, by + ny, 0.5f, 0.5f, texIdx, matAbs, rr, gg, bb, aa, bm, clip.minXY, clip.maxXY };
		sl.vboCount += 6;
	}
} // anonymous

void Graphics::RecordDrawTexture(WorkerRecord& r,
	const Texture* tex, float x, float y, float width, float height,
	float rotation, const glm::vec4& tint)
{
	if (!tex) return;
	VkWorkerSlice& sl = r.slice;
	if (!SliceHasRoom(sl, 6, 1)) return;

	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	local = glm::scale(local, glm::vec3(width, height, 1.0f));
	if (rotation != 0.0f) {
		local = glm::translate(local, glm::vec3(0.5f, 0.5f, 0.0f));
		local = glm::rotate(local, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		local = glm::translate(local, glm::vec3(-0.5f, -0.5f, 0.0f));
	}
	const bool curId = tl_transformIsIdentity->back() != 0;
	const glm::mat4 finalMatrix = curId ? local : (tl_transformStack->back() * local);
	const uint32_t matAbs = PushSliceMatrix(sl, finalMatrix);

	const glm::vec4 nt = NormalizeColor(tint);
	const float bm = EncodeBatchBlendMode(tl_blend);
	EmitQuad(sl, 0.0f, 0.0f, 1.0f, 1.0f, tex->BindingId(), matAbs, nt.r, nt.g, nt.b, nt.a, bm);
}

void Graphics::RecordDrawTextureMatrix(WorkerRecord& r,
	const Texture* tex, const glm::mat4& transform,
	float pivotX, float pivotY, const glm::vec4& tint, BlendMode blendMode)
{
	// Animator 最热路径（~9 万次/帧）。镜像公开 DrawTextureMatrix 的批处理段，但写进切片。
	if (!tex) return;

	// 显式 blendMode 参数语义为"per-draw override，不污染全局状态"——与非录制路径
	// （DrawTextureMatrix 只把 actualMode 写进 per-vertex bm 字段而不动 m_currentBlendMode）
	// 对齐。回放按 SetBlend 命令分段切流水线，所以这里必须在画完后把 tl_blend 恢复回去，
	// 否则 Animator 画完发光 quad 留下的 Add 状态会污染本 slot 后续所有 DrawTexture 调用
	// （典型现象：下一个 GameObject 的 ShadowComponent 影子被 Add 混合成不可见）。
	const BlendMode savedBlend = tl_blend;
	const bool needSwitch = (blendMode != BlendMode::None && blendMode != tl_blend);
	if (needSwitch) {
		RecordSetBlendMode(r, blendMode);   // 插入 SetBlend 命令并更新 tl_blend
	}

	VkWorkerSlice& sl = r.slice;
	if (!SliceHasRoom(sl, 6, 1)) {
		if (needSwitch) RecordSetBlendMode(r, savedBlend);   // 容量不足提前返回也要恢复
		return;
	}

	const Texture* bindTex = tex;
	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	if (tex->atlasPage) {
		bindTex = tex->atlasPage;
		u0 = tex->aU0; v0 = tex->aV0; u1 = tex->aU1; v1 = tex->aV1;
	}

	glm::mat4 pivotTransform;
	if (pivotX != 0.0f || pivotY != 0.0f) {
		const glm::vec3 pivot(pivotX, pivotY, 0.0f);
		pivotTransform = glm::translate(glm::mat4(1.0f), pivot)
			* transform
			* glm::translate(glm::mat4(1.0f), -pivot);
	}
	else {
		pivotTransform = transform;
	}
	// 单位阵快速路径——Animator 通常压入单位阵作为基线，跳过 mat4×mat4 节省大量浮点。
	const bool curId = tl_transformIsIdentity->back() != 0;
	const glm::mat4 finalMatrix = curId ? pivotTransform
		: (tl_transformStack->back() * pivotTransform);
	const uint32_t matAbs = PushSliceMatrix(sl, finalMatrix);

	const glm::vec4 nt = NormalizeColor(tint);
	const BlendMode actualMode = (blendMode == BlendMode::None) ? tl_blend : blendMode;
	const float bm = EncodeBatchBlendMode(actualMode);
	EmitQuad(sl, u0, v0, u1, v1, bindTex->BindingId(), matAbs, nt.r, nt.g, nt.b, nt.a, bm);

	if (needSwitch) {
		RecordSetBlendMode(r, savedBlend);   // 恢复进入时的 tl_blend，避免污染后续绘制
	}
}

void Graphics::RecordDrawTextureRegion(WorkerRecord& r,
	const Texture* tex,
	float srcX, float srcY, float srcW, float srcH,
	float dstX, float dstY, float dstW, float dstH,
	float rotation, const glm::vec4& tint)
{
	if (!tex || tex->BindingId() == 0) return;
	VkWorkerSlice& sl = r.slice;
	if (!SliceHasRoom(sl, 6, 1)) return;

	const float u0 = srcX / tex->width;
	const float v0 = srcY / tex->height;
	const float u1 = (srcX + srcW) / tex->width;
	const float v1 = (srcY + srcH) / tex->height;

	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(dstX, dstY, 0.0f));
	local = glm::scale(local, glm::vec3(dstW, dstH, 1.0f));
	if (rotation != 0.0f) {
		local = glm::translate(local, glm::vec3(0.5f, 0.5f, 0.0f));
		local = glm::rotate(local, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		local = glm::translate(local, glm::vec3(-0.5f, -0.5f, 0.0f));
	}
	const bool curId = tl_transformIsIdentity->back() != 0;
	const glm::mat4 finalMatrix = curId ? local : (tl_transformStack->back() * local);
	const uint32_t matAbs = PushSliceMatrix(sl, finalMatrix);

	const glm::vec4 nt = NormalizeColor(tint);
	const float bm = EncodeBatchBlendMode(tl_blend);
	EmitQuad(sl, u0, v0, u1, v1, tex->BindingId(), matAbs, nt.r, nt.g, nt.b, nt.a, bm);
}

void Graphics::RecordDrawCachedText(WorkerRecord& r,
	const CachedText& handle, float x, float y, float scale)
{
	// CachedText 是 immutable POD——worker 直接走快速路径，不需要 DeferredText。
	if (handle.BindingId() == 0) return;
	// 过期句柄丢弃（同 DrawCachedText）。m_textGeneration 只在帧外（RecomputeLetterbox）变更，
	// 并行录制期间稳定，worker 读取安全。
	if (handle.generation != m_textGeneration) return;

	// Vulkan 默认路径把整行纹理也录成一条 InstanceRecord，与僵尸 sprite/字形保持
	// 原地交错顺序。-NoInstance 和裁剪场景保留原六顶点 batch，作为兼容与对照路径。
	if (m_useInstancePath && !IsClipActive()) {
		const float inv = scale / handle.superSample;
		const float width = handle.width * inv;
		const float height = handle.height * inv;
		const float drawX = x + handle.offsetX * inv;
		const float drawY = y + handle.offsetY * inv;
		const glm::mat4& top = tl_transformStack->back();
		const bool topIsId = tl_transformIsIdentity->back() != 0;

		InstanceRecord rec;
		if (topIsId) {
			rec.tA = width; rec.tB = 0.0f; rec.tC = 0.0f; rec.tD = height;
			rec.tx = drawX; rec.ty = drawY;
		}
		else {
			rec.tA = top[0].x * width; rec.tB = top[0].y * width;
			rec.tC = top[1].x * height; rec.tD = top[1].y * height;
			rec.tx = top[0].x * drawX + top[1].x * drawY + top[3].x;
			rec.ty = top[0].y * drawX + top[1].y * drawY + top[3].y;
		}
		rec.u0 = 0.0f; rec.v0 = 0.0f;
		rec.u1 = 1.0f; rec.v1 = 1.0f;
		rec.texSlot = handle.BindingId();
		rec.colorRGBA8 = 0xFFFFFFFFu;
		AppendReanimInstance(rec, BlendMode::Alpha);
		return;
	}

	VkWorkerSlice& sl = r.slice;
	if (!SliceHasRoom(sl, 6, 1)) return;

	glm::mat4 local = glm::mat4(1.0f);
	// 超采样纹理：除回逻辑尺寸保证屏幕布局不变（handle.superSample=1 时等价）。
	const float inv = scale / handle.superSample;
	local = glm::translate(local, glm::vec3(
		x + handle.offsetX * inv, y + handle.offsetY * inv, 0.0f));
	local = glm::scale(local, glm::vec3(handle.width * inv, handle.height * inv, 1.0f));
	const bool curId = tl_transformIsIdentity->back() != 0;
	const glm::mat4 finalMatrix = curId ? local : (tl_transformStack->back() * local);
	const uint32_t matAbs = PushSliceMatrix(sl, finalMatrix);
	const float bm = EncodeBatchBlendMode(tl_blend);
	EmitQuad(sl, 0.0f, 0.0f, 1.0f, 1.0f, handle.BindingId(), matAbs, 1.0f, 1.0f, 1.0f, 1.0f, bm);
}

void Graphics::RecordDrawText(WorkerRecord& r,
	const std::string& text, const std::string& fontKey, int fontSize,
	const glm::vec4& color, float x, float y, float scale, bool onTop)
{
	// DrawText 涉及 TTF 渲染 + GPU 上传 + LRU 维护，全部都不是线程安全的——一律 defer 到主线程。
	// onTop=false：ReplayAndEndParallel 在该 cmd 记录的几何位置就地渲染（对象同 z-order）。
	// onTop=true ：收集到所有 slot 几何之后统一渲染（绝对顶层）。
	DeferredTextCmd t;
	t.text = text;
	t.fontKey = fontKey;
	t.fontSize = fontSize;
	t.color = color;
	t.x = x;
	t.y = y;
	t.scale = scale;
	t.onTop = onTop;
	if (!onTop && tl_clipStack && !tl_clipStack->empty()) {
		t.hasClipRect = true;
		t.clipRect = tl_clipStack->back();
	}
	r.textCmds.push_back(std::move(t));

	RecordCmd c{};
	c.type = RecCmdType::DeferredText;
	c.vertOffsetAtCmd = r.slice.vboCount;
	c.instOffsetAtCmd = r.slice.instCount;   // Task 4
	c.payloadIdx = (uint32_t)(r.textCmds.size() - 1);
	r.cmds.push_back(c);
}

void Graphics::RecordDrawGlyphRun(WorkerRecord& r,
	const std::string& text, const std::string& fontKey, int fontSize,
	const glm::vec4& color, float x, float y, float scale)
{
	// 快路径：图集已就绪时，worker 就地把字形拼成 InstanceRecord 写进本 slot 切片——与
	// reanim 精灵同一条 instancing 管线。避免 defer 到串行 replay 逐行 FlushBatch/重绑/打断
	// instancing（20000 可见血量行时 replay 从 0.04ms 涨到 ~25ms 的根因）。记录顺序即绘制
	// 顺序，z-order 与旧的就地交错完全一致。
	// 线程安全前提：并行录制期主线程不绘制、不触碰 m_glyphAtlases（只读并发 find 安全）；
	// 图集缺失/缺码点/letterbox 变化走下面的 defer 慢路径，由主线程在 replay 中构建，下帧
	// 起自动回到快路径。
	do {
		// 紧凑 InstanceRecord 不携带裁剪框；裁剪字形保留下面的 deferred batch 路径。
		if (IsClipActive()) break;
		WorkerGlyphRunCache& cache = tl_glyphRunCache;
		const bool sameAtlasKey = cache.owner == this
			&& cache.atlasGeneration == m_glyphAtlasGeneration
			&& cache.fontSize == fontSize && cache.fontKey == fontKey;
		if (!sameAtlasKey) {
			const auto itA = m_glyphAtlases.find(fontKey + "|" + std::to_string(fontSize));
			if (itA == m_glyphAtlases.end()) break;
			cache.owner = this;
			cache.atlasGeneration = m_glyphAtlasGeneration;
			cache.fontKey = fontKey;
			cache.fontSize = fontSize;
			cache.atlas = &itA->second;
			cache.text.clear();
			cache.glyphCount = 0;
		}
		if (!cache.atlas) break;
		const GlyphAtlas& atlas = *cache.atlas;
		if (atlas.BindingId() == 0) break;
		float ss;
		if (atlas.physSize != ComputeTextRasterSize(fontSize, m_letterboxScale, ss)) break;

		// 连续相同文本直接复用解码和 glyph hash 结果；字形几何与原路径完全相同。
		if (cache.text != text) {
			uint32_t cps[64];
			int n = 0;
			bool tooLong = false;
			for (size_t i = 0; i < text.size(); ) {
				uint32_t cp = DecodeUtf8(text, i);
				if (!cp) continue;
				if (n >= 64) { tooLong = true; break; }
				cps[n++] = cp;
			}
			if (tooLong) break;
			if (n == 0) return;

			bool missing = false;
			for (int i = 0; i < n; ++i) {
				auto itG = atlas.glyphs.find(cps[i]);
				if (itG == atlas.glyphs.end()) { missing = true; break; }
				cache.glyphs[i] = &itG->second;
			}
			if (missing) break;
			cache.text = text;
			cache.glyphCount = n;
		}
		const int n = cache.glyphCount;
		if (n == 0) return;

		VkWorkerSlice& sl = r.slice;
		// 字形走 Alpha 混合；与 AppendReanimInstance 相同的 SetBlend 包夹 + 恢复模式，
		// 防止污染同 slot 后续记录的 bm。
		const BlendMode savedBlend = tl_blend;
		const bool needSwitch = savedBlend != BlendMode::None
			&& savedBlend != BlendMode::Alpha;
		if (needSwitch) RecordSetBlendMode(r, BlendMode::Alpha);

		// r=lsb, a=msb，同 reanim_inst.vert.glsl 的解包约定（颜色是 0..255 约定）。
		auto pack8 = [](float v) -> uint32_t {
			return (uint32_t)std::clamp((int)(v + 0.5f), 0, 255);
			};
		const uint32_t colorRGBA8 = pack8(color.r) | (pack8(color.g) << 8)
			| (pack8(color.b) << 16) | (pack8(color.a) << 24);

		const glm::mat4& top = tl_transformStack->back();
		const bool topIsId = tl_transformIsIdentity->back() != 0;
		const float invSS = scale / atlas.superSample;
		const float ascent = (float)atlas.ascent;
		const uint32_t atlasBinding = atlas.BindingId();
		float penX = 0.0f;
		for (int i = 0; i < n; ++i) {
			const GlyphInfo& gi = *cache.glyphs[i];
			if (gi.pxW > 0 && gi.pxH > 0) {
				sl.instDemand++;   // 计入"想写"的实例（含被容量拒绝的），供扩容 + 负载均衡
				if (sl.instCount >= sl.instCap) {
					sl.instOverflowed = true;
				}
				else {
					const float gx = x + (penX + gi.bearingX) * invSS;
					const float gy = y + (ascent - gi.bearingY) * invSS;
					const float gw = gi.pxW * invSS;
					const float gh = gi.pxH * invSS;
					InstanceRecord rec;
					if (topIsId) {
						rec.tA = gw; rec.tB = 0.0f; rec.tC = 0.0f; rec.tD = gh;
						rec.tx = gx; rec.ty = gy;
					}
					else {
						// final = top * T(gx,gy) * S(gw,gh)：线性部分 = top 前两列各乘 gw/gh，
						// 平移 = top 作用于 (gx,gy)。
						rec.tA = top[0].x * gw; rec.tB = top[0].y * gw;
						rec.tC = top[1].x * gh; rec.tD = top[1].y * gh;
						rec.tx = top[0].x * gx + top[1].x * gy + top[3].x;
						rec.ty = top[0].y * gx + top[1].y * gy + top[3].y;
					}
					rec.u0 = gi.u0; rec.v0 = gi.v0;
					rec.u1 = gi.u1; rec.v1 = gi.v1;
					rec.texSlot = atlasBinding;
					rec.colorRGBA8 = colorRGBA8;
					sl.instPtr[sl.instCount++] = rec;
				}
			}
			penX += gi.advance;
		}

		if (needSwitch) RecordSetBlendMode(r, savedBlend);
		return;
	} while (false);

	// 慢路径（首帧图集未建 / 新码点 / 建失败 / letterbox 变化 / 超长串）：只打包参数，
	// 图集构建 + 字形 quad 发射 defer 到主线程 replay（就地、当前层 z-order）。
	DeferredGlyphRunCmd t;
	t.text = text;
	t.fontKey = fontKey;
	t.fontSize = fontSize;
	t.color = color;
	t.x = x;
	t.y = y;
	t.scale = scale;
	if (tl_clipStack && !tl_clipStack->empty()) {
		t.hasClipRect = true;
		t.clipRect = tl_clipStack->back();
	}
	r.glyphRunCmds.push_back(std::move(t));

	RecordCmd c{};
	c.type = RecCmdType::DeferredGlyphRun;
	c.vertOffsetAtCmd = r.slice.vboCount;
	c.instOffsetAtCmd = r.slice.instCount;
	c.payloadIdx = (uint32_t)(r.glyphRunCmds.size() - 1);
	r.cmds.push_back(c);
}

void Graphics::RecordFillRect(WorkerRecord& r,
	float x, float y, float width, float height, const glm::vec4& color)
{
	VkWorkerSlice& sl = r.slice;
	if (!SliceHasRoom(sl, 6, 1)) return;

	glm::mat4 local = glm::mat4(1.0f);
	local = glm::translate(local, glm::vec3(x, y, 0.0f));
	local = glm::scale(local, glm::vec3(width, height, 1.0f));
	const bool curId = tl_transformIsIdentity->back() != 0;
	const glm::mat4 finalMatrix = curId ? local : (tl_transformStack->back() * local);
	const uint32_t matAbs = PushSliceMatrix(sl, finalMatrix);

	const glm::vec4 nc = NormalizeColor(color);
	const float bm = EncodeBatchBlendMode(tl_blend);
	EmitQuad(sl, 0.0f, 0.0f, 1.0f, 1.0f, m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm);
}

void Graphics::RecordDrawLine(WorkerRecord& r,
	float x1, float y1, float x2, float y2, const glm::vec4& color)
{
	VkWorkerSlice& sl = r.slice;
	// 单段线：6 顶点 + 1 单位矩阵
	if (!SliceHasRoom(sl, 6, 1)) return;

	const glm::mat4& transform = tl_transformStack->back();
	const glm::vec4 p0 = transform * glm::vec4(x1, y1, 0.0f, 1.0f);
	const glm::vec4 p1 = transform * glm::vec4(x2, y2, 0.0f, 1.0f);

	// 顶点本身已经是世界坐标，所以矩阵走单位阵（shader 用它做 model 变换会等价 no-op）。
	const uint32_t matAbs = PushSliceMatrix(sl, glm::mat4(1.0f));
	const glm::vec4 nc = NormalizeColor(color);
	const float bm = EncodeBatchBlendMode(tl_blend);
	EmitEdgeQuad(sl, p0.x, p0.y, p1.x, p1.y, m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm);
}

void Graphics::RecordDrawRect(WorkerRecord& r,
	float x, float y, float width, float height, const glm::vec4& color)
{
	VkWorkerSlice& sl = r.slice;
	// 矩形边：4 个边段，每段 6 顶点 = 24 顶点 + 1 共享单位矩阵
	if (!SliceHasRoom(sl, 24, 1)) return;

	const glm::mat4& transform = tl_transformStack->back();
	const glm::vec4 p[4] = {
		transform * glm::vec4(x,         y,          0.0f, 1.0f),
		transform * glm::vec4(x + width, y,          0.0f, 1.0f),
		transform * glm::vec4(x + width, y + height, 0.0f, 1.0f),
		transform * glm::vec4(x,         y + height, 0.0f, 1.0f),
	};
	const uint32_t matAbs = PushSliceMatrix(sl, glm::mat4(1.0f));
	const glm::vec4 nc = NormalizeColor(color);
	const float bm = EncodeBatchBlendMode(tl_blend);

	for (int i = 0; i < 4; ++i) {
		const int j = (i + 1) % 4;
		EmitEdgeQuad(sl, p[i].x, p[i].y, p[j].x, p[j].y,
			m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm);
	}
}

void Graphics::RecordDrawCircle(WorkerRecord& r,
	float cx, float cy, float radius, const glm::vec4& color, int segments)
{
	if (segments <= 0) return;
	VkWorkerSlice& sl = r.slice;
	// segments 个边段，每段 6 顶点 + 1 共享矩阵
	if (!SliceHasRoom(sl, (uint32_t)segments * 6, 1)) return;

	const glm::mat4& transform = tl_transformStack->back();
	const uint32_t matAbs = PushSliceMatrix(sl, glm::mat4(1.0f));
	const glm::vec4 nc = NormalizeColor(color);
	const float bm = EncodeBatchBlendMode(tl_blend);

	for (int i = 0; i < segments; ++i) {
		const float a0 = 2.0f * glm::pi<float>() * i / segments;
		const float a1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
		const glm::vec4 p0 = transform * glm::vec4(cx + radius * std::cos(a0), cy + radius * std::sin(a0), 0.0f, 1.0f);
		const glm::vec4 p1 = transform * glm::vec4(cx + radius * std::cos(a1), cy + radius * std::sin(a1), 0.0f, 1.0f);
		EmitEdgeQuad(sl, p0.x, p0.y, p1.x, p1.y,
			m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm);
	}
}

void Graphics::RecordFillCircle(WorkerRecord& r,
	float cx, float cy, float radius, const glm::vec4& color, int segments)
{
	if (segments <= 0) return;
	VkWorkerSlice& sl = r.slice;
	// 三角扇：segments 个三角形 × 3 顶点 + 1 共享矩阵。
	// 注意 EmitDrawRange 是 6 顶点 stride，3 顶点会被对齐丢弃——但 FillCircle 几乎只在
	// 调试路径（-Debug 显示 hitbox）使用，且原 RecordFillCircle 也是 3 顶点/段，对齐
	// 行为与原实现一致（不会有人在并行 record 路径里画大量 FillCircle）。
	if (!SliceHasRoom(sl, (uint32_t)segments * 3, 1)) return;

	const glm::mat4& transform = tl_transformStack->back();
	const glm::vec4 center = transform * glm::vec4(cx, cy, 0.0f, 1.0f);
	const uint32_t matAbs = PushSliceMatrix(sl, glm::mat4(1.0f));
	const glm::vec4 nc = NormalizeColor(color);
	const float bm = EncodeBatchBlendMode(tl_blend);
	const PackedClipRect clip = CurrentPackedClipRect();

	for (int i = 0; i < segments; ++i) {
		const float a0 = 2.0f * glm::pi<float>() * i / segments;
		const float a1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
		const glm::vec4 p0 = transform * glm::vec4(cx + radius * std::cos(a0), cy + radius * std::sin(a0), 0.0f, 1.0f);
		const glm::vec4 p1 = transform * glm::vec4(cx + radius * std::cos(a1), cy + radius * std::sin(a1), 0.0f, 1.0f);

		BatchVertex* v = sl.vboPtr + sl.vboCount;
		v[0] = { center.x, center.y, 0.5f, 0.5f, m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm, clip.minXY, clip.maxXY };
		v[1] = { p0.x,     p0.y,     0.5f, 0.5f, m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm, clip.minXY, clip.maxXY };
		v[2] = { p1.x,     p1.y,     0.5f, 0.5f, m_whiteTexture, matAbs, nc.r, nc.g, nc.b, nc.a, bm, clip.minXY, clip.maxXY };
		sl.vboCount += 3;
	}
}

void Graphics::RecordSetBlendMode(WorkerRecord& r, BlendMode mode) {
	if (mode == tl_blend) return;
	tl_blend = mode;

	r.blendModes.push_back(mode);
	RecordCmd c{};
	c.type = RecCmdType::SetBlend;
	c.vertOffsetAtCmd = r.slice.vboCount;
	c.instOffsetAtCmd = r.slice.instCount;   // Task 4
	c.payloadIdx = (uint32_t)(r.blendModes.size() - 1);
	r.cmds.push_back(c);
}
