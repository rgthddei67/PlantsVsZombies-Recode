#pragma once

/** 防灾工程师的实体、灰烬事务和指挥官预测共用参数。 */
namespace DisasterEngineerRules {
inline constexpr int Cost = 35; // 指挥官购买价格，冰块
inline constexpr int Health = 1000; // 无防具本体生命
inline constexpr float RadiusCells = 1.5f; // 同行前后保护距离，格
inline constexpr int Capacity = 3; // 每只工程师同时保护的制冰工上限
inline constexpr int ReloadCost = 10; // 每次开始装填的冰块费用
inline constexpr float ReloadSeconds = 6.0f; // 已付款装填所需有效行动秒数
}
