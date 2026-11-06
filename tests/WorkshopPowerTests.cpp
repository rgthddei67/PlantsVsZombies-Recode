#include "Game/Workshop/Workshop.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>

namespace {
using namespace Workshop;
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAILED workshop power: " << reason << '\n'; std::exit(1); }
}
void Load(World& world, const nlohmann::json& save, const std::source_location at = std::source_location::current()) {
    std::string error;
    if (!world.Load(save, error)) { std::cerr << "line " << at.line() << ": " << error << '\n'; Check(false, "load fixture"); }
}
std::unique_ptr<World> Fixture(int completed = 8, int grid = 1) {
    auto w = std::make_unique<World>(); auto save = w->Save();
    save["state"]["completedWaves"] = completed; save["state"]["gridLevel"] = grid; save["state"]["energy"] = 10000;
    Load(*w, save); return w;
}
[[maybe_unused]] void Schedule(World& w) {
    for (int frame = 0; frame < 12000 && w.RemainingSpawns(); ++frame) {
        w.Tick(1.0f / 60);
        if (w.NextSpawnRow() >= 0) w.CommitSpawn();
    }
    w.CompleteAssault();
}

struct SupplyResult { int requested = 0, supplied = 0, focusMissed = 0; };
/** 同预算、同布局、满库存连续十波的供需压力代理；不把供弹成功率冒充原生战斗胜率。 */
SupplyResult CompareDispatch(int mode, int grid = 5, float focusDemand = 1.35f) {
    auto w = Fixture(22, grid);
    // 生产结构对应后期豌豆 + 冰瓜两套供弹。三案购买同样传感器/门，只改变接线。
    for (int row = 0; row < Rows; ++row) {
        const int i = row * MaxColumns;
        for (auto [offset, type] : {std::pair{2, Machine::Grower}, {3, Machine::Feeder}, {5, Machine::MelonGrower}, {6, Machine::Cooler}, {7, Machine::Feeder},
            {8, Machine::StockSensor}, {9, Machine::Not}, {10, Machine::StockSensor}, {11, Machine::Not},
            {12, Machine::EnemySensor}, {13, Machine::And}, {14, Machine::StockSensor}, {15, Machine::Not}, {16, Machine::Or}}) {
            auto doc = w->Save(); doc["state"]["columns"] = 20; Load(*w, doc);
            w->RefundEnergy(10000); Check(w->Build(i + offset, type, 0), "comparison build");
            if (RatedPower(type)) { w->Upgrade(i + offset); if (type != Machine::Grower) w->Upgrade(i + offset); }
        }
        for (int box : {i + 17, i + 18}) {
            Check(w->Build(box, Machine::PowerBox, 0) && w->ConnectPower(MainPowerSource, box), "comparison branch");
        }
        w->ConnectPower(i + 17, i + 2); w->ConnectPower(i + 18, i + 5); w->ConnectPower(i + 18, i + 6);
        w->SetFilter(i + 3, 0); w->SetFilter(i + 7, 4); w->SetPlan(i + 6, 1);
        w->SetAmmoQuota(row, Ammo::IceMelon, 60);
        w->SetFilter(i + 8, 0); w->SetSetting(i + 8, 10);
        w->SetFilter(i + 10, 4); w->SetSetting(i + 10, 60);
        const int alternate = (row + 2) % Rows;
        w->SetFilter(i + 14, 4); w->SetSetting(i + 14, 60); w->SetRoute(i + 14, alternate);
        w->SetSetting(i + 12, 3); w->SetRoute(i + 12, alternate);
        w->SetRoute(i + 7, alternate, 1); w->SetFilter(i + 7, 4, 1);
        if (mode) {
            w->Connect(i + 8, i + 9, 0); w->Connect(i + 9, i + 17, 0);
            w->Connect(i + 10, i + 11, 0);
            int control = i + 11;
            if (mode == 2) {
                w->Connect(i + 14, i + 15, 0);
                w->Connect(i + 12, i + 13, 0); w->Connect(i + 15, i + 13, 1);
                w->Connect(i + 13, i + 7, 1); // 备用路线受攻击且未满时，装填口转供备用路线。
                w->Connect(i + 13, i + 16, 0); w->Connect(i + 11, i + 16, 1); control = i + 16;
            }
            w->Connect(control, i + 18, 0);
        }
    }
    // 足够阳光隔离电网瓶颈，三案都具有相同三十六台 L3 集光器。
    int solar = 0;
    for (int i = 0; i < CellCount && solar < 36; ++i) {
        if (i / MaxColumns >= w->FactoryRows() || i % MaxColumns >= w->Columns()) continue;
        if (w->Cells()[i].type == Machine::Empty) { w->RefundEnergy(10000); w->Build(i, Machine::Solar, 0); }
        if (w->Cells()[i].type == Machine::Solar) { w->Upgrade(i); w->Upgrade(i); ++solar; }
    }
    auto save = w->Save(); save["state"]["energy"] = 5000;
    for (auto& lane : save["state"]["lanes"]) { lane["ammo"] = 20; lane["extraAmmo"][2] = 60; }
    Load(*w, save);
    SupplyResult result; std::array<float, Rows> pea{}, ice{};
    for (int wave = 0; wave < 10; ++wave) {
        Check(w->StartAssault(), "continuous comparison start");
        for (int frame = 0; frame < 80 * 60; ++frame) {
            const int focus = w->FocusRoute(w->AssaultStage());
            std::array<int, Rows> enemies{}; enemies.fill(1); enemies[focus] = 6;
            w->ObserveFrontline(enemies, {});
            w->Tick(1.0f / 60);
            if (w->NextSpawnRow() >= 0) w->CommitSpawn();
            for (int row = 0; row < Rows; ++row) {
                pea[row] += .25f / 60; ice[row] += (row == focus ? focusDemand : .12f) / 60;
                if (pea[row] >= 1) { pea[row] -= 1; w->ReserveAmmo(row, Ammo::Pea); }
                if (ice[row] >= 1) {
                    ice[row] -= 1; ++result.requested;
                    if (w->ReserveAmmo(row, Ammo::IceMelon)) ++result.supplied;
                    else if (row == focus) ++result.focusMissed;
                }
            }
        }
        w->CompleteAssault(); Check(w->State() == Phase::Planning, "continuous comparison clear");
        const auto planning = w->Save(); w->Tick(100); Check(w->Save() == planning, "no between-wave stockpiling");
    }
    return result;
}
}

void TestWorkshopPower() {
    Check(UpgradeCost(75, 1) == 75 && UpgradeCost(150, 2) == 300 && GridCosts[1] == 225 && GridCosts[5] == 1500, "only grid upgrades cost 1.5 times; machines and plants unchanged");
    auto w = Fixture(2);
    Check(w->UpgradeGrid() && w->GridLevel() == 2, "wave gated purchase");
    const auto paid = w->Save(); Check(!w->UpgradeGrid() && paid == w->Save(), "money cannot bypass grid tier cap");
    for (int i = 2; i < 10; ++i) Check(w->Build(i, Machine::Grower, 1) && w->ConnectPower(StarterPowerBox, i), "overload machines");
    Check(std::abs(w->Power().speed - 1) < .001f, "exact capacity runs full speed");
    w->Build(22, Machine::Grower, 1); w->ConnectPower(StarterPowerBox, 22);
    Check(std::abs(w->Power().speed - (16.0f / 18) * (16.0f / 18)) < .001f, "shared overload ratio");
    const auto planning = w->Save(); w->Tick(60); Check(w->Save() == planning, "preparation fully freezes power and progress");
    w->StartAssault(); w->Tick(.5f);
    Check(std::abs(w->Cells()[2].clock - .5f * (16.0f / 18) * (16.0f / 18)) < .002f && w->Cells()[2].clock == w->Cells()[22].clock, "fair proportional processing independent of cell order");
    w->SetMachineRunning(22, false); const float clock = w->Cells()[22].clock;
    w->Tick(.5f); Check(w->MachinePower(22) == 0 && w->Power().speed == 1 && w->Cells()[22].clock == clock, "manual shutoff releases power and preserves work");
    auto replay = Fixture(); Load(*replay, w->Save()); w->Tick(.25f); replay->Tick(.25f); Check(w->Save() == replay->Save(), "mid-process resume deterministic");
    auto bad = w->Save(); bad["state"]["gridLevel"] = 6; std::string error;
    const auto intact = w->Save(); Check(!w->Load(bad, error) && w->Save() == intact, "invalid future tier rejected atomically");
    w = Fixture(); w->Build(2, Machine::Cooler, 0); w->ConnectPower(StarterPowerBox, 2);
    Check(std::abs(w->MachinePower(2) - .6f) < .001f, "starved cooler has standby load");
    w->Build(4, Machine::Grower, 0); w->ConnectPower(StarterPowerBox, 4); auto full = w->Save(); full["state"]["cells"][4]["stock"] = 5; Load(*w, full);
    Check(std::abs(w->MachinePower(4) - .3f) < .001f, "blocked machine retains standby draw");
    w->Build(6, Machine::Switch, 0); w->Configure(6); Check(!w->Connect(6, 4, 0), "processors no longer accept A"); w->Connect(6, StarterPowerBox, 0); w->StartAssault(); w->Tick(.5f);
    Check(w->MachinePower(4) == 0, "A closed releases standby load");
    for (const auto type : {Machine::Grower, Machine::Cooler, Machine::SporeGrower, Machine::MelonGrower, Machine::Recycler, Machine::RepairAssembler})
        Check(RatedPower(type, 3) * ProductionPeriod(type, 3) < RatedPower(type, 1) * ProductionPeriod(type, 1), "upgrades improve energy per item");
    for (const auto type : {Machine::Solar, Machine::Belt, Machine::Store, Machine::And, Machine::Cannon}) Check(RatedPower(type) == 0, "logistics controls and cannon remain outside grid");

    // 两个同级支路先公平分配，再提升其中一条优先级；绝不凭格位抢电。
    w = Fixture();
    Check(w->Build(41, Machine::PowerBox, 0) && w->ConnectPower(MainPowerSource, 41), "second branch");
    Check(!w->ConnectPower(StarterPowerBox, 41) && !w->ConnectPower(MainPowerSource, 2), "no chained boxes or direct machine mains");
    for (int i = 2; i < 10; ++i) {
        Check(w->Build(i, Machine::Grower, 1), "priority processors");
        Check(w->MachinePower(i) == 0, "unwired processor draws nothing");
        w->ConnectPower(i < 5 ? StarterPowerBox : 41, i);
    }
    Check(std::abs(w->MachineSpeed(2) - .5625f) < .001f && w->MachineSpeed(2) == w->MachineSpeed(8), "12 over 16 squared shared fairly");
    w->SetPlan(StarterPowerBox, 1);
    Check(w->MachineSpeed(2) == 1 && std::abs(w->MachineSpeed(8) - .36f) < .001f, "priority gets 6 watts, normal shares remaining 6 of 10");
    w->SetMachineRunning(StarterPowerBox, false);
    Check(w->MachineSpeed(2) == 0 && w->MachineSpeed(8) == 1, "off branch frees all allocation");
    w->Remove(41); Check(w->Cells()[8].powerSource == -1 && w->MachineSpeed(8) == 0, "removing box disconnects its machines");
    // 维修包完整生产配送，准备期停机，修复原子扣除正确路线的包和阳光。
    w = Fixture(10, 4); w->Build(2, Machine::RepairAssembler, 0); w->Build(3, Machine::Feeder, 0);
    w->ConnectPower(StarterPowerBox, 2); w->SetFilter(3, static_cast<int>(Ammo::RepairKit)); w->SetRoute(3, 2);
    w->EnsureAmmoQuota(2, Ammo::RepairKit);
    auto repair = w->Save(); repair["state"]["cells"][2]["extraStock"][4] = 2; Load(*w, repair);
    w->Tick(10); Check(w->Save() == repair, "preparation freezes repair materials");
    w->StartAssault();
    for (int frame = 0; frame < 540; ++frame) { w->Tick(1.0f / 60); if (w->NextSpawnRow() >= 0) w->CommitSpawn(); }
    Check(w->Lanes()[2].Inventory(Ammo::RepairKit) == 2 && w->Cells()[2].Inventory(Ammo::Scrap) == 0, "scrap becomes kits and reaches the configured lane");
    const auto resources = w->Save();
    Check(!w->SpendRepair(1, 100) && w->Save() == resources, "wrong lane repair cannot charge sun");
    const float sun = w->Energy();
    Check(w->SpendRepair(2, 100) && w->Energy() == sun - 100 && w->Lanes()[2].Inventory(Ammo::RepairKit) == 1, "repair atomically spends one kit and sun");
    Load(*replay, w->Save()); Check(replay->Save() == w->Save(), "kit and power wires persist exactly");
    repair = w->Save(); repair["state"]["energy"] = 99; Load(*w, repair);
    Check(!w->SpendRepair(2, 100) && w->Save() == repair, "insufficient sun preserves kit");
    w = Fixture(8, 4); w->StartAssault();
    int focused = 0, total = 0; std::array<int, 3> stages{};
    while (w->RemainingSpawns()) {
        w->Tick(1.0f / 60); const int row = w->NextSpawnRow();
        if (row < 0) continue;
        const int stage = w->AssaultStage(); ++stages[stage]; ++total; focused += row == w->FocusRoute(stage);
        if (total == 4) { Load(*replay, w->Save()); Check(replay->NextSpawnRow() == row, "saved schedule matches preannouncement"); }
        w->CommitSpawn();
    }
    Check(total == 21 && focused > total / 2 && focused < total && stages[0] && stages[1] && stages[2], "focused waves retain total and secondary route harassment");
    const auto brute = CompareDispatch(0), basic = CompareDispatch(1), smart = CompareDispatch(2);
    std::cout << "Power supply comparison (10 full-stock-start continuous waves): requested=" << brute.requested
        << " supplied(all/basic/smart)=" << brute.supplied << '/' << basic.supplied << '/' << smart.supplied
        << " focusMissed=" << brute.focusMissed << '/' << basic.focusMissed << '/' << smart.focusMissed << '\n';
    Check(brute.requested == basic.requested && basic.requested == smart.requested, "same deterministic demand across budgets");
    Check(smart.focusMissed < brute.focusMissed && smart.supplied > brute.supplied, "dispatch benefits survive full stocks and continuous waves");
    for (int grid : {4, 6}) {
        const auto all = CompareDispatch(0, grid), dispatch = CompareDispatch(2, grid);
        std::cout << "Grid capacity=" << GridCapacities[grid - 1] << " supplied(all/dispatch)=" << all.supplied << '/' << dispatch.supplied
            << " focusMissed=" << all.focusMissed << '/' << dispatch.focusMissed << '\n';
    }
}
