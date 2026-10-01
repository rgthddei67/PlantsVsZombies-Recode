#pragma once

/** 冰仓坚果的承伤与付费修复参数；生产和攻击加速不改变这些计时。 */
namespace IceStorageNutRules {
constexpr int kHealth = 8000; // 本体最大生命
constexpr int kCrushDamage = 1000; // 每次巨人砸击或车辆碾压的基础伤害
constexpr float kInvulnerability = 0.0f; // 既有指挥官投影的兼容参数；坚果已取消伤害无敌
constexpr float kVehicleRetreatCells = 1.0f; // 成功挡车后向右推退距离，格
constexpr int kRepairHealth = 1000; // 单次恢复生命；自动挡须缺少完整这一量
constexpr int kRepairIce = 20; // 冷藏站每次修复冰费
constexpr int kRepairSun = 100; // 其他地图每次修复阳光费
constexpr float kRepairCooldown = 10.0f; // 修复成功后的冷却游戏秒数
}
