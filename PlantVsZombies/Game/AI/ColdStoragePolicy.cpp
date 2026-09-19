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
	if (level != 82 && level != 83) return nullptr;
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
				&& (!data.contains("preferences") || ParsePreferences(data.at("preferences"), publishedPreferences));
		} catch (const nlohmann::json::exception&) { return false; }
	}();
	return valid ? &published : nullptr;
}
bool SetExperiment(const nlohmann::json& weights, bool allUnits, const nlohmann::json* preferences) {
	ColdStorageSearch::Weights candidate{};
	if (!weights.is_null() && !Parse(weights, candidate)) return false;
	std::map<ZombieType, ColdStorageSearch::ContextWeights> parsed;
	if (preferences && !ParsePreferences(*preferences, parsed)) return false;
	experiment = true; enabled = !weights.is_null(); parameters = candidate;
	allTypes = allUnits; unitPreferences = std::move(parsed);
	return true;
}
bool AllUnits() { return experiment && allTypes; }
ColdStorageSearch::ContextWeights UnitPreference(ZombieType type) {
	const auto& values = experiment ? unitPreferences : publishedPreferences;
	const auto it = values.find(type);
	return (!experiment || enabled) && it != values.end() ? it->second : ColdStorageSearch::ContextWeights{};
}
void ResetExperiment() { experiment = enabled = allTypes = false; unitPreferences.clear(); }
}
