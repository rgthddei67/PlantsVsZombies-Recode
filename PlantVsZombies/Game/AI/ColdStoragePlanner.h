#pragma once

#include "ColdStorageSearch.h"
#include <atomic>
#include <memory>
#include <thread>

namespace ColdStorageSearch {
/** 独占快照的单工作线程；不接触 Board、资源单例或正式随机数，领取结果才回到主线程。 */
class Planner {
public:
	struct Work {
		Snapshot snapshot;
		Weights weights{};
		QueueRevision revision;
		Result result;
		double milliseconds = 0;
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
	/** 复制模型并转移数值快照；每个 Board 最多一个任务，繁忙时拒绝重复提交。 */
	bool Start(Snapshot snapshot, const Weights& weights, std::uint32_t seed);
	/** 非阻塞轮询；只领取完整结果，不发布被取消或尚未结束的半成品。 */
	std::unique_ptr<Work> TakeReady();
	bool Busy() const { return mWork != nullptr; }
	/** 在积分步边界合作取消并回收线程；不得让任务或模型指针逃逸到下一张棋盘。 */
	void Cancel();
private:
	std::unique_ptr<Work> mWork;
	std::thread mThread;
};
}
