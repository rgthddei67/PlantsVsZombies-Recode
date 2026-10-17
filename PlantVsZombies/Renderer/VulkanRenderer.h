#pragma once

#include "RenderBackend.h"
#include <volk.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pvz {
	class VulkanContext;
	class VulkanPipeline;

	// Phase 1.5 — 持有命令池、命令缓冲、同步原语，跑 acquire→record→submit→present 循环。
	// Phase 2a — 可选挂一个 pipeline，每帧画 3 个顶点的三角形。
	class VulkanRenderer final : public CaptureBackend {
	public:
		static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

		VulkanRenderer();
		~VulkanRenderer();

		VulkanRenderer(const VulkanRenderer&) = delete;
		VulkanRenderer& operator=(const VulkanRenderer&) = delete;

		bool Initialize(VulkanContext* ctx);
		void Shutdown();

		// swapchain 热重建后调用：销毁旧的 per-image renderFinished 信号量、按新的 image 数重建。
		// 必须在 vkDeviceWaitIdle 后、下次 BeginFrame 之前调用。
		bool OnSwapchainRecreated();

		// Phase 3b — 帧生命周期拆分。
		// BeginFrame: wait fence + acquire image + reset pool + begin cb + barrier + beginRendering + setViewport/Scissor。
		//             成功后 CurrentCmdBuffer() 返回本帧 cb，调用方可往里录命令；失败返回 false 时调用方不应再调用 EndFrame。
		// EndFrame:   endRendering + barrier → PRESENT_SRC + endCb + submit2 + present。失败返回 false。
		bool BeginFrame(float r, float g, float b, float a);
		bool EndFrame();

		/** 不可恢复的帧错误会锁住后续 BeginFrame；调用方必须结束渲染并销毁后端。 */
		bool HasFrameFailure() const { return !mFrameError.empty(); }
		const std::string& GetFrameError() const { return mFrameError; }
		enum class FrameFailurePoint { None, Begin, End, Submit };
		/** 仅供显式 AutoTest 命令注入下一帧的 API 失败，不在正常游戏中调用。 */
		void FailNextFrameForTesting(FrameFailurePoint point) { mTestFailurePoint = point; }

		/**
		 * @brief 登记一次 AutoTest 截图，并返回可查询的单调递增 ticket。
		 * @details 下一次 EndFrame 在 present 前回读并写 PNG；同一时刻只接受一个未完成请求。
		 */
		CaptureTicket RequestCapture(const std::string& pngPath) override;
		CaptureStatus GetCaptureStatus(CaptureTicket ticket) const override;
		std::string GetCaptureError(CaptureTicket ticket) const override;

		VkCommandBuffer CurrentCmdBuffer() const;
		uint32_t        CurrentFrameIdx() const { return mFrameIdx; }

		// 当 acquire / present 报 OUT_OF_DATE / SUBOPTIMAL 时被置 true。
		// 上层应在帧外调用 VulkanContext::RecreateSwapchain + OnSwapchainRecreated 后调 Clear。
		bool NeedsSwapchainRebuild() const { return mSwapchainNeedsRebuild; }
		void ClearSwapchainRebuildFlag() { mSwapchainNeedsRebuild = false; }

	private:
		struct PerFrame {
			VkCommandPool   cmdPool = VK_NULL_HANDLE;
			VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
			VkSemaphore     imageAvailable = VK_NULL_HANDLE;  // signaled by acquire
			VkFence         inFlight = VK_NULL_HANDLE;  // CPU↔GPU
			VkQueryPool     timestampPool = VK_NULL_HANDLE; // -Profile 下的本帧 GPU 渲染时间戳
			bool            timestampPending = false;         // 该槽位上次提交是否有可读结果
			VkQueryPool     pipelineStatsPool = VK_NULL_HANDLE; // -Profile 下的 shader 调用量
			bool            pipelineStatsPending = false;
		};

		bool CreateFrameResources();
		void DestroyFrameResources();
		/** 在槽位 fence 完成后回收上次 GPU 诊断查询，不向渲染热路注入 CPU 等待。 */
		void CollectGpuQueries(PerFrame& frame);
		/** 记录终止原因、结束待处理截图，并阻止再次等待没有提交的 fence。 */
		bool FailFrame(const char* operation, VkResult result);

		// 当前帧 acquire 到的 swapchain image 索引；仅在 BeginFrame..EndFrame 之间有效。
		uint32_t mAcquiredImageIdx = UINT32_MAX;
		bool     mFrameActive = false;
		std::string mFrameError;
		FrameFailurePoint mTestFailurePoint = FrameFailurePoint::None;

		VulkanContext* mCtx = nullptr;

		std::array<PerFrame, FRAMES_IN_FLIGHT> mFrames;
		std::vector<VkSemaphore> mRenderFinished;  // 每张 swapchain image 一个（用于 present 时等待）
		float mTimestampPeriodNs = 0.0f;            // 设备 timestamp tick 的纳秒数
		uint32_t mTimestampValidBits = 0;           // graphics queue family 的有效位数，0=不支持

		uint32_t mFrameIdx = 0;
		bool     mSwapchainNeedsRebuild = false;

		struct CaptureRecord {
			CaptureStatus status = CaptureStatus::Pending;
			std::string error;
		};

		CaptureTicket mNextCaptureTicket = 1;
		CaptureTicket mPendingCaptureTicket = 0;
		std::unordered_map<CaptureTicket, CaptureRecord> mCaptureRecords;
		std::string mCapturePath;   // 非空 = 本帧 EndFrame 执行回读

		// 原子发布当前待处理 ticket 的最终状态，并释放单请求槽位。
		void CompleteCapture(bool succeeded, const std::string& error = {});
		bool WriteCapturePng(const void* bgraPixels, uint32_t w, uint32_t h,
			std::string& error);
	};
} // namespace pvz
