#pragma once

/** 浮光葵生产规则；费用与卡牌冷却由 gamedata.json 维护。 */
namespace FloatSunflowerRules {
inline constexpr int Health = 300; // 基础生命值
inline constexpr float Interval = 24.0f; // 含发光阶段的完整生产周期，基础行动秒
inline constexpr float FirstDelay = 8.0f; // 新种后首次产出等待，基础行动秒
inline constexpr int LandSunCount = 1; // 陆地每轮普通阳光颗数，每颗25点
inline constexpr int WaterSunCount = 2; // 水中每轮普通阳光颗数，共50点
}
