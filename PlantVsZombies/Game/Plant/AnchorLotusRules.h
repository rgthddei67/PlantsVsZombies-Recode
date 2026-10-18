#pragma once

/** 锚藕能力与目标束缚共用的参数；费用及卡牌冷却只在 gamedata.json 维护。 */
namespace AnchorLotusRules {
inline constexpr int Health = 4000; // 基础生命值
inline constexpr float Cooldown = 6.0f; // 两次成功缠绕之间的游戏秒数
inline constexpr float RangeCells = 3.0f; // 本行前方搜索距离，单位：格宽
inline constexpr float HoldSeconds = 3.0f; // 只限制移动的束缚时长，游戏秒
inline constexpr float ImmunitySeconds = 4.0f; // 挣脱后所有锚藕共享的免疫时长，游戏秒
}
