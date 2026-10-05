#pragma once

#include <vector>

namespace ColdStorageSearch {
struct Snapshot;
struct Unit;
struct ConstructionStats;
inline constexpr float HealerForecastStep = .5f; // 与完整候选积分步相同的游戏秒，调用方须保持一致

/**
 * 按纯数值快照推进治疗冷却、停步前摇与三层修复；不会生成实体或扣真实资源。
 * 在本步受伤后、外部停止计时递减及移动/啃食前调用，当前体位用于圆形范围判定。
 * 新选择使用正式确定性回退；已锁定单疗保留稳定身份，群疗结算时重新查询活体。
 * initialHealth 必须与 units 对齐；撤回的爆区损失累计到 stats.healerRecoveryCredit，
 * 调用方从本候选削血特征中减去其本步增量，并乘用 healer.movementActivity。
 */
void AdvanceHealers(const Snapshot& snapshot, float time, std::vector<Unit>& units,
    const std::vector<float>& initialHealth, ConstructionStats& stats);
}
