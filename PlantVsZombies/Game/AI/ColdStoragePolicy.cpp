#include "ColdStoragePolicy.h"
#include "FileManager.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/MiniGameDefinition.h"
#include <map>
#include <cmath>
#include <nlohmann/json.hpp>

namespace ColdStoragePolicy {
namespace {
bool experiment = false, enabled = false, allTypes = false;
std::map<ZombieType, ColdStorageSearch::ContextWeights> unitPreferences, publishedPreferences;
ColdStorageSearch::Weights parameters{};
ColdStorageSearch::ProductionCalibration experimentalModel, publishedModel;
ColdStorageSearch::StateModel experimentalState, publishedState;
bool hasExperimentalState = false, hasPublishedState = false;
bool experimentalNetEconomy = false, publishedNetEconomy = false;
bool experimentalBuilding = false, publishedBuilding = false;

/** 校准资源限制树大小及拓扑；非法模型使整份候选失败，不悄悄略过。 */
bool ParseCalibration(const nlohmann::json& value, ColdStorageSearch::ProductionCalibration& out) {
	if (!value.is_object() || value.value("schema",0) != 1
		|| value.value("featureCount",0) != ColdStorageSearch::ProductionFeatureCount
		|| !value.contains("nodes") || !value.at("nodes").is_array() || value.at("nodes").size() > 63) return false;
	ColdStorageSearch::ProductionCalibration parsed;
	for (const auto& node : value.at("nodes")) {
		if (!node.is_object() || !node.at("feature").is_number_integer()
			|| !node.at("left").is_number_integer() || !node.at("right").is_number_integer()
			|| !node.at("threshold").is_number() || !node.at("value").is_number()) return false;
		parsed.nodes.push_back({node.at("feature").get<int>(),node.at("left").get<int>(),node.at("right").get<int>(),
			node.at("threshold").get<float>(),node.at("value").get<float>()});
	}
	if (!parsed.IsValid()) return false;
	out = std::move(parsed); return true;
}

/** 从资源/实验读取严格有限的固定维度权重。 */
bool Parse(const nlohmann::json& value, ColdStorageSearch::Weights& out) {
	if (!value.is_array() || value.size() != out.size()) return false;
	for (size_t i = 0; i < out.size(); ++i) {
		if (!value[i].is_number()) return false;
		out[i] = value[i].get<float>();
	}
	return ColdStorageSearch::ValidWeights(out);
}
/** 严格固定局势层维度；试玩标志不能绕过结构和有限值校验。 */
bool ParseStateModel(const nlohmann::json& value, ColdStorageSearch::StateModel& out) {
	if (!value.is_object() || value.value("schema",0) != 1
		|| value.value("featureCount",0) != ColdStorageSearch::StateFeatureCount
		|| !value.contains("coefficients") || !value.at("coefficients").is_array()
		|| value.at("coefficients").size() != out.coefficients.size()) return false;
	ColdStorageSearch::StateModel parsed;
	for (size_t i = 0; i < parsed.coefficients.size(); ++i)
		if (!Parse(value.at("coefficients")[i],parsed.coefficients[i])) return false;
	out = parsed; return true;
}
/** 兵种按稳定枚举名登记；每种权重对应公开的局势特征，未知名称拒绝加载。 */
bool ParsePreferences(const nlohmann::json& value, std::map<ZombieType, ColdStorageSearch::ContextWeights>& out) {
	std::map<ZombieType, ColdStorageSearch::ContextWeights> parsed;
	{
		if (!value.is_object()) return false;
		const auto& data = GameDataManager::GetInstance();
		for (const auto type : data.GetAllZombieTypes()) {
			const auto name = data.ZombieTypeToEnumName(type);
			if (!value.contains(name)) continue;
			const auto& entry = value.at(name);
			ColdStorageSearch::ContextWeights bias{};
			if (entry.is_number()) bias[0] = entry.get<float>();
			else if (!entry.empty() && entry.is_array() && entry.size() <= bias.size()) {
				for (size_t i = 0; i < entry.size(); ++i) { if (!entry[i].is_number()) return false; bias[i] = entry[i].get<float>(); }
			} else return false;
			for (float weight : bias) if (!std::isfinite(weight) || std::abs(weight) > 100) return false;
			parsed[type] = bias;
		}
		if (parsed.size() != value.size()) return false;
	}
	out = std::move(parsed); return true;
}
}
const ColdStorageSearch::Weights* Get(int level) {
	// 大混战使用同一份正式策略；冒险发布范围仍保持既有 10-1/10-2。
	if (!MiniGame::IsBrawl(level) && (level < 82 || level > 90 || (!experiment && level > 83))) return nullptr;
	if (experiment) return enabled ? &parameters : nullptr;
	// 发布的参数是只读版本化资源；实验进程的覆盖不会污染下次普通启动。
	static ColdStorageSearch::Weights published{};
	static const bool valid = [] {
		nlohmann::json data;
		if (!FileManager::FileExists("./resources/ai/cold_storage_policy.json")
			|| !FileManager::LoadJsonFile("./resources/ai/cold_storage_policy.json", data)) return false;
		try {
			publishedNetEconomy = data.value("netEconomy",false);
			publishedBuilding = data.value("anticipateBuilding",false);
			// 主人可显式试玩未通过胜率门槛的候选；保留 validated=false，且仍严格校验全部参数。
			return data.value("schema", 0) == 1 && (data.value("validated", false) || data.value("userRequestedTrial", false))
				&& Parse(data.at("weights"), published)
				&& (!data.contains("stateModel") || (hasPublishedState = ParseStateModel(data.at("stateModel"), publishedState)))
				&& (!data.contains("preferences") || ParsePreferences(data.at("preferences"), publishedPreferences))
				&& (!data.contains("productionCalibration") || ParseCalibration(data.at("productionCalibration"), publishedModel));
		} catch (const nlohmann::json::exception&) { return false; }
	}();
	return valid ? &published : nullptr;
}
bool SetExperiment(const nlohmann::json& weights, bool allUnits, const nlohmann::json* preferences, const nlohmann::json* calibration,
	const nlohmann::json* stateModel, bool netEconomy, bool anticipateBuilding) {
	ColdStorageSearch::Weights candidate{};
	std::map<ZombieType, ColdStorageSearch::ContextWeights> parsed;
	ColdStorageSearch::ProductionCalibration model;
	ColdStorageSearch::StateModel adaptive;
	const bool hasState = stateModel && !stateModel->is_null();
	try {
		if (!weights.is_null() && !Parse(weights, candidate)) return false;
		if (preferences && !ParsePreferences(*preferences, parsed)) return false;
		if (calibration && !calibration->is_null() && !ParseCalibration(*calibration,model)) return false;
		if (hasState && !ParseStateModel(*stateModel,adaptive)) return false;
	} catch (const nlohmann::json::exception&) { return false; }
	experiment = true; enabled = !weights.is_null(); parameters = candidate;
	allTypes = allUnits; unitPreferences = std::move(parsed); experimentalModel = std::move(model);
	experimentalState = adaptive; hasExperimentalState = hasState;
	experimentalNetEconomy = netEconomy;
	experimentalBuilding = anticipateBuilding;
	return true;
}
bool AnticipateBuilding() { return experiment ? enabled && experimentalBuilding : publishedBuilding; }
bool NetEconomy() { return experiment ? enabled && experimentalNetEconomy : publishedNetEconomy; }
const ColdStorageSearch::StateModel* AdaptiveModel() {
	if (experiment) return enabled && hasExperimentalState ? &experimentalState : nullptr;
	return hasPublishedState ? &publishedState : nullptr;
}
const ColdStorageSearch::ProductionCalibration* ProductionModel() {
	const auto& model = experiment ? experimentalModel : publishedModel;
	return model.nodes.empty() ? nullptr : &model;
}
bool AllUnits() { return experiment && allTypes; }
ColdStorageSearch::ContextWeights UnitPreference(ZombieType type) {
	const auto& values = experiment ? unitPreferences : publishedPreferences;
	const auto it = values.find(type);
	return (!experiment || enabled) && it != values.end() ? it->second : ColdStorageSearch::ContextWeights{};
}
void ResetExperiment() {
	experiment = enabled = allTypes = hasExperimentalState = false;
	experimentalNetEconomy = experimentalBuilding = false;
	unitPreferences.clear(); experimentalModel.nodes.clear(); experimentalState = {};
}
}
