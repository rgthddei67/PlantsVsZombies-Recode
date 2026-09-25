#pragma once
#include "ColdStorageSearch.h"
#include <nlohmann/json_fwd.hpp>
#include "Game/Zombie/ZombieType.h"

namespace ColdStoragePolicy {
/** 仅 10-1/10-2 使用获准或明确试玩的有效资源；缺失、未授权或非法时回退旧指挥官。 */
const ColdStorageSearch::Weights* Get(int level);
/** AutoTest 独立试验覆盖，不写玩家存档/资源；null 强制旧 AI，Reset 清除覆盖。 */
bool SetExperiment(const nlohmann::json& weights, bool allUnits = false,
	const nlohmann::json* preferences = nullptr, const nlohmann::json* calibration = nullptr,
	const nlohmann::json* stateModel = nullptr, bool netEconomy = false);
/** Get 成功后返回策略的经济评分版本；仅显式启用的新策略使用净冰收益。 */
bool NetEconomy();
/** Get 成功后取得同一策略的局势评分层；旧配置返回空指针并保留原搜索规模。 */
const ColdStorageSearch::StateModel* AdaptiveModel();
/** Get 成功后使用同一策略的产冰校准；缺省为原预测，返回对象在本次同步搜索期间有效。 */
const ColdStorageSearch::ProductionCalibration* ProductionModel();
/** 仅 AutoTest 实验请求能解除训练波次限制，正式资源不能开启此模式。 */
bool AllUnits();
/** 实战训练修正兵种先验，补偿简化推演没有表达的特殊能力；未训练类型返回零。 */
ColdStorageSearch::ContextWeights UnitPreference(ZombieType type);
void ResetExperiment();
}
