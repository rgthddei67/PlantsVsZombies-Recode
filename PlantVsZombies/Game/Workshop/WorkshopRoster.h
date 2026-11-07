#pragma once
#include "Workshop.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieType.h"
#include <string_view>

namespace Workshop {
/** 工坊独立购买表；不读取冒险解锁，价格只用于本模式。 */
struct PlantDefinition {
    PlantType type;
    const char* key;
    const char* name;
    const char* role;
    int cost;
    int unlockWave;
    Ammo ammo;
    bool upgrades;
    Blueprint blueprint = Blueprint::Count;
};
inline constexpr std::array<PlantDefinition, 10> Plants{{
    {PlantType::PLANT_PEASHOOTER, "pea", "豌豆射手", "普通弹 · 单行直射", ShooterCost, 0, Ammo::Pea, true},
    {PlantType::PLANT_SNOWPEA, "snowpea", "寒冰射手", "寒冰弹 · 单体减速", IceShooterCost, 3, Ammo::Ice, true},
    {PlantType::PLANT_WALLNUT, "wallnut", "坚果", "不耗弹药 · 前排阻挡", 50, 2, Ammo::Count, false},
    {PlantType::PLANT_ELITE_SCAREDYSHROOM, "elite_scaredy", "精英胆小菇", "孢子 · 持续射击成长，受惊清零", 200, 4, Ammo::Spore, false},
    {PlantType::PLANT_MELONPULT, "melon", "西瓜投手", "西瓜 · 三行范围溅射", 300, 5, Ammo::Melon, true},
    {PlantType::PLANT_TALLNUT, "tallnut", "高坚果", "不耗弹药 · 高耐久阻挡", 125, 7, Ammo::Count, false},
    {PlantType::PLANT_WINTERMELON, "wintermelon", "冰瓜", "升级现有西瓜 · 冰瓜范围减速", 350, 8, Ammo::IceMelon, true},
    {PlantType::PLANT_ICESTORAGENUT, "ice_nut", "冰仓坚果", "抗巨人重击 · 维修包与阳光修复", 300, 10, Ammo::Count, false},
    {PlantType::PLANT_GATLINGPEA, "heavy_gatling", "重装机枪射手", "四连发 · 双倍伤害 · 一次穿透", HeavyGatlingCost, BlueprintUnlockWaves[static_cast<int>(Blueprint::HeavyGatling)], Ammo::Pea, true, Blueprint::HeavyGatling},
    {PlantType::PLANT_TORCHWOOD, "blaze_torch", "炽焰树桩", "点燃增伤三倍 · 保留重装穿透", BlazeTorchCost, BlueprintUnlockWaves[static_cast<int>(Blueprint::BlazeTorch)], Ammo::Count, false, Blueprint::BlazeTorch}
}}; // cost 单位阳光，unlockWave 为必须守住的波数；同时部署只受 5×6 格位约束
inline const PlantDefinition* FindPlant(PlantType type) {
    for (const auto& plant : Plants) if (plant.type == type) return &plant;
    return nullptr;
}
inline const PlantDefinition* FindPlant(std::string_view key) {
    for (const auto& plant : Plants) if (plant.key == key) return &plant;
    return nullptr;
}
inline bool PlantUnlocked(const World& world, const PlantDefinition& plant) {
    return world.CompletedWaves() >= plant.unlockWave && (plant.blueprint == Blueprint::Count || world.Tasks().Has(plant.blueprint));
}
inline int PlantCost(const World& world, const PlantDefinition& plant) {
    return world.Tasks().FreeUses(plant.blueprint) > 0 ? 0 : plant.cost;
}

struct Threat {
    int wave; ZombieType type; const char* name;
    int period, slot; // period 同时表达新池稀有度；slot 仅用于旧版定表迁移，零基序号
    int debutSlot = -1; // 非负时首次登场整波只使用这一个出兵序号
};
inline constexpr std::array<Threat, 8> LegacyThreats{{
    {3, ZombieType::ZOMBIE_TRAFFIC_CONE, "路障", 4, 3}, {5, ZombieType::ZOMBIE_NEWSPAPER, "报纸", 6, 5},
    {7, ZombieType::ZOMBIE_BUCKET, "铁桶", 8, 7}, {8, ZombieType::ZOMBIE_FOOTBALL, "橄榄球", 8, 3},
    {14, ZombieType::ZOMBIE_GARGANTUAR, "巨人", 18, 9, 4}, {18, ZombieType::ZOMBIE_HEALER, "急救员", 16, 12},
    {22, ZombieType::ZOMBIE_CRYSTAL_DRUMMER, "震晶鼓手", 20, 16}, {26, ZombieType::ZOMBIE_ADAPTIVE_HELMET, "适应头盔", 14, 10}
}}; // wave 是首次登场波次，顺序也表示重合时的替换优先级；预告与正式出兵共用

inline constexpr std::array<Threat, 14> Threats{{
    LegacyThreats[0], LegacyThreats[1], LegacyThreats[2], LegacyThreats[3],
    {11, ZombieType::ZOMBIE_POLEVAULTER, "撑杆", 8, 0}, LegacyThreats[4],
    {16, ZombieType::ZOMBIE_LADDER, "扶梯", 12, 0}, LegacyThreats[5],
    {20, ZombieType::ZOMBIE_CATAPULT, "投篮", 16, 0}, LegacyThreats[6],
    {24, ZombieType::ZOMBIE_POGO, "跳跳", 14, 0}, LegacyThreats[7],
    {28, ZombieType::ZOMBIE_ELITE_POLEVAULTER, "精英撑杆", 16, 0},
    {30, ZombieType::ZOMBIE_ELITE_CATAPULT, "精英投篮", 20, 0}
}}; // period 保留为混编稀有度：越大越少；首次登场固定一只，其后进入随机池

/** 仅供 v7 正在进行的袭击迁移，保持旧档已经承诺的后续编成。 */
inline ZombieType SpawnType(int wave, int number) {
    auto type = ZombieType::ZOMBIE_NORMAL;
    for (const auto& threat : LegacyThreats) {
        if (wave < threat.wave) continue;
        const bool chosen = wave == threat.wave && threat.debutSlot >= 0
            ? number == threat.debutSlot : number % threat.period == threat.slot;
        if (chosen) type = threat.type;
    }
    return type;
}

/** 每波一次性抽选：普通僵尸固定保留，首次开放品种保证一只；结果必须随本波保存。 */
inline std::vector<int> RollAssaultTypes(int wave, int count, std::uint32_t& random, int eliteWeight = 1) {
    constexpr int kNormalWeight = 12; // 随机名额里的普通僵尸权重，此外每三只保留一只普通
    auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    int total = kNormalWeight;
    for (const auto& threat : Threats) if (threat.wave < wave) total += std::max(1, 24 / threat.period) * eliteWeight;
    std::vector<int> result(count, static_cast<int>(ZombieType::ZOMBIE_NORMAL));
    for (int i = 0; i < count; ++i) {
        if (i % 3 == 0) continue;
        int choice = static_cast<int>(next() % total) - kNormalWeight;
        if (choice < 0) continue;
        for (const auto& threat : Threats) if (threat.wave < wave) {
            choice -= std::max(1, 24 / threat.period) * eliteWeight;
            if (choice < 0) { result[i] = static_cast<int>(threat.type); break; }
        }
    }
    for (const auto& threat : Threats) if (threat.wave == wave && count > 1)
        result[std::min(4, count - 1)] = static_cast<int>(threat.type);
    return result;
}
}
