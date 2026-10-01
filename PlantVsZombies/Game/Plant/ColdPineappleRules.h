#pragma once

/** 已确认领域的实体与防线预测共用参数。 */
namespace ColdPineappleRules {
constexpr float kDuration = 10.0f; // 每次强化持续游戏秒数
constexpr float kCooldown = 12.0f; // 强化结束后的冷却游戏秒数
constexpr int kIceCost = 40; // 冷藏站单次技能冰费
constexpr int kSunCost = 100; // 其他地图单次技能阳光费
}
