#pragma once

#include "PlantType.h"

enum class PlantUpgradeLayer {
	NONE,
	NORMAL,
	UNDER,
};

/** 返回升级植物要求的基础植物；非升级植物返回 NUM_PLANT_TYPES。 */
constexpr PlantType GetUpgradeBasePlantType(PlantType type)
{
	switch (type) {
	case PlantType::PLANT_TWINSUNFLOWER:
		return PlantType::PLANT_SUNFLOWER;
	case PlantType::PLANT_GLOOMSHROOM:
		return PlantType::PLANT_FUMESHROOM;
	case PlantType::PLANT_WINTERMELON:
		return PlantType::PLANT_MELONPULT;
	case PlantType::PLANT_COBCANNON:
		return PlantType::PLANT_KERNELPULT;
	case PlantType::PLANT_GOLD_MAGNET:
		return PlantType::PLANT_MAGNETSHROOM;
	case PlantType::PLANT_THORNLILY:
		return PlantType::PLANT_LILYPAD;
	case PlantType::PLANT_LIGHTNINGRODPOT:
		return PlantType::PLANT_FLOWERPOT;
	case PlantType::PLANT_GATLINGPEA:
		return PlantType::PLANT_REPEATER;
	case PlantType::PLANT_AURORATORCHWOOD:
		return PlantType::PLANT_TORCHWOOD;
	default:
		return PlantType::NUM_PLANT_TYPES;
	}
}

/** 返回紫卡原位替换的格子层；普通紫卡替换 normal，底座紫卡替换 under（洪水花盆转 foundation）。 */
constexpr PlantUpgradeLayer GetUpgradePlantLayer(PlantType type)
{
	switch (type) {
	case PlantType::PLANT_TWINSUNFLOWER:
	case PlantType::PLANT_GLOOMSHROOM:
	case PlantType::PLANT_WINTERMELON:
	case PlantType::PLANT_COBCANNON:
	case PlantType::PLANT_GOLD_MAGNET:
	case PlantType::PLANT_GATLINGPEA:
	case PlantType::PLANT_AURORATORCHWOOD:
		return PlantUpgradeLayer::NORMAL;
	case PlantType::PLANT_THORNLILY:
	case PlantType::PLANT_LIGHTNINGRODPOT:
		return PlantUpgradeLayer::UNDER;
	default:
		return PlantUpgradeLayer::NONE;
	}
}

/** 返回指定类型是否必须覆盖一株基础植物才能正式种植。 */
constexpr bool IsUpgradePlantType(PlantType type)
{
	return GetUpgradeBasePlantType(type) != PlantType::NUM_PLANT_TYPES;
}

/** 返回植物是否占用承载层；承载层紫卡必须与基础花盆共用该层。 */
constexpr bool IsUnderPlantLayerType(PlantType type)
{
	return type == PlantType::PLANT_LILYPAD
		|| type == PlantType::PLANT_FLOWERPOT
		|| GetUpgradePlantLayer(type) == PlantUpgradeLayer::UNDER;
}
