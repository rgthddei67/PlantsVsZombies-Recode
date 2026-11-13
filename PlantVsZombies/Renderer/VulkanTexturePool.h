#pragma once

#include "RenderBackend.h"
#include <volk.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace pvz {
	class VulkanContext;

	// 一个 bindless 纹理槽位。bindlessIndex 直接对应 fragment shader 中 textures[index] 的下标。
	struct VulkanTexture final : RenderTexture {
		VkImage         image = VK_NULL_HANDLE;
		VmaAllocation   alloc = VK_NULL_HANDLE;
		VkImageView     view = VK_NULL_HANDLE;
		uint32_t        bindlessIndex = 0;
		uint32_t        mipLevels = 1;   // 完整 mip 链级数（不支持 blit 时回退 1）
		// 后续 phase 会补 atlasPage + a{U,V}{0,1}
	};

	// Phase 2b — bindless 纹理系统：
	//   - 单个 descriptor set (set=0)，binding=1 为 sampler2D[8192] 数组
	//   - PARTIALLY_BOUND + UPDATE_AFTER_BIND + VARIABLE_DESCRIPTOR_COUNT
	//   - 共享一个 immutable LINEAR/CLAMP 采样器
	//   - 通过 free-list 管理 bindless 索引
	//   - CreateTextureRGBA8 走 staging buffer → vkCmdCopyBufferToImage → 屏障到 SHADER_READ_ONLY
	class VulkanTexturePool final : public TextureBackend {
	public:
		static constexpr uint32_t MAX_TEXTURES = 8192;

		VulkanTexturePool();
		~VulkanTexturePool();

		VulkanTexturePool(const VulkanTexturePool&) = delete;
		VulkanTexturePool& operator=(const VulkanTexturePool&) = delete;

		bool Initialize(VulkanContext* ctx);
		void Shutdown();

		// 上传 RGBA8 数据创建一张纹理。返回的指针由 pool 拥有；调用 DestroyTexture 释放。
		RendererBackend Backend() const override { return RendererBackend::Vulkan; }
		VulkanTexture* CreateTextureRGBA8(int width, int height, const void* pixels) override;
		/** 单级可采样颜色附件；初始 UNDEFINED，布局由调用方维护。 */
		VulkanTexture* CreateColorAttachment(int width, int height, VkFormat format);
		bool UpdateTextureRGBA8(RenderTexture* texture, int x, int y,
			int width, int height, const void* pixels) override;
		int MaxTextureSize() const override { return mMaxTextureSize; }

		// 把纹理交给延迟删除队列：立即从可见集合移除，但 GPU 句柄 + bindless 槽位保留到
		// 至少 FRAMES_IN_FLIGHT 帧后再回收，确保没有 in-flight 帧仍在引用。不再 vkDeviceWaitIdle。
		void DestroyTexture(RenderTexture* texture) override;

		// 每帧调一次（Graphics::BeginFrame 中、renderer->BeginFrame 成功后）：推进帧计数，
		// 回收"已过 FRAMES_IN_FLIGHT 帧"的延迟删除项。无活动帧/teardown 期不要调用。
		void BeginFrameTick();

		VkDescriptorSetLayout DescriptorSetLayout() const { return mLayout; }
		VkDescriptorSet       DescriptorSet()       const { return mSet; }

	private:
		bool CreateSampler();
		bool CreateLayout();
		bool CreatePoolAndSet();
		bool CreateUploadCommandPool();

		uint32_t AllocBindlessIndex();
		void     FreeBindlessIndex(uint32_t idx);

		bool UploadPixels(VulkanTexture& tex, const void* pixels, VkDeviceSize byteSize);
		void WriteDescriptor(uint32_t bindlessIndex, VkImageView view);

		// 真正销毁一项延迟删除：destroy view+image、归还 bindless 槽位。
		void ReclaimDeletion(VulkanTexture& tex);
		// teardown 用：无视帧龄，销毁队列里全部待删项（调用方须先 vkDeviceWaitIdle）。
		void FlushAllDeletions();

		VulkanContext* mCtx = nullptr;

		// R8G8B8A8_UNORM 是否支持 linear blit（optimal tiling）：Initialize 时查询一次。
		// 桌面 GPU 全支持；万一驱动缺失则所有纹理回退单级 mip（糊但不黑屏）。
		bool mMipmapCapable = false;
		int mMaxTextureSize = 4096;

		VkSampler             mSampler = VK_NULL_HANDLE;
		VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
		VkDescriptorPool      mPool = VK_NULL_HANDLE;
		VkDescriptorSet       mSet = VK_NULL_HANDLE;
		VkCommandPool         mUploadPool = VK_NULL_HANDLE;
		VkFence               mUploadFence = VK_NULL_HANDLE;

		std::vector<std::unique_ptr<VulkanTexture>> mTextures;  // 由 bindlessIndex 索引
		std::vector<uint32_t>                       mFreeList;
		uint32_t                                    mNextIndex = 0;

		// 延迟删除队列：DestroyTexture 把纹理移进来并打上当前帧号 tick；BeginFrameTick 在
		// (mFrameTick - tick >= FRAMES_IN_FLIGHT) 时回收。GPU 句柄 + bindless 槽位随 unique_ptr 存活至回收。
		struct PendingDeletion {
			std::unique_ptr<VulkanTexture> tex;
			uint64_t                       tick = 0;
		};
		std::vector<PendingDeletion> mPendingDeletions;
		uint64_t                     mFrameTick = 0;  // 单调递增，BeginFrameTick 每帧 +1
	};
} // namespace pvz
