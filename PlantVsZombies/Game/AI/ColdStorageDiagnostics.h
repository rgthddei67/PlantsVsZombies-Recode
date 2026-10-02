#pragma once

namespace ColdStorageSearch {
/** 含某兵种/行的完整候选统计；收益属于整案，不能视为这只单位的独立边际收益。 */
struct CandidateStats {
	int type = 0, row = 0, evaluated = 0, standalone = 0, allowed = 0;
	int regroupRejected = 0, capitalRejected = 0, bestDenial = 0; // 0可接受、1低库存回报不足、2大额资本风险
	bool bestBreach = false;
	float bestScore = 0, bestCash = 0, bestProduction = 0, bestCost = 0, bestBlastLoss = 0;
	bool bestAllowedBreach = false;
	float bestAllowedScore = 0; // 与最终选择同样通过资金门禁的最佳含此兵种候选
};
/** 后台计划作废的诊断位；多原因同时发生时分别累计，不改变原提交门禁。 */
enum class PlanDiscardReason { Failed, Age, Policy, Budget, WorldChanged, PaidArrival, EscortLoss, Count };
}
