#include "Board.h"
#include "Game/AdventureProgression.h"
#include "Game/Zombie/Zombie.h"
#include "DeltaTime.h"
#include <cmath>
#include <nlohmann/json.hpp>

namespace {
constexpr float kOpeningLock11_8 = 120.0f; // 11-8 开局三台设备共同锁定的游戏秒
constexpr float kOpeningLock11_9 = 180.0f; // 11-9 开局三台设备共同锁定的游戏秒

/** 返回指定冒险关的开幕锁定时长；其他关卡不注入天气。 */
float OpeningLockSeconds(int level) {
    return level == 98 ? kOpeningLock11_8 : level == 99 ? kOpeningLock11_9 : 0.0f;
}
}

void Board::InitializeStationOpeningWeather() {
    if (!IsWeatherStation() || mIsSurvival || mIsLoadSave) return;
    const float duration = OpeningLockSeconds(mLevel);
    if (duration <= 0.0f) return;
    mWeatherStation.controls[WeatherStationRules::RAIN].value = static_cast<int>(RainIntensity::HEAVY);
    mWeatherStation.controls[WeatherStationRules::FOG].value = static_cast<int>(FogWeatherIntensity::DENSE) + 1;
    mWeatherStation.controls[WeatherStationRules::CHARGE].value = 1;
    // 复用双方及预测共用的保护期，计时仅在实战推进；到期只解锁，不自动关闭设备。
    for (auto& control : mWeatherStation.controls) control.protection = duration;
    ++mWeatherStation.revision;
    ApplyStationEnvironment();
}

bool Board::IsStationDeviceUnlocked(int device) const {
    return IsWeatherStation() && device>=0 && device<WeatherStationRules::COUNT
        && AdventureProgression::GetLevelNumberInArea(mLevel)>=device+1;
}

bool Board::CanChangeStationControl(int device,int value,bool player) const {
    return IsStationDeviceUnlocked(device) && mBoardState==BoardState::GAME
        && !mTrophySpawned && !DeltaTime::IsPaused()
        && WeatherStationRules::CanChange(mWeatherStation.controls[device],device,value)
        && (player ? mColdStorage.playerIce : mColdStorage.enemyIce)>=WeatherStationRules::Cost(device,value);
}

bool Board::TryChangeStationControl(int device,int value,bool player) {
    if (!CanChangeStationControl(device,value,player)) return false;
    const int cost=WeatherStationRules::Cost(device,value);
    (player ? mColdStorage.playerIce : mColdStorage.enemyIce)-=cost;
    if (!player) { mColdStorage.spent+=cost; mColdStorage.incomeWindow.push_back({mColdStorage.elapsed,0,cost}); }
    auto& control=mWeatherStation.controls[device];
    control.pending=value; control.warning=WeatherStationRules::WarningSeconds; control.player=player;
    ++mWeatherStation.revision;
    if(!player) mWeatherStation.enemyUsedMask|=1u<<device;
	if (!UsesRandomColdStorageSpawns()) mColdStorage.decisionRemaining=0; // 只有战术模式需因环境重评；随机出兵保持节奏。
    return true;
}

/** 更新设备事务，付款不重入；切换边沿只调用一次既有天气展示与倍率入口。 */
void Board::UpdateWeatherStation(float seconds) {
    if (!IsWeatherStation() || mBoardState!=BoardState::GAME || mTrophySpawned || seconds<=0) return;
    bool changed=false;
    for (auto& control:mWeatherStation.controls) changed=WeatherStationRules::Advance(control,seconds)||changed;
    if (changed) {
        ++mWeatherStation.revision;
        ApplyStationEnvironment();
        if (!UsesRandomColdStorageSpawns()) mColdStorage.decisionRemaining=0;
    }
}

void Board::ApplyStationEnvironment() {
    if (!IsWeatherStation()) return;
    const auto rain=static_cast<RainIntensity>(mWeatherStation.controls[WeatherStationRules::RAIN].value);
    if (rain!=mRainIntensity) {
        if (rain==RainIntensity::CLEAR) EndRain();
        else BeginRain(rain,86400.0f,false,false); // 设备维持状态，时长只供既有雨效使用。
    }
    const int fog=mWeatherStation.controls[WeatherStationRules::FOG].value;
    mFogWeather.ApplyDeviceControl(SupportsStageFog(),
        static_cast<FogWeatherIntensity>(std::max(0,fog-1)));
    mWeatherForecastReady=false;
}

float Board::GetStationFogMoveMultiplier(const Zombie* zombie) const {
    return IsWeatherStation() && zombie && !zombie->IsMindControlled() && IsZombieObscuredByFog(zombie)
        ? WeatherStationRules::FogMoveMultiplier : 1.0f;
}

/** 全部出生入口共享排除表，避免免费召唤绕过章节兵池承诺。 */
bool Board::IsStationZombieAllowed(ZombieType type) const {
    if (!IsWeatherStation()) return true;
    switch(type) {
    case ZombieType::ZOMBIE_ROOF_MARSHAL: case ZombieType::ZOMBIE_ELITE_DANCER:
    case ZombieType::ZOMBIE_POOL_NORMAL: case ZombieType::ZOMBIE_POOL_CONE: case ZombieType::ZOMBIE_POOL_BUCKET:
    case ZombieType::ZOMBIE_DOLPHIN_RIDER: case ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER:
    case ZombieType::ZOMBIE_BOBSLED_TEAM: case ZombieType::ZOMBIE_ICE_WALL_ENGINEER:
    case ZombieType::ZOMBIE_ICE_CRACK_DRILL: case ZombieType::ZOMBIE_SNOW_BURROW:
    case ZombieType::ZOMBIE_EXCAVATOR: case ZombieType::ZOMBIE_ELITE_CATAPULT:
    case ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER: return false;
    default: return type>=ZombieType::ZOMBIE_NORMAL && type<ZombieType::NUM_ZOMBIE_TYPES;
    }
}

/** 保存真实设备状态，包含所有已付款指令；不保存可从场景推导的可用性。 */
nlohmann::json Board::SaveWeatherStation() const {
    auto result=nlohmann::json::object();
    if (!IsWeatherStation()) return result;
    result["revision"]=mWeatherStation.revision;
    result["enemyUsedMask"]=mWeatherStation.enemyUsedMask;
    result["controls"]=nlohmann::json::array();
    for (const auto& c:mWeatherStation.controls)
        result["controls"].push_back({{"value",c.value},{"pending",c.pending},{"warning",c.warning},
            {"protection",c.protection},{"player",c.player}});
    return result;
}

/** 读档只恢复剩余事务，不再次扣款；非法或未解锁设备恢复关闭。 */
void Board::LoadWeatherStation(const nlohmann::json& value) {
    mWeatherStation={};
    if (!IsWeatherStation() || !value.is_object()) return;
    mWeatherStation.revision=value.value("revision",0u);
    mWeatherStation.enemyUsedMask=value.value("enemyUsedMask",0u)&7u;
    if (!value.contains("controls") || !value["controls"].is_array()) return;
    for (int d=0;d<WeatherStationRules::COUNT && d<static_cast<int>(value["controls"].size());++d) {
        const auto& saved=value["controls"][d];
        if (!IsStationDeviceUnlocked(d) || !saved.is_object()) continue;
        auto& c=mWeatherStation.controls[d];
        c.value=std::clamp(saved.value("value",0),0,WeatherStationRules::Count(d)-1);
        c.pending=std::clamp(saved.value("pending",-1),-1,WeatherStationRules::Count(d)-1);
        auto duration=[&](const char* key,float maximum) { const float v=saved.value(key,0.0f); return std::isfinite(v) ? std::clamp(v,0.0f,maximum) : 0.0f; };
        c.warning=duration("warning",WeatherStationRules::WarningSeconds);
        // 开幕锁定也保存在既有保护期中，不能被普通切换的 30 秒上限截短。
        c.protection=duration("protection",std::max(WeatherStationRules::ProtectionSeconds,
            mIsSurvival ? 0.0f : OpeningLockSeconds(mLevel)));
        c.player=saved.value("player",false);
    }
}
