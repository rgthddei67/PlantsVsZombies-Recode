#pragma once

/** 刺莲的承载与近身群攻参数；费用和卡牌冷却维护在 gamedata.json。 */
namespace ThornLilyRules {
    inline constexpr int Health = 1500; // 基础生命值，不与上层植物共享
    inline constexpr int Damage = 60; // 每轮对每个地面目标的伤害
    inline constexpr float Interval = 1.5f; // 两轮根刺间隔，基础行动秒
    inline constexpr int RowRadius = 1; // 覆盖本行和上下相邻行
    inline constexpr float HalfWidthCells = 1.5f; // 三列区域相对本格中心的半宽，格
}
