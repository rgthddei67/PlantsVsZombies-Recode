#include "ColdStoragePlanner.h"
#include "ColdStoragePlanEvaluator.h"
#include <chrono>
#include <algorithm>
#include <bit>
#include <utility>

namespace ColdStorageSearch {
Planner::~Planner() { Cancel(); }

bool Planner::Start(Snapshot snapshot, const Weights& weights, std::uint32_t seed, double timeBudgetMs) {
	if (Busy()) return false;
	auto work = std::make_unique<Work>();
	work->seed=seed;
	work->snapshot = std::move(snapshot);
	work->snapshot.stallProposal=std::exchange(mStallProposal,{});
	work->snapshot.stallPrecisionTargets=std::exchange(mStallPrecisionTargets,{});
	work->snapshot.proposals=mProposals; // 仅数值动作副本，每轮重新验证并评分；不允许旧结果直接付款。
	work->snapshot.searchRound=mSearchRound++;
	if(mAssaultIntent.Active()) {
		work->snapshot.assaultIntentRevision=mAssaultIntent.Revision();
		work->snapshot.searchVersion=2;
		// 未过门槛的公开变化仍允许正常新搜索；只把真正可触发的后队送入当前钱包重评。
		// 条件已满足时普通搜索也可重评，不受专用唤醒额度限制。
		if(mAssaultIntent.Eligible() && (mAssaultIntent.Conditional() || mAssaultIntent.NeedsReplan())) {
			work->snapshot.assaultContinuation=mAssaultProposals;
			work->snapshot.assaultContinuationRelativeTiming=mAssaultIntent.Conditional();
		}
	}
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
				std::unique_ptr<PlanEvaluator> evaluator;
				// 同步无截止夹具保留原随机/比较顺序；正式后台最多两条计算线程共享同一截止。
				if (timeBudgetMs>0) {
					try { evaluator=std::make_unique<PlanEvaluator>(); }
					catch (const std::system_error&) {} // 辅助线程无法创建时沿用单线程，仍保持同一预算。
				}
				job->workerThreads=evaluator ? 2 : 1;
                // 旧队列仅用一小部分预算重排，把主要计算机会留给自由采购。
                job->snapshot.searchDeadline=begin+duration/6;
				job->revision = ReplanCommitted(job->snapshot,job->weights,seed ^ 0x91A7u);
                job->snapshot.searchDeadline=deadline;
				job->result = Search(job->snapshot,job->weights,seed,evaluator.get());
				job->parallelPlans=evaluator ? evaluator->Submitted() : 0;
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
	if(!mWork->failed) {
		mProposals=mWork->result.proposals;
		if(!mProposals.empty()) {
			mProposalCursor=(mProposalCursor+5)%mProposals.size();
			std::rotate(mProposals.begin(),mProposals.begin()+mProposalCursor,mProposals.end());
		}
	}
	return std::move(mWork);
}

void Planner::Cancel() {
	mStallProposal.clear(); mStallPrecisionTargets.clear();
	mProposals.clear(); mProposalCursor=0;
	ClearAssault(); mSearchRound=0;
	if (!mWork) return;
	mWork->cancel.store(true,std::memory_order_relaxed);
	if (mThread.joinable()) mThread.join();
	mWork.reset();
}

void Planner::RememberStallPlan(const Snapshot& snapshot,const Result& result) {
	mStallProposal.clear(); mStallPrecisionTargets.clear();
	for(const auto& action:result.actions) {
		if(action.option<0 || action.option>=static_cast<int>(snapshot.options.size())) {mStallProposal.clear(); return;}
		const auto& option=snapshot.options[action.option];
		mStallProposal.push_back({option.type,option.row,option.device,option.setting,action.delay});
	}
	if(result.precisionTargetID>0) {
		mStallPrecisionTargets.push_back(result.precisionTargetID);
		mStallPrecisionTargets.insert(mStallPrecisionTargets.end(),result.precisionAdditionalTargetIDs.begin(),result.precisionAdditionalTargetIDs.end());
	}
}

bool Planner::ObserveAssault(float now,const AssaultObservation& observation) {
	mAssaultIntent.Observe(now,observation);
	if(!mAssaultIntent.Active()) mAssaultProposals.clear();
	return mAssaultIntent.NeedsReplan() && !mAssaultProposals.empty();
}

void Planner::RememberAssault(float now,const AssaultObservation& observation,const std::vector<Proposal>& proposals) {
	mWaitAnchorUsed=false; // 此接口只由已确认的新真实付款调用。
	mAssaultProposals.clear();
	for(const auto& proposal:proposals) {
		if(proposal.size()<4 || proposal.size()>64) continue;
		mAssaultProposals.push_back(proposal);
		if(mAssaultProposals.size()==2) break;
	}
	if(mAssaultProposals.empty() || (observation.activeSources==0 && observation.committedShots==0)) {mAssaultIntent.Clear(); return;}
	if(!mAssaultIntent.Arm(now,observation)) mAssaultProposals.clear();
}

bool Planner::RememberConditionalAssault(float now,const AssaultObservation& observation,const Snapshot& snapshot,
	const Result& result,bool purchased) {
	if(!result.phasedPlanning.selected || result.phasedPlanning.evaluated<=0
		|| result.followupPolicy.actions.empty() || result.followupPolicy.actions.size()>64) return false;
	if(!purchased && (!result.phasedPlanning.waitSelected || !result.actions.empty() || result.precisionTargetID>0
		|| mAssaultIntent.Active() || mWaitAnchorUsed)) return false;
	Proposal proposal;
	for(const auto& action:result.followupPolicy.actions) {
		if(action.option<0 || action.option>=static_cast<int>(snapshot.options.size())
			|| (std::bit_cast<std::uint32_t>(action.delay)&0x7f800000u)==0x7f800000u
			|| action.delay<0 || action.delay>300) return false;
		const auto& option=snapshot.options[action.option];
		if(option.row<0 || option.row>=snapshot.rows || option.unit.body.row!=option.row
			|| option.device>=0 || !CanForecastFollowup(option)) return false;
		proposal.push_back({option.type,option.row,-1,0,action.delay});
	}
	const auto& policy=result.followupPolicy;
	const AssaultTrigger trigger{policy.earliestSeconds,policy.expirySeconds,
		policy.requireObservedCobLaunch,policy.requireShotsClear,policy.maxReadyCobs};
	if(!mAssaultIntent.ArmConditional(now,observation,trigger)) {
		mAssaultProposals.clear();
		return false;
	}
	mAssaultProposals={std::move(proposal)};
	mWaitAnchorUsed=!purchased;
	return true;
}

void Planner::CompleteAssaultReview(std::uint64_t revision,bool purchased) {
	if(revision==0) return;
	if(purchased && mAssaultIntent.Active() && revision==mAssaultIntent.Revision()) {
		mAssaultIntent.Clear(); mAssaultProposals.clear();
		mWaitAnchorUsed=false;
	} else mAssaultIntent.MarkReplanned(revision);
}
}
