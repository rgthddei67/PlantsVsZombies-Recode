#pragma once
#include "ColdStorageSearch.h"
#include <nlohmann/json_fwd.hpp>
#include "Game/Zombie/ZombieType.h"

namespace ColdStoragePolicy {
/** 仅 10-1/10-2 使用通过评测的资源参数；缺失、未通过或非法时回退旧指挥官。 */
const ColdStorageSearch::Weights* Get(int level);
/** AutoTest 独立试验覆盖，不写玩家存档/资源；null 强制旧 AI，Reset 清除覆盖。 */
bool SetExperiment(const nlohmann::json& weights, bool allUnits = false,
	const nlohmann::json* preferences = nullptr);
/** 仅 AutoTest 实验请求能解除训练波次限制，正式资源不能开启此模式。 */
bool AllUnits();
/** 实战训练修正兵种先验，补偿简化推演没有表达的特殊能力；未训练类型返回零。 */
ColdStorageSearch::ContextWeights UnitPreference(ZombieType type);
void ResetExperiment();
}
