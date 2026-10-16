#pragma once

#include "ColdStorageSearch.h"
#include "ColdStorageAssaultIntent.h"
#include <atomic>
#include <memory>
#include <thread>

namespace ColdStorageSearch {
/** 独占快照的协调/计算线程和一个辅助计算线程；不接触 Board、资源单例或正式随机数。 */
class Planner {
public:
	struct Work {
		std::uint32_t seed=0; // 捕获本次后台实际数值搜索种子，诊断不能用提交时的时间重新猜种子
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
	/** 复制模型并转移数值快照，加入本棋盘缓存提案作当前局势重评；繁忙时拒绝提交，零预算表示不限时。 */
	bool Start(Snapshot snapshot, const Weights& weights, std::uint32_t seed, double timeBudgetMs = 0);
	/** 非阻塞领取完整结果并更新未付款探索提案；调用方仍须校验局势，失败/取消任务不发布半成品。 */
	std::unique_ptr<Work> TakeReady();
	bool Busy() const { return mWork != nullptr; }
	/** 只读诊断：已领取前区分仍在计算与完整结果等待提交；只能由拥有 Planner 的线程调用。 */
	bool Computing() const { return mWork && !mWork->ready.load(std::memory_order_acquire); }
	/** 只保存完整复查的动作身份；下一次短搜索在当前快照重新比较，不沿用旧收益或购买权。 */
	void RememberStallPlan(const Snapshot& snapshot,const Result& result);
	/** 在积分步边界取消并回收线程，同时清空探索提案；任务、模型指针和缓存不得逃逸到下一张棋盘。 */
	void Cancel();
	/** 主线程只观察公开反制边沿；返回值仅唤醒重新评价，绝不代表炮阵已安全。 */
	bool ObserveAssault(float now,const AssaultObservation& observation);
	/** 在实际付款成功后保存有限未付款构成；旧案的价格、评分和预计收入均不保留。 */
	void RememberAssault(float now,const AssaultObservation& observation,const std::vector<Proposal>& proposals);
	/** 仅接纳完整比较胜出的条件案；真付款可立新锚，零付款等待每个付款周期最多立锚一次。
	 * 只复制当前选项的类型/路线/付款相对延迟和有界公开门槛，不保存旧评分、价格或购买权。
	 */
	bool RememberConditionalAssault(float now,const AssaultObservation& observation,const Snapshot& snapshot,
		const Result& result,bool purchased);
	/** 完整且未过期的结果实际采用后确认对应事件；新事件不得被旧结果清除。 */
	void CompleteAssaultReview(std::uint64_t revision,bool purchased);
	/** 关闭策略时只清未付款观察，不取消/重发已付事务，也不阻塞工作线程。 */
	void ClearAssault() {mAssaultIntent.Clear(); mAssaultProposals.clear(); mWaitAnchorUsed=false;}
	const AssaultIntentState& AssaultIntent() const {return mAssaultIntent;}
private:
	std::uint32_t mSearchRound=0;
	AssaultIntentState mAssaultIntent;
	std::vector<Proposal> mAssaultProposals;
	bool mWaitAnchorUsed=false; // 到期不清此锁，重复等待不能永久滚动保留整车；新付款或显式清理才复位
	size_t mProposalCursor=0; // 跨轮轮转不同探索构成，紧预算不能始终只复算列表头部
	std::vector<Proposal> mProposals; // 同棋盘内的无状态探索起点；Cancel/销毁清空，不写存档
	Proposal mStallProposal;
	std::vector<int> mStallPrecisionTargets;
	std::unique_ptr<Work> mWork;
	std::thread mThread;
};
}
