#include "ColdStoragePlanEvaluator.h"
#include <stdexcept>
#include <utility>

namespace ColdStorageSearch {
PlanEvaluator::PlanEvaluator() : mThread(&PlanEvaluator::Run,this) {}

PlanEvaluator::~PlanEvaluator() {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mStopping = true;
	}
	mWake.notify_one();
	if (mThread.joinable()) mThread.join();
}

std::future<Result> PlanEvaluator::Submit(std::packaged_task<Result()> task) {
	auto result = task.get_future();
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mStopping || mTask.valid()) throw std::logic_error("commander evaluator queue is occupied");
		mTask = std::move(task);
		++mSubmitted;
	}
	mWake.notify_one();
	return result;
}

void PlanEvaluator::Run() {
	for (;;) {
		std::packaged_task<Result()> task;
		{
			std::unique_lock<std::mutex> lock(mMutex);
			mWake.wait(lock,[&] { return mStopping || mTask.valid(); });
			if (mStopping && !mTask.valid()) return;
			task = std::move(mTask);
		}
		task(); // packaged_task 将异常交给协调线程；辅助线程不发布半个结果。
	}
}
}
