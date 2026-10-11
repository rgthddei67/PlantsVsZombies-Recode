#pragma once
#include "ColdStoragePhasedPlanning.h"

namespace ColdStorageSearch {
/** 两套世界的原始结果独立保存；expected 只是同世界增量排序，不成为真钱或硬风险特征。 */
struct ObservedPlanComparison {
    PhasedPlanComparison robust, behavior;
    Result robustPassive, behaviorPassive;
    Result robustTimedWait, behaviorTimedWait, robustTimedDeferred, behaviorTimedDeferred;
    ObservedDecisionDiagnostics diagnostics;
    bool complete=false;
};
/** 先冻结共同条件权重，再完整比较已声明Profile下的各案及公平等待；截止不完整则禁止采用。 */
ObservedPlanComparison CompareObservedPlan(const Snapshot& state,const Weights& weights,
    const PhasedPlan& plan,const std::vector<Action>& incumbent,const ColdStorageResponse::Profile& profile,
    bool enableFollowup);
/** 已有付费资产先占试错额度；队列已在current中，不能重复计费或把免费单位算成资本。 */
float ObservedPurchaseAllowance(const Snapshot& state);
/** 有限通用能力/路线菜单，依据当次合法选项和真实可付款额度，不含关卡/陪练特判。 */
std::vector<Action> BuildObservedPurchaseCandidate(const Snapshot& state,float allowance);
/** 与直接候选共用嵌套轮转，保证每种可付能力都能配对每份当前证据。 */
std::size_t ObservedPurchaseProfileIndex(const Snapshot& state,float allowance);
}
