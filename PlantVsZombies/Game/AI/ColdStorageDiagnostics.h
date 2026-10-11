#pragma once
#include "ColdStorageBoundedTrials.h"
#include <vector>
#include <cstdint>

namespace ColdStorageSearch {
/** 信息试探仅记录真实完整对照；potential 是无新付费灰烬的假设上界，不是概率或评分收益。 */
enum class TrialRejectReason { Disabled, NoCandidate, NoRisk, History, NoPaidReaction, Incomplete,
    InvalidCandidate, NoMaterialPotential, NormalPriority, Selected };
struct TrialDiagnostics {
    std::uint64_t signature=0;
    int bill=0,cap=0,removedCounters=0;
    float robustScore=0,robustCash=0,robustPlantAssets=0,potentialCash=0,potentialPlantAssets=0; // cash按原始到账增量，plant按攻击未恢复资产损失
    bool robustBreach=false,potentialBreach=false,complete=false,selected=false;
    TrialRejectReason reason=TrialRejectReason::Disabled;
    ColdStorageTrials::Reason historyReason=ColdStorageTrials::Reason::Allowed;
};
/** 稳定诊断名，不作为交易权限。 */
const char* TrialRejectReasonName(TrialRejectReason reason);


/** 公开历史只改变排序；原完整最坏世界与实际钱包仍单独保留。 */
struct ObservedDecisionDiagnostics {
    struct Member {int type=0,row=0,cost=0;float delay=0;bool future=false;};
    std::vector<Member> candidate; // 完整比较的购物车及未付款尾队，不等同于实战已采购
    int profiles=0, evaluated=0, incomplete=0, valueRejected=0, capitalRejected=0, allowanceRejected=0;
    bool selected=false, phased=false;
    std::uint64_t revision=0;
    float windowSeconds=0, weight=0, riskCap=0, plannedBill=0;
    float robustDelta=0, behaviorDelta=0, combinedDelta=0, incumbentDelta=0;
    int behaviorFutureWorldsPurchased=0;
    int robustDelayedWorlds=0, behaviorDelayedWorlds=0; // 完整响应姿态中至少一次实际延期的世界数，仅诊断
    float behaviorFutureSpent=0, behaviorFuturePurchasedAt=-1;
};

/** 完整进攻的候选覆盖与拒绝原因；只用于诊断，不参与评分、付款或存档。 */
struct AssaultSearchDiagnostics {
	int continuationEvaluated=0, continuationUnavailable=0, continuationValueRejected=0, continuationCapitalRejected=0;
	int timingEvaluated=0; // 完整编队的事件时序变体实际评估数
	int jointEvaluated=0, jointLargestPlan=0, jointAfterStrike=0; // 技能联合案、实际兵数上限、整队在打击后出生的案数
	int jointValueRejected=0, jointRegroupRejected=0, jointCapitalRejected=0;
	int jointDeepSearches=0, jointProposalsRetained=0; // 未要求粗案先胜出的深化次数及跨轮保留构成
};
/** 现金边际估值的实际快照输入，单位为冰块/名额；未来兵价仅描述现钱包可走到的解锁路径。 */
struct CapitalUtilityInputs {
	int budget=0, capacity=0, recoveryReserve=0;
	int highestAffordableTroopCost=0, fundableUnlockTroopCost=0;
	bool weatherStation=false;
};
/** 显式诊断用工人轨迹；income 非零表示本步兑现的一批产冰，其余为两秒状态采样。 */
struct WorkerForecastTrace {
	int id = 0, row = 0;
	float at = 0, x = 0, health = 0, income = 0;
};
/** 显式诊断用灰烬实际引爆点；被提前消灭而取消的来源不会进入此表。 */
struct CounterForecastTrace {
	float at = 0, x = 0, damage = 0;
	int row = -1, column = -1;
	bool clearsCell = false;
};
/** 显式战斗取证的单个承伤者；总血量与防具分层并列，类型未知时为-1。 */
struct CombatForecastHit {
    int id=0, type=-1, row=-1, column=-1;
    float x=0, y=0, requestedDamage=0, before=0, after=0;
    float beforeBody=0, afterBody=0, beforeHelm=0, afterHelm=0, beforeShield=0, afterShield=0;
    bool activeBefore=false, activeAfter=false, headlessBefore=false, headlessAfter=false;
};
/** 预测相对时间的真实推进事件；事件名/原因只引用程序常量，不读或更改游戏随机流。 */
struct CombatForecastTrace {
    const char* event="";
    const char* reason="";
    float at=0, x=0, y=0, resolveAt=0;
    int attackID=0, sourceID=0, sourceRow=-1, sourceColumn=-1, targetRow=-1, targetColumn=-1, actionIndex=-1;
    std::vector<CombatForecastHit> targets;
};
/** 含某兵种/行的完整候选统计；收益属于整案，不能视为这只单位的独立边际收益。 */
struct CandidateStats {
	int type = 0, row = 0, evaluated = 0, standalone = 0, allowed = 0;
	int regroupRejected = 0, capitalRejected = 0, bestDenial = 0; // 0可接受、1低库存回报不足、2大额或累计资本风险
	bool bestBreach = false;
	float bestScore = 0, bestCash = 0, bestProduction = 0, bestCost = 0, bestBlastLoss = 0;
	bool bestAllowedBreach = false;
	float bestAllowedScore = 0; // 与最终选择同样通过资金门禁的最佳含此兵种候选
};
/** 后台计划作废的诊断位；多原因同时发生时分别累计，不改变原提交门禁。 */
enum class PlanDiscardReason { Failed, Age, Policy, Budget, WorldChanged, PaidArrival, EscortLoss, Count };
}
