#include "Game/Workshop/Workshop.h"
#include <cstdlib>
#include <cmath>
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
    if (world.State() == Phase::Planning) Check(world.StartAssault(), "start assault");
    FinishSchedule(world);
    Check(world.State() == Phase::Planning && world.CompletedWaves() == previous + 1, "Board clear acknowledgement advances progression");
}
}
int main() {
    World world;
    Check(!world.Build(CellCount, Machine::Belt, 0), "reject out-of-range build");
    Check(!world.Build(10, Machine::Belt, 0), "reject unexpanded land");
    Check(!world.Build(2, Machine::Latch, 0), "unlock progression isolated");
    Supply(world);
    const auto prepared = world.Save(); Step(world, 600);
    Check(world.Save() == prepared, "planning does not advance resources or clocks");
    const auto save = world.Save(); World restored; std::string error;
    Check(restored.Load(save, error) && restored.Save() == save, "exact planning round trip");
    Step(world, 4); Step(restored, 4);
    Check(restored.Save() == world.Save(), "restored transport continues identically");
    auto bad = save; bad["state"]["cells"][0]["inputs"][0] = 999;
    const auto unchanged = restored.Save();
    Check(!restored.Load(bad, error) && restored.Save() == unchanged, "bad wire rejected transactionally");
    bad = save; bad["schemaVersion"] = 3;
    Check(!restored.Load(bad, error) && restored.Save() == unchanged, "future schema protected");
    Check(world.StartAssault(), "start save test"); Step(world, 4);
    Check(world.Delivered() > 0 && world.Lanes()[4].ammo > 0, "empty magazines receive first supply after assault begins");
    const auto assault = world.Save(); Check(restored.Load(assault, error), "load in-flight combat");
    Step(world, 9); Step(restored, 9);
    Check(world.Save() == restored.Save(), "factory and spawn countdown round trip");
    FinishSchedule(world); Check(world.CompletedWaves() == 1 && world.Unlocked(Machine::Splitter), "wave completion unlocks logistics");
    ClearWave(world); Check(world.Unlocked(Machine::Latch), "second wave unlocks circuits");
    Check(world.StartAssault(), "run circuit wave");
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
    ClearWave(world); Check(world.Expand() && world.Columns() == 12, "third wave expands factory");

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
    Check(transport.StartAssault(), "run transport");
    Step(transport, GrowPeriod + 0.1f);
    Check(transport.Produced() == 1 && transport.Cells()[7].stock == 0, "produced pea cannot instantly cross the line");
    Step(transport, 30);
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
    Check(circuits.StartAssault(), "run circuits");
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
    Check(split.Build(4, Machine::Store, 0) && split.Build(MaxColumns + 3, Machine::Store, 0), "front and right outputs");
    Check(split.StartAssault(), "run splitter");
    Step(split, 12);
    const int forward = split.Cells()[4].stock, side = split.Cells()[MaxColumns + 3].stock;
    Check(forward > 0 && side > 0 && std::abs(forward - side) <= 1, "unwired splitter balances both exits");
    stock = 0; for (const auto& cell : split.Cells()) stock += cell.stock;
    Check(stock == split.Produced(), "splitter conserves ammo");
    bad = circuits.Save(); bad["state"]["cells"][4]["inputs"][0] = 2.5;
    Check(!restored.Load(bad, error), "reject fractional wire index");
    bad = empty.Save(); bad["checkpoint"] = nullptr;
    Check(!restored.Load(bad, error), "dead save requires recoverable checkpoint");

    World upgrades;
    Check(!upgrades.Upgrade(0), "second level locked before wave two");
    auto fixture = upgrades.Save(); fixture["state"]["completedWaves"] = 2; fixture["state"]["energy"] = 1000;
    Check(upgrades.Load(fixture, error), "upgrade fixture");
    Check(upgrades.Build(2, Machine::Grower, 0) && upgrades.Build(3, Machine::Store, 0), "upgradable production line");
    Check(upgrades.StartAssault(), "run upgrade production");
    Step(upgrades, 1);
    const auto beforeUpgrade = upgrades.Save();
    Check(upgrades.Upgrade(2), "buy grower level two");
    Check(std::abs(upgrades.Energy() - (beforeUpgrade["state"]["energy"].get<float>() - 75)) < 0.001f, "exact upgrade payment");
    Check(std::abs(upgrades.Cells()[2].clock / GrowPeriods[1] - beforeUpgrade["state"]["cells"][2]["clock"].get<float>() / GrowPeriods[0]) < 0.001f,
        "upgrade retains production fraction");
    Check(upgrades.Cells()[2].stock == beforeUpgrade["state"]["cells"][2]["stock"], "upgrade does not create ammo");
    Check(!upgrades.Upgrade(2), "third level locked before wave five");
    Check(upgrades.Upgrade(0) && upgrades.Upgrade(3), "solar and storage level two");
    const float solarBefore = upgrades.Energy();
    Check(upgrades.Remove(2), "remove grower before measuring solar");
    Step(upgrades, 10);
    Check(std::abs(upgrades.Energy() - solarBefore - 150 - 37.5f) < 0.1f, "level two solar produces 2.25 per second plus base 1.5");
    FinishSchedule(upgrades);
    fixture = upgrades.Save(); fixture["state"]["completedWaves"] = 5;
    fixture["state"]["cells"][3]["stock"] = 120;
    Check(upgrades.Load(fixture, error) && upgrades.Upgrade(3), "full level two warehouse upgrades without losing stock");
    Check(upgrades.Cells()[3].stock == 120 && Capacity(Machine::Store, 3) == 200, "warehouse capacity and stock");
    const float refundBefore = upgrades.Energy();
    Check(!upgrades.Upgrade(3), "max level cannot charge again");
    Check(upgrades.Remove(3) && std::abs(upgrades.Energy() - refundBefore - 160) < 0.001f, "refund building plus both upgrades");
    fixture = upgrades.Save(); fixture["state"]["energy"] = 0;
    Check(upgrades.Load(fixture, error), "empty wallet fixture");
    const auto emptyWallet = upgrades.Save();
    Check(!upgrades.Upgrade(0) && upgrades.Save() == emptyWallet, "insufficient energy is transactional");
    bad = emptyWallet; bad["state"]["cells"][0]["level"] = 4;
    Check(!upgrades.Load(bad, error) && upgrades.Save() == emptyWallet, "invalid level rejected transactionally");
    bad = emptyWallet; bad["state"]["cells"][2]["level"] = 2;
    Check(!upgrades.Load(bad, error), "empty cell cannot carry upgrades");
    auto legacy = World().Save();
    for (auto& c : legacy["state"]["cells"]) c.erase("level");
    Check(upgrades.Load(legacy, error) && upgrades.Save() == World().Save(), "old workshop saves start at level one");
    std::array<int, 3> production{};
    for (int level = 1; level <= 3; ++level) {
        World line;
        fixture = line.Save(); fixture["state"]["completedWaves"] = 5; fixture["state"]["energy"] = 1000;
        Check(line.Load(fixture, error) && line.Build(2, Machine::Grower, 0) && line.Build(3, Machine::Feeder, 0), "rate fixture");
        for (int l = 1; l < level; ++l) Check(line.Upgrade(2), "rate upgrade");
        Check(line.StartAssault(), "run rate fixture");
        Step(line, 20.1f); production[level - 1] = line.Produced();
        Check(restored.Load(line.Save(), error) && restored.Save() == line.Save(), "upgraded production round trip");
    }
    Check(production[0] == 8 && production[1] == 10 && production[2] == 12, "actual production follows 2.5 / 2.0 / 1.6 second periods");
    // 冷加工守恒、远程路线与混合弹仓：任何阶段均不能用另一种弹药代付。
    World cold;
    fixture = cold.Save(); fixture["state"]["completedWaves"] = 5; fixture["state"]["energy"] = 1000;
    Check(cold.Load(fixture, error), "cold fixture");
    Check(cold.Build(142, Machine::Cooler, 0) && cold.Build(143, Machine::Feeder, 0), "cold line beyond frontline rows");
    Check(cold.SetRoute(143, 4) && cold.SetFilter(143, 1), "explicit typed remote delivery");
    fixture = cold.Save(); fixture["state"]["cells"][142]["stock"] = 5;
    Check(cold.Load(fixture, error), "five input peas");
    Check(cold.StartAssault(), "run cold production");
    const float coldEnergy = cold.Energy(); Step(cold, 13.25f);
    Check(cold.Lanes()[4].iceAmmo == 5 && cold.Lanes()[4].ammo == 0 && cold.Cells()[142].TotalStock() == 0, "one pea becomes one ice pea");
    Check(std::abs(cold.Energy() - coldEnergy - (13.25f * 3 - 5 * CoolingEnergy)) < 0.1f, "cold energy accounting");
    Check(cold.SetQuota(4, 80) && cold.Lanes()[4].iceAmmo == 5, "quota change preserves existing inventory");
    Check(!cold.ReserveAmmo(4, Ammo::Pea) && cold.ReserveAmmo(4, Ammo::Ice), "no ammo type substitution");
    Check(cold.Upgrade(142) && cold.Upgrade(142), "cooler shares three level upgrades");
    Check(restored.Load(cold.Save(), error) && restored.Save() == cold.Save(), "typed stock round trip");
    fixture = cold.Save(); fixture["state"]["cells"][143]["iceStock"] = 1;
    Check(cold.Load(fixture, error), "over-quota feeder fixture"); Step(cold, 1);
    Check(cold.Cells()[143].iceStock == 1 && cold.Lanes()[4].iceAmmo == 4, "over-quota stops new input");
    Check(cold.SetQuota(4, 60) && cold.SetFilter(143, 0), "switch feeder type without discarding cargo"); Step(cold, 1);
    Check(cold.Cells()[143].iceStock == 1 && cold.Lanes()[4].iceAmmo == 4, "nonmatching cargo waits after feeder filter change");
    Check(cold.SetFilter(143, 1), "restore feeder type"); Step(cold, 1);
    Check(cold.Cells()[143].iceStock == 0 && cold.Lanes()[4].iceAmmo == 5, "retained cargo delivers after filter restored");
    fixture = cold.Save(); fixture["state"]["lanes"][4]["ammo"] = 77;
    Check(!restored.Load(fixture, error), "shared eighty round capacity enforced");

    World sorting;
    fixture = sorting.Save(); fixture["state"]["completedWaves"] = 3;
    Check(sorting.Load(fixture, error), "sort fixture");
    Check(sorting.Build(3, Machine::Splitter, 0) && sorting.Build(4, Machine::Store, 0)
        && sorting.Build(23, Machine::Store, 0), "typed splitter outlets");
    Check(sorting.SetFilter(3, 1) && sorting.SetFilter(4, 0) && sorting.SetFilter(23, 1), "typed splitter and warehouses");
    fixture = sorting.Save(); fixture["state"]["cells"][3]["stock"] = 1; fixture["state"]["cells"][3]["iceStock"] = 1;
    Check(sorting.Load(fixture, error) && sorting.StartAssault(), "mixed cargo fixture"); Step(sorting, 1);
    Check(sorting.Cells()[4].stock == 1 && sorting.Cells()[4].iceStock == 0 && sorting.Cells()[23].stock == 0
        && sorting.Cells()[23].iceStock == 1, "filter sends ice to side and pea forward");
    Check(sorting.SetFilter(23, 0), "change warehouse acceptance");
    fixture = sorting.Save(); fixture["state"]["cells"][3]["iceStock"] = 1;
    Check(sorting.Load(fixture, error), "blocked filter fixture"); Step(sorting, 1);
    Check(sorting.Cells()[3].iceStock == 1 && sorting.Cells()[4].iceStock == 0, "blocked sorted output never contaminates forward line");

    // 带库存与半周期进度的准备期、读档和死亡继续均不得靠等待获益。
    Check(cold.Build(141, Machine::Grower, 0) && cold.Build(144, Machine::Timer, 0), "planning freeze fixture");
    FinishSchedule(cold);
    const auto betweenWaves = cold.Save(); Step(cold, 600);
    Check(cold.Save() == betweenWaves, "between-wave factory freezes inventory, solar and circuit clocks");
    Check(restored.Load(betweenWaves, error), "load frozen factory"); Step(restored, 600);
    Check(restored.Save() == betweenWaves, "loading does not restart preparation production");
    Check(restored.StartAssault(), "resume frozen factory"); Step(restored, 1);
    Check(restored.Energy() != cold.Energy() && restored.Cells()[144].clock != cold.Cells()[144].clock, "assault resumes economy and circuits");
    restored.RecordBreach(); restored.RecordBreach(); restored.RecordBreach();
    Check(restored.Retry(), "retry frozen factory"); Step(restored, 600);
    Check(restored.Save() == betweenWaves, "death retry keeps old inventory without free preparation gains");

    // 旧版 12 列跨度包含已购买末列、电线和死亡检查点，迁移必须同时处理。
    auto old = World().Save(); old["schemaVersion"] = 1;
    auto& oldState = old["state"]; oldState["columns"] = 12; oldState["completedWaves"] = 3;
    auto oldCells = nlohmann::json::array();
    for (int row = 0; row < 5; ++row) for (int col = 0; col < 12; ++col) {
        auto c = oldState["cells"][row * MaxColumns + col];
        for (const auto* key : {"iceStock", "route", "filter", "iceNext"}) c.erase(key);
        oldCells.push_back(c);
    }
    oldCells[59]["type"] = "switch"; oldCells[58]["type"] = "grower"; oldCells[58]["inputs"][0] = 59;
    oldState["cells"] = oldCells;
    for (const auto* key : {"rows", "processed", "iceFired"}) oldState.erase(key);
    for (auto& lane : oldState["lanes"]) { lane.erase("iceAmmo"); lane.erase("plainQuota"); }
    old["checkpoint"] = oldState; oldState["phase"] = 2; oldState["health"] = 0;
    Check(restored.Load(old, error) && restored.Columns() == 12 && restored.FactoryRows() == 10, "legacy expanded dimensions retained");
    Check(restored.Cells()[90].inputs[0] == 91 && restored.Cells()[91].type == Machine::Switch, "legacy wires and edge cells remapped");
    Check(restored.Retry() && restored.Cells()[90].inputs[0] == 91 && restored.Cells()[91].type == Machine::Switch, "legacy checkpoint migrated too");
    fixture = restored.Save(); fixture["state"]["energy"] = 10000;
    Check(restored.Load(fixture, error), "expansion budget");
    while (restored.Expand()) {}
    Check(restored.Columns() == 20 && restored.FactoryRows() == 20 && restored.Cells()[90].inputs[0] == 91, "expansion preserves stable wires and clamps both dimensions");
    std::cout << "Workshop production, spawn transactions, signals, persistence and recovery passed\n";
}
