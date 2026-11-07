#pragma once
#include "Workshop.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieType.h"
#include "Game/Board/WaveSpawnRules.h"
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
inline constexpr std::array<PlantDefinition, 13> Plants{{
    {PlantType::PLANT_PEASHOOTER, "pea", "豌豆射手", "普通弹 · 单行直射", ShooterCost, 0, Ammo::Pea, true},
    {PlantType::PLANT_SNOWPEA, "snowpea", "寒冰射手", "寒冰弹 · 单体减速", IceShooterCost, 3, Ammo::Ice, true},
    {PlantType::PLANT_WALLNUT, "wallnut", "坚果", "不耗弹药 · 前排阻挡", 50, 2, Ammo::Count, false},
    {PlantType::PLANT_ELITE_SCAREDYSHROOM, "elite_scaredy", "精英胆小菇", "孢子 · 持续射击成长，受惊清零", 200, 4, Ammo::Spore, false},
    {PlantType::PLANT_MELONPULT, "melon", "西瓜投手", "西瓜 · 三行范围溅射", 300, 5, Ammo::Melon, true},
    {PlantType::PLANT_TALLNUT, "tallnut", "高坚果", "不耗弹药 · 高耐久阻挡", 125, 7, Ammo::Count, false},
    {PlantType::PLANT_WINTERMELON, "wintermelon", "冰瓜", "升级现有西瓜 · 冰瓜范围减速", 350, 8, Ammo::IceMelon, true},
    {PlantType::PLANT_ICESTORAGENUT, "ice_nut", "冰仓坚果", "抗巨人重击 · 维修包与阳光修复", 300, 10, Ammo::Count, false},
    {PlantType::PLANT_GATLINGPEA, "heavy_gatling", "重装机枪射手", "四连发 · 双倍伤害 · 一次穿透", HeavyGatlingCost, BlueprintUnlockWaves[static_cast<int>(Blueprint::HeavyGatling)], Ammo::Pea, true, Blueprint::HeavyGatling},
    {PlantType::PLANT_TORCHWOOD, "blaze_torch", "炽焰树桩", "点燃增伤三倍 · 保留重装穿透", BlazeTorchCost, BlueprintUnlockWaves[static_cast<int>(Blueprint::BlazeTorch)], Ammo::Count, false, Blueprint::BlazeTorch},
    {PlantType::PLANT_MAGNETSHROOM, "rapid_magnet", "强磁菇", "常醒 · 每5秒吸取一件金属装备", 200, BlueprintUnlockWaves[static_cast<int>(Blueprint::RapidMagnet)], Ammo::Count, false, Blueprint::RapidMagnet},
    {PlantType::PLANT_THREEPEATER, "heavy_threepeater", "重装三线射手", "三路双倍伤害 · 每轮耗3颗普通弹", 450, BlueprintUnlockWaves[static_cast<int>(Blueprint::HeavyThreepeater)], Ammo::Pea, true, Blueprint::HeavyThreepeater},
    {PlantType::PLANT_MENDINGCOTTON, "emergency_cotton", "急救棉花", "每2秒恢复180血 · 每次消耗1维修包", 250, BlueprintUnlockWaves[static_cast<int>(Blueprint::EmergencyCotton)], Ammo::RepairKit, false, Blueprint::EmergencyCotton}
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

inline constexpr std::array<Threat, 25> Threats{{
    LegacyThreats[0], LegacyThreats[1], LegacyThreats[2], LegacyThreats[3],
    {9, ZombieType::ZOMBIE_DOOR, "铁门", 8, 0},
    {10, ZombieType::ZOMBIE_POLEVAULTER, "撑杆", 8, 0},
    {12, ZombieType::ZOMBIE_DANCER, "舞王", 14, 0}, LegacyThreats[4],
    {16, ZombieType::ZOMBIE_LADDER, "扶梯", 12, 0}, LegacyThreats[5],
    {20, ZombieType::ZOMBIE_CATAPULT, "投篮", 16, 0}, LegacyThreats[6],
    {24, ZombieType::ZOMBIE_POGO, "跳跳", 14, 0}, LegacyThreats[7],
    {28, ZombieType::ZOMBIE_ELITE_POLEVAULTER, "精英撑杆", 16, 0},
    {30, ZombieType::ZOMBIE_ELITE_CATAPULT, "精英投篮", 20, 0},
    {32, ZombieType::ZOMBIE_JACK_IN_THE_BOX, "小丑", 16, 0},
    {34, ZombieType::ZOMBIE_FASTPAPER, "加强报纸", 12, 0},
    {34, ZombieType::ZOMBIE_POLAR_CLOCKMAKER, "极夜钟匠", 24, 0},
    {36, ZombieType::ZOMBIE_REINFORCED_DOOR, "加固铁门", 16, 0},
    {38, ZombieType::ZOMBIE_PINK_FOOTBALL, "粉色橄榄球", 18, 0},
    {39, ZombieType::ZOMBIE_GILDED_ZAMBONI, "鎏金冰车", 24, 0},
    {40, ZombieType::ZOMBIE_ELITE_LADDER, "精英扶梯", 20, 0},
    {42, ZombieType::ZOMBIE_ELITE_POGO, "精英跳跳", 20, 0},
    {44, ZombieType::ZOMBIE_REDEYE_GARGANTUAR, "红眼巨人", 24, 0}
}}; // period 保留为混编稀有度：越大越少；首次登场固定一只，其后进入随机池

struct ThreatPreview { int wave = 0; std::string names; };
/** 两种视图共用下一次首发预告；同波新增多种时必须全部列出。 */
inline ThreatPreview NextThreatPreview(int completedWaves) {
    ThreatPreview preview;
    for (const auto& threat : Threats) {
        if (threat.wave <= completedWaves) continue;
        if (preview.wave != 0 && threat.wave != preview.wave) break;
        preview.wave = threat.wave;
        if (!preview.names.empty()) preview.names += "、";
        preview.names += threat.name;
    }
    return preview;
}

/** 正式出兵名单的每波累计上限；不限制同屏数，不在死亡或读档时返还名额。 */
inline int ThreatWaveLimit(ZombieType type) {
    using namespace WaveSpawnRules;
    switch (type) {
    case ZombieType::ZOMBIE_ELITE_POLEVAULTER: return kElitePolevaulterMaxPerWave;
    case ZombieType::ZOMBIE_ELITE_CATAPULT: return kEliteCatapultMaxPerWave;
    case ZombieType::ZOMBIE_REINFORCED_DOOR: return kReinforcedDoorMaxPerWave;
    case ZombieType::ZOMBIE_ELITE_LADDER: return kEliteLadderMaxPerWave;
    case ZombieType::ZOMBIE_ELITE_POGO: return kElitePogoMaxPerWave;
    case ZombieType::ZOMBIE_REDEYE_GARGANTUAR: return kRedeyeGargantuarMaxPerAdventureWave;
    case ZombieType::ZOMBIE_ADAPTIVE_HELMET: return kAdaptiveHelmetMaxPerWave;
    case ZombieType::ZOMBIE_POLAR_CLOCKMAKER: return ClockmakerPerWave;
    case ZombieType::ZOMBIE_GILDED_ZAMBONI: return kGildedZamboniMaxPerWave;
    default: return MaxAssaultSpawns;
    }
}

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
    constexpr int kNormalWeight = 12; // 前期随机名额的普通僵尸权重，另每三只保留一只普通
    constexpr int kMixStartWave = 20, kMixEndWave = 30; // 此区间逐步取消额外普通权重，成熟池约三分之一普通
    const int normalWeight = kNormalWeight * std::clamp(kMixEndWave - wave, 0, kMixEndWave - kMixStartWave) / (kMixEndWave - kMixStartWave);
    auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    std::vector<int> result(count, static_cast<int>(ZombieType::ZOMBIE_NORMAL));
    std::array<int, Threats.size()> chosen{};
    int debutSlot = std::min(4, count - 1);
    for (size_t t = 0; t < Threats.size() && count > 1; ++t) if (Threats[t].wave == wave) {
        // 同波多种首发各占独立名额；不覆盖已放入的首发或每三只保留的普通。
        while (debutSlot < count && debutSlot % 3 == 0) ++debutSlot;
        if (debutSlot >= count) break;
        result[debutSlot++] = static_cast<int>(Threats[t].type); ++chosen[t];
    }
    for (int i = 0; i < count; ++i) {
        if (i % 3 == 0 || result[i] != static_cast<int>(ZombieType::ZOMBIE_NORMAL)) continue;
        int total = normalWeight;
        for (size_t t = 0; t < Threats.size(); ++t) if (Threats[t].wave < wave && chosen[t] < ThreatWaveLimit(Threats[t].type))
            total += std::max(1, 24 / Threats[t].period) * eliteWeight;
        if (total == 0) continue;
        int choice = static_cast<int>(next() % total) - normalWeight;
        if (choice < 0) continue;
        for (size_t t = 0; t < Threats.size(); ++t) {
            const auto& threat = Threats[t];
            if (threat.wave >= wave || chosen[t] >= ThreatWaveLimit(threat.type)) continue;
            choice -= std::max(1, 24 / threat.period) * eliteWeight;
            if (choice < 0) { result[i] = static_cast<int>(threat.type); ++chosen[t]; break; }
        }
    }
    return result;
}
}
