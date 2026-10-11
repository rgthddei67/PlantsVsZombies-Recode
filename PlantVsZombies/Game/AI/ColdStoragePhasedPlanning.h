#pragma once
#include "ColdStorageSearch.h"

namespace ColdStorageSearch {
/** 新重建的通用两阶段方案；first 是唯一当前购物车，后队仍需新快照重新决策。 */
struct PhasedPlan { std::vector<Action> first; FollowupPolicy followup; };
struct PhasedPlanningMenu { std::vector<PhasedPlan> plans; };
/** 同一时域/权重/响应窗口的完整因果对照；complete=false 时任何分支都不能胜出。 */
struct PhasedPlanComparison {
    Result prefix, full, wait, deferred, incumbent;
    bool complete=false;
};
/** 有界生成完整合法构成，当前现金覆盖两批；不指定必购兵种或修改评分。 */
PhasedPlanningMenu BuildPhasedPlanningMenu(const Snapshot& state,const Result& incumbent);
/** 只读单案：同世界实际到账后才允许后队整笔付款，资本基线为真实被动等待。 */
Result EvaluatePhasedCandidate(const Snapshot& state,const Weights& weights,
    const std::vector<Action>& first,const FollowupPolicy& followup);
/** 只读消融：关闭后队不创建未来单位；共同响应窗口不是额外推演时长或收益。 */
Result EvaluatePhasedCounterfactual(const Snapshot& state,const Weights& weights,
    const std::vector<Action>& first,const FollowupPolicy& followup,bool enableFollowup,
    float sharedResponseEnvelope=0);
/** 前缀/全案/同尾等待/整队延后及原购物车共用当次冻结权重；在完整分支之间检查截止。 */
PhasedPlanComparison ComparePhasedPlan(const Snapshot& state,const Weights& weights,
    const PhasedPlan& plan,const std::vector<Action>& incumbent={});
}
