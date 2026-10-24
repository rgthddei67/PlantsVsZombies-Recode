#include "LevelDocumentValidation.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace {
using Json = nlohmann::json;
enum class Kind { Bool, Number, String, Array, Object };

/** 只校验已知结构和类型，缺省字段继续交给各域兼容，未知字段保持可向前扩展。 */
void Fields(const Json& object, std::string_view names, Kind kind,
    const std::string& path, std::string& error)
{
    while (!names.empty() && error.empty()) {
        const auto end = names.find(' ');
        const auto key = names.substr(0, end);
        names = end == std::string_view::npos ? std::string_view{} : names.substr(end + 1);
        if (key.empty()) continue;
        const auto it = object.find(std::string(key));
        if (it == object.end()) continue;
        bool valid = false;
        switch (kind) {
        case Kind::Bool: valid = it->is_boolean(); break;
        case Kind::Number:
            valid = it->is_number() && std::isfinite(it->get<double>())
                && std::abs(it->get<double>()) <= std::numeric_limits<float>::max();
            break;
        case Kind::String: valid = it->is_string(); break;
        case Kind::Array: valid = it->is_array(); break;
        case Kind::Object: valid = it->is_object(); break;
        }
        if (!valid) error = path + "/" + std::string(key) + ": invalid field type";
    }
}

/** 直接 get 的必填字段必须在创建任何对象之前出现。 */
void Required(const Json& object, std::string_view names, const std::string& path, std::string& error)
{
    while (!names.empty() && error.empty()) {
        const auto end = names.find(' ');
        const auto key = names.substr(0, end);
        names = end == std::string_view::npos ? std::string_view{} : names.substr(end + 1);
        if (!key.empty() && !object.contains(std::string(key))) error = path + "/" + std::string(key) + ": missing field";
    }
}

/** 植物、僵尸、弹丸、卡牌和动画的公共外壳；品种 extraData 内部由各品种拥有。 */
void EntityFields(const Json& object, const std::string& path, std::string& error)
{
    Fields(object,
        "armorBreakRushSpent auroraPlayedHitSound cobCannonMotion collected draggedUnderByTangleKelp floodCharmed "
        "fogBreakoutObserved fogBreakoutTriggered garlicRedirectActive garlicRowChanged hasArm hasHead hasTongue "
        "imitated inPool isCooldown isDying isEating isMindControlled isSleeping isSquished lobTargetsIceWall "
        "lobbedMotion mineTargetReturning mistFuelRewardClaimed needDropArm needDropHead occupiesGridSlot "
        "polarGuided polarWindMiss small targetsFlying threepeaterMotion unyieldingRootsSpent wasObscuredByFog ", Kind::Bool, path, error);
    Fields(object,
        "animClipSpeed animFrame animPlayState animSpeed animTargetTrackBlendTime animTargetTrackSpeed "
        "armorBreakRushTimer attackDamage bloverDirection bodyHealth bodyMaxHealth butterTimer cobDuration "
        "cobElapsed cobStartX cobTargetRow cobTargetX column cooldownTime cooldownTimer damage "
        "downstreamDamageMultiplier dyingTimer eatPlantID extraSpeed floodSlowRemaining freeHitsRemaining frontX "
        "frozenTimer garlicRedirectElapsed health helmHealth helmMaxHealth helmType hitAuroraTorchwoodColumn "
        "hitTorchwoodColumn iceSealOwnerZombieID id imitaterTarget ladderAltitude ladderClimbPhase lobApexHeight "
        "lobDuration lobElapsed lobStartX lobTargetX maxHealth mineTargetCell mistFuelReward mowerHeight "
        "nextColumn paralysisTimer plantOriginKind plantOriginLineage plantType poolType poolVisualOffsetY posX "
        "posY pressureProtectorID pressureShareRemainder prismMarkRemaining roofMarshalAssaultBiteMultiplier "
        "roofMarshalAssaultMoveMultiplier roofMarshalAssaultTimer rotationDegrees rotationSpeedDegrees row "
        "shieldHealth shieldMaxHealth shieldType shutdownTimer spawnWave speed squishTimer squishVisualX "
        "squishVisualY state style sunCost tangleKelpGrabFrame tangleKelpPlantID tangleKelpSinkOffset targetType "
        "thermalEndpointX thermalEndpointY thermalOriginalPlantID thunderResistanceTimer timeLeft "
        "toxinDamageRemainder type unyieldingRootsTimer useLadderColumn velocityX velocityY wakeUpTimer x y ", Kind::Number, path, error);
    Fields(object,
        "animTargetTrack animTrack ", Kind::String, path, error);
    Fields(object,
        "auroraHitZombieIDs bambooHitIDs piercedZombieIDs spikeDamageRemainders ", Kind::Array, path, error);
    Fields(object,
        "extraData anchorRoot ", Kind::Object, path, error);
    if (const auto root = object.find("anchorRoot"); root != object.end() && root->is_object()) {
        Fields(*root, "plantID remaining immunity", Kind::Number, path + "/anchorRoot", error);
        if (root->contains("plantID") && !(*root)["plantID"].is_number_integer())
            error = path + "/anchorRoot/plantID: invalid entity ID";
    }
    // 所有者不共享生命周期；同名 ID 数组只允许有限数值，不允许嵌套对象。
    for (const char* key : {"piercedZombieIDs", "spikeDamageRemainders", "bambooHitIDs", "auroraHitZombieIDs"}) {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_array()) continue;
        for (const auto& value : *it) if (!value.is_number() || !std::isfinite(value.get<double>())) {
            error = path + "/" + key + ": invalid numeric element"; return;
        }
    }
    if (const auto it = object.find("starRoofReferenceY"); it != object.end()
        && !it->is_null() && !it->is_number()) error = path + "/starRoofReferenceY: invalid field type";
}

/** 校验实体容器及直接读取的字段；空集合与省略集合均兼容旧档。 */
void Entities(const Json& document, const char* key, std::string_view required, std::string& error)
{
    const auto it = document.find(key);
    if (it == document.end() || !error.empty()) return;
    if (!it->is_array()) { error = std::string("/") + key + ": expected array"; return; }
    for (std::size_t i = 0; i < it->size() && error.empty(); ++i) {
        const auto path = std::string("/") + key + "/" + std::to_string(i);
        const auto& entry = (*it)[i];
        if (!entry.is_object()) { error = path + ": expected object"; return; }
        Required(entry, required, path, error);
        EntityFields(entry, path, error);
    }
}

/** 每类实体各有 ID 空间；拒绝含糊身份，并为旧档自动编号保留安全的递增空间。 */
void EntityIDs(const Json& document, const char* key, const char* counter, std::string& error)
{
    if (!error.empty()) return;
    const auto validID = [](const Json& value, bool allowMissing) {
        if (!value.is_number_integer()) return false;
        const double number = value.get<double>();
        return (allowMissing && number == -1)
            || (number >= 1 && number < std::numeric_limits<int>::max());
    };
    const auto savedNext = document.find(counter);
    if (savedNext != document.end() && !validID(*savedNext, false)) {
        error = std::string("/") + counter + ": invalid entity ID counter"; return;
    }
    std::int64_t next = document.value(counter, 1);
    std::int64_t automatic = 0;
    std::unordered_set<int> seen;
    if (const auto entries = document.find(key); entries != document.end()) {
        for (std::size_t i = 0; i < entries->size(); ++i) {
            const auto& entity = (*entries)[i];
            const auto id = entity.find("id");
            const auto path = std::string("/") + key + "/" + std::to_string(i) + "/id";
            if (id == entity.end()) { ++automatic; continue; }
            if (!validID(*id, true)) { error = path + ": invalid entity ID"; return; }
            const int value = id->get<int>();
            if (value == -1) { ++automatic; continue; }
            if (!seen.insert(value).second) { error = path + ": duplicate entity ID"; return; }
            if (value >= next) next = static_cast<std::int64_t>(value) + 1;
        }
    }
    // Add*WithID 和自动编号都会递增；读档后也必须仍有可分配的 ID。
    if (next + automatic >= std::numeric_limits<int>::max())
        error = std::string("/") + counter + ": entity ID space exhausted";
}
}

bool LevelDocumentValidation::Validate(const nlohmann::json& document, std::string& error)
{
    error.clear();
    if (!document.is_object()) { error = "/: expected object"; return false; }
    Fields(document,
        "activeGustPlantMoved adaptiveHelmetTutorialWaveSpawned auroraPriestGuaranteeConsumed "
        "clockmakerGuaranteeConsumed coldWaveForecastDisrupted eliteDancerSpawnedThisTyphoon "
        "fogWeatherForecastDisrupted fogWeatherForecastReady fogWeatherInitialized hijackerSpawnBlockedThisWave "
        "hxyModeEnabled isSurvival nightRoofChargeGuided nightRoofHijackerFinalizing "
        "nightRoofHijackerSelectionAttempted nightRoofHijackerWarningExtended openingColdWavePlanInitialized "
        "pendingHeavyTyphoonOpeningProtected pendingHeavyTyphoonPrepared polarFinalWaveUpgradeApplied "
        "polarFirstWhiteoutCompleted polarHumidityEpisodeConsumed polarLastPlanWasFalse polarNightInitialized "
        "polarPlanIsWhiteout polarTutorialHoleBatchConsumed rainCanHold rainCanIntensify "
        "snowBurrowTutorialHoleSpawnConsumed stormyNightInitialized thermalSniperTutorialSpawned typhoonGustActive "
        "weatherForecastDisrupted weatherForecastReady weatherInitialized winterTemperatureInitialized ", Kind::Bool, "", error);
    Fields(document,
        "activeGustDirection activeGustDuration activeGustPlantMoveTimer activeGustStrength activeGustTimer "
        "actualForecastFogWeatherIntensity actualForecastRainIntensity adaptiveHelmetsSpawnedThisWave "
        "ambientTemperatureC auroraPriestsSpawnedThisWave boardFrame boardState bobsledTeamsSpawnedThisWave "
        "clockmakersSpawnedThisWave coldWaveCoolingDuration coldWaveHoldDuration coldWavePhase coldWaveStrength "
        "coldWaveTargetTemperatureC coldWaveThawDuration coldWaveTimer crystalDrummersSpawnedThisWave "
        "crystalMinersSpawnedThisWave currentWave currentWaveZombieHP currentWeatherNoticeTimer "
        "dawnNavigationTimer eliteCatapultsSpawnedThisWave eliteDancersSpawnedThisWave eliteDiggersSpawnedThisWave "
        "eliteDolphinRidersSpawnedThisWave eliteJackInTheBoxesSpawnedThisWave eliteLaddersSpawnedThisWave "
        "elitePogosSpawnedThisWave elitePolevaultersSpawnedThisWave eliteScaredyShroomsPlanted "
        "failedForecastRainIntensity failedForecastTyphoonStrength fogDispersal fogVisualOffsetX "
        "fogWeatherIntensity fogWeatherTimer forecastFogWeatherIntensity forecastRainIntensity "
        "gildedZambonisSpawnedThisWave groundingZombiesSpawnedThisWave heavyPhasesWithoutTyphoon "
        "hijackerSpawnCooldownWavesRemaining hijackersSpawnedThisWave iceCrackDrillsSpawnedThisWave "
        "iceStatueExecutionersSpawnedThisWave iceWallEngineersSpawnedThisWave insulatorsSpawnedThisWave "
        "lightningTimer maxWave mistFuelDropAccumulator nextBulletID nextCoinID nextDiscontinuousTransactionID "
        "nextMowerID nextPlantID nextWaveSpawnZombieHP nextZombieID nightRoofCharge nightRoofChargeGuideID "
        "nightRoofChargePhase nightRoofChargePhaseTimer nightRoofChargeRow nightRoofHijackerID nightRoofOvercharge "
        "pendingHeavyRainPromptVariant pendingHeavyTyphoonGustsRemaining pendingHeavyTyphoonStrength "
        "pendingHeavyTyphoonStrengthTimer pendingHeavyWindDirection pendingHeavyWindDirectionTimer "
        "pendingHeavyWindGustTimer plantDamageEchoHitCounter polarAllDangerTimer polarDangerMask "
        "polarFluctuationDuration polarFluctuationTimer polarHighHumidityTimer polarHumidityPercent "
        "polarNightPhase polarPhaseDuration polarPhaseTimer polarStartHumidityPercent polarStartTemperatureC "
        "polarStartWindSpeedMps polarTargetHumidityPercent polarTargetTemperatureC polarTargetWindSpeedMps "
        "polarTemperatureC polarVerticalWindDirection polarWhiteoutTimer polarWindSpeedMps poolGridVersion "
        "poolSunCountDown previousRainIntensity rainIntensity redeyeGargantuarsSpawnedThisWave "
        "reinforcedDoorsSpawnedThisWave roofRunoffCharge roofRunoffPhase roofRunoffPhaseTimer "
        "roofRunoffRetainedCharge roofRunoffRowMask schemaVersion snowBurrowsSpawnedThisWave "
        "stormyNightFlashPattern stormyNightFlashTimer sun sunCountDown sunThievesSpawnedThisWave survivalRound "
        "thermalSnipersSpawnedThisWave totalZombieHP typhoonGustsRemaining typhoonStrength typhoonStrengthTimer "
        "weakWeatherPhasesSinceHeavy weatherForecastFailureActualIntensity "
        "weatherForecastFailureActualTyphoonStrength weatherForecastFailureTimer weatherJammersSpawnedThisWave "
        "weatherPanelInterferenceTimer weatherTimer weatherTransitionTimer windDirection windDirectionTimer "
        "windGustTimer winterFrostVariant zombieCountDown ", Kind::Number, "", error);
    Fields(document,
        "bullets cards craters echoWaves goldenIceTrails groundRifts iceTrails ladders mowers pendingAuroraRifts "
        "pendingSnowHoleSpawns plants snowHoles spawnList sunTheftLedger suns survivalCardCooldowns "
        "temporalAnchors trophies zombies ", Kind::Array, "", error);
    Fields(document, "coldStorage watersideFlood", Kind::Object, "", error);
    if (const auto it = document.find("watersideFlood"); it != document.end() && it->is_object()) {
        Fields(*it, "water overflow warningRemaining surgeCount", Kind::Number, "/watersideFlood", error);
        Fields(*it, "targets", Kind::Array, "/watersideFlood", error);
        if (const auto targets = it->find("targets"); targets != it->end() && targets->is_array()) {
            for (const auto& target : *targets) {
                if (!target.is_number_integer() || target.get<int64_t>() < 0
                    || target.get<int64_t>() >= std::numeric_limits<int>::max()) {
                    error = "/watersideFlood/targets: expected plant IDs";
                    return false;
                }
            }
        }
    }
    if (const auto it = document.find("coldStorage"); it != document.end() && it->is_object()) {
        Fields(*it, "randomSpawns", Kind::Bool, "/coldStorage", error);
        Fields(*it, "randomWeatherRemaining", Kind::Number, "/coldStorage", error);
    }
    Entities(document, "plants", "type row column health maxHealth isSleeping", error);
    Entities(document, "zombies", "type row x", error);
    Entities(document, "mowers", "type row x y", error);
    Entities(document, "bullets", "type row x y damage velocityX velocityY", error);
    Entities(document, "suns", "x y", error);
    Entities(document, "trophies", "x y", error);
    // 轮间旧卡只用于迁移冷却，不能要求当时未写出的绘制字段。
    if (!error.empty()) return false;
    const bool choosing = document.value("isSurvival", false)
        && document.value("boardState", 1) == 0;
    Entities(document, "cards", choosing ? "" : "plantType posX posY sunCost cooldownTime isCooldown cooldownTimer", error);
    Entities(document, "survivalCardCooldowns", "plantType", error);
    for (const char* key : {"craters", "ladders", "groundRifts", "sunTheftLedger"}) Entities(document, key, "", error);
    if (const auto it = document.find("spawnList"); it != document.end() && it->is_array()) {
        for (const auto& value : *it) if (!value.is_number()) { error = "/spawnList: invalid numeric element"; break; }
    }
    EntityIDs(document, "plants", "nextPlantID", error);
    EntityIDs(document, "zombies", "nextZombieID", error);
    EntityIDs(document, "bullets", "nextBulletID", error);
    EntityIDs(document, "suns", "nextCoinID", error);
    EntityIDs(document, "mowers", "nextMowerID", error);
    return error.empty();
}
