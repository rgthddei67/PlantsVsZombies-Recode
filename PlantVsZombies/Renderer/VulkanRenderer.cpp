#include "VulkanRenderer.h"
#include "VulkanContext.h"
#include "VulkanPipeline.h"
#include "VulkanBuffer.h"
#include "../Logger.h"
#include "../Profiler.h"
#include <SDL2/SDL_image.h>
#include <vector>
#include <cstring>

namespace pvz {
	namespace {
#define VK_CHECK(expr)                                                      \
    do {                                                                    \
        VkResult _r = (expr);                                               \
        if (_r != VK_SUCCESS) {                                             \
            LOG_ERROR("VulkanRenderer") << #expr " failed (VkResult=" << (int)_r << ")"; \
            return false;                                                   \
        }                                                                   \
    } while (0)

		VkSemaphore MakeSemaphore(VkDevice dev) {
			VkSemaphoreCreateInfo ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VkSemaphore s = VK_NULL_HANDLE;
			vkCreateSemaphore(dev, &ci, nullptr, &s);
			return s;
		}

		VkFence MakeFenceSignaled(VkDevice dev) {
			VkFenceCreateInfo ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 第一帧能立刻通过 wait
			VkFence f = VK_NULL_HANDLE;
			vkCreateFence(dev, &ci, nullptr, &f);
			return f;
		}

	} // anonymous

	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() { Shutdown(); }

	CaptureTicket VulkanRenderer::RequestCapture(const std::string& pngPath) {
		const CaptureTicket ticket = mNextCaptureTicket++;
		auto& record = mCaptureRecords[ticket];
		if (pngPath.empty()) {
			record.status = CaptureStatus::Failed;
			record.error = "截图路径为空";
			return ticket;
		}
		if (mPendingCaptureTicket != 0) {
			record.status = CaptureStatus::Failed;
			record.error = "已有未完成截图请求";
			return ticket;
		}

		mPendingCaptureTicket = ticket;
		mCapturePath = pngPath;
		return ticket;
	}

	CaptureStatus VulkanRenderer::GetCaptureStatus(CaptureTicket ticket) const {
		const auto it = mCaptureRecords.find(ticket);
		return it == mCaptureRecords.end() ? CaptureStatus::Unknown : it->second.status;
	}

	std::string VulkanRenderer::GetCaptureError(CaptureTicket ticket) const {
		const auto it = mCaptureRecords.find(ticket);
		return it == mCaptureRecords.end() ? "未知截图 ticket" : it->second.error;
	}

	void VulkanRenderer::CompleteCapture(bool succeeded, const std::string& error) {
		if (mPendingCaptureTicket == 0) return;
		auto& record = mCaptureRecords[mPendingCaptureTicket];
		record.status = succeeded ? CaptureStatus::Succeeded : CaptureStatus::Failed;
		record.error = succeeded ? std::string{} : error;
		mPendingCaptureTicket = 0;
		mCapturePath.clear();
	}

	bool VulkanRenderer::Initialize(VulkanContext* ctx) {
		mCtx = ctx;
		mFrameError.clear();
		mTestFailurePoint = FrameFailurePoint::None;
		if (g_ProfileEnabled) {
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(ctx->PhysicalDevice(), &properties);
			mTimestampPeriodNs = properties.limits.timestampPeriod;

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(
				ctx->PhysicalDevice(), &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(
				ctx->PhysicalDevice(), &queueFamilyCount, queueFamilies.data());
			if (ctx->GraphicsQueueFamily() < queueFamilies.size()) {
				mTimestampValidBits = queueFamilies[ctx->GraphicsQueueFamily()].timestampValidBits;
			}
		}
		return CreateFrameResources();
	}

	bool VulkanRenderer::CreateFrameResources() {
		VkDevice dev = mCtx->Device();

		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			PerFrame& pf = mFrames[i];

			VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			pci.queueFamilyIndex = mCtx->GraphicsQueueFamily();
			pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK(vkCreateCommandPool(dev, &pci, nullptr, &pf.cmdPool));

			VkCommandBufferAllocateInfo aci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			aci.commandPool = pf.cmdPool;
			aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			aci.commandBufferCount = 1;
			VK_CHECK(vkAllocateCommandBuffers(dev, &aci, &pf.cmdBuffer));

			pf.imageAvailable = MakeSemaphore(dev);
			pf.inFlight = MakeFenceSignaled(dev);

			if (g_ProfileEnabled && mTimestampValidBits > 0) {
				VkQueryPoolCreateInfo queryInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
				queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
				queryInfo.queryCount = 2;
				VK_CHECK(vkCreateQueryPool(dev, &queryInfo, nullptr, &pf.timestampPool));
			}
			if (g_ProfileEnabled && mCtx->UsesPipelineStatisticsQuery()) {
				VkQueryPoolCreateInfo queryInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
				queryInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
				queryInfo.queryCount = 1;
				queryInfo.pipelineStatistics =
					VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
					VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
				VK_CHECK(vkCreateQueryPool(dev, &queryInfo, nullptr, &pf.pipelineStatsPool));
			}
		}

		// 每张 swapchain image 一个 renderFinished 信号量
		mRenderFinished.resize(mCtx->SwapchainImages().size());
		for (auto& s : mRenderFinished) s = MakeSemaphore(dev);

		return true;
	}

	void VulkanRenderer::DestroyFrameResources() {
		if (!mCtx) return;
		VkDevice dev = mCtx->Device();
		if (!dev) return;

		for (auto s : mRenderFinished) if (s) vkDestroySemaphore(dev, s, nullptr);
		mRenderFinished.clear();

		for (auto& pf : mFrames) {
			if (pf.timestampPool) {
				vkDestroyQueryPool(dev, pf.timestampPool, nullptr);
				pf.timestampPool = VK_NULL_HANDLE;
				pf.timestampPending = false;
			}
			if (pf.pipelineStatsPool) {
				vkDestroyQueryPool(dev, pf.pipelineStatsPool, nullptr);
				pf.pipelineStatsPool = VK_NULL_HANDLE;
				pf.pipelineStatsPending = false;
			}
			if (pf.inFlight) { vkDestroyFence(dev, pf.inFlight, nullptr);       pf.inFlight = VK_NULL_HANDLE; }
			if (pf.imageAvailable) { vkDestroySemaphore(dev, pf.imageAvailable, nullptr); pf.imageAvailable = VK_NULL_HANDLE; }
			if (pf.cmdPool) { vkDestroyCommandPool(dev, pf.cmdPool, nullptr);  pf.cmdPool = VK_NULL_HANDLE; pf.cmdBuffer = VK_NULL_HANDLE; }
		}
	}

	void VulkanRenderer::CollectGpuQueries(PerFrame& frame) {
		if (frame.timestampPool && frame.timestampPending && mTimestampValidBits > 0) {
			uint64_t ticks[2]{};
			const VkResult result = vkGetQueryPoolResults(mCtx->Device(), frame.timestampPool,
				0, 2, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			if (result == VK_SUCCESS) {
				uint64_t deltaTicks = ticks[1] - ticks[0];
				if (mTimestampValidBits < 64) {
					const uint64_t mask = (uint64_t{ 1 } << mTimestampValidBits) - 1;
					deltaTicks &= mask;
				}
				const double elapsedMs = static_cast<double>(deltaTicks)
					* static_cast<double>(mTimestampPeriodNs) / 1'000'000.0;
				Profiler::Get().Add("GPU.Frame_render", elapsedMs);
			}
			else if (result != VK_NOT_READY) {
				LOG_WARN("VulkanRenderer") << "GPU timestamp query failed (VkResult="
					<< static_cast<int>(result) << ")";
			}
			frame.timestampPending = false;
		}

		if (frame.pipelineStatsPool && frame.pipelineStatsPending) {
			uint64_t invocations[2]{};
			const VkResult result = vkGetQueryPoolResults(mCtx->Device(), frame.pipelineStatsPool,
				0, 1, sizeof(invocations), invocations, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			if (result == VK_SUCCESS) {
				Profiler::Get().CountGpuPipelineStats(invocations[0], invocations[1]);
			}
			else if (result != VK_NOT_READY) {
				LOG_WARN("VulkanRenderer") << "GPU pipeline statistics query failed (VkResult="
					<< static_cast<int>(result) << ")";
			}
			frame.pipelineStatsPending = false;
		}
	}

	void VulkanRenderer::Shutdown() {
		if (mCtx && mCtx->Device()) {
			vkDeviceWaitIdle(mCtx->Device());
		}
		DestroyFrameResources();
		mCtx = nullptr;
	}

	bool VulkanRenderer::OnSwapchainRecreated() {
		if (!mCtx || !mCtx->Device()) return false;
		VkDevice dev = mCtx->Device();

		// image 数可能变化（例如 MAILBOX 通常返回 3，FIFO 可能 2/3）。
		// 销毁旧的 per-image renderFinished 信号量，按新的 image 数重建。
		for (auto s : mRenderFinished) if (s) vkDestroySemaphore(dev, s, nullptr);
		mRenderFinished.clear();

		mRenderFinished.resize(mCtx->SwapchainImages().size());
		for (auto& s : mRenderFinished) s = MakeSemaphore(dev);

		// 帧索引复位，避免新一轮 BeginFrame 用错 PerFrame 槽位。
		mFrameIdx = 0;
		mAcquiredImageIdx = UINT32_MAX;
		mFrameActive = false;
		return true;
	}

	bool VulkanRenderer::FailFrame(const char* operation, VkResult result) {
		mFrameError = std::string(operation) + " failed (VkResult=" + std::to_string(static_cast<int>(result)) + ")";
		LOG_ERROR("VulkanRenderer") << mFrameError;
		CompleteCapture(false, mFrameError);
		mFrameActive = false;
		mAcquiredImageIdx = UINT32_MAX;
		return false;
	}

	bool VulkanRenderer::BeginFrame(float r, float g, float b, float a) {
		if (HasFrameFailure()) return false;
		if (!mCtx || !mCtx->IsInitialized()) return false;
		if (mFrameActive) {
			LOG_WARN("VulkanRenderer") << "BeginFrame called twice without EndFrame";
			return false;
		}
		VkDevice dev = mCtx->Device();
		PerFrame& frame = mFrames[mFrameIdx];

		// 1) 等本帧上一次 GPU 提交结束。独立统计以区分 GPU 背压与 CPU 命令准备成本。
		{
			PROFILE_SCOPE("C1a.BeginFrame_waitFence");
			const VkResult result = vkWaitForFences(dev, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
			if (result != VK_SUCCESS) return FailFrame("vkWaitForFences", result);
		}
		CollectGpuQueries(frame);

		// 2) 拿下一张 swapchain image
		VkResult acquireResult;
		{
			PROFILE_SCOPE("C1b.BeginFrame_acquire");
			acquireResult = vkAcquireNextImageKHR(
				dev, mCtx->Swapchain(), UINT64_MAX,
				frame.imageAvailable, VK_NULL_HANDLE, &mAcquiredImageIdx);
		}
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
			// swapchain 失效：标记需要重建，本帧跳过。fence 没 reset，下次照样能 wait。
			mSwapchainNeedsRebuild = true;
			return false;
		}
		if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
			return FailFrame("vkAcquireNextImageKHR", acquireResult);
		}
		if (acquireResult == VK_SUBOPTIMAL_KHR) {
			// 还能用，本帧继续，但帧末触发重建。
			mSwapchainNeedsRebuild = true;
		}

		{
			PROFILE_SCOPE("C1c.BeginFrame_cmdSetup");
			// 3) 重置命令池并 begin
			const VkResult resetResult = vkResetCommandPool(dev, frame.cmdPool, 0);
			if (resetResult != VK_SUCCESS) return FailFrame("vkResetCommandPool", resetResult);

			VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			const VkResult beginResult = mTestFailurePoint == FrameFailurePoint::Begin
				? VK_ERROR_OUT_OF_HOST_MEMORY : vkBeginCommandBuffer(frame.cmdBuffer, &bi);
			if (beginResult != VK_SUCCESS) return FailFrame("vkBeginCommandBuffer", beginResult);
			if (frame.timestampPool) {
				vkCmdResetQueryPool(frame.cmdBuffer, frame.timestampPool, 0, 2);
				vkCmdWriteTimestamp(frame.cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					frame.timestampPool, 0);
			}
			if (frame.pipelineStatsPool) {
				vkCmdResetQueryPool(frame.cmdBuffer, frame.pipelineStatsPool, 0, 1);
				vkCmdBeginQuery(frame.cmdBuffer, frame.pipelineStatsPool, 0, 0);
			}

			VkImage swapImage = mCtx->SwapchainImages()[mAcquiredImageIdx];
			const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			mCtx->CmdImageBarrier(frame.cmdBuffer, swapImage,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorRange);

			if (mCtx->UsesDynamicRendering()) {
				VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				color.imageView = mCtx->SwapchainImageViews()[mAcquiredImageIdx];
				color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				color.clearValue.color = { { r, g, b, a } };

				VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				renderingInfo.renderArea = { {0, 0}, mCtx->SwapchainExtent() };
				renderingInfo.layerCount = 1;
				renderingInfo.colorAttachmentCount = 1;
				renderingInfo.pColorAttachments = &color;
				mCtx->CmdBeginRendering(frame.cmdBuffer, renderingInfo);
			}
			else {
				VkClearValue clearValue{};
				clearValue.color = { { r, g, b, a } };
				VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
				renderPassInfo.renderPass = mCtx->LegacyRenderPass();
				renderPassInfo.framebuffer = mCtx->LegacyFramebuffer(mAcquiredImageIdx);
				renderPassInfo.renderArea = { {0, 0}, mCtx->SwapchainExtent() };
				renderPassInfo.clearValueCount = 1;
				renderPassInfo.pClearValues = &clearValue;
				vkCmdBeginRenderPass(frame.cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			}

			// 默认 viewport / scissor 固定整帧；对象级 ClipRect 随顶点/实例进入 fragment shader。
			// 负高度 viewport：Vulkan 1.1+ 支持，效果是把 clip-space Y 翻转，让 PVZ 沿用
			// 的 GL 风格正交矩阵 (top=0, bottom=h) 渲染到 framebuffer 时方向正确。
			// y 偏到 height，height 取负，等价于绕屏幕水平线翻转。
			VkViewport vp{};
			vp.x = 0.0f;
			vp.y = (float)mCtx->SwapchainExtent().height;
			vp.width = (float)mCtx->SwapchainExtent().width;
			vp.height = -(float)mCtx->SwapchainExtent().height;
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
			VkRect2D scissorRect{ {0, 0}, mCtx->SwapchainExtent() };
			vkCmdSetViewport(frame.cmdBuffer, 0, 1, &vp);
			vkCmdSetScissor(frame.cmdBuffer, 0, 1, &scissorRect);
		}

		mFrameActive = true;
		return true;
	}

	bool VulkanRenderer::EndFrame() {
		if (!mFrameActive) {
			LOG_WARN("VulkanRenderer") << "EndFrame called without BeginFrame";
			return false;
		}
		PerFrame& frame = mFrames[mFrameIdx];
		VkImage swapImage = mCtx->SwapchainImages()[mAcquiredImageIdx];

		if (mCtx->UsesDynamicRendering())
			mCtx->CmdEndRendering(frame.cmdBuffer);
		else
			vkCmdEndRenderPass(frame.cmdBuffer);
		if (frame.timestampPool) {
			vkCmdWriteTimestamp(frame.cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				frame.timestampPool, 1);
			frame.timestampPending = true;
		}
		if (frame.pipelineStatsPool) {
			vkCmdEndQuery(frame.cmdBuffer, frame.pipelineStatsPool, 0);
			frame.pipelineStatsPending = true;
		}

		// AutoTest 截图：present 之后图像归显示引擎所有，回读必须发生在 present 之前。
		VulkanBuffer captureBuf;   // 函数内 RAII：submit 后等 fence 再读，函数尾析构
		if (!mCapturePath.empty() && !mCtx->SwapchainSupportsTransferSrc()) {
			LOG_ERROR("VulkanRenderer") << "交换链不支持 TRANSFER_SRC，截图不可用";
			CompleteCapture(false, "交换链不支持 TRANSFER_SRC");
		}
		const bool capturing = !mCapturePath.empty();
		const VkExtent2D captureExt = mCtx->SwapchainExtent();
		if (capturing) {
			const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			mCtx->CmdImageBarrier(frame.cmdBuffer, swapImage,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colorRange);
			if (captureBuf.Create(mCtx,
				VkDeviceSize(captureExt.width) * captureExt.height * 4,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT, /*hostVisible=*/true, /*hostReadback=*/true)) {
				VkBufferImageCopy region{};   // bufferRowLength=0 → 行紧密排列
				region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				region.imageExtent = { captureExt.width, captureExt.height, 1 };
				vkCmdCopyImageToBuffer(frame.cmdBuffer, swapImage,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuf.Handle(), 1, &region);
			}
			else {
				LOG_ERROR("VulkanRenderer") << "截图 buffer 创建失败，本次截图跳过";
				CompleteCapture(false, "截图回读 buffer 创建失败");
			}
			mCtx->CmdImageBarrier(frame.cmdBuffer, swapImage,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, colorRange);
		}
		else {
			const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			mCtx->CmdImageBarrier(frame.cmdBuffer, swapImage,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, colorRange);
		}

		const VkResult endCommandResult = mTestFailurePoint == FrameFailurePoint::End
			? VK_ERROR_OUT_OF_HOST_MEMORY : vkEndCommandBuffer(frame.cmdBuffer);
		if (endCommandResult != VK_SUCCESS) {
			return FailFrame("vkEndCommandBuffer", endCommandResult);
		}

		// 只有完整录制后才撤销 fence 信号；提交失败则终止，绝不能再等这个未提交的 fence。
		const VkResult resetResult = vkResetFences(mCtx->Device(), 1, &frame.inFlight);
		if (resetResult != VK_SUCCESS) return FailFrame("vkResetFences", resetResult);
		const VkResult submitResult = mTestFailurePoint == FrameFailurePoint::Submit
			? VK_ERROR_OUT_OF_HOST_MEMORY : mCtx->SubmitCommandBuffer(
			frame.cmdBuffer, frame.inFlight,
			frame.imageAvailable, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			mRenderFinished[mAcquiredImageIdx], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		if (submitResult != VK_SUCCESS) {
			return FailFrame("frame submission", submitResult);
		}

		VkSwapchainKHR sc = mCtx->Swapchain();
		VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		pi.waitSemaphoreCount = 1;
		pi.pWaitSemaphores = &mRenderFinished[mAcquiredImageIdx];
		pi.swapchainCount = 1;
		pi.pSwapchains = &sc;
		pi.pImageIndices = &mAcquiredImageIdx;
		VkResult presentResult = vkQueuePresentKHR(mCtx->GraphicsQueue(), &pi);
		if (capturing) {
			// 即使 present 失败，已提交的回读仍可能在执行，局部 buffer 析构前必须等 GPU 完成。
			const VkResult waitResult = vkWaitForFences(mCtx->Device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
			if (waitResult != VK_SUCCESS) {
				vkDeviceWaitIdle(mCtx->Device());
				return FailFrame("capture vkWaitForFences", waitResult);
			}
		}
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
			// swapchain 已 stale：本帧已经送出去，无需视为失败；标记由上层在帧外重建。
			mSwapchainNeedsRebuild = true;
		}
		else if (presentResult != VK_SUCCESS) {
			return FailFrame("vkQueuePresentKHR", presentResult);
		}

		if (capturing) {
			if (captureBuf.MappedPtr()) {
				// GPU availability 已由 fence 保证；显式 invalidate 确保 CPU 可见性
				// （对 HOST_COHERENT 内存是 no-op，对 non-coherent 刷新 CPU cache line）。
				captureBuf.InvalidateMapped();
				std::string error;
				const bool saved = WriteCapturePng(
					captureBuf.MappedPtr(), captureExt.width, captureExt.height, error);
				CompleteCapture(saved, error);
			}
			else if (mPendingCaptureTicket != 0) {
				CompleteCapture(false, "截图回读 buffer 未映射");
			}
		}

		mFrameActive = false;
		mAcquiredImageIdx = UINT32_MAX;
		mFrameIdx = (mFrameIdx + 1) % FRAMES_IN_FLIGHT;
		return true;
	}

	VkCommandBuffer VulkanRenderer::CurrentCmdBuffer() const {
		if (!mFrameActive) return VK_NULL_HANDLE;
		return mFrames[mFrameIdx].cmdBuffer;
	}

	bool VulkanRenderer::WriteCapturePng(const void* bgraPixels, uint32_t w, uint32_t h,
		std::string& error) {
		// swapchain alpha 不参与合成、值不可靠：强制 255，避免 PNG 透明
		std::vector<uint8_t> pixels(size_t(w) * h * 4);
		std::memcpy(pixels.data(), bgraPixels, pixels.size());
		for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0xFF;

		// VK_FORMAT_B8G8R8A8_UNORM 内存序 = 小端 ARGB8888
		SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
			pixels.data(), (int)w, (int)h, 32, (int)w * 4, SDL_PIXELFORMAT_ARGB8888);
		if (!surf) {
			error = "截图 surface 创建失败: " + std::string(SDL_GetError());
			LOG_ERROR("VulkanRenderer") << error;
			return false;
		}
		if (IMG_SavePNG(surf, mCapturePath.c_str()) != 0) {
			error = "IMG_SavePNG 失败: " + std::string(IMG_GetError());
			LOG_ERROR("VulkanRenderer") << error;
			SDL_FreeSurface(surf);
			return false;
		}
		LOG_INFO("VulkanRenderer") << "截图已保存: " << mCapturePath;
		SDL_FreeSurface(surf);
		return true;
	}
} // namespace pvz
