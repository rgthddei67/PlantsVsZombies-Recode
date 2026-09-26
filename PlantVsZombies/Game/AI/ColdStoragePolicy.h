#pragma once
#include "ColdStorageSearch.h"
#include <nlohmann/json_fwd.hpp>
#include "Game/Zombie/ZombieType.h"

namespace ColdStoragePolicy {
/** 正式策略仅供 10-1/10-2 与大混战；训练可覆盖第十章，资源无效时回退旧指挥官。 */
const ColdStorageSearch::Weights* Get(int level);
/** AutoTest 独立试验覆盖，不写玩家存档/资源；null 强制旧 AI，Reset 清除覆盖。 */
bool SetExperiment(const nlohmann::json& weights, bool allUnits = false,
	const nlohmann::json* preferences = nullptr, const nlohmann::json* calibration = nullptr,
	const nlohmann::json* stateModel = nullptr, bool netEconomy = false, bool anticipateBuilding = false, int searchVersion = 1, float opponentWeight = 0);
/** 对方资源与植株资产终点差的学习权重，缺省零，不改变旧策略。 */
float OpponentWeight();
/** 已加载策略指定的搜索空间版本；评分权重和局势层不隐式决定第二版是否启用。 */
int SearchVersion();
/** Get 成功后返回策略的经济评分版本；仅显式启用的新策略使用净冰收益。 */
bool NetEconomy();
/** 显式启用受真实卡槽、资源和冷却约束的未来建设；旧配置保持原推演。 */
bool AnticipateBuilding();
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
