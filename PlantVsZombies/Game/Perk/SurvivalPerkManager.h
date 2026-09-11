#pragma once
#include <array>
#include <vector>
#include <nlohmann/json_fwd.hpp>   // 仅前置声明 nlohmann::json，Save/Load 取引用参数足够
#include "PerkType.h"

// 生存模式词条聚合管理器。Board 以值成员持有；非生存关恒空，聚合 getter 返回中性值。
class SurvivalPerkManager {
public:
    bool AddPerk(PerkType type);          // +1 层，到 maxStacks 截断；已满返回 false
    int  GetStacks(PerkType type) const;
    void Clear();                         // 进新生存局时清空

    // 聚合效果——空词条时为中性值（乘法单位元 / 原值），各钟点自动 no-op
    double GetPlantDamageMultiplier() const;       // 1 + 0.12 * stacks
    double GetPlantAttackSpeedMultiplier() const;  // 1 + 0.15 * stacks，0 层=1.0
    double GetPlantDamageTakenMultiplier() const;  // 1 - 0.03 * stacks，夹底 0.55
    double GetSunIncomeMultiplier() const;         // 1 + 0.15 * stacks
    double GetPlantCardRechargeMultiplier() const; // 1 + 0.12 * stacks
    double GetZombieHealthMultiplier() const;      // 1 + 0.12 * stacks
    double GetZombieDamageTakenMultiplier() const; // 1 - 0.03 * stacks，夹底 0.55

    int ScalePlantDamage(int base) const;          // round(base * 植物伤害倍率)，base>=1 时结果>=1
    int ScaleDamageToZombie(int base) const;       // round(base * 免伤倍率)，base>=1 时结果>=1
    /** 环境与词条共用数值缩放口径，保留秒杀哨兵与正伤害下限。 */
    static int ScaleNumericDamage(int base, double multiplier);
    int ScaleTotalDamageToZombie(int base) const;  // 植物来源专用：依次应用植物增伤与僵尸免伤，base>=1 时结果>=1
    int ScaleDamageToPlant(int base) const;        // round(base * 植物承伤倍率)，base>=1 时结果>=1
    int ScaleSunIncome(int base) const;            // round(base * 阳光收益倍率)，base>=1 时结果>=1

    double GetZombieDamageMultiplier() const;       // 1 + 0.05 * stacks（不限层）
    int    ScaleZombieDamage(int base) const;       // round(base * 僵尸伤害倍率)，base>=1 时结果>=1
    int    GetZombieInvulnHits() const;             // 4 * stacks（无层→0）
    float  GetPlantRegenInterval() const;           // 固定 5.0 秒（恒定；调用方以 GetPlantRegenPerPulse()>0 判定是否生效）
    int    GetPlantRegenPerPulse() const;           // 65 * stacks（无层→0）
    int    GetPlantRegenHpCap(int maxHealth) const; // 5 层起→maxHealth*3，否则 maxHealth
    double GetMistFuelMultiplier() const;            // 1 + 0.5 * stacks
    bool   HasCorrosiveToxin() const;
    bool   HasUnyieldingRoots() const;
    bool   HasPlantDamageEcho() const;
    bool   HasFogBreakout() const;
    bool   HasZombieDeathRelay() const;
    bool   HasZombieDevourRepair() const;
    bool   HasZombieArmorBreakRush() const;

    static const PerkInfo& GetInfo(PerkType type); // 静态元数据表（UI 用）
    std::vector<PerkType> AvailablePerks(PerkCategory cat,
        const PerkOfferContext& context, PerkRarity rarity) const;

    void Save(nlohmann::json& j) const;            // 仅写 stacks>0 的项，按 key 字符串
    void Load(const nlohmann::json& j);            // 缺 key→0；越界→夹回 [0,maxStacks]

private:
    std::array<int, static_cast<size_t>(PerkType::COUNT)> mStacks{};  // 全 0 初始
};

// 随机生成至多 count 个互不相同的 {植物增益, 僵尸增难} 配对，用 GameRandom（-Seed 可复现）。
// 任一侧无可用词条 → 返回空；可用配对总数 < count → 返回全部。
std::vector<PerkPairing> RollPerkPairings(const SurvivalPerkManager& mgr, int count,
    const PerkOfferContext& context = {});
