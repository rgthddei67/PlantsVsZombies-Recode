#include "Game/Workshop/Workshop.h"
#include <cstdlib>
#include <iostream>

using namespace Workshop;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED workshop: " << message << '\n'; std::exit(1); }
}
void Step(World& world, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) {
        world.Tick(1.0f / 60.0f);
        if (world.NextSpawnRow() >= 0) world.CommitSpawn();
    }
}
void Supply(World& world) {
    for (int row = 0; row < Rows; ++row) {
        Check(world.Build(row * MaxColumns + 2, Machine::Grower, 0), "build grower");
        Check(world.Build(row * MaxColumns + 3, Machine::Belt, 0), "build belt");
        Check(world.Build(row * MaxColumns + 4, Machine::Feeder, 0), "build feeder");
    }
    Check(world.Build(2 * MaxColumns, Machine::Solar, 0), "third solar");
    Check(world.Build(3 * MaxColumns, Machine::Solar, 0), "fourth solar");
}
// 纯逻辑测试只验证出兵事务；真实击杀、动画与啃食由可见 AutoTest 验证。
void FinishSchedule(World& world) {
    for (int i = 0; i < 12000 && world.RemainingSpawns() > 0; ++i) {
        world.Tick(1.0f / 60.0f);
        if (world.NextSpawnRow() >= 0) world.CommitSpawn();
    }
    world.CompleteAssault();
}
void ClearWave(World& world) {
    const int previous = world.CompletedWaves();
    Step(world, 20);
    Check(world.StartAssault(), "start assault");
    FinishSchedule(world);
    Check(world.State() == Phase::Planning && world.CompletedWaves() == previous + 1, "Board clear acknowledgement advances progression");
}
}
int main() {
    World world;
    Check(!world.Build(CellCount, Machine::Belt, 0), "reject out-of-range build");
    Check(!world.Build(8, Machine::Belt, 0), "reject unexpanded land");
    Check(!world.Build(2, Machine::Latch, 0), "unlock progression isolated");
    Supply(world); Step(world, 5);
    Check(world.Delivered() > 0 && world.Fired() == 0, "transport supplies magazines during planning");
    Check(world.Lanes()[4].ammo > 0, "bottom lane receives supply");
    const auto save = world.Save(); World restored; std::string error;
    Check(restored.Load(save, error) && restored.Save() == save, "exact planning round trip");
    Step(world, 4); Step(restored, 4);
    Check(restored.Save() == world.Save(), "restored transport continues identically");
    auto bad = save; bad["state"]["cells"][0]["inputs"][0] = 999;
    const auto unchanged = restored.Save();
    Check(!restored.Load(bad, error) && restored.Save() == unchanged, "bad wire rejected transactionally");
    bad = save; bad["schemaVersion"] = 2;
    Check(!restored.Load(bad, error) && restored.Save() == unchanged, "future schema protected");
    Check(world.StartAssault(), "start save test"); Step(world, 3);
    const auto assault = world.Save(); Check(restored.Load(assault, error), "load in-flight combat");
    Step(world, 9); Step(restored, 9);
    Check(world.Save() == restored.Save(), "factory and spawn countdown round trip");
    FinishSchedule(world); Check(world.CompletedWaves() == 1 && world.Unlocked(Machine::Splitter), "wave completion unlocks logistics");
    ClearWave(world); Check(world.Unlocked(Machine::Latch), "second wave unlocks circuits");
    Step(world, 100);
    Check(world.Build(5, Machine::Switch, 0), "build control switch");
    Check(world.Build(6, Machine::Not, 0), "build not gate");
    Check(world.Connect(5, 6, 0), "wire inverter");
    Check(world.Connect(6, 2, 0), "wire grower control");
    Step(world, 1); Check(!world.Enabled(2), "inverted high stops production");
    Check(world.Configure(5), "toggle switch"); Step(world, 1);
    Check(world.Enabled(2), "low through inverter resumes production");
    Check(world.Build(7, Machine::Latch, 0), "build latch");
    Check(world.Connect(6, 7, 0), "connect latch set"); Step(world, 1);
    Check(world.Cells()[7].signal, "latch stores set");
    Check(world.Connect(-1, 7, 0), "disconnect latch input"); Step(world, 1);
    Check(world.Cells()[7].signal, "latch retains unconnected memory");
    Check(restored.Load(world.Save(), error) && restored.Cells()[7].signal, "latch survives save");
    Check(world.Remove(6), "remove inverter");
    Check(world.Cells()[2].inputs[0] == -1, "removal disconnects dangling wire");
    ClearWave(world); Check(world.Expand() && world.Columns() == 10, "third wave expands factory");

    World empty;
    const auto checkpoint = empty.Save();
    Check(empty.StartAssault(), "start unarmed attack");
    Check(!empty.ReserveAmmo(0), "empty magazine cannot reserve attack");
    empty.RecordBreach(); empty.RecordBreach(); empty.RecordBreach();
    Check(empty.State() == Phase::Dead && empty.Health() == 0 && empty.Fired() == 0, "no ammo means no firing and defeat");
    Check(restored.Load(empty.Save(), error) && restored.State() == Phase::Dead, "dead base survives reload");
    Check(restored.Retry() && restored.Save() == checkpoint, "retry exactly restores pre-assault base");
    restored.Reset(); Check(restored.Save() == World().Save(), "reset only creates fresh independent state");

    // 一个运输节拍内不能级联穿过整条带；堵塞不得复制或遗失弹药。
    World transport; Check(transport.Build(2, Machine::Grower, 0), "transport grower");
    for (int i = 3; i <= 7; ++i) Check(transport.Build(i, Machine::Belt, 0), "long belt");
    Step(transport, GrowPeriod + 0.1f);
    Check(transport.Produced() == 1 && transport.Cells()[7].stock == 0, "produced pea cannot instantly cross the line");
    Step(transport, 20);
    int stock = 0; for (const auto& cell : transport.Cells()) stock += cell.stock;
    Check(stock == transport.Produced() && stock == 10, "blocked line conserves all produced ammo");
    Check(transport.Status(7) == "出口堵塞", "blocked outlet is observable");

    World circuits;
    auto circuitFixture = circuits.Save(); circuitFixture["state"]["completedWaves"] = 2;
    circuitFixture["state"]["energy"] = 1000;
    Check(circuits.Load(circuitFixture, error), "load isolated unlocked circuit fixture");
    Check(circuits.Build(2, Machine::Switch, 0) && circuits.Build(3, Machine::Switch, 0), "logic inputs");
    Check(circuits.Build(4, Machine::And, 0) && circuits.Build(5, Machine::Or, 0), "binary gates");
    for (int gate : {4, 5}) {
        Check(circuits.Connect(2, gate, 0) && circuits.Connect(3, gate, 1), "two-port wiring");
    }
    Step(circuits, 1); Check(circuits.Cells()[4].signal && circuits.Cells()[5].signal, "both high");
    Check(circuits.Configure(2), "lower A"); Step(circuits, 1);
    Check(!circuits.Cells()[4].signal && circuits.Cells()[5].signal, "AND and OR differ for mixed inputs");
    Check(circuits.Build(6, Machine::Timer, 0), "timer"); Step(circuits, 2.5f);
    Check(circuits.Cells()[6].signal, "timer high half period");
    Check(circuits.Connect(2, 6, 0), "disable timer"); Step(circuits, 1);
    Check(!circuits.Cells()[6].signal && circuits.Cells()[6].clock == 0, "timer stops on low input");
    Check(circuits.Build(7, Machine::Not, 0) && circuits.Connect(7, 7, 0), "feedback circuit");
    Step(circuits, 10); Check(restored.Load(circuits.Save(), error), "cyclic circuit is finite and serializable");

    World split;
    auto splitFixture = split.Save(); splitFixture["state"]["completedWaves"] = 1;
    Check(split.Load(splitFixture, error), "splitter fixture");
    Check(split.Build(2, Machine::Grower, 0) && split.Build(3, Machine::Splitter, 0), "splitter supply");
    Check(split.Build(4, Machine::Store, 0) && split.Build(15, Machine::Store, 0), "front and right outputs");
    Step(split, 12);
    const int forward = split.Cells()[4].stock, side = split.Cells()[15].stock;
    Check(forward > 0 && side > 0 && std::abs(forward - side) <= 1, "unwired splitter balances both exits");
    stock = 0; for (const auto& cell : split.Cells()) stock += cell.stock;
    Check(stock == split.Produced(), "splitter conserves ammo");
    bad = circuits.Save(); bad["state"]["cells"][4]["inputs"][0] = 2.5;
    Check(!restored.Load(bad, error), "reject fractional wire index");
    bad = empty.Save(); bad["checkpoint"] = nullptr;
    Check(!restored.Load(bad, error), "dead save requires recoverable checkpoint");
    std::cout << "Workshop production, spawn transactions, signals, persistence and recovery passed\n";
}
