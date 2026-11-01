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
    Check(p.Act("belt") && !p.Act("cell", 24) && p.Model().Save() == empty && p.Step() == 0, "wrong placement does not pass or spend supplies");
    Check(p.Act("station") && p.Act("cell", Practice::PowerBox) && p.Step() == 1, "connect station to box");
    Wire(p, "wire_power", Practice::PowerBox, Practice::Grower);
    Check(p.Step() == 2, "connect box to processor");
    Check(p.Act("belt") && p.Act("cell", Practice::Conveyor) && p.Step() == 3, "belt placement required");
    Check(p.Act("feeder") && p.Act("cell", Practice::Feeder) && p.Step() == 4, "feeder placement");
    Check(p.Act("route") && p.Step() == 5, "configure target route"); p.Advance(6);
    Check(p.Complete() && p.Model().Lanes()[2].ammo > 0 && !p.Running(), "actual delivery completes lesson and pauses");

    p.Reset(1); p.Advance(1);
    Check(p.Step() == 1 && p.Model().Power().speed < 1, "observe overload");
    p.Act("cell", Practice::PowerBox); p.Act("priority"); Check(p.Step() == 2, "set priority");
    p.Advance(1); Check(p.Step() == 3 && p.Model().MachineSpeed(Practice::Grower) == 1, "observe priority allocation");
    p.Act("cell", Practice::BackgroundBox); p.Act("power"); Check(p.Step() == 4, "turn off background branch");
    p.Advance(1); Check(p.Complete() && p.Model().Power().speed == 1, "actual power release");

    p.Reset(2); p.Act("cell", Practice::Sensor); p.Act("route"); p.Act("route");
    Check(p.Step() == 1, "enemy route configured");
    Wire(p, "wire_a", Practice::Sensor, Practice::PowerBox); p.Advance(1);
    Check(p.Step() == 1 && p.Hint().find("A") != std::string::npos, "wrong port cannot pass");
    p.Act("disconnect"); Wire(p, "wire_b", Practice::Sensor, Practice::PowerBox);
    Check(p.Step() == 2, "correct B priority connection"); p.Act("enemy", 1); p.Advance(1);
    Check(p.Step() == 3 && p.Model().ActivePlan(Practice::PowerBox) == 1, "enemy gives branch priority");
    p.Act("enemy", 0); p.Advance(1);
    Check(p.Complete() && p.Model().ActivePlan(Practice::PowerBox) == 0, "no enemy returns to normal priority");

    p.Reset(3); p.Act("cell", Practice::Sensor); p.Act("route"); p.Act("route");
    p.Act("filter"); for (int i = 0; i < 5; ++i) p.Act("setting", -1);
    Check(p.Step() == 3, "stock route material and threshold configured by player");
    Wire(p, "wire_a", Practice::Sensor, Practice::Inverter); Wire(p, "wire_a", Practice::Inverter, Practice::PowerBox);
    Check(p.Step() == 5, "two actual A connections required");
    p.Act("stock", 5); Check(p.Step() == 5, "stock injection does not pretend circuit already propagated"); p.Advance(1);
    Check(p.Step() == 6 && !p.Model().Enabled(Practice::PowerBox) && p.Model().Cells()[Practice::Sensor].signal, "high stock propagates through inverter and stops factory");
    p.Act("stock", 0); p.Advance(5);
    Check(p.Complete() && p.Model().Enabled(Practice::PowerBox) && p.Model().Produced() > 0, "low stock restarts actual production");
    p.Reset(0); Check(p.Step() == 0 && p.Model().Produced() == 0 && p.Model().Lanes()[2].ammo == 0, "replay clears only the practice world");

    // 独立题只检查生产结果，不要求特定接线顺序；错误分组和手动关机不能冒充自动控制。
    auto configureChallenge = [&](int threshold, bool separate) {
        p.Reset(Practice::ChallengeLesson);
        Check(p.HighlightCell() == -1 && p.HighlightControl().empty(), "challenge gives no placement or action answer");
        if (separate) {
            p.Act("station"); p.Act("cell", Practice::BackgroundBox);
            Wire(p, "wire_power", Practice::BackgroundBox, Practice::Grower);
        }
        p.Act("inspect"); p.Act("cell", Practice::Sensor); p.Act("route"); p.Act("route"); p.Act("filter");
        p.Act("setting", threshold - p.Model().Cells()[Practice::Sensor].setting);
        Wire(p, "wire_a", Practice::Sensor, Practice::Inverter);
        Wire(p, "wire_a", Practice::Inverter, separate ? Practice::BackgroundBox : Practice::PowerBox);
    };
    auto finishVerification = [&] {
        for (int i = 0; i < 14 && p.Verifying(); ++i) p.Advance(1);
    };
    configureChallenge(5, false); p.Act("verify"); finishVerification();
    Check(!p.Complete() && p.Notice().find("第2路") != std::string::npos, "stopping both lines fails independent grouping");
    configureChallenge(6, true); p.Act("verify"); finishVerification();
    Check(!p.Complete() && p.Notice().find("自动停机") != std::string::npos, "too-high threshold fails full-stock stop");
    configureChallenge(4, true); p.Act("verify"); finishVerification();
    Check(!p.Complete() && p.Notice().find("重新生产") != std::string::npos, "too-low threshold fails low-stock recovery");
    configureChallenge(5, true); p.Act("inspect"); p.Act("cell", Practice::Grower); p.Act("power");
    p.Act("verify"); finishVerification();
    Check(!p.Complete(), "manual processor off cannot pass automated stop");
    configureChallenge(5, true);
    Check(p.Act("verify") && p.Verifying() && !p.Act("stock", 0) && !p.Act("wire_a"), "verification locks configuration and injected stock");
    p.Advance(4.1f);
    Check(p.Step() == 1 && p.Verifying() && p.Model().Lanes()[2].ammo == 4, "automatic stop passes before testing real recovery");
    const auto backgroundProduced = p.Model().Cells()[Practice::BackgroundGrower].producedItems[0];
    finishVerification();
    Check(p.Complete() && !p.Verifying() && !p.Running() && p.Model().Lanes()[2].ammo >= 5
        && p.Model().Cells()[Practice::Grower].producedItems[0] > 0
        && p.Model().Cells()[Practice::BackgroundGrower].producedItems[0] > backgroundProduced,
        "independent challenge requires delivery and uninterrupted background production in both phases");
    p.Reset(Practice::ChallengeLesson);
    Check(!p.Complete() && !p.Verifying() && p.Model().Produced() == 0, "challenge replay resets validation and practice only");

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
