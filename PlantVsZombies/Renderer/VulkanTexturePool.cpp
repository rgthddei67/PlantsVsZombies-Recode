#include "VulkanTexturePool.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"   // FRAMES_IN_FLIGHT —— 延迟删除回收阈值的单一真相源
#include "../Logger.h"

#include <cstring>
#include <utility>

namespace pvz {
	namespace {
#define VK_CHECK(expr)                                                          \
    do {                                                                        \
        VkResult _r = (expr);                                                   \
        if (_r != VK_SUCCESS) {                                                 \
            LOG_ERROR("VulkanTexturePool") << #expr " failed (VkResult=" << (int)_r << ")"; \
            return false;                                                       \
        }                                                                       \
    } while (0)
	} // anonymous

	VulkanTexturePool::VulkanTexturePool() = default;
	VulkanTexturePool::~VulkanTexturePool() { Shutdown(); }

	bool VulkanTexturePool::Initialize(VulkanContext* ctx) {
		mCtx = ctx;
		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(mCtx->PhysicalDevice(), &deviceProperties);
		mMaxTextureSize = static_cast<int>(deviceProperties.limits.maxImageDimension2D);

		// mipmap 能力：生成 mip 链要求 R8G8B8A8_UNORM 在 optimal tiling 下支持
		// blit 源/目标 + linear 过滤采样。查询一次，不支持则全部纹理回退单级。
		{
			VkFormatProperties fp{};
			vkGetPhysicalDeviceFormatProperties(mCtx->PhysicalDevice(),
				VK_FORMAT_R8G8B8A8_UNORM, &fp);
			constexpr VkFormatFeatureFlags kNeeded =
				VK_FORMAT_FEATURE_BLIT_SRC_BIT |
				VK_FORMAT_FEATURE_BLIT_DST_BIT |
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			mMipmapCapable = (fp.optimalTilingFeatures & kNeeded) == kNeeded;
			if (!mMipmapCapable) {
				LOG_WARN("VulkanTexturePool") << "RGBA8 不支持 linear blit，mipmap 关闭（缩小绘制将欠采样）";
			}
		}

		if (!CreateSampler())            return false;
		if (!CreateLayout())             return false;
		if (!CreatePoolAndSet())         return false;
		if (!CreateUploadCommandPool())  return false;

		VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VK_CHECK(vkCreateFence(mCtx->Device(), &fci, nullptr, &mUploadFence));

		mTextures.reserve(64);
		return true;
	}

	bool VulkanTexturePool::CreateSampler() {
		VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		sci.magFilter = VK_FILTER_LINEAR;
		sci.minFilter = VK_FILTER_LINEAR;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.maxLod = VK_LOD_CLAMP_NONE;
		VK_CHECK(vkCreateSampler(mCtx->Device(), &sci, nullptr, &mSampler));
		return true;
	}

	bool VulkanTexturePool::CreateLayout() {
		// immutable sampler 数组：descriptorCount 个 entry，全部指向同一个采样器
		std::vector<VkSampler> samplers(MAX_TEXTURES, mSampler);

		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 1;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = MAX_TEXTURES;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		binding.pImmutableSamplers = samplers.data();

		VkDescriptorBindingFlags bindingFlags =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo bfci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		bfci.bindingCount = 1;
		bfci.pBindingFlags = &bindingFlags;

		VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		lci.pNext = &bfci;
		lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		lci.bindingCount = 1;
		lci.pBindings = &binding;

		VK_CHECK(vkCreateDescriptorSetLayout(mCtx->Device(), &lci, nullptr, &mLayout));
		return true;
	}

	bool VulkanTexturePool::CreatePoolAndSet() {
		VkDescriptorPoolSize ps{};
		ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		ps.descriptorCount = MAX_TEXTURES;

		VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		pci.maxSets = 1;
		pci.poolSizeCount = 1;
		pci.pPoolSizes = &ps;
		VK_CHECK(vkCreateDescriptorPool(mCtx->Device(), &pci, nullptr, &mPool));

		uint32_t variableCount = MAX_TEXTURES;
		VkDescriptorSetVariableDescriptorCountAllocateInfo vdc{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
		vdc.descriptorSetCount = 1;
		vdc.pDescriptorCounts = &variableCount;

		VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		ai.pNext = &vdc;
		ai.descriptorPool = mPool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &mLayout;
		VK_CHECK(vkAllocateDescriptorSets(mCtx->Device(), &ai, &mSet));
		return true;
	}

	bool VulkanTexturePool::CreateUploadCommandPool() {
		VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pci.queueFamilyIndex = mCtx->GraphicsQueueFamily();
		pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(mCtx->Device(), &pci, nullptr, &mUploadPool));
		return true;
	}

	uint32_t VulkanTexturePool::AllocBindlessIndex() {
		if (!mFreeList.empty()) {
			uint32_t i = mFreeList.back();
			mFreeList.pop_back();
			return i;
		}
		return mNextIndex++;
	}

	void VulkanTexturePool::FreeBindlessIndex(uint32_t idx) {
		mFreeList.push_back(idx);
	}

	void VulkanTexturePool::WriteDescriptor(uint32_t bindlessIndex, VkImageView view) {
		VkDescriptorImageInfo dii{};
		dii.imageView = view;
		dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// sampler 由 immutable sampler 提供，这里忽略

		VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		w.dstSet = mSet;
		w.dstBinding = 1;
		w.dstArrayElement = bindlessIndex;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		w.pImageInfo = &dii;

		vkUpdateDescriptorSets(mCtx->Device(), 1, &w, 0, nullptr);
	}

	bool VulkanTexturePool::UploadPixels(VulkanTexture& tex, const void* pixels, VkDeviceSize byteSize) {
		// 1) 创建 staging buffer，把像素 memcpy 进去
		VulkanBuffer staging;
		if (!staging.Create(mCtx, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, /*hostVisible=*/true)) return false;
		// 预乘 alpha：在拷入 staging 时把 rgb 乘以 a。
		// 源数据统一为 [R,G,B,A] 字节序（ResourceManager 转 ABGR8888，小端内存即 RGBA），
		// 目标 format 为 VK_FORMAT_R8G8B8A8_UNORM，布局一致。
		// 这样完全透明像素 (a=0) 的 rgb 归零，双线性过滤在边缘插值时不会把源图
		// 透明区域里隐藏的（常为白色）rgb 渗到不透明边缘，从根上消除白边/光晕。
		// 配套：管线混合 srcColorBlendFactor 用 ONE，片元着色器把 vColor.a 预乘进 rgb。
		{
			const uint8_t* src = static_cast<const uint8_t*>(pixels);
			uint8_t* dst = static_cast<uint8_t*>(staging.MappedPtr());
			const size_t pixelCount = static_cast<size_t>(byteSize) / 4;
			for (size_t i = 0; i < pixelCount; ++i) {
				const uint32_t a = src[i * 4 + 3];
				dst[i * 4 + 0] = static_cast<uint8_t>((src[i * 4 + 0] * a + 127) / 255);
				dst[i * 4 + 1] = static_cast<uint8_t>((src[i * 4 + 1] * a + 127) / 255);
				dst[i * 4 + 2] = static_cast<uint8_t>((src[i * 4 + 2] * a + 127) / 255);
				dst[i * 4 + 3] = static_cast<uint8_t>(a);
			}
		}

		// 2) 分配一次性命令缓冲
		VkCommandBuffer cb = VK_NULL_HANDLE;
		VkCommandBufferAllocateInfo aci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		aci.commandPool = mUploadPool;
		aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		aci.commandBufferCount = 1;
		VK_CHECK(vkAllocateCommandBuffers(mCtx->Device(), &aci, &cb));

		VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cb, &bi));

		// 3) 屏障：UNDEFINED → TRANSFER_DST_OPTIMAL（覆盖全部 mip 级；copy 只写 level 0，
		//    其余级随后由 blit 链写入，同样需要 TRANSFER_DST 布局）
		{
			const VkImageSubresourceRange range{
				VK_IMAGE_ASPECT_COLOR_BIT, 0, tex.mipLevels, 0, 1 };
			mCtx->CmdImageBarrier(cb, tex.image,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
				VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
		}

		// 4) Copy
		VkBufferImageCopy region{};
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageExtent = { (uint32_t)tex.width, (uint32_t)tex.height, 1 };
		vkCmdCopyBufferToImage(cb, staging.Handle(), tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// 5) 生成 mip 链：逐级 blit(linear)，level i-1 缩一半写入 level i。
		//    源像素已预乘 alpha，线性平均即正确降采样（不会把透明区隐藏色渗进 mip）。
		//    每级完成后立即转 SHADER_READ；mipLevels==1 时循环不执行，等价旧路径。
		{
			auto barrier = [&](uint32_t level,
				VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkImageLayout oldLayout,
				VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout) {
					const VkImageSubresourceRange range{
						VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
					mCtx->CmdImageBarrier(cb, tex.image,
						srcStage, srcAccess, dstStage, dstAccess,
						oldLayout, newLayout, range);
				};

			int32_t mipW = tex.width, mipH = tex.height;
			for (uint32_t i = 1; i < tex.mipLevels; ++i) {
				const int32_t nextW = mipW > 1 ? mipW / 2 : 1;
				const int32_t nextH = mipH > 1 ? mipH / 2 : 1;

				// level i-1：TRANSFER_DST → TRANSFER_SRC（等 copy/上一次 blit 写完）
				barrier(i - 1,
					VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

				VkImageBlit blit{};
				blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
				blit.srcOffsets[1] = { mipW, mipH, 1 };
				blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
				blit.dstOffsets[1] = { nextW, nextH, 1 };
				vkCmdBlitImage(cb,
					tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit, VK_FILTER_LINEAR);

				// level i-1：TRANSFER_SRC → SHADER_READ_ONLY（用完即放行给采样）
				barrier(i - 1,
					VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				mipW = nextW;
				mipH = nextH;
			}

			// 最后一级（无 mip 时即 level 0）：TRANSFER_DST → SHADER_READ_ONLY
			barrier(tex.mipLevels - 1,
				VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		VK_CHECK(vkEndCommandBuffer(cb));

		// 6) 同步提交（一次性、阻塞）
		vkResetFences(mCtx->Device(), 1, &mUploadFence);
		VK_CHECK(mCtx->SubmitCommandBuffer(cb, mUploadFence));
		vkWaitForFences(mCtx->Device(), 1, &mUploadFence, VK_TRUE, UINT64_MAX);

		vkFreeCommandBuffers(mCtx->Device(), mUploadPool, 1, &cb);
		// staging 在作用域结束时自动销毁
		return true;
	}

	VulkanTexture* VulkanTexturePool::CreateTextureRGBA8(int width, int height, const void* pixels) {
		if (!mCtx || width <= 0 || height <= 0 || !pixels) return nullptr;

		const uint32_t idx = AllocBindlessIndex();
		if (idx >= MAX_TEXTURES) {
			LOG_ERROR("VulkanTexturePool") << "bindless slot exhausted (max=" << MAX_TEXTURES << ")";
			FreeBindlessIndex(idx);
			return nullptr;
		}

		auto t = std::make_unique<VulkanTexture>();
		t->backend = RendererBackend::Vulkan;
		t->bindingId = idx;
		t->width = width;
		t->height = height;
		t->bindlessIndex = idx;

		// 完整 mip 链（到 1x1）。采样器早已是 LINEAR mipmapMode + LOD_CLAMP_NONE，
		// 图像有了链后自动按缩放比选级，缩小超过 2:1 不再欠采样发糊。
		if (mMipmapCapable) {
			uint32_t m = (uint32_t)(width > height ? width : height);
			while (m > 1) { m >>= 1; ++t->mipLevels; }
		}

		VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_R8G8B8A8_UNORM;
		ici.extent = { (uint32_t)width, (uint32_t)height, 1 };
		ici.mipLevels = t->mipLevels;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		// TRANSFER_SRC：生成 mip 链时上一级作为 blit 源
		ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo aci{};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		VkResult r = vmaCreateImage(mCtx->Allocator(), &ici, &aci, &t->image, &t->alloc, nullptr);
		if (r != VK_SUCCESS) {
			LOG_ERROR("VulkanTexturePool") << "vmaCreateImage failed (" << (int)r << ")";
			FreeBindlessIndex(idx);
			return nullptr;
		}

		VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		vci.image = t->image;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, t->mipLevels, 0, 1 };
		r = vkCreateImageView(mCtx->Device(), &vci, nullptr, &t->view);
		if (r != VK_SUCCESS) {
			LOG_ERROR("VulkanTexturePool") << "vkCreateImageView failed (" << (int)r << ")";
			vmaDestroyImage(mCtx->Allocator(), t->image, t->alloc);
			FreeBindlessIndex(idx);
			return nullptr;
		}

		const VkDeviceSize byteSize = (VkDeviceSize)width * (VkDeviceSize)height * 4;
		if (!UploadPixels(*t, pixels, byteSize)) {
			vkDestroyImageView(mCtx->Device(), t->view, nullptr);
			vmaDestroyImage(mCtx->Allocator(), t->image, t->alloc);
			FreeBindlessIndex(idx);
			return nullptr;
		}

		WriteDescriptor(idx, t->view);

		// 把指针存进槽位（按 bindlessIndex 索引）
		if (mTextures.size() <= idx) mTextures.resize(idx + 1);
		VulkanTexture* raw = t.get();
		mTextures[idx] = std::move(t);
		return raw;
	}

	VulkanTexture* VulkanTexturePool::CreateColorAttachment(int width, int height, VkFormat format) {
		if (!mCtx || width <= 0 || height <= 0) return nullptr;
		const uint32_t idx = AllocBindlessIndex();
		if (idx >= MAX_TEXTURES) return nullptr;
		auto t = std::make_unique<VulkanTexture>();
		t->backend = RendererBackend::Vulkan;
		t->bindingId = t->bindlessIndex = idx;
		t->width = width; t->height = height;
		VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
		image.mipLevels = image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VmaAllocationCreateInfo allocation{};
		allocation.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(mCtx->Allocator(), &image, &allocation, &t->image, &t->alloc, nullptr) != VK_SUCCESS) {
			FreeBindlessIndex(idx); return nullptr;
		}
		VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image = t->image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
		view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if (vkCreateImageView(mCtx->Device(), &view, nullptr, &t->view) != VK_SUCCESS) {
			vmaDestroyImage(mCtx->Allocator(), t->image, t->alloc);
			FreeBindlessIndex(idx); return nullptr;
		}
		WriteDescriptor(idx, t->view);
		if (mTextures.size() <= idx) mTextures.resize(idx + 1);
		auto* result = t.get();
		mTextures[idx] = std::move(t);
		return result;
	}

	bool VulkanTexturePool::UpdateTextureRGBA8(RenderTexture*, int, int, int, int, const void*) {
		// 当前 Vulkan 资源均为只读贴图；运行时字体采用整张重建。保留明确失败语义，
		// 避免在未知 image layout 上做不安全的原地更新。
		return false;
	}

	void VulkanTexturePool::DestroyTexture(RenderTexture* texture) {
		auto* tex = dynamic_cast<VulkanTexture*>(texture);
		if (!tex || !mCtx) return;
		const uint32_t idx = tex->bindlessIndex;
		if (idx >= mTextures.size() || mTextures[idx].get() != tex) return;

		// 延迟回收：从可见集合移出（此刻起 mTextures[idx] 即为空，查不到），GPU 句柄 + bindless
		// 槽位随 unique_ptr 进队列，等 BeginFrameTick 过了 FRAMES_IN_FLIGHT 帧再真正销毁。
		// 槽位此时不归还——否则新纹理会 WriteDescriptor 覆盖仍被 in-flight 帧采样的 descriptor。
		// 取代旧的 vkDeviceWaitIdle（整 GPU 停顿），消除每帧路径上的停顿隐患。
		mPendingDeletions.push_back({ std::move(mTextures[idx]), mFrameTick });
	}

	void VulkanTexturePool::ReclaimDeletion(VulkanTexture& tex) {
		if (tex.view)  vkDestroyImageView(mCtx->Device(), tex.view, nullptr);
		if (tex.image) vmaDestroyImage(mCtx->Allocator(), tex.image, tex.alloc);
		FreeBindlessIndex(tex.bindlessIndex);   // 句柄销毁后才归还槽位
	}

	void VulkanTexturePool::BeginFrameTick() {
		if (!mCtx) return;
		++mFrameTick;
		// 回收"已过 FRAMES_IN_FLIGHT 帧"的项。swap-and-pop 原地压缩，免 erase 搬移；
		// 最后一项不自移动，避免 unique_ptr 自移赋值。
		constexpr uint64_t kFIF = VulkanRenderer::FRAMES_IN_FLIGHT;
		for (size_t i = 0; i < mPendingDeletions.size();) {
			PendingDeletion& pd = mPendingDeletions[i];
			if (pd.tex && mFrameTick - pd.tick >= kFIF) {
				ReclaimDeletion(*pd.tex);
				if (i != mPendingDeletions.size() - 1) pd = std::move(mPendingDeletions.back());
				mPendingDeletions.pop_back();
			}
			else {
				++i;
			}
		}
	}

	void VulkanTexturePool::FlushAllDeletions() {
		// teardown 专用：调用方须已 vkDeviceWaitIdle，这里无视帧龄一律回收。
		for (auto& pd : mPendingDeletions) {
			if (pd.tex) ReclaimDeletion(*pd.tex);
		}
		mPendingDeletions.clear();
	}

	void VulkanTexturePool::Shutdown() {
		if (!mCtx) return;
		VkDevice dev = mCtx->Device();
		if (dev) vkDeviceWaitIdle(dev);

		// GPU 已空闲：先把延迟删除队列里所有待删项无视帧龄回收干净，再销毁仍在册的纹理。
		FlushAllDeletions();

		for (auto& up : mTextures) {
			if (!up) continue;
			if (up->view)  vkDestroyImageView(dev, up->view, nullptr);
			if (up->image) vmaDestroyImage(mCtx->Allocator(), up->image, up->alloc);
		}
		mTextures.clear();
		mFreeList.clear();
		mNextIndex = 0;

		if (mUploadFence) { vkDestroyFence(dev, mUploadFence, nullptr); mUploadFence = VK_NULL_HANDLE; }
		if (mUploadPool) { vkDestroyCommandPool(dev, mUploadPool, nullptr); mUploadPool = VK_NULL_HANDLE; }
		if (mPool) { vkDestroyDescriptorPool(dev, mPool, nullptr);    mPool = VK_NULL_HANDLE; mSet = VK_NULL_HANDLE; }
		if (mLayout) { vkDestroyDescriptorSetLayout(dev, mLayout, nullptr); mLayout = VK_NULL_HANDLE; }
		if (mSampler) { vkDestroySampler(dev, mSampler, nullptr); mSampler = VK_NULL_HANDLE; }
		mCtx = nullptr;
	}
} // namespace pvz
