#pragma once

#include "Game/Plant/PlantType.h"

namespace PlantCardRechargeRules {
inline constexpr double OpeningSupportMultiplier = 2.2; // 战前支援卡槽恢复速度提高120%，实际冷却除以2.2

/** 合并卡片计时速度；实战类型为玉米炮时只排除战前支援，独立生存词条仍保留。
 * 调用者负责先解析模仿者目标并判定支援已选完/地图资格；本规则不改变选卡合法性、初始就绪或炮体装填。 */
constexpr double GetMultiplier(PlantType gameplayType, bool openingSupport, double survivalMultiplier)
{
    const double support = openingSupport && gameplayType != PlantType::PLANT_COBCANNON
        ? OpeningSupportMultiplier : 1.0;
    return survivalMultiplier * support;
}
}
