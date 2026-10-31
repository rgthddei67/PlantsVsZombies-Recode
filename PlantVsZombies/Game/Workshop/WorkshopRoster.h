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
};
inline constexpr std::array<PlantDefinition, 8> Plants{{
    {PlantType::PLANT_PEASHOOTER, "pea", "豌豆射手", "普通弹 · 单行直射", ShooterCost, 0, Ammo::Pea, true},
    {PlantType::PLANT_SNOWPEA, "snowpea", "寒冰射手", "寒冰弹 · 单体减速", IceShooterCost, 3, Ammo::Ice, true},
    {PlantType::PLANT_WALLNUT, "wallnut", "坚果", "不耗弹药 · 前排阻挡", 50, 2, Ammo::Count, false},
    {PlantType::PLANT_ELITE_SCAREDYSHROOM, "elite_scaredy", "精英胆小菇", "孢子 · 持续射击成长，受惊清零", 200, 4, Ammo::Spore, false},
    {PlantType::PLANT_MELONPULT, "melon", "西瓜投手", "西瓜 · 三行范围溅射", 300, 5, Ammo::Melon, true},
    {PlantType::PLANT_TALLNUT, "tallnut", "高坚果", "不耗弹药 · 高耐久阻挡", 125, 7, Ammo::Count, false},
    {PlantType::PLANT_WINTERMELON, "wintermelon", "冰瓜", "升级现有西瓜 · 冰瓜范围减速", 350, 8, Ammo::IceMelon, true},
    {PlantType::PLANT_ICESTORAGENUT, "ice_nut", "冰仓坚果", "抗巨人重击 · 阳光修复与护体", 300, 10, Ammo::Count, false}
}}; // cost 单位阳光，unlockWave 为必须守住的波数；同时部署只受 5×6 格位约束
inline const PlantDefinition* FindPlant(PlantType type) {
    for (const auto& plant : Plants) if (plant.type == type) return &plant;
    return nullptr;
}
inline const PlantDefinition* FindPlant(std::string_view key) {
    for (const auto& plant : Plants) if (plant.key == key) return &plant;
    return nullptr;
}

struct Threat {
    int wave; ZombieType type; const char* name;
    int period, slot; // 每 period 个正式出兵名额，在 slot 位置混入，均为零基序号
    int debutSlot = -1; // 非负时首次登场整波只使用这一个出兵序号
};
inline constexpr std::array<Threat, 8> Threats{{
    {3, ZombieType::ZOMBIE_TRAFFIC_CONE, "路障", 4, 3}, {5, ZombieType::ZOMBIE_NEWSPAPER, "报纸", 6, 5},
    {7, ZombieType::ZOMBIE_BUCKET, "铁桶", 8, 7}, {8, ZombieType::ZOMBIE_FOOTBALL, "橄榄球", 8, 3},
    {14, ZombieType::ZOMBIE_GARGANTUAR, "巨人", 18, 9, 4}, {18, ZombieType::ZOMBIE_HEALER, "急救员", 16, 12},
    {22, ZombieType::ZOMBIE_CRYSTAL_DRUMMER, "震晶鼓手", 20, 16}, {26, ZombieType::ZOMBIE_ADAPTIVE_HELMET, "适应头盔", 14, 10}
}}; // wave 是首次登场波次，顺序也表示重合时的替换优先级；预告与正式出兵共用

/** 同波序号决定编成，读档不重抽；首次登场的整波名额与后续混编周期均来自同一表。 */
inline ZombieType SpawnType(int wave, int number) {
    auto type = ZombieType::ZOMBIE_NORMAL;
    for (const auto& threat : Threats) {
        if (wave < threat.wave) continue;
        const bool chosen = wave == threat.wave && threat.debutSlot >= 0
            ? number == threat.debutSlot : number % threat.period == threat.slot;
        if (chosen) type = threat.type;
    }
    return type;
}
}
