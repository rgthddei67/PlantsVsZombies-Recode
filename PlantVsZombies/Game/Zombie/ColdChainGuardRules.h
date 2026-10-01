#pragma once

/** 冷链护卫实体与纯数值预测共用修复规则；防具破碎后不重建。 */
namespace ColdChainGuardRules {
constexpr int kBodyHealth = 500; // 本体生命，破盾后不能修复
constexpr int kShieldHealth = 1300; // 非磁性一类冰盾初始生命
constexpr int kRepairHealth = 400; // 每轮恢复盾值，不超过当前最大生命
constexpr int kRepairIce = 1; // 冷藏站每轮消耗所属阵营公共冰块
constexpr float kRepairInterval = 2.5f; // 完整修复周期，游戏秒；硬控暂停
}
