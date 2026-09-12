#include "SaveSchema.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieType.h"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace {
	enum class DocumentKind {
		Player,
		Level,
	};

	bool ReadSchemaVersion(const nlohmann::json& document, int currentVersion,
		const char* documentName, int& version, std::string& error)
	{
		if (!document.is_object()) {
			error = std::string(documentName) + "存档根节点必须是对象";
			return false;
		}
		if (!document.contains("schemaVersion")) {
			version = 0;
			return true;
		}

		const auto& value = document["schemaVersion"];
		if (value.is_number_unsigned()) {
			const std::uint64_t raw = value.get<std::uint64_t>();
			if (raw > static_cast<std::uint64_t>(currentVersion)) {
				error = std::string(documentName) + "存档来自更高版本";
				return false;
			}
			version = static_cast<int>(raw);
			return true;
		}
		if (!value.is_number_integer()) {
			error = std::string(documentName) + "存档 schemaVersion 必须是整数";
			return false;
		}

		const std::int64_t raw = value.get<std::int64_t>();
		if (raw < 0) {
			error = std::string(documentName) + "存档 schemaVersion 不能为负数";
			return false;
		}
		if (raw > currentVersion) {
			error = std::string(documentName) + "存档来自更高版本";
			return false;
		}
		version = static_cast<int>(raw);
		return true;
	}

	/** 为已经通关 3-8 的旧玩家补发前移后的毒囊射手，且不重复已有卡片。 */
	void MigrateMovedToxicPeaReward(nlohmann::json& document)
	{
		constexpr int kFirstLevelAfterReward = 27; // 3-8 通关后推进到的内部冒险关卡
		if (!document.contains("adventureLevel") ||
			!document["adventureLevel"].is_number_integer() ||
			document["adventureLevel"].get<int>() < kFirstLevelAfterReward ||
			!document.contains("havecards") || !document["havecards"].is_array()) {
			return;
		}

		auto& cards = document["havecards"];
		const int reward = static_cast<int>(PlantType::PLANT_TOXICPEASHOOTER);
		const bool alreadyOwned = std::any_of(cards.begin(), cards.end(),
			[](const nlohmann::json& card) {
				return card.is_number_integer() && card.get<int>() ==
					static_cast<int>(PlantType::PLANT_TOXICPEASHOOTER);
			});
		if (!alreadyOwned) cards.push_back(reward);
	}

	constexpr int kLegacyCommittedPhase = 3; // v11 祭司与钟匠的一次性 COMMITTED 阶段值
	constexpr int kRepeatableCooldownPhase = 5; // v12 新增且不改写旧阶段编号的循环冷却值
	constexpr float kAuroraPriestCooldownSeconds = 5.0f; // 祭司旧提交态迁移后的完整循环冷却
	constexpr float kPolarClockmakerCooldownSeconds = 10.0f; // 钟匠旧提交态迁移后的完整循环冷却

	/** 将 v11 一次性提交态升级为可重复技能的完整冷却，并同步活动时间锚中的能力快照。 */
	void MigrateRepeatableFinaleCooldowns(nlohmann::json& document)
	{
		auto getCooldown = [](int zombieType) {
			if (zombieType == static_cast<int>(ZombieType::ZOMBIE_AURORA_PRIEST)) {
				return kAuroraPriestCooldownSeconds;
			}
			if (zombieType == static_cast<int>(ZombieType::ZOMBIE_POLAR_CLOCKMAKER)) {
				return kPolarClockmakerCooldownSeconds;
			}
			return 0.0f;
		};

		if (document.contains("zombies") && document["zombies"].is_array()) {
			for (auto& zombie : document["zombies"]) {
				if (!zombie.is_object() || !zombie.contains("type")
					|| !zombie["type"].is_number_integer()
					|| !zombie.contains("extraData") || !zombie["extraData"].is_object()) {
					continue;
				}
				const int type = zombie["type"].get<int>();
				const float cooldown = getCooldown(type);
				if (cooldown <= 0.0f) continue;
				auto& extraData = zombie["extraData"];
				const char* phaseKey = type == static_cast<int>(
					ZombieType::ZOMBIE_AURORA_PRIEST) ? "ritualPhase" : "clockPhase";
				const char* remainingKey = type == static_cast<int>(
					ZombieType::ZOMBIE_AURORA_PRIEST) ? "ritualRemaining" : "clockRemaining";
				if (extraData.contains(phaseKey) && extraData[phaseKey].is_number_integer()
					&& extraData[phaseKey].get<int>() == kLegacyCommittedPhase) {
					extraData[phaseKey] = kRepeatableCooldownPhase;
					extraData[remainingKey] = cooldown;
				}
			}
		}

		if (!document.contains("temporalAnchors")
			|| !document["temporalAnchors"].is_array()) return;
		for (auto& anchor : document["temporalAnchors"]) {
			if (!anchor.is_object() || !anchor.contains("targets")
				|| !anchor["targets"].is_array()) continue;
			for (auto& target : anchor["targets"]) {
				const auto abilityValid = target.is_object()
					? target.find("abilityStateValid") : target.end();
				if (!target.is_object() || abilityValid == target.end()
					|| !abilityValid->is_boolean() || !abilityValid->get<bool>()
					|| !target.contains("type") || !target["type"].is_number_integer()
					|| !target.contains("abilityPhase")
					|| !target["abilityPhase"].is_number_integer()
					|| target["abilityPhase"].get<int>() != kLegacyCommittedPhase) {
					continue;
				}
				const float cooldown = getCooldown(target["type"].get<int>());
				if (cooldown <= 0.0f) continue;
				target["abilityPhase"] = kRepeatableCooldownPhase;
				target["abilityRemaining"] = cooldown;
			}
		}
	}

	/** 在副本上按文档类型执行迁移，全部成功后才提交，避免失败留下半迁移文档。 */
	bool UpgradeDocument(nlohmann::json& document, int currentVersion,
		const char* documentName, DocumentKind kind, std::string& error)
	{
		error.clear();
		int version = 0;
		if (!ReadSchemaVersion(document, currentVersion,
			documentName, version, error)) {
			return false;
		}

		nlohmann::json upgraded = document;
		while (version < currentVersion) {
			switch (version) {
			case 0:
				// v1 只建立显式版本入口；历史字段与缺省兼容规则保持不变。
				version = 1;
				upgraded["schemaVersion"] = version;
				break;
			case 1:
				if (kind == DocumentKind::Player) {
					// 玩家 v2 对已通关 3-8 的旧档补发前移后的植物奖励。
					MigrateMovedToxicPeaReward(upgraded);
				}
				// 关卡 v2 加入四大关雾势字段；旧档由关卡上下文重建，迁移层不伪造状态。
				version = 2;
				upgraded["schemaVersion"] = version;
				break;
			case 2: {
				if (kind == DocumentKind::Player) {
					// 玩家 v3 新增高级暂停设置；旧玩家默认进入更严格的普通暂停。
					if (!upgraded.contains("advancedPauseEnabled")) {
						upgraded["advancedPauseEnabled"] = false;
					}
				}
				else {
					// 关卡 v3 把旧 CLEAR/DENSE 二态扩成 DEFAULT/SMALL/NORMAL/DENSE，保留旧档视觉强度。
					constexpr const char* kFogIntensityKeys[] = {
						"fogWeatherIntensity",
						"forecastFogWeatherIntensity",
						"actualForecastFogWeatherIntensity",
					};
					for (const char* key : kFogIntensityKeys) {
						if (!upgraded.contains(key) || !upgraded[key].is_number_integer()) continue;
						const int oldValue = upgraded[key].get<int>();
						if (oldValue == 0) upgraded[key] = 1;      // 旧 CLEAR 是双层并扩 1 格，对应小雾
						else if (oldValue == 1) upgraded[key] = 3; // 旧 DENSE 仍是大雾
					}
				}
				version = 3;
				upgraded["schemaVersion"] = version;
				break;
			}
			case 3:
				if (kind == DocumentKind::Player) {
					if (!upgraded.contains("lastSelectedCards")) {
						// 玩家 v4 新增上次已提交的选卡；旧档从空记录开始，不猜测历史关卡卡组。
						upgraded["lastSelectedCards"] = nlohmann::json::array();
					}
				}
				else {
					// 关卡 v4 新增冰裂钻机每波预算和独立地裂；旧档没有已提交威胁，从单位元恢复。
					if (!upgraded.contains("iceCrackDrillsSpawnedThisWave")) {
						upgraded["iceCrackDrillsSpawnedThisWave"] = 0;
					}
					if (!upgraded.contains("groundRifts")) {
						upgraded["groundRifts"] = nlohmann::json::array();
					}
				}
				version = 4;
				upgraded["schemaVersion"] = version;
				break;
			case 4:
				if (kind == DocumentKind::Player) {
					if (!upgraded.contains("crazyDaveTutorialsSeen")) {
						// 玩家 v5 新增按冒险关卡号保存的戴夫闲聊已读集合；旧档从未读开始。
						upgraded["crazyDaveTutorialsSeen"] = nlohmann::json::array();
					}
				}
				else {
					// 关卡 v5 保存整栏天气干扰结果与气象干扰僵尸每波名额；旧档均从单位元恢复。
					if (!upgraded.contains("weatherForecastDisrupted")) {
						upgraded["weatherForecastDisrupted"] = false;
					}
					if (!upgraded.contains("fogWeatherForecastDisrupted")) {
						upgraded["fogWeatherForecastDisrupted"] = false;
					}
					if (!upgraded.contains("weatherJammersSpawnedThisWave")) {
						upgraded["weatherJammersSpawnedThisWave"] = 0;
					}
				}
				version = 5;
				upgraded["schemaVersion"] = version;
				break;
			case 5:
				if (kind == DocumentKind::Level
					&& !upgraded.contains("redeyeGargantuarsSpawnedThisWave")) {
					// 关卡 v6 增加冒险红眼每波上限计数；旧档从尚未消费的单位元恢复。
					upgraded["redeyeGargantuarsSpawnedThisWave"] = 0;
				}
				version = 6;
				upgraded["schemaVersion"] = version;
				break;
			case 6:
				if (kind == DocumentKind::Level
					&& !upgraded.contains("openingColdWavePlanInitialized")) {
					// 关卡 v7 新增 7-8/7-9 开幕寒潮标志；旧档默认仍有一次开幕事件可消费。
					upgraded["openingColdWavePlanInitialized"] = false;
				}
				version = 7;
				upgraded["schemaVersion"] = version;
				break;
			case 7:
				if (kind == DocumentKind::Level) {
					// 关卡 v8 新增极夜环境计划；旧档只标记未初始化，由目标背景确定性建立首轮。
					if (!upgraded.contains("polarNightInitialized")) {
						upgraded["polarNightInitialized"] = false;
					}
					if (!upgraded.contains("snowHoles")) {
						upgraded["snowHoles"] = nlohmann::json::array();
					}
					if (!upgraded.contains("pendingSnowHoleSpawns")) {
						upgraded["pendingSnowHoleSpawns"] = nlohmann::json::array();
					}
				}
				version = 8;
				upgraded["schemaVersion"] = version;
				break;
			case 8:
				if (kind == DocumentKind::Level) {
					// 关卡 v9 保存适应头盔出怪预算与在途弹丸来源；旧档均从未提交单位元恢复。
					if (!upgraded.contains("adaptiveHelmetsSpawnedThisWave")) {
						upgraded["adaptiveHelmetsSpawnedThisWave"] = 0;
					}
					if (!upgraded.contains("adaptiveHelmetTutorialWaveSpawned")) {
						upgraded["adaptiveHelmetTutorialWaveSpawned"] = false;
					}
					if (upgraded.contains("bullets") && upgraded["bullets"].is_array()) {
						for (auto& bullet : upgraded["bullets"]) {
							if (!bullet.is_object()) continue;
							if (!bullet.contains("plantOriginKind")) {
								bullet["plantOriginKind"] = 0;
							}
							if (!bullet.contains("plantOriginLineage")) {
								bullet["plantOriginLineage"] =
									static_cast<int>(PlantType::NUM_PLANT_TYPES);
							}
						}
					}
				}
				version = 9;
				upgraded["schemaVersion"] = version;
				break;
			case 9:
				if (kind == DocumentKind::Level) {
					// 关卡 v10 新增 8-7/8-8 独立裂隙、时间锚与曙光导航；旧档从单位元恢复。
					if (!upgraded.contains("pendingAuroraRifts"))
						upgraded["pendingAuroraRifts"] = nlohmann::json::array();
					if (!upgraded.contains("temporalAnchors"))
						upgraded["temporalAnchors"] = nlohmann::json::array();
					if (!upgraded.contains("dawnNavigationTimer"))
						upgraded["dawnNavigationTimer"] = 0.0f;
				}
				version = 10;
				upgraded["schemaVersion"] = version;
				break;
			case 10:
				if (kind == DocumentKind::Level
					&& upgraded.contains("temporalAnchors")
					&& upgraded["temporalAnchors"].is_array()) {
					// 关卡 v11 为时间锚加入品种能力快照；旧锚保留只认 submitted 的原语义。
					for (auto& anchor : upgraded["temporalAnchors"]) {
						if (!anchor.is_object() || !anchor.contains("targets")
							|| !anchor["targets"].is_array()) continue;
						for (auto& target : anchor["targets"]) {
							if (!target.is_object()) continue;
							if (!target.contains("abilityStateValid"))
								target["abilityStateValid"] = false;
							if (!target.contains("abilityPhase")) target["abilityPhase"] = -1;
							if (!target.contains("abilityRemaining"))
								target["abilityRemaining"] = 0.0f;
						}
					}
				}
				version = 11;
				upgraded["schemaVersion"] = version;
				break;
			case 11:
				if (kind == DocumentKind::Level) {
					// 关卡 v12 把压轴僵尸的永久提交态升级为从提交边沿开始的循环冷却。
					MigrateRepeatableFinaleCooldowns(upgraded);
				}
				version = 12;
				upgraded["schemaVersion"] = version;
				break;
			case 12:
				// v13新增矿道存档；旧关的玩法字段保持原样，缺省行进格由 Zombie 加载时补 -1。
				version = 13;
				upgraded["schemaVersion"] = version;
				break;
			case 13:
				// v14 按来源保存鼓舞，旧档没有已提交鼓舞或鼓手投放。
				if (kind == DocumentKind::Level) {
					if (!upgraded.contains("crystalDrummersSpawnedThisWave")) upgraded["crystalDrummersSpawnedThisWave"] = 0;
					if (upgraded.contains("zombies") && upgraded["zombies"].is_array())
						for (auto& zombie : upgraded["zombies"])
							if (zombie.is_object() && !zombie.contains("drumInspiration")) zombie["drumInspiration"] = nlohmann::json::array();
				}
				version = 14;
				upgraded["schemaVersion"] = version;
				break;
			default:
				error = std::string(documentName) + "存档缺少迁移路径";
				return false;
			}
		}
		document = std::move(upgraded);
		return true;
	}
}

bool SaveSchema::UpgradePlayerDocument(
	nlohmann::json& document, std::string& error)
{
	return UpgradeDocument(document, kCurrentPlayerVersion,
		"玩家", DocumentKind::Player, error);
}

bool SaveSchema::UpgradeLevelDocument(
	nlohmann::json& document, std::string& error)
{
	return UpgradeDocument(document, kCurrentLevelVersion,
		"关卡", DocumentKind::Level, error);
}
