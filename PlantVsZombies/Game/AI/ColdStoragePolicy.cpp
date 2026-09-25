#include "ColdStoragePolicy.h"
#include "FileManager.h"
#include "Game/Plant/GameDataManager.h"
#include <map>
#include <cmath>
#include <nlohmann/json.hpp>

namespace ColdStoragePolicy {
namespace {
bool experiment = false, enabled = false, allTypes = false;
std::map<ZombieType, ColdStorageSearch::ContextWeights> unitPreferences, publishedPreferences;
ColdStorageSearch::Weights parameters{};
ColdStorageSearch::ProductionCalibration experimentalModel, publishedModel;

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
	// 训练可覆盖整个第十章；正式发布范围保持既有 10-1/10-2，扩展须另经各关验证。
	if (level < 82 || level > 90 || (!experiment && level > 83)) return nullptr;
	if (experiment) return enabled ? &parameters : nullptr;
	// 发布的参数是只读版本化资源；实验进程的覆盖不会污染下次普通启动。
	static ColdStorageSearch::Weights published{};
	static const bool valid = [] {
		nlohmann::json data;
		if (!FileManager::FileExists("./resources/ai/cold_storage_policy.json")
			|| !FileManager::LoadJsonFile("./resources/ai/cold_storage_policy.json", data)) return false;
		try {
			return data.value("schema", 0) == 1 && data.value("validated", false)
				&& Parse(data.at("weights"), published)
				&& (!data.contains("preferences") || ParsePreferences(data.at("preferences"), publishedPreferences))
				&& (!data.contains("productionCalibration") || ParseCalibration(data.at("productionCalibration"), publishedModel));
		} catch (const nlohmann::json::exception&) { return false; }
	}();
	return valid ? &published : nullptr;
}
bool SetExperiment(const nlohmann::json& weights, bool allUnits, const nlohmann::json* preferences, const nlohmann::json* calibration) {
	ColdStorageSearch::Weights candidate{};
	std::map<ZombieType, ColdStorageSearch::ContextWeights> parsed;
	ColdStorageSearch::ProductionCalibration model;
	try {
		if (!weights.is_null() && !Parse(weights, candidate)) return false;
		if (preferences && !ParsePreferences(*preferences, parsed)) return false;
		if (calibration && !calibration->is_null() && !ParseCalibration(*calibration,model)) return false;
	} catch (const nlohmann::json::exception&) { return false; }
	experiment = true; enabled = !weights.is_null(); parameters = candidate;
	allTypes = allUnits; unitPreferences = std::move(parsed); experimentalModel = std::move(model);
	return true;
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
void ResetExperiment() { experiment = enabled = allTypes = false; unitPreferences.clear(); experimentalModel.nodes.clear(); }
}
