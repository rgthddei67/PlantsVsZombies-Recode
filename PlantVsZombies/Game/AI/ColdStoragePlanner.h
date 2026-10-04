#pragma once

#include "ColdStorageSearch.h"
#include <atomic>
#include <memory>
#include <thread>

namespace ColdStorageSearch {
/** 独占快照的协调/计算线程和一个辅助计算线程；不接触 Board、资源单例或正式随机数。 */
class Planner {
public:
	struct Work {
		Snapshot snapshot;
		Weights weights{};
		QueueRevision revision;
		Result result;
		double milliseconds = 0;
        double budgetMilliseconds = 0; // 本次实时搜索的墙钟预算，毫秒；零表示不设截止
		int workerThreads = 1, parallelPlans = 0; // 实际计算线程数、辅助线程承担的完整候选数
		bool failed = false;
	private:
		friend class Planner;
		ProductionCalibration production;
		StateModel state;
		std::atomic<bool> cancel{false}, ready{false};
	};
	Planner() = default;
	~Planner();
	Planner(const Planner&) = delete;
	Planner& operator=(const Planner&) = delete;
	/** 复制模型并转移数值快照；繁忙时拒绝重复提交。预算在完整候选之间检查，零表示不限时。 */
	bool Start(Snapshot snapshot, const Weights& weights, std::uint32_t seed, double timeBudgetMs = 0);
	/** 非阻塞轮询；只领取完整结果，不发布被取消或尚未结束的半成品。 */
	std::unique_ptr<Work> TakeReady();
	bool Busy() const { return mWork != nullptr; }
	/** 只读诊断：已领取前区分仍在计算与完整结果等待提交；只能由拥有 Planner 的线程调用。 */
	bool Computing() const { return mWork && !mWork->ready.load(std::memory_order_acquire); }
	/** 在积分步边界合作取消并回收线程；不得让任务或模型指针逃逸到下一张棋盘。 */
	void Cancel();
private:
	std::unique_ptr<Work> mWork;
	std::thread mThread;
};
}
