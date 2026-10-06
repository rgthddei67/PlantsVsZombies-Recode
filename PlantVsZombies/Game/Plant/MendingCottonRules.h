#pragma once

/** 棉花实体与指挥官治疗预测共用规则。 */
namespace MendingCottonRules {
inline constexpr float Interval = 3.0f; // 每次治疗后的有效行动秒数；闲置最多保留一次就绪
inline constexpr int Amount = 60; // 单次固定恢复生命，不超过目标本体上限
inline constexpr int Health = 300; // 棉花本体生命
inline constexpr int IceCost = 10; // 冰块经济地图的种植冰价
}
