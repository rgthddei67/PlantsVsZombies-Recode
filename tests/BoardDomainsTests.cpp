#include "Game/Board/RoofRunoff.h"
#include "Game/Board/FogWeather.h"
#include "Game/Board/WaveSpawnCounts.h"
#include "GameRandom.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
    void Check(bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    }

    /** 不构造场景/资源即可验证环境阶段、冻结、旧档字段和恢复不消费随机数。 */
    void TestRunoff() {
        RoofRunoff runoff;
        Check(!runoff.Advance(10, true, 5, RainIntensity::LIGHT) && runoff.Charge() == 10, "light accumulation");
        Check(!runoff.Advance(0, true, 5, RainIntensity::HEAVY) && runoff.Charge() == 10, "pause freezes state");
        runoff.Restore(true, 5, 100, RoofRunoffPhase::WARNING, 10, 1.0f, 45);
        nlohmann::json saved;
        runoff.Save(saved);
        Check(saved == nlohmann::json({{"roofRunoffCharge",100.0f},{"roofRunoffRetainedCharge",45.0f},
            {"roofRunoffPhase",1},{"roofRunoffPhaseTimer",1.0f},{"roofRunoffRowMask",10}}), "legacy save representation");
        GameRandom::SetSeed(42);
        const int next = GameRandom::Range(0,100000);
        GameRandom::SetSeed(42);
        RoofRunoff restored;
        restored.Load(saved, true, 5);
        Check(GameRandom::Range(0,100000) == next, "load must not consume random numbers");
        Check(!restored.Advance(1, true, 5, RainIntensity::CLEAR)
            && restored.Phase() == RoofRunoffPhase::FLOWING && restored.RowMask() == 10, "warning commits original rows");
        restored.Advance(3, true, 5, RainIntensity::HEAVY);
        Check(restored.Phase() == RoofRunoffPhase::IDLE && restored.Charge() == 45
            && restored.RetainedCharge() == 0 && restored.RowMask() == 0, "flow settles retained charge once");
        restored.Load(saved, false, 5);
        Check(restored.Charge() == 0 && restored.RowMask() == 0, "unsupported terrain neutralizes saved runoff");
        restored.Restore(true,5,100,RoofRunoffPhase::WARNING,0,1,45);
        Check(restored.Charge() == 0 && restored.Phase() == RoofRunoffPhase::IDLE, "empty locked rows rejected");
        restored.Restore(true,5,std::numeric_limits<float>::quiet_NaN(),RoofRunoffPhase::IDLE,0,0,0);
        Check(restored.Charge() == 0, "nonfinite saved state rejected");
        restored.Load(nlohmann::json::object(), true, 5);
        Check(restored.Charge() == 0, "missing legacy fields are neutral");
        restored.Restore(true,5,100,RoofRunoffPhase::IDLE,0,0,0);
        Check(restored.Advance(.01f,true,5,RainIntensity::LIGHT), "full accumulation requests one lock");
        restored.LockRows(5,4);
        const auto mask = restored.RowMask();
        const auto retained = restored.RetainedCharge();
        Check(restored.RowSelected(4) && restored.RowCount() >= 1 && restored.RowCount() <= 3, "guide replaces within chosen group");
        restored.LockRows(5,0);
        Check(restored.RowMask() == mask && restored.RetainedCharge() == retained, "committed lock cannot reroll");
    }

    /** 检查随机调用次序、已提交预报、风致回流和旧 JSON，防止拆分改变未来事件。 */
    void TestFogWeather() {
        using Fog = FogWeatherIntensity;
        GameRandom::SetSeed(42);
        const float initial = GameRandom::Range(45.0f, 70.0f);
        const int roll = GameRandom::Range(1, 100);
        const Fog expected = roll <= 45 ? Fog::DENSE : (roll <= 73 ? Fog::SMALL : Fog::NORMAL);
        const float elevated = GameRandom::Range(35.0f, 55.0f);
        const float recovery = GameRandom::Range(50.0f, 80.0f);
        const int nextRandom = GameRandom::Range(0, 100000);
        GameRandom::SetSeed(42);
        FogWeather fog;
        fog.Initialize(true);
        fog.Initialize(true);
        Check(fog.Timer() == initial, "fog initialization is idempotent");
        Check(!fog.AdvanceWeather(initial - 15.0f, 0) && fog.HasForecast()
            && fog.ActualForecast() == expected, "forecast locks at lead window");
        fog.DisruptForecast();
        Check(fog.ForecastDisrupted() && !fog.HasForecast(), "disruption only hides forecast");
        nlohmann::json saved;
        fog.Save(saved);
        Check(saved.size() == 9 && saved["fogWeatherForecastReady"] == true
            && saved["fogWeatherForecastDisrupted"] == true, "legacy forecast representation");
        FogWeather restored;
        restored.Load(saved, true);
        Check(restored.ForecastDisrupted() && restored.ActualForecast() == expected,
            "load preserves hidden committed result");
        Check(!restored.AdvanceWeather(0, 1) && restored.Timer() == 15,
            "paused fog cannot reveal or reroll");
        Check(restored.AdvanceWeather(15, 1) && restored.Intensity() == expected
            && restored.Timer() == elevated && !restored.HasForecast(), "reveal honors original commitment");
        Check(restored.AdvanceWeather(elevated + 100, 0) && restored.Intensity() == Fog::DEFAULT
            && restored.Timer() == recovery, "one phase per step, no overshoot carry");
        Check(GameRandom::Range(0, 100000) == nextRandom,
            "initialize, commit, restore and reveal preserve random consumption");

        restored.SetFixedWeather(Fog::DENSE, 60, true);
        Check(!restored.AdvanceDispersal(1, TyphoonStrength::SEVERE,
            WindDirection::TOWARD_HOUSE, true, 0) && restored.Dispersal() == 0
            && restored.VisualOffsetX() == -36, "weaker wind drifts without clearing fog");
        Check(restored.AdvanceDispersal(4, TyphoonStrength::SUPER,
            WindDirection::TOWARD_FRONT, true, 0) && restored.Intensity() == Fog::DEFAULT
            && restored.Dispersal() == 1 && restored.VisualOffsetX() == 180,
            "super typhoon ends elevated fog exactly at full dispersal");
        Check(!restored.AdvanceDispersal(4, TyphoonStrength::SUPER,
            WindDirection::TOWARD_FRONT, true, 0), "full dispersal does not restart recovery");
        restored.AdvanceDispersal(20, TyphoonStrength::NONE, WindDirection::NONE, false, 0);
        Check(restored.Dispersal() == 0 && restored.VisualOffsetX() == 0, "fog returns after wind");
        restored.SetDispersal(.42f);
        restored.SetFixedWeather(Fog::DENSE, 86400, false);
        Check(restored.Dispersal() == .42f && restored.Timer() == 86400,
            "fixed weather enforcement preserves dispersal");
        restored.SetForecast(Fog::SMALL, Fog::NORMAL, 12);
        restored.ApplyDeviceControl(true, Fog::DENSE);
        Check(!restored.HasForecast() && restored.Timer() == 12 && restored.Dispersal() == .42f,
            "device takeover preserves timer and drift");
        Check(!restored.SetDispersal(std::numeric_limits<float>::quiet_NaN())
            && restored.Dispersal() == .42f, "invalid test dispersal rejected");
        restored.AdvanceAnimation(20);
        restored.Save(saved);
        FogWeather roundtrip;
        roundtrip.Load(saved, true);
        nlohmann::json resaved;
        roundtrip.Save(resaved);
        // 设备接管会留下不可见的旧预报值；读档按既有规则将未锁定预报归零。
        saved["forecastFogWeatherIntensity"] = 0;
        saved["actualForecastFogWeatherIntensity"] = 0;
        Check(resaved == saved && roundtrip.AnimationTime() == 0,
            "legacy normalization and transient animation clock");
        roundtrip.Load(saved, false);
        Check(!roundtrip.Initialized() && roundtrip.Intensity() == Fog::DEFAULT
            && roundtrip.Dispersal() == 0 && roundtrip.Timer() == 0,
            "unsupported map clears loaded state");
        FogWeather legacy;
        legacy.Load(nlohmann::json::object(), true);
        Check(!legacy.Initialized(), "old save without fog waits for normal initialization");
        legacy.Load({{"fogWeatherIntensity",99},{"fogWeatherForecastReady",true},
            {"forecastFogWeatherIntensity",1},{"fogWeatherTimer",-5},
            {"fogDispersal",2},{"fogVisualOffsetX",999}}, true);
        Check(legacy.Initialized() && legacy.Intensity() == Fog::DEFAULT
            && !legacy.HasForecast() && legacy.Timer() == 0 && legacy.Dispersal() == 1
            && legacy.VisualOffsetX() == 180, "invalid enum, missing truth and range clamps");
        legacy.Initialize(false);
        Check(!legacy.Initialized() && legacy.Dispersal() == 0, "map reset removes fog state");
    }

    /** 以外部存档表示检验整批重置、历史兼容、关卡门禁与失败预留撤销。 */
    void TestWaveCounts() {
        WaveSpawnCounts counts;
        counts.Record(ZombieType::ZOMBIE_REINFORCED_DOOR);
        counts.Record(ZombieType::ZOMBIE_REINFORCED_DOOR);
        counts.CancelReservation(ZombieType::ZOMBIE_REINFORCED_DOOR);
        nlohmann::json saved;
        counts.Save(saved);
        Check(saved.size() == 27 && saved["reinforcedDoorsSpawnedThisWave"] == 1, "all legacy keys saved together");
        counts.Reset();
        nlohmann::json empty;
        counts.Save(empty);
        for (const auto& value : empty) Check(value == 0, "every type resets at wave boundary");
        for (auto& value : saved) value = 999;
        counts.Load(saved,64,false);
        Check(counts.Get(ZombieType::ZOMBIE_SNOW_BURROW) == 1, "tutorial snow cap");
        Check(counts.Get(ZombieType::ZOMBIE_REDEYE_GARGANTUAR) == 3, "adventure redeye cap");
        Check(counts.Get(ZombieType::ZOMBIE_POLAR_CLOCKMAKER) == 3, "retain existing clockmaker restore semantics");
        counts.Load(saved,68,false);
        Check(counts.Get(ZombieType::ZOMBIE_THERMAL_SNIPER) == 3, "tutorial sniper cap");
        counts.Load(saved,69,true);
        Check(counts.Get(ZombieType::ZOMBIE_SNOW_BURROW) == 2
            && counts.Get(ZombieType::ZOMBIE_THERMAL_SNIPER) == 4
            && counts.Get(ZombieType::ZOMBIE_REDEYE_GARGANTUAR) == 0, "contextual restore limits");
        counts.Load({{"eliteDancerSpawnedThisTyphoon",true},{"reinforcedDoorsSpawnedThisWave",-2}},1,false);
        Check(counts.Get(ZombieType::ZOMBIE_ELITE_DANCER) == 1
            && counts.Get(ZombieType::ZOMBIE_REINFORCED_DOOR) == 0
            && counts.Get(ZombieType::ZOMBIE_THERMAL_SNIPER) == 0, "legacy dancer and missing/negative fields");
    }
}

int main() {
    TestRunoff();
    TestFogWeather();
    TestWaveCounts();
    std::cout << "Board domain transitions and legacy persistence passed\n";
}
