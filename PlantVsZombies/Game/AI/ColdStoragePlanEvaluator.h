#pragma once

#include "ColdStorageSearch.h"
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace ColdStorageSearch {
/** 一次后台任务独占的辅助计算线程；仅执行完整候选，不访问棋盘或参与排名/付款。 */
class PlanEvaluator {
public:
	PlanEvaluator();
	~PlanEvaluator();
	PlanEvaluator(const PlanEvaluator&) = delete;
	PlanEvaluator& operator=(const PlanEvaluator&) = delete;
	/** 移交一个独立推演，调用方必须在快照/权重离开作用域前领取结果；队列最多一项。 */
	std::future<Result> Submit(std::packaged_task<Result()> task);
	int Submitted() const { return mSubmitted; }
private:
	/** 动态领取下一候选；停止时完成已领取/排队的任务，保证借用的只读数据仍有效。 */
	void Run();
	std::mutex mMutex;
	std::condition_variable mWake;
	std::packaged_task<Result()> mTask;
	bool mStopping = false;
	int mSubmitted = 0;
	std::thread mThread;
};
}
