#include "Game/Board/RoofRunoff.h"
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
    TestWaveCounts();
    std::cout << "Board domain transitions and legacy persistence passed\n";
}
