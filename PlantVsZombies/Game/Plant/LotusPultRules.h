#pragma once

/** 莲蓬投手的弹丸和能力参数；费用与卡牌冷却由 gamedata.json 维护。 */
namespace LotusPultRules {
    inline constexpr int Health = 300; // 植物基础生命值
    inline constexpr float Interval = 3.0f; // 基础攻击周期，游戏秒
    inline constexpr int Damage = 80; // 大莲子单次直击伤害
    inline constexpr int FragmentDamage = 20; // 每颗小莲子的单次伤害
    inline constexpr int FragmentLimit = 6; // 每次分裂最多选择的不同次要目标数
    inline constexpr float SplitHalfWidth = .5f; // 分裂候选横向半宽，单位格宽
    inline constexpr int SplitRowRadius = 1; // 分裂覆盖本行与上下相邻行
    inline constexpr float FlightSeconds = 1.2f; // 大莲子抛射时长，游戏秒
    inline constexpr float ArcHeight = 210.0f; // 大莲子抛射拱高，世界像素
    inline constexpr float FragmentFlightSeconds = .45f; // 小莲子短距离追踪飞行时长，游戏秒
    inline constexpr float FragmentArcHeight = 42.0f; // 分裂小莲子的可见弧线高度，世界像素
}
