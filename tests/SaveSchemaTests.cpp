#include "SaveSchema.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieType.h"

#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {
	int gFailureCount = 0;

	void Expect(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++gFailureCount;
		}
	}

	void TestLegacyPlayerUpgradePreservesFields() {
		nlohmann::json document = {
			{ "difficulty", 3 },
			{ "havecards", { 1, 2, 5 } },
			{ "nested", { { "kept", true } } }
		};
		const auto legacy = document;
		std::string error;

		Expect(SaveSchema::UpgradePlayerDocument(document, error),
			"无版本玩家旧档应升级成功");
		Expect(error.empty(), "成功迁移不应返回错误");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentPlayerVersion,
			"玩家旧档应写入当前版本");
		for (const auto& [key, value] : legacy.items()) {
			Expect(document[key] == value, "玩家旧字段必须原样保留");
		}
	}

	void TestMovedToxicRewardPlayerUpgrade() {
		const int toxicPea = static_cast<int>(PlantType::PLANT_TOXICPEASHOOTER);
		auto countToxicPea = [toxicPea](const nlohmann::json& document) {
			return static_cast<int>(std::count(
				document["havecards"].begin(), document["havecards"].end(), toxicPea));
		};
		std::string error;

		nlohmann::json beforeCompletion = {
			{ "schemaVersion", 1 },
			{ "adventureLevel", 26 },
			{ "havecards", { static_cast<int>(PlantType::PLANT_PEASHOOTER) } }
		};
		Expect(SaveSchema::UpgradePlayerDocument(beforeCompletion, error),
			"尚未通关3-8的玩家档应升级成功");
		Expect(countToxicPea(beforeCompletion) == 0,
			"尚未通关3-8时不得提前补发毒囊射手");

		nlohmann::json afterCompletion = {
			{ "schemaVersion", 1 },
			{ "adventureLevel", 27 },
			{ "havecards", { static_cast<int>(PlantType::PLANT_PEASHOOTER) } }
		};
		Expect(SaveSchema::UpgradePlayerDocument(afterCompletion, error),
			"已通关3-8的玩家档应升级成功");
		Expect(afterCompletion["schemaVersion"] == SaveSchema::kCurrentPlayerVersion,
			"奖励迁移后应写入当前玩家版本");
		Expect(countToxicPea(afterCompletion) == 1,
			"已通关3-8的旧档应补发一次毒囊射手");

		nlohmann::json alreadyOwned = {
			{ "schemaVersion", 1 },
			{ "adventureLevel", 36 },
			{ "havecards", {
				static_cast<int>(PlantType::PLANT_PEASHOOTER), toxicPea } }
		};
		Expect(SaveSchema::UpgradePlayerDocument(alreadyOwned, error),
			"已拥有毒囊射手的玩家档应升级成功");
		Expect(countToxicPea(alreadyOwned) == 1,
			"玩家档迁移不得重复已有毒囊射手");
	}

	void TestCurrentPlayerDocumentIsStable() {
		nlohmann::json document = {
			{ "schemaVersion", SaveSchema::kCurrentPlayerVersion },
			{ "adventureLevel", 27 },
			{ "havecards", { static_cast<int>(PlantType::PLANT_PEASHOOTER) } }
		};
		const auto before = document;
		std::string error;

		Expect(SaveSchema::UpgradePlayerDocument(document, error),
			"当前版本玩家档应通过");
		Expect(document == before, "当前版本玩家档不应重复执行历史迁移");
	}

	void TestVersionTwoPlayerUpgradeDefaultsToStrictPause() {
		nlohmann::json document = {
			{ "schemaVersion", 2 },
			{ "difficulty", 2 },
			{ "enableMonteCarloAI", false }
		};
		std::string error;

		Expect(SaveSchema::UpgradePlayerDocument(document, error),
			"v2 玩家档应升级到高级暂停设置版本");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentPlayerVersion,
			"v2 玩家档应写入当前玩家版本");
		Expect(document["advancedPauseEnabled"] == false,
			"旧玩家默认应关闭高级暂停");
		Expect(document["enableMonteCarloAI"] == false,
			"高级暂停迁移不得改写已有玩家设置");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 2 },
			{ "advancedPauseEnabled", true }
		};
		Expect(SaveSchema::UpgradePlayerDocument(prereleaseDocument, error),
			"已含高级暂停字段的 v2 玩家档应升级成功");
		Expect(prereleaseDocument["advancedPauseEnabled"] == true,
			"迁移不得覆盖玩家已有的高级暂停选择");
	}

	void TestVersionThreePlayerUpgradeAddsLastSelectedCards() {
		nlohmann::json document = {
			{ "schemaVersion", 3 },
			{ "advancedPauseEnabled", true },
			{ "havecards", { static_cast<int>(PlantType::PLANT_PEASHOOTER) } }
		};
		std::string error;

		Expect(SaveSchema::UpgradePlayerDocument(document, error),
			"v3 玩家档应升级到上次选卡版本");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentPlayerVersion,
			"v3 玩家档应写入当前玩家版本");
		Expect(document["lastSelectedCards"].is_array()
			&& document["lastSelectedCards"].empty(),
			"旧玩家没有历史选卡时应补为空数组");
		Expect(document["advancedPauseEnabled"] == true,
			"上次选卡迁移不得改写既有设置");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 3 },
			{ "lastSelectedCards", { "PLANT_SUNFLOWER", "PLANT_PEASHOOTER" } }
		};
		Expect(SaveSchema::UpgradePlayerDocument(prereleaseDocument, error),
			"已含上次选卡字段的 v3 玩家档应升级成功");
		Expect(prereleaseDocument["lastSelectedCards"] == nlohmann::json::array({
			"PLANT_SUNFLOWER", "PLANT_PEASHOOTER" }),
			"迁移不得覆盖预发布玩家已有的选卡记录");
	}

	void TestVersionFourPlayerUpgradeAddsCrazyDaveTutorialsSeen() {
		nlohmann::json document = {
			{ "schemaVersion", 4 },
			{ "lastSelectedCards", { "PLANT_SUNFLOWER" } }
		};
		std::string error;

		Expect(SaveSchema::UpgradePlayerDocument(document, error),
			"v4 玩家档应升级到戴夫闲聊已读版本");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentPlayerVersion,
			"v4 玩家档应写入当前玩家版本");
		Expect(document["crazyDaveTutorialsSeen"].is_array()
			&& document["crazyDaveTutorialsSeen"].empty(),
			"旧玩家没有戴夫闲聊记录时应补为空数组");
		Expect(document["lastSelectedCards"] == nlohmann::json::array({
			"PLANT_SUNFLOWER" }),
			"戴夫闲聊迁移不得改写既有选卡记录");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 4 },
			{ "crazyDaveTutorialsSeen", { 10, 29, 55 } }
		};
		Expect(SaveSchema::UpgradePlayerDocument(prereleaseDocument, error),
			"已含戴夫闲聊记录的 v4 玩家档应升级成功");
		Expect(prereleaseDocument["crazyDaveTutorialsSeen"]
			== nlohmann::json::array({ 10, 29, 55 }),
			"迁移不得覆盖预发布玩家已有的戴夫闲聊记录");
	}

	void TestCurrentLevelDocumentIsStable() {
		nlohmann::json document = {
			{ "schemaVersion", SaveSchema::kCurrentLevelVersion },
			{ "currentWave", 8 },
			{ "weatherTimer", 12.5f }
		};
		const auto before = document;
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"当前版本关卡档应通过");
		Expect(document == before, "当前版本关卡档不应被改写");
	}

	void TestLegacyLevelUpgradePreservesGameplayState() {
		nlohmann::json document = {
			{ "boardState", 1 },
			{ "currentWave", 20 },
			{ "weatherForecastReady", true },
			{ "zombies", nlohmann::json::array({ { { "id", 7 } } }) }
		};
		const auto legacy = document;
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"无版本关卡旧档应升级成功");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"关卡旧档应写入当前版本");
		for (const auto& [key, value] : legacy.items()) {
			auto migrated=document[key];
			if (key=="zombies") for (auto& zombie : migrated) zombie.erase("drumInspiration");
			Expect(migrated == value, "旧玩法字段必须保留；允许新增中性的鼓舞字段");
		}
	}

	void TestVersionOneLevelUpgradeDefersFogInitializationToBoard() {
		nlohmann::json document = {
			{ "schemaVersion", 1 },
			{ "boardState", 1 },
			{ "weatherTimer", 12.5f }
		};
		const auto legacy = document;
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v1 关卡档应升级到雾势结构版本");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v1 关卡档应写入当前版本");
		Expect(!document.contains("fogWeatherInitialized"),
			"迁移层不能在缺少关卡上下文时伪造雾势初始化状态");
		for (const auto& [key, value] : legacy.items()) {
			if (key == "schemaVersion") continue;
			Expect(document[key] == value, "v1 关卡玩法字段必须原样保留");
		}
	}

	void TestVersionTwoLevelUpgradePreservesFogStrength() {
		nlohmann::json document = {
			{ "schemaVersion", 2 },
			{ "fogWeatherInitialized", true },
			{ "fogWeatherIntensity", 0 },
			{ "forecastFogWeatherIntensity", 1 },
			{ "actualForecastFogWeatherIntensity", 0 },
			{ "fogWeatherForecastReady", true }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v2 关卡档应升级到四档雾势结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v2 关卡档应写入当前版本");
		Expect(document["fogWeatherIntensity"] == 1,
			"旧 CLEAR 的双层扩展表现应迁移为小雾");
		Expect(document["forecastFogWeatherIntensity"] == 3,
			"旧 DENSE 预报应继续表示大雾");
		Expect(document["actualForecastFogWeatherIntensity"] == 1,
			"旧 CLEAR 真实预报应迁移为小雾");
		Expect(document["fogWeatherForecastReady"] == true,
			"雾势枚举迁移不得改变预报锁定状态");
	}

	void TestVersionThreeLevelUpgradeAddsIceCrackDrillState() {
		nlohmann::json document = {
			{ "schemaVersion", 3 },
			{ "currentWave", 7 },
			{ "zombies", nlohmann::json::array() }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v3 关卡档应升级到冰裂钻机持久化结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v3 关卡档应写入当前版本");
		Expect(document["iceCrackDrillsSpawnedThisWave"] == 0,
			"旧档没有冰裂钻机波次预算时应补零");
		Expect(document["groundRifts"].is_array() && document["groundRifts"].empty(),
			"旧档没有已提交地裂时应补空数组");
		Expect(document["currentWave"] == 7 && document["zombies"].empty(),
			"冰裂钻机迁移不得改写既有关卡状态");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 3 },
			{ "iceCrackDrillsSpawnedThisWave", 1 },
			{ "groundRifts", nlohmann::json::array({ {
				{ "row", 2 }, { "frontX", 720.0f }, { "nextColumn", 5 },
				{ "downstreamDamageMultiplier", 0.5f }
			} }) }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含冰裂钻机字段的 v3 预发布档应升级成功");
		Expect(prereleaseDocument["iceCrackDrillsSpawnedThisWave"] == 1,
			"迁移不得覆盖预发布档的钻机波次预算");
		Expect(prereleaseDocument["groundRifts"].size() == 1
			&& prereleaseDocument["groundRifts"][0]["nextColumn"] == 5,
			"迁移不得覆盖预发布档的地裂传播前沿");
	}

	void TestVersionFourLevelUpgradeAddsWeatherJammerState() {
		nlohmann::json document = {
			{ "schemaVersion", 4 },
			{ "currentWave", 3 },
			{ "weatherForecastReady", true },
			{ "fogWeatherForecastReady", true }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v4 关卡档应升级到气象干扰持久化结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v4 关卡档应写入当前版本");
		Expect(document["weatherForecastDisrupted"] == false,
			"旧档雨雪预报默认未受干扰");
		Expect(document["fogWeatherForecastDisrupted"] == false,
			"旧档雾势预报默认未受干扰");
		Expect(document["weatherJammersSpawnedThisWave"] == 0,
			"旧档没有气象干扰僵尸波次名额时应补零");
		Expect(document["currentWave"] == 3
			&& document["weatherForecastReady"] == true
			&& document["fogWeatherForecastReady"] == true,
			"气象干扰迁移不得改写既有预报与波次状态");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 4 },
			{ "weatherForecastDisrupted", true },
			{ "fogWeatherForecastDisrupted", true },
			{ "weatherJammersSpawnedThisWave", 1 }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含气象干扰字段的 v4 预发布档应升级成功");
		Expect(prereleaseDocument["weatherForecastDisrupted"] == true
			&& prereleaseDocument["fogWeatherForecastDisrupted"] == true
			&& prereleaseDocument["weatherJammersSpawnedThisWave"] == 1,
			"迁移不得覆盖预发布档已经提交的干扰与波次名额");
	}

	void TestVersionFiveLevelUpgradeAddsRedeyeWaveCapState() {
		nlohmann::json document = {
			{ "schemaVersion", 5 },
			{ "currentWave", 20 },
			{ "zombies", nlohmann::json::array() }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v5 关卡档应升级到红眼波次上限结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v5 关卡档应写入当前版本");
		Expect(document["redeyeGargantuarsSpawnedThisWave"] == 0,
			"旧档没有红眼波次计数时应补零");
		Expect(document["currentWave"] == 20 && document["zombies"].empty(),
			"红眼波次计数迁移不得改写既有关卡状态");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 5 },
			{ "redeyeGargantuarsSpawnedThisWave", 3 }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含红眼波次计数的 v5 预发布档应升级成功");
		Expect(prereleaseDocument["redeyeGargantuarsSpawnedThisWave"] == 3,
			"迁移不得覆盖预发布档已经消费的红眼名额");
	}

	void TestVersionSixLevelUpgradeDefaultsOpeningColdWaveUnconsumed() {
		nlohmann::json document = {
			{ "schemaVersion", 6 },
			{ "currentWave", 20 },
			{ "zombies", nlohmann::json::array() }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v6 关卡档应升级到开幕寒潮标志结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v6 关卡档应写入当前版本");
		Expect(document["openingColdWavePlanInitialized"] == false,
			"旧关卡档应保留一次尚未消费的 7-8/7-9 开幕寒潮");
		Expect(document["currentWave"] == 20 && document["zombies"].empty(),
			"开幕寒潮迁移不得改写既有关卡状态");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 6 },
			{ "openingColdWavePlanInitialized", true }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含开幕寒潮标志的 v6 预发布档应升级成功");
		Expect(prereleaseDocument["openingColdWavePlanInitialized"] == true,
			"迁移不得覆盖预发布档已经消费的开幕寒潮");
	}

	void TestVersionSevenLevelUpgradeAddsPolarNightState() {
		nlohmann::json document = {
			{ "schemaVersion", 7 },
			{ "currentWave", 9 },
			{ "zombies", nlohmann::json::array() }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v7 关卡档应升级到极夜环境持久化结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v7 关卡档应写入当前版本");
		Expect(document["polarNightInitialized"] == false,
			"旧关卡档的极夜导演应由目标背景首次初始化");
		Expect(document["snowHoles"].is_array() && document["snowHoles"].empty(),
			"旧关卡档没有雪穴时应补空数组");
		Expect(document["pendingSnowHoleSpawns"].is_array()
			&& document["pendingSnowHoleSpawns"].empty(),
			"旧关卡档没有雪穴出生事务时应补空数组");
		Expect(document["currentWave"] == 9 && document["zombies"].empty(),
			"极夜迁移不得改写既有波次和僵尸状态");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 7 },
			{ "polarNightInitialized", true },
			{ "snowHoles", nlohmann::json::array({ {
				{ "column", 5 }, { "phase", 2 }, { "timer", 0.0f }
			} }) },
			{ "pendingSnowHoleSpawns", nlohmann::json::array({ {
				{ "type", 0 }, { "row", 0 }, { "holeColumn", 5 },
				{ "spawnWave", 9 }, { "timer", 0.5f }
			} }) }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含极夜字段的 v7 预发布档应升级成功");
		Expect(prereleaseDocument["polarNightInitialized"] == true
			&& prereleaseDocument["snowHoles"].size() == 1
			&& prereleaseDocument["pendingSnowHoleSpawns"].size() == 1,
			"迁移不得覆盖预发布档已经提交的极夜事务");
	}

	void TestVersionEightLevelUpgradeAddsAdaptiveDamageOriginState() {
		nlohmann::json document = {
			{ "schemaVersion", 8 },
			{ "currentWave", 9 },
			{ "bullets", nlohmann::json::array({ {
				{ "type", 0 }, { "damage", 20 }
			} }) }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v8 关卡档应升级到适应头盔与弹丸来源结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v8 关卡档应写入当前版本");
		Expect(document["adaptiveHelmetsSpawnedThisWave"] == 0
			&& document["adaptiveHelmetTutorialWaveSpawned"] == false,
			"旧关卡档的适应头盔预算应从未消费单位元恢复");
		Expect(document["bullets"][0]["plantOriginKind"] == 0
			&& document["bullets"][0]["plantOriginLineage"]
				== static_cast<int>(PlantType::NUM_PLANT_TYPES),
			"旧在途弹丸应迁移为无植物来源");

		nlohmann::json prereleaseDocument = {
			{ "schemaVersion", 8 },
			{ "adaptiveHelmetsSpawnedThisWave", 2 },
			{ "adaptiveHelmetTutorialWaveSpawned", true },
			{ "bullets", nlohmann::json::array({ {
				{ "plantOriginKind", 1 },
				{ "plantOriginLineage", static_cast<int>(PlantType::PLANT_REPEATER) }
			} }) }
		};
		Expect(SaveSchema::UpgradeLevelDocument(prereleaseDocument, error),
			"已含适应字段的 v8 预发布档应升级成功");
		Expect(prereleaseDocument["adaptiveHelmetsSpawnedThisWave"] == 2
			&& prereleaseDocument["adaptiveHelmetTutorialWaveSpawned"] == true
			&& prereleaseDocument["bullets"][0]["plantOriginKind"] == 1
			&& prereleaseDocument["bullets"][0]["plantOriginLineage"]
				== static_cast<int>(PlantType::PLANT_REPEATER),
			"迁移不得覆盖预发布档已经提交的预算和弹丸来源");
	}

	void TestVersionTenLevelUpgradePreservesLegacyTemporalSemantics() {
		nlohmann::json document = {
			{ "schemaVersion", 10 },
			{ "temporalAnchors", nlohmann::json::array({ {
				{ "ownerZombieID", 17 },
				{ "targets", nlohmann::json::array({ {
					{ "zombieID", 23 }, { "specialActionSubmitted", true }
				} }) }
			} }) }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v10 关卡档应升级到时间锚能力快照结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v10 关卡档应写入当前版本");
		const auto& target = document["temporalAnchors"][0]["targets"][0];
		Expect(target["specialActionSubmitted"] == true,
			"旧锚已经记录的提交位不得被迁移覆盖");
		Expect(target["abilityStateValid"] == false
			&& target["abilityPhase"] == -1
			&& target["abilityRemaining"] == 0.0f,
			"旧锚不得凭空获得可返还的能力状态");
	}

	void TestVersionElevenLevelUpgradeStartsRepeatableFinaleCooldowns() {
		const int priestType = static_cast<int>(ZombieType::ZOMBIE_AURORA_PRIEST);
		const int clockType = static_cast<int>(ZombieType::ZOMBIE_POLAR_CLOCKMAKER);
		nlohmann::json document = {
			{ "schemaVersion", 11 },
			{ "zombies", nlohmann::json::array({
				{
					{ "type", priestType },
					{ "extraData", {
						{ "ritualPhase", 3 }, { "ritualRemaining", 0.0f }
					} }
				},
				{
					{ "type", clockType },
					{ "extraData", {
						{ "clockPhase", 3 }, { "clockRemaining", 0.0f }
					} }
				},
				{
					{ "type", priestType },
					{ "extraData", {
						{ "ritualPhase", 0 }, { "ritualRemaining", 2.5f }
					} }
				}
			}) },
			{ "temporalAnchors", nlohmann::json::array({ {
				{ "targets", nlohmann::json::array({
					{
						{ "type", priestType }, { "abilityStateValid", true },
						{ "abilityPhase", 3 }, { "abilityRemaining", 0.0f }
					},
					{
						{ "type", clockType }, { "abilityStateValid", true },
						{ "abilityPhase", 3 }, { "abilityRemaining", 0.0f }
					},
					{
						{ "type", clockType }, { "abilityStateValid", false },
						{ "abilityPhase", 3 }, { "abilityRemaining", 0.0f }
					}
				}) }
			} }) }
		};
		std::string error;

		Expect(SaveSchema::UpgradeLevelDocument(document, error),
			"v11 关卡档应升级到压轴僵尸循环冷却结构");
		Expect(document["schemaVersion"] == SaveSchema::kCurrentLevelVersion,
			"v11 关卡档应写入当前版本");
		Expect(document["zombies"][0]["extraData"]["ritualPhase"] == 5
			&& document["zombies"][0]["extraData"]["ritualRemaining"] == 5.0f,
			"旧祭司永久提交态应迁移为完整五秒循环冷却");
		Expect(document["zombies"][1]["extraData"]["clockPhase"] == 5
			&& document["zombies"][1]["extraData"]["clockRemaining"] == 10.0f,
			"旧钟匠永久提交态应迁移为完整十秒循环冷却");
		Expect(document["zombies"][2]["extraData"]["ritualPhase"] == 0
			&& document["zombies"][2]["extraData"]["ritualRemaining"] == 2.5f,
			"迁移不得改写尚未提交的祭司阶段与剩余时间");
		const auto& targets = document["temporalAnchors"][0]["targets"];
		Expect(targets[0]["abilityPhase"] == 5
			&& targets[0]["abilityRemaining"] == 5.0f,
			"祭司有效能力快照的旧提交态应迁移为完整冷却");
		Expect(targets[1]["abilityPhase"] == 5
			&& targets[1]["abilityRemaining"] == 10.0f,
			"钟匠有效能力快照的旧提交态应迁移为完整冷却");
		Expect(targets[2]["abilityPhase"] == 3
			&& targets[2]["abilityRemaining"] == 0.0f,
			"无效的旧能力快照必须保留仅认 submitted 的兼容语义");
	}

	void TestFutureVersionIsRejectedTransactionally() {
		nlohmann::json document = {
			{ "schemaVersion", SaveSchema::kCurrentLevelVersion + 1 },
			{ "currentWave", 99 }
		};
		const auto before = document;
		std::string error;

		Expect(!SaveSchema::UpgradeLevelDocument(document, error),
			"未来版本关卡档必须拒绝");
		Expect(!error.empty(), "拒绝未来版本时应说明原因");
		Expect(document == before, "迁移失败不得修改原文档");
	}

	void TestInvalidRootAndVersionAreRejected() {
		nlohmann::json arrayDocument = nlohmann::json::array({ 1, 2 });
		const auto beforeArray = arrayDocument;
		std::string error;
		Expect(!SaveSchema::UpgradePlayerDocument(arrayDocument, error),
			"非对象根节点必须拒绝");
		Expect(arrayDocument == beforeArray, "根节点校验失败不得修改输入");

		nlohmann::json invalidVersion = {
			{ "schemaVersion", "one" },
			{ "difficulty", 1 }
		};
		const auto beforeVersion = invalidVersion;
		Expect(!SaveSchema::UpgradePlayerDocument(invalidVersion, error),
			"非整数版本必须拒绝");
		Expect(invalidVersion == beforeVersion, "版本校验失败不得修改输入");

		nlohmann::json negativeVersion = {
			{ "schemaVersion", -1 },
			{ "difficulty", 1 }
		};
		const auto beforeNegative = negativeVersion;
		Expect(!SaveSchema::UpgradePlayerDocument(negativeVersion, error),
			"负版本号必须拒绝");
		Expect(negativeVersion == beforeNegative, "负版本校验失败不得修改输入");
	}
}

int main() {
	{
		nlohmann::json previous={{"schemaVersion",14},{"mine",{{"rocks",{true,false,true}},{"wavePlan",{{52,1}}}}}};
		const auto mine = previous["mine"];
		std::string error;
		Expect(SaveSchema::UpgradeLevelDocument(previous,error),"v14 矿道应迁移布局版本");
		Expect(previous["mine"]["layoutRevision"]==0,"旧档保留原矿道");
		Expect(previous["mine"]["rocks"]==mine["rocks"] && previous["mine"]["wavePlan"]==mine["wavePlan"],"迁移不重画地形或重抽预报");
	}
	{
		nlohmann::json previous={{"schemaVersion",13},{"zombies",nlohmann::json::array({{{"bodyHealth",270}}})}};
		std::string error;
		Expect(SaveSchema::UpgradeLevelDocument(previous,error),"v13 应迁移鼓舞单位元");
		Expect(previous["crystalDrummersSpawnedThisWave"]==0,"旧档不凭空消耗鼓手名额");
		Expect(previous["zombies"][0]["drumInspiration"].empty(),"旧目标没有已提交鼓舞");
		Expect(previous["zombies"][0]["bodyHealth"]==270,"迁移保持旧目标生命");
	}
	TestLegacyPlayerUpgradePreservesFields();
	TestMovedToxicRewardPlayerUpgrade();
	TestCurrentPlayerDocumentIsStable();
	TestVersionTwoPlayerUpgradeDefaultsToStrictPause();
	TestVersionThreePlayerUpgradeAddsLastSelectedCards();
	TestVersionFourPlayerUpgradeAddsCrazyDaveTutorialsSeen();
	TestCurrentLevelDocumentIsStable();
	TestLegacyLevelUpgradePreservesGameplayState();
	TestVersionOneLevelUpgradeDefersFogInitializationToBoard();
	TestVersionTwoLevelUpgradePreservesFogStrength();
	TestVersionThreeLevelUpgradeAddsIceCrackDrillState();
	TestVersionFourLevelUpgradeAddsWeatherJammerState();
	TestVersionFiveLevelUpgradeAddsRedeyeWaveCapState();
	TestVersionSixLevelUpgradeDefaultsOpeningColdWaveUnconsumed();
	TestVersionSevenLevelUpgradeAddsPolarNightState();
	TestVersionEightLevelUpgradeAddsAdaptiveDamageOriginState();
	TestVersionTenLevelUpgradePreservesLegacyTemporalSemantics();
	TestVersionElevenLevelUpgradeStartsRepeatableFinaleCooldowns();
	TestFutureVersionIsRejectedTransactionally();
	TestInvalidRootAndVersionAreRejected();

	if (gFailureCount == 0) {
		std::cout << "SaveSchemaTests passed\n";
	}
	return gFailureCount == 0 ? 0 : 1;
}
