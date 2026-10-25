#include "WaveSpawnCounts.h"
#include "WaveSpawnRules.h"
#include "Game/Zombie/SalvageZombieRules.h"
#include "Game/Zombie/SludgeZombieRules.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace {
    using namespace WaveSpawnRules;
    struct Entry { ZombieType type; const char* key; int restoreLimit; };
    // 新增限额类型只需登记持久化身份；正式生成门禁仍由 Board resolver 负责。
    // 钟匠历史读档夹紧为3（正式生成上限4），本次结构迁移保留原兼容语义。
    constexpr Entry entries[] = {
    {ZombieType::ZOMBIE_SLUDGE, "sludgeZombiesSpawnedThisWave", SludgeZombieRules::PerWaveLimit},
    {ZombieType::ZOMBIE_SALVAGE, "salvageZombiesSpawnedThisWave", SalvageZombieRules::CompositeLimit},
    {ZombieType::ZOMBIE_ELITE_DANCER, "eliteDancersSpawnedThisWave", kEliteDancerMaxPerWave},
    {ZombieType::ZOMBIE_REINFORCED_DOOR, "reinforcedDoorsSpawnedThisWave", kReinforcedDoorMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_POLEVAULTER, "elitePolevaultersSpawnedThisWave", kElitePolevaulterMaxPerWave},
    {ZombieType::ZOMBIE_GILDED_ZAMBONI, "gildedZambonisSpawnedThisWave", kGildedZamboniMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER, "eliteDolphinRidersSpawnedThisWave", kEliteDolphinRiderMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX, "eliteJackInTheBoxesSpawnedThisWave", kEliteJackInTheBoxMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_DIGGER, "eliteDiggersSpawnedThisWave", kEliteDiggerMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_POGO, "elitePogosSpawnedThisWave", kElitePogoMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_LADDER, "eliteLaddersSpawnedThisWave", kEliteLadderMaxPerWave},
    {ZombieType::ZOMBIE_ELITE_CATAPULT, "eliteCatapultsSpawnedThisWave", kEliteCatapultMaxPerWave},
    {ZombieType::ZOMBIE_REDEYE_GARGANTUAR, "redeyeGargantuarsSpawnedThisWave", kRedeyeGargantuarMaxPerAdventureWave},
    {ZombieType::ZOMBIE_INSULATOR, "insulatorsSpawnedThisWave", kInsulatorMaxPerWave},
    {ZombieType::ZOMBIE_HIJACKER, "hijackersSpawnedThisWave", kHijackerMaxPerWave},
    {ZombieType::ZOMBIE_GROUNDING, "groundingZombiesSpawnedThisWave", kGroundingZombieMaxPerWave},
    {ZombieType::ZOMBIE_BOBSLED_TEAM, "bobsledTeamsSpawnedThisWave", kBobsledTeamMaxPerWave},
    {ZombieType::ZOMBIE_ICE_WALL_ENGINEER, "iceWallEngineersSpawnedThisWave", kIceWallEngineerMaxPerWave},
    {ZombieType::ZOMBIE_ICE_CRACK_DRILL, "iceCrackDrillsSpawnedThisWave", kIceCrackDrillMaxPerWave},
    {ZombieType::ZOMBIE_WEATHER_JAMMER, "weatherJammersSpawnedThisWave", kWeatherJammerMaxPerWave},
    {ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER, "iceStatueExecutionersSpawnedThisWave", kIceStatueExecutionerMaxPerWave},
    {ZombieType::ZOMBIE_SNOW_BURROW, "snowBurrowsSpawnedThisWave", kSnowBurrowCompositeMaxPerWave},
    {ZombieType::ZOMBIE_ADAPTIVE_HELMET, "adaptiveHelmetsSpawnedThisWave", kAdaptiveHelmetMaxPerWave},
    {ZombieType::ZOMBIE_THERMAL_SNIPER, "thermalSnipersSpawnedThisWave", kThermalSniperCompositeMaxPerWave},
    {ZombieType::ZOMBIE_AURORA_PRIEST, "auroraPriestsSpawnedThisWave", kAuroraPriestMaxPerWave},
    {ZombieType::ZOMBIE_POLAR_CLOCKMAKER, "clockmakersSpawnedThisWave", 3},
    {ZombieType::ZOMBIE_CRYSTAL_DRUMMER, "crystalDrummersSpawnedThisWave", 1},
    {ZombieType::ZOMBIE_CRYSTAL_HORN_MINER, "crystalMinersSpawnedThisWave", 1},
    {ZombieType::ZOMBIE_SUN_THIEF, "sunThievesSpawnedThisWave", 2},
    };
}

void WaveSpawnCounts::Save(nlohmann::json& document) const {
    for (const auto& entry : entries) document[entry.key] = Get(entry.type);
}

void WaveSpawnCounts::Load(const nlohmann::json& document, int level, bool survival) {
    Reset();
    for (const auto& entry : entries) {
        int limit = entry.restoreLimit;
        if (entry.type == ZombieType::ZOMBIE_SALVAGE && !survival && level == SalvageZombieRules::TutorialLevel)
            limit = SalvageZombieRules::TutorialLimit;
        if (entry.type == ZombieType::ZOMBIE_REDEYE_GARGANTUAR && survival) limit = 0;
        if (entry.type == ZombieType::ZOMBIE_SNOW_BURROW && level == kSnowBurrowTutorialLevel)
            limit = kSnowBurrowTutorialMaxPerWave;
        if (entry.type == ZombieType::ZOMBIE_THERMAL_SNIPER && level == kThermalSniperTutorialLevel)
            limit = kThermalSniperTutorialMaxPerWave;
        const int fallback = entry.type == ZombieType::ZOMBIE_ELITE_DANCER
            && document.value("eliteDancerSpawnedThisTyphoon", false) ? 1 : 0;
        RestoreCount(entry.type, std::clamp(document.value(entry.key, fallback), 0, limit));
    }
}
