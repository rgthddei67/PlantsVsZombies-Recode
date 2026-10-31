#include "Game/Workshop/WorkshopPractice.h"
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAILED workshop practice: " << reason << '\n'; std::exit(1); }
}
void Wire(Workshop::Practice& p, const char* port, int from, int to) {
    Check(p.Act(port) && p.Act("cell", from) && p.Act("cell", to), "real source then target gesture");
}
}

void TestWorkshopPractice() {
    using namespace Workshop;
    auto practice = std::make_unique<Practice>(); auto& p = *practice;
    const auto empty = p.Model().Save();
    Check(p.Act("belt") && !p.Act("cell", 24) && p.Model().Save() == empty && p.Step() == 0, "wrong placement does not pass step or spend supplies");
    Check(p.Act("cell", Practice::Conveyor) && p.Step() == 1, "belt placement actually required");
    Check(p.Act("feeder") && p.Act("cell", Practice::Feeder) && p.Step() == 2, "feeder placement");
    Check(p.Act("route") && p.Step() == 3, "configure route independently of factory row");
    Check(!p.Complete(), "construction alone cannot complete supply proof"); p.Advance(6);
    Check(p.Complete() && p.Model().Lanes()[2].ammo > 0 && p.Model().Lanes()[1].ammo == 0 && !p.Running(), "actual delivered ammo completes supply lesson and pauses");

    p.Reset(1); p.Advance(1);
    Check(p.Step() == 1 && p.Model().Power().speed < 1, "overload must be observed before power lesson advances");
    p.Act("cell", 3); p.Act("power"); Check(p.Step() == 2 && !p.Complete(), "turning off requires subsequent simulation proof");
    p.Advance(1); Check(p.Complete() && p.Model().Power().speed == 1 && p.Model().MachinePower(3) == 0, "manual power release is observable");

    p.Reset(2); p.Act("cell", Practice::Sensor); p.Act("route"); p.Act("route");
    Check(p.Step() == 1, "enemy route configured"); p.Act("cell", Practice::Feeder); p.Act("route2");
    Check(p.Step() == 2, "second route configured");
    Wire(p, "wire_a", Practice::Sensor, Practice::Feeder); p.Advance(1);
    Check(p.Step() == 2 && p.Hint().find("A启停") != std::string::npos, "wrong port has specific feedback and cannot pass");
    p.Act("disconnect"); Wire(p, "wire_b", Practice::Sensor, Practice::Feeder);
    Check(p.Step() == 3, "correct B connection"); p.Act("enemy", 1); p.Advance(5);
    Check(p.Step() == 4 && p.Model().ActiveRoute(Practice::Feeder) == 2 && p.Model().Lanes()[2].ammo > 0, "high signal really reroutes cargo");
    p.Act("enemy", 0); p.Advance(5);
    Check(p.Complete() && p.Model().ActiveRoute(Practice::Feeder) == 1 && p.Model().Lanes()[1].ammo > 0, "low signal returns to first plan");

    p.Reset(3); p.Act("cell", Practice::Sensor); p.Act("route"); p.Act("route");
    p.Act("filter"); for (int i = 0; i < 5; ++i) p.Act("setting", -1);
    Check(p.Step() == 3, "stock route material and threshold configured by player");
    Wire(p, "wire_a", Practice::Sensor, Practice::Inverter); Wire(p, "wire_a", Practice::Inverter, Practice::Grower);
    Check(p.Step() == 5, "two actual A connections required");
    p.Act("stock", 5); Check(p.Step() == 5, "stock injection does not pretend circuit already propagated"); p.Advance(1);
    Check(p.Step() == 6 && !p.Model().Enabled(Practice::Grower) && p.Model().Cells()[Practice::Sensor].signal, "high stock propagates through inverter and stops factory");
    p.Act("stock", 0); p.Advance(5);
    Check(p.Complete() && p.Model().Enabled(Practice::Grower) && p.Model().Produced() > 0, "low stock restarts actual production");
    p.Reset(0); Check(p.Step() == 0 && p.Model().Produced() == 0 && p.Model().Lanes()[2].ammo == 0, "replay clears only the practice world");

    auto world = std::make_unique<World>(); auto setup = world->Save(); setup["state"]["completedWaves"] = 2; setup["state"]["energy"] = 10000;
    std::string error; Check(world->Load(setup, error), "diagnostic fixture");
    for (auto [i, type] : {std::pair{5, Machine::EnemySensor}, {6, Machine::EnemySensor}, {7, Machine::Not}, {8, Machine::And}, {23, Machine::Feeder}}) world->Build(i, type, 0);
    world->SetRoute(5, 2); world->SetRoute(6, 1); world->Connect(6, 7, 0); world->Connect(5, 8, 0); world->Connect(7, 8, 1); world->Connect(8, 23, 1);
    setup = world->Save(); setup["state"]["lanes"][1]["ammo"] = 10; setup["state"]["lanes"][1]["plainQuota"] = 10; setup["state"]["lanes"][1]["iceQuota"] = 70;
    Check(world->Load(setup, error), "quota diagnostic fixture");
    const auto before = world->Save(); std::string explanation;
    for (const auto& line : world->Explain(23)) explanation += line;
    Check(explanation.find("第3路有僵尸") != std::string::npos && explanation.find("第2路没有僵尸") != std::string::npos
        && explanation.find("且") != std::string::npos && explanation.find("配额已满：10/10") != std::string::npos, "real two-route expression and per-ammo blockage are explained");
    Check(before == world->Save(), "diagnosis never changes signals inventories or clocks");
    Check(PortLabel(world->Cells()[23], 0) == "A 启停" && PortLabel(world->Cells()[23], 1) == "B 方案"
        && std::string(SignalName(false)) == "关（低）", "port identity and signal value are separate concepts");
}
