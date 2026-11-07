#include "Game/Workshop/Workshop.h"
#include "Game/Workshop/WorkshopRoster.h"
#include "Game/Workshop/WorkshopGuide.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Workshop;
void TestWorkshopCircuits();
void TestWorkshopPower();
void TestWorkshopPractice();
void TestWorkshopMaintenance();
void TestWorkshopCommissions();
namespace {
// 这些用例隔离物流/配方行为，生产机显式接入起始箱；断电与配电由专项测试覆盖。
bool Build(Workshop::World& world, int index, Workshop::Machine type, int direction) {
    return world.Build(index, type, direction) && (!Workshop::RatedPower(type)
        || world.ConnectPower(Workshop::StarterPowerBox, index));
}

// 多个完整基地夹具放堆上，避免 Cell 扩展后超过 Windows 测试进程默认栈。
World& TestWorld() {
    static std::vector<std::unique_ptr<World>> worlds;
    worlds.push_back(std::make_unique<World>());
    return *worlds.back();
}
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
        Check(Build(world, row * MaxColumns + 2, Machine::Grower, 0), "build grower");
        Check(Build(world, row * MaxColumns + 3, Machine::Belt, 0), "build belt");
        Check(Build(world, row * MaxColumns + 4, Machine::Feeder, 0), "build feeder");
    }
    Check(Build(world, 4 * MaxColumns, Machine::Solar, 0), "third solar");
    Check(Build(world, 3 * MaxColumns, Machine::Solar, 0), "fourth solar");
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
    const auto starterGuide = GuideTopics(0);
    Check(starterGuide.size() == 8 && starterGuide.front() == 0, "newcomer guide contains only starter machines and pea");
    const auto firstUnlock = GuideTopics(1, 0), circuitsUnlock = GuideTopics(2, 1);
    Check(firstUnlock.size() == 5 && circuitsUnlock.size() == 11, "each completed wave produces one complete unlock batch");
    for (int topic : circuitsUnlock) Check(GuideUnlockWave(topic) == 2, "unlock popup does not reveal future or repeat older items");
    Check(GuideTopics(2, 2).empty() && GuideTopics(14).size() == static_cast<int>(Machine::Count) + Plants.size() + 4, "read batches stay quiet and every item has a guide entry");
    nlohmann::json guideSave = nlohmann::json::object();
    Check(ReadGuideSeenWave(guideSave, 8) == 8, "legacy saves do not replay historical unlock tutorials");
    guideSave["guideSeenWave"] = 2;
    const auto guideRestored = nlohmann::json::parse(guideSave.dump());
    Check(ReadGuideSeenWave(guideRestored, 3) == 2 && GuideTopics(3, ReadGuideSeenWave(guideRestored, 3)).size() == 2, "unread batch survives a save round trip");
    guideSave["guideSeenWave"] = 100000000000ull;
    Check(ReadGuideSeenWave(guideSave, 3) == 3, "future guide watermark is clamped after progression rewind");
    guideSave["guideSeenWave"] = "bad";
    Check(ReadGuideSeenWave(guideSave, 3) == 3, "optional malformed guide metadata cannot invalidate a base");
    nlohmann::json history = {{"workshopTutorialsSeen", {"practice:intro", "machine:grower", "machine:grower", 7, "unknown", "plant:pea"}}};
    const auto tutorialKeys = ReadTutorialHistory(nlohmann::json::parse(history.dump()));
    Check(tutorialKeys == std::vector<std::string>{"machine:grower", "plant:pea", "practice:intro"}, "player tutorial history round trip filters malformed entries and duplicates");
    Check(GuideKey(static_cast<int>(Machine::Grower)) == "machine:grower" && GuideKey(PlantGuideBase) == "plant:pea", "persisted tutorial keys use stable names");
    for (const auto& invalid : {nlohmann::json(nullptr), nlohmann::json(5), nlohmann::json("bad")}) {
        history["workshopTutorialsSeen"] = invalid;
        Check(ReadTutorialHistory(history).empty(), "malformed optional history defaults to unseen");
    }
    TestWorkshopCommissions();
    {
        for (const auto& threat : Threats) for (std::uint32_t seed = 1; seed <= 30; ++seed) {
            auto random = seed;
            const auto roster = RollAssaultTypes(threat.wave, 5 + std::min(100, (threat.wave - 1) * 2), random);
            Check(std::count(roster.begin(), roster.end(), static_cast<int>(threat.type)) == 1, "every debut guarantees one newcomer");
            Check(std::find(roster.begin(), roster.end(), static_cast<int>(ZombieType::ZOMBIE_NORMAL)) != roster.end(), "normal zombies remain in every wave");
            for (int type : roster) if (type != static_cast<int>(ZombieType::ZOMBIE_NORMAL)) {
                bool eligible = false;
                for (const auto& available : Threats) if (available.wave <= threat.wave && type == static_cast<int>(available.type)) eligible = true;
                Check(eligible, "random wave only samples unlocked zombies");
            }
        }
        std::uint32_t first = 42, second = 42, third = 7;
        Check(RollAssaultTypes(31, 65, first) == RollAssaultTypes(31, 65, second), "wave roll is reproducible from saved RNG");
        Check(RollAssaultTypes(31, 65, first) != RollAssaultTypes(31, 65, third), "later waves use genuinely different compositions");
    }
    {
        World& factory = TestWorld();
        auto setup = factory.Save(); setup["state"]["completedWaves"] = 14; setup["state"]["energy"] = 10000;
        std::string error;
        Check(factory.Load(setup, error), "pressure factory setup");
        for (int round = 0; round < 1000 && !factory.Tasks().Has(Blueprint::PressureGrower); ++round) {
            factory.Tasks().OnWaveCompleted(15);
            for (int i = 0; i < CommissionOfferCount; ++i) if (factory.Tasks().Offers()[i].blueprint == Blueprint::PressureGrower) {
                const auto offer = factory.Tasks().Offers()[i];
                Check(factory.AcceptCommission(i), "pressure blueprint accepted");
                if (offer.IsBattle()) factory.Tasks().OnWaveCompleted(15);
                else factory.Tasks().Record(offer.goal, offer.target);
                Check(factory.ClaimCommission(0), "pressure blueprint claimed"); break;
            }
        }
        Check(factory.Tasks().Has(Blueprint::PressureGrower), "pressure blueprint appears without forcing rewards");
        while (factory.UpgradeGrid()) {}
        Check(Build(factory, 1, Machine::Grower, 0) && Build(factory, 2, Machine::Feeder, 0)
            && Build(factory, 21, Machine::PressureGrower, 0) && Build(factory, 22, Machine::Feeder, 0), "two equivalent production lines built");
        Check(factory.RemoveRefund(21) == 0 && factory.Tasks().FreeUses(Blueprint::PressureGrower) == 0,
            "free machine has no construction refund");
        Check(factory.StartAssault(), "pressure production starts");
        Step(factory, 30);
        const auto normal = factory.Cells()[1].producedItems[0], pressure = factory.Cells()[21].producedItems[0];
        Check(normal >= 11 && pressure >= normal * 3 && pressure <= normal * 3 + 2, "live pressure output is three times equivalent normal production");
        Check(RatedPower(Machine::PressureGrower) == RatedPower(Machine::Grower) * 2, "pressure power cost is doubled");
    }
    World& world = TestWorld();
    const auto beforePreview = world.Save();
    Check(world.CanBuild(2, Machine::Grower, 0) && !world.CanBuild(0, Machine::Grower, 0)
        && !world.CanBuild(CellCount, Machine::Belt, 0) && !world.CanBuild(2, Machine::Belt, 4)
        && !world.CanBuild(2, Machine::Latch, 0) && world.Save() == beforePreview, "preview shares build eligibility without mutation");
    Check(!Build(world, CellCount, Machine::Belt, 0), "reject out-of-range build");
    Check(!Build(world, 10, Machine::Belt, 0), "reject unexpanded land");
    Check(!Build(world, 2, Machine::Latch, 0), "unlock progression isolated");
    Supply(world);
    const auto prepared = world.Save(); Step(world, 600);
    Check(world.Save() == prepared, "planning does not advance resources or clocks");
    const auto save = world.Save(); World& restored = TestWorld(); std::string error;
    Check(restored.Load(save, error) && restored.Save() == save, "exact planning round trip");
    Step(world, 4); Step(restored, 4);
    Check(restored.Save() == world.Save(), "restored transport continues identically");
    auto bad = save; bad["state"]["cells"][0]["inputs"][0] = 999;
    const auto unchanged = restored.Save();
    Check(!restored.Load(bad, error) && restored.Save() == unchanged, "bad wire rejected transactionally");
    bad = save; bad["schemaVersion"] = save.at("schemaVersion").get<int>() + 1;
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
    Check(Build(world, 5, Machine::Switch, 0), "build control switch");
    Check(Build(world, 6, Machine::Not, 0), "build not gate");
    Check(world.Connect(5, 6, 0), "wire inverter");
    Check(world.Connect(6, StarterPowerBox, 0), "wire grower control");
    Step(world, 1); Check(!world.Enabled(StarterPowerBox), "inverted high stops production");
    Check(world.Configure(5), "toggle switch"); Step(world, 1);
    Check(world.Enabled(StarterPowerBox), "low through inverter resumes production");
    Check(Build(world, 7, Machine::Latch, 0), "build latch");
    Check(world.Connect(6, 7, 0), "connect latch set"); Step(world, 1);
    Check(world.Cells()[7].signal, "latch stores set");
    Check(world.Connect(-1, 7, 0), "disconnect latch input"); Step(world, 1);
    Check(world.Cells()[7].signal, "latch retains unconnected memory");
    Check(restored.Load(world.Save(), error) && restored.Cells()[7].signal, "latch survives save");
    Check(world.Remove(6), "remove inverter");
    Check(world.Cells()[StarterPowerBox].inputs[0] == -1, "removal disconnects dangling wire");
    ClearWave(world); Check(world.Expand() && world.Columns() == 12, "third wave expands factory");

    World& empty = TestWorld();
    const auto checkpoint = empty.Save();
    Check(empty.StartAssault(), "start unarmed attack");
    Check(!empty.ReserveAmmo(0), "empty magazine cannot reserve attack");
    empty.RecordBreach(); empty.RecordBreach(); empty.RecordBreach();
    Check(empty.State() == Phase::Dead && empty.Health() == 0 && empty.Fired() == 0, "no ammo means no firing and defeat");
    Check(restored.Load(empty.Save(), error) && restored.State() == Phase::Dead, "dead base survives reload");
    Check(restored.Retry() && restored.Save() == checkpoint, "retry exactly restores pre-assault base");
    restored.Reset(); Check(restored.Save() == World().Save(), "reset only creates fresh independent state");

    // 一个运输节拍内不能级联穿过整条带；堵塞不得复制或遗失弹药。
    World& transport = TestWorld(); Check(Build(transport, 2, Machine::Grower, 0), "transport grower");
    for (int i = 3; i <= 7; ++i) Check(Build(transport, i, Machine::Belt, 0), "long belt");
    Check(transport.StartAssault(), "run transport");
    Step(transport, GrowPeriod + 0.1f);
    Check(transport.Produced() == 1 && transport.Cells()[7].stock == 0, "produced pea cannot instantly cross the line");
    Step(transport, 30);
    int stock = 0; for (const auto& cell : transport.Cells()) stock += cell.stock;
    Check(stock == transport.Produced() && stock == 10, "blocked line conserves all produced ammo");
    Check(transport.Status(7) == "右侧没有接收设备", "blocked outlet identifies its direction and cause");
    const auto blockedSave = transport.Save();
    Check(!transport.InspectOutput(7).ready && transport.InspectOutput(7).target == 8
        && transport.Save() == blockedSave, "output inspection never advances logistics");
    Check(transport.InspectOutput(2).message == "右侧目标缓存已满", "full receiver is distinguished from a missing receiver");
    Check(transport.Rotate(2) && transport.Rotate(2), "turn grower away from the belt");
    Check(transport.InspectOutput(2).message == "左侧没有接收设备", "nearby belt cannot receive from the wrong direction");
    Check(!transport.Rotate(0), "non-directional machines do not pretend to rotate");
    Check(transport.Rotate(2) && transport.Rotate(2), "restore grower direction");

    World& circuits = TestWorld();
    auto circuitFixture = circuits.Save(); circuitFixture["state"]["completedWaves"] = 2;
    circuitFixture["state"]["energy"] = 1000;
    Check(circuits.Load(circuitFixture, error), "load isolated unlocked circuit fixture");
    Check(Build(circuits, 2, Machine::Switch, 0) && Build(circuits, 3, Machine::Switch, 0), "logic inputs");
    Check(Build(circuits, 4, Machine::And, 0) && Build(circuits, 5, Machine::Or, 0), "binary gates");
    for (int gate : {4, 5}) {
        Check(circuits.Connect(2, gate, 0) && circuits.Connect(3, gate, 1), "two-port wiring");
    }
    const auto wiredSave = circuits.Save();
    Check(circuits.CanConnect(2, 5, 1) && !circuits.CanConnect(0, 5, 1)
        && !circuits.CanConnect(2, 0, 1) && !circuits.CanConnect(2, 19, 0)
        && circuits.Save() == wiredSave, "wire preview shares port validation without mutation");
    Check(circuits.StartAssault(), "run circuits");
    Step(circuits, 1); Check(circuits.Cells()[4].signal && circuits.Cells()[5].signal, "both high");
    Check(circuits.Configure(2), "lower A"); Step(circuits, 1);
    Check(!circuits.Cells()[4].signal && circuits.Cells()[5].signal, "AND and OR differ for mixed inputs");
    Check(Build(circuits, 6, Machine::Timer, 0), "timer"); Step(circuits, 2.5f);
    Check(circuits.Cells()[6].signal, "timer high half period");
    Check(circuits.Connect(2, 6, 0), "disable timer"); Step(circuits, 1);
    Check(!circuits.Cells()[6].signal && circuits.Cells()[6].clock == 0, "timer stops on low input");
    Check(Build(circuits, 7, Machine::Not, 0) && circuits.Connect(7, 7, 0), "feedback circuit");
    Step(circuits, 10); Check(restored.Load(circuits.Save(), error), "cyclic circuit is finite and serializable");

    World& split = TestWorld();
    auto splitFixture = split.Save(); splitFixture["state"]["completedWaves"] = 1;
    Check(split.Load(splitFixture, error), "splitter fixture");
    Check(Build(split, 2, Machine::Grower, 0) && Build(split, 3, Machine::Splitter, 0), "splitter supply");
    Check(Build(split, 4, Machine::Store, 0) && Build(split, MaxColumns + 3, Machine::Store, 0), "front and right outputs");
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

    World& upgrades = TestWorld();
    Check(!upgrades.Upgrade(0), "second level locked before wave two");
    auto fixture = upgrades.Save(); fixture["state"]["completedWaves"] = 2; fixture["state"]["energy"] = 1000;
    Check(upgrades.Load(fixture, error), "upgrade fixture");
    Check(Build(upgrades, 2, Machine::Grower, 0) && Build(upgrades, 3, Machine::Store, 0), "upgradable production line");
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
    Check(std::abs(upgrades.Energy() - solarBefore - 150 - 10 * (SolarRates[1] + SolarRates[0])) < 0.1f, "solar levels use the reduced production rates");
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
        World& line = TestWorld();
        fixture = line.Save(); fixture["state"]["completedWaves"] = 5; fixture["state"]["energy"] = 1000;
        Check(line.Load(fixture, error) && Build(line, 2, Machine::Grower, 0) && Build(line, 3, Machine::Feeder, 0), "rate fixture");
        for (int l = 1; l < level; ++l) Check(line.Upgrade(2), "rate upgrade");
        Check(line.StartAssault(), "run rate fixture");
        Step(line, 20.1f); production[level - 1] = line.Produced();
        Check(restored.Load(line.Save(), error) && restored.Save() == line.Save(), "upgraded production round trip");
    }
    Check(production[0] == 8 && production[1] == 10 && production[2] == 12, "actual production follows 2.5 / 2.0 / 1.6 second periods");
    // 冷加工守恒、远程路线与混合弹仓：任何阶段均不能用另一种弹药代付。
    World& cold = TestWorld();
    fixture = cold.Save(); fixture["state"]["completedWaves"] = 5; fixture["state"]["energy"] = 1000;
    Check(cold.Load(fixture, error), "cold fixture");
    Check(Build(cold, 142, Machine::Cooler, 0) && Build(cold, 143, Machine::Feeder, 0), "cold line beyond frontline rows");
    Check(cold.SetRoute(143, 4) && cold.SetFilter(143, 1), "explicit typed remote delivery");
    fixture = cold.Save(); fixture["state"]["cells"][142]["stock"] = 5;
    Check(cold.Load(fixture, error), "five input peas");
    World& outletProbe = TestWorld(); Check(outletProbe.Load(fixture, error), "output preview fixture");
    Check(Build(outletProbe, 141, Machine::Grower, 0), "output preview input line");
    Check(outletProbe.InspectOutput(141).message == "右侧目标缓存已满", "cooler has a separate five-pea input buffer");
    Check(outletProbe.InspectOutput(142).ready, "cooler output probes ice rather than its plain input inventory");
    Check(outletProbe.SetFilter(143, 0) && outletProbe.InspectOutput(142).message == "右侧目标不接收此弹种",
        "output preview identifies incompatible receiver filters");
    Check(cold.StartAssault(), "run cold production");
    const float coldEnergy = cold.Energy(); Step(cold, 13.25f);
    Check(cold.Lanes()[4].iceAmmo == 5 && cold.Lanes()[4].ammo == 0 && cold.Cells()[142].TotalStock() == 0, "one pea becomes one ice pea");
    Check(std::abs(cold.Energy() - coldEnergy - (13.25f * 2 * SolarRate - 5 * CoolingEnergy)) < 0.1f, "cold energy accounting");
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

    World& sorting = TestWorld();
    fixture = sorting.Save(); fixture["state"]["completedWaves"] = 3;
    Check(sorting.Load(fixture, error), "sort fixture");
    Check(Build(sorting, 3, Machine::Splitter, 0) && Build(sorting, 4, Machine::Store, 0)
        && Build(sorting, 23, Machine::Store, 0), "typed splitter outlets");
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
    Check(Build(cold, 141, Machine::Grower, 0) && Build(cold, 144, Machine::Timer, 0), "planning freeze fixture");
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

    // v7 以前的电网重构仍拒绝；v7 可补入中性的委托与确定性出兵状态。
    const auto beforeLegacy = restored.Save();
    for (int version = 1; version < 7; ++version) {
        auto old = beforeLegacy; old["schemaVersion"] = version;
        Check(!restored.Load(old, error) && restored.Save() == beforeLegacy, "old schema rejected without modifying current world");
    }
    {
        auto legacy = beforeLegacy;
        legacy["schemaVersion"] = 7;
        for (const char* key : {"state", "checkpoint"}) if (legacy[key].is_object()) {
            legacy[key].erase("commissions"); legacy[key].erase("spawnRandom"); legacy[key].erase("assaultTypes");
        }
        World& imported = TestWorld();
        Check(imported.Load(legacy, error) && imported.Energy() == restored.Energy()
            && imported.CompletedWaves() == restored.CompletedWaves(), "v7 base migrates without losing resources or progress");
    }
    fixture = restored.Save(); fixture["state"]["energy"] = 10000;
    Check(restored.Load(fixture, error), "expansion budget");
    while (restored.Expand()) {}
    Check(restored.Columns() == 20 && restored.FactoryRows() == 20, "expansion clamps both dimensions");

    // 新版五弹种共用总弹仓，残骸不进入前线；冷凝配方和存档同时保留。
    World& advanced = TestWorld();
    fixture = advanced.Save(); fixture["state"]["completedWaves"] = 10; fixture["state"]["energy"] = 6000;
    fixture["state"]["gridLevel"] = 4; // 本例验证配方/物流，配置已解锁且足够的电网；过载由专项覆盖。
    Check(advanced.Load(fixture, error), "advanced production fixture");
    Check(Build(advanced, 21, Machine::SporeGrower, 0) && Build(advanced, 22, Machine::Belt, 0)
        && Build(advanced, 23, Machine::Feeder, 0) && advanced.SetRoute(23, 0) && advanced.SetFilter(23, 2), "spore production line");
    Check(Build(advanced, 41, Machine::MelonGrower, 0) && Build(advanced, 42, Machine::Cooler, 0)
        && advanced.SetRecipe(42, 1) && Build(advanced, 43, Machine::Feeder, 0) && advanced.SetFilter(43, 4)
        && advanced.SetRoute(43, 1), "winter melon conversion line");
    Check(Build(advanced, 81, Machine::MelonGrower, 0) && Build(advanced, 82, Machine::Feeder, 0)
        && advanced.SetFilter(82, 3) && advanced.SetRoute(82, 2), "ordinary melon line");
    advanced.EnsureAmmoQuota(0, Ammo::Spore); advanced.EnsureAmmoQuota(1, Ammo::IceMelon); advanced.EnsureAmmoQuota(2, Ammo::Melon);
    Check(advanced.StartAssault(), "run advanced production"); Step(advanced, 30);
    Check(advanced.Lanes()[0].Inventory(Ammo::Spore) == 20 && advanced.Lanes()[1].Inventory(Ammo::IceMelon) >= 5
        && advanced.Lanes()[2].Inventory(Ammo::Melon) >= 6, "three supply types reach their designated rows");
    Check(!advanced.ReserveAmmo(0, Ammo::Melon) && advanced.ReserveAmmo(0, Ammo::Spore), "ammo types cannot substitute");
    Check(!advanced.ReserveAmmo(0, Ammo::Scrap) && !advanced.SetFilter(23, static_cast<int>(Ammo::Scrap)), "scrap cannot enter or be consumed by frontline");
    Check(advanced.SetAmmoQuota(0, Ammo::Spore, 0) && advanced.Lanes()[0].Inventory(Ammo::Spore) == 19, "quota reduction preserves stock");
    int quotaSum = 0; for (int a = 0; a < AmmoCount; ++a) quotaSum += advanced.Lanes()[0].Quota(static_cast<Ammo>(a));
    Check(quotaSum == MagazineCapacity && advanced.SetAmmoQuota(0, Ammo::Melon, 80), "quota transfers conserve total space");
    Check(restored.Load(advanced.Save(), error), "new material round trip"); Step(advanced, 4); Step(restored, 4);
    Check(advanced.Save() == restored.Save(), "new production and round robin continue identically after load");
    bad = advanced.Save(); bad["state"]["lanes"][0]["extraQuota"][0] = 81;
    const auto advancedUnchanged = advanced.Save();
    Check(!advanced.Load(bad, error) && advanced.Save() == advancedUnchanged, "invalid quota rejected transactionally");

    // 击杀收入必须经过物流加工，多个入口共享一次掉落；每波上限独立于机器数量。
    World& recycling = TestWorld();
    fixture = recycling.Save(); fixture["state"]["completedWaves"] = 1; fixture["state"]["energy"] = 1000;
    Check(recycling.Load(fixture, error) && recycling.Remove(0) && recycling.Remove(MaxColumns), "recycling isolates passive solar");
    for (int start : {2, 22}) Check(Build(recycling, start, Machine::SalvagePort, 0)
        && Build(recycling, start + 1, Machine::Belt, 0) && Build(recycling, start + 2, Machine::Recycler, 0), "parallel recycling lines");
    recycling.RecordKill(); Check(recycling.PendingScrap() == 0, "preparation cannot award kills");
    Check(recycling.StartAssault(), "run recycling");
    const float salvageBudget = recycling.Energy();
    for (int n = 0; n < 100; ++n) recycling.RecordKill();
    Check(recycling.PendingScrap() == 40 && recycling.WaveScrap() == 40 && recycling.Energy() == salvageBudget, "kills enqueue bounded salvage without immediate income");
    Step(recycling, 4); Check(restored.Load(recycling.Save(), error), "inflight salvage round trip");
    Step(recycling, 36); Step(restored, 36);
    Check(recycling.Save() == restored.Save() && recycling.PendingScrap() == 0
        && std::abs(recycling.Energy() - salvageBudget - 200) < .01f, "parallel processing conserves capped salvage income");
    const auto capped = recycling.Save(); recycling.RecordKill(); Check(recycling.Save() == capped, "wave cap does not multiply with machines");
    FinishSchedule(recycling); const auto salvagePlanning = recycling.Save(); Step(recycling, 600);
    Check(recycling.Save() == salvagePlanning, "salvage processing also freezes between waves");
    Check(recycling.StartAssault() && recycling.WaveScrap() == 0, "new wave resets only its drop quota");
    recycling.RecordKill(); recycling.RecordBreach(); recycling.RecordBreach(); recycling.RecordBreach();
    Check(recycling.Retry() && recycling.Save() == salvagePlanning, "retry rolls back salvage along with the base");

    World& counter = TestWorld();
    fixture = counter.Save(); fixture["state"]["completedWaves"] = 4; fixture["state"]["energy"] = 1000;
    Check(counter.Load(fixture, error) && Build(counter, 2, Machine::Switch, 0) && Build(counter, 3, Machine::Counter, 0)
        && Build(counter, 4, Machine::Switch, 0) && counter.Configure(4)
        && counter.Connect(2, 3, 0) && counter.Connect(4, 3, 1), "counter A and B inputs");
    Check(counter.StartAssault(), "run counter"); Step(counter, 2);
    Check(counter.Cells()[3].count == 1, "held high counts only one rising edge");
    Check(counter.Configure(2), "falling edge"); Step(counter, .6f);
    Check(counter.Configure(2), "second rising edge"); Step(counter, .6f);
    Check(counter.Cells()[3].count == 2 && restored.Load(counter.Save(), error), "two edges and counter snapshot");
    Step(counter, 1); Step(restored, 1); Check(counter.Save() == restored.Save() && counter.Cells()[3].count == 2, "load does not invent a rising edge");
    Check(counter.Configure(4), "counter reset"); Step(counter, 1); Check(counter.Cells()[3].count == 0, "B reset has priority");
    Check(counter.Configure(4), "release reset"); Step(counter, 1); Check(counter.Cells()[3].count == 0, "held A cannot recount after reset");

    int giantDebut = 0;
    for (int n = 0; n < 5 + 13 * 2; ++n) if (SpawnType(14, n) == ZombieType::ZOMBIE_GARGANTUAR) ++giantDebut;
    Check(giantDebut == 1 && SpawnType(8, 3) == ZombieType::ZOMBIE_FOOTBALL
        && SpawnType(18, 12) == ZombieType::ZOMBIE_HEALER && SpawnType(22, 16) == ZombieType::ZOMBIE_CRYSTAL_DRUMMER
        && SpawnType(26, 10) == ZombieType::ZOMBIE_ADAPTIVE_HELMET, "threat milestones and one-giant debut");
    TestWorkshopCircuits();
    TestWorkshopPower();
    TestWorkshopPractice();
    TestWorkshopMaintenance();
    std::cout << "Workshop production, spawn transactions, signals, persistence and recovery passed\n";
}
