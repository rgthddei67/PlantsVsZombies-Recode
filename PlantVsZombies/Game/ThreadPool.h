#pragma once
#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <functional>
#include <algorithm>

class ThreadPool {
public:
	explicit ThreadPool(int numThreads)
	{
		mWorkers.reserve(numThreads);
		for (int i = 0; i < numThreads; i++)
			mWorkers.emplace_back(&ThreadPool::WorkerLoop, this, i);
	}

	~ThreadPool() {
		{
			std::unique_lock<std::mutex> lock(mMutex);
			mShutdown = true;
		}
		mStartCV.notify_all();
		for (auto& t : mWorkers) t.join();
	}

	int GetWorkerCount() const { return static_cast<int>(mWorkers.size()); }

	/** 由单一调用线程同步分发；回调不能递归调用同一池，零工作线程时在调用者执行。 */
	void Dispatch(int totalItems, std::function<void(int, int)> func) {
		if (totalItems <= 0) return;
		if (mWorkers.empty()) { func(0,totalItems); return; }
		int n = static_cast<int>(mWorkers.size());
		if (n > totalItems) n = totalItems;

		{
			std::unique_lock<std::mutex> lock(mMutex);
			// 任务参数和代号一起发布；未参与上一轮的慢线程不能读到下一轮的一半状态。
			mWorkFunc = std::move(func);
			mTotalItems = totalItems;
			mNumActive = n;
			mDoneCount.store(0);
			mWorkGen++;
		}
		mStartCV.notify_all();

		std::unique_lock<std::mutex> lock(mMutex);
		mDoneCV.wait(lock, [this, n] { return mDoneCount.load() == n; });
	}

private:
	/** 每代先在锁内确定参与资格，只有计入本代完成屏障的线程才能在锁外读任务。 */
	void WorkerLoop(int idx) {
		int lastGen = 0;
		while (true) {
			int numActive, totalItems;
			{
				std::unique_lock<std::mutex> lock(mMutex);
				mStartCV.wait(lock, [this, lastGen] { return mShutdown || mWorkGen != lastGen; });
				if (mShutdown) return;
				lastGen = mWorkGen;
				// 不参与的线程必须在本代锁内返回等待，否则下一轮扩大任务数时可能重复执行并多计完成数。
				if (idx >= mNumActive) continue;
				numActive = mNumActive;
				totalItems = mTotalItems;
			}

			int chunkSize = (totalItems + numActive - 1) / numActive;
			int start = idx * chunkSize;
			int end = std::min(start + chunkSize, totalItems);
			if (start < totalItems)
				mWorkFunc(start, end);

			// 只有参与本轮分发的线程才计数
			if (idx < numActive) {
				if (mDoneCount.fetch_add(1) + 1 == numActive) {
					std::unique_lock<std::mutex> lock(mMutex);
					mDoneCV.notify_one();
				}
			}
		}
	}

	std::vector<std::thread> mWorkers;
	std::function<void(int, int)> mWorkFunc;
	int mTotalItems = 0;
	int mNumActive = 0;
	std::mutex mMutex;
	std::condition_variable mStartCV;
	std::condition_variable mDoneCV;
	std::atomic<int> mDoneCount{0};
	bool mShutdown = false;
	int mWorkGen = 0;
};

#endif
