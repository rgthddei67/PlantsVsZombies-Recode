#pragma once

namespace ColdStorageSearch {
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
