#include "ColdStoragePlanner.h"
#include <chrono>
#include <utility>

namespace ColdStorageSearch {
Planner::~Planner() { Cancel(); }

bool Planner::Start(Snapshot snapshot, const Weights& weights, std::uint32_t seed, double timeBudgetMs) {
	if (Busy()) return false;
	auto work = std::make_unique<Work>();
	work->snapshot = std::move(snapshot);
	work->weights = weights;
	// 策略可被测试覆盖/重载，后台不能借用策略单例中的指针。
	if (work->snapshot.productionCalibration) {
		work->production = *work->snapshot.productionCalibration;
		work->snapshot.productionCalibration = &work->production;
	}
	if (work->snapshot.stateModel) {
		work->state = *work->snapshot.stateModel;
		work->snapshot.stateModel = &work->state;
	}
	work->snapshot.cancellation = &work->cancel;
	mWork = std::move(work);
	try {
		mThread = std::thread([job = mWork.get(),seed,timeBudgetMs] {
			const auto begin = std::chrono::steady_clock::now();
            job->budgetMilliseconds=timeBudgetMs;
            job->snapshot.timeLimitedSearch=timeBudgetMs>0;
            const auto duration=std::chrono::microseconds(static_cast<long long>(timeBudgetMs*1000));
            const auto deadline=begin+duration;
			try {
                // 旧队列仅用一小部分预算重排，把主要计算机会留给自由采购。
                job->snapshot.searchDeadline=begin+duration/6;
				job->revision = ReplanCommitted(job->snapshot,job->weights,seed ^ 0x91A7u);
                job->snapshot.searchDeadline=deadline;
				job->result = Search(job->snapshot,job->weights,seed);
			} catch (...) {
				// 取消和计算失败均不把半个方案提交给 Board。
				job->failed = true;
			}
			job->milliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
			job->ready.store(true,std::memory_order_release);
		});
	} catch (...) { mWork.reset(); return false; }
	return true;
}

std::unique_ptr<Planner::Work> Planner::TakeReady() {
	if (!mWork || !mWork->ready.load(std::memory_order_acquire)) return {};
	mThread.join();
	return std::move(mWork);
}

void Planner::Cancel() {
	if (!mWork) return;
	mWork->cancel.store(true,std::memory_order_relaxed);
	if (mThread.joinable()) mThread.join();
	mWork.reset();
}
}
