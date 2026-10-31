#include "Game/Workshop/Workshop.h"
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using namespace Workshop;
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAILED workshop circuits: " << reason << '\n'; std::exit(1); }
}
std::unique_ptr<World> Fixture(int wave = 8) {
    auto world = std::make_unique<World>(); auto save = world->Save(); std::string error;
    save["state"]["completedWaves"] = wave; save["state"]["energy"] = 10000;
    Check(world->Load(save, error), "unlocked fixture"); return world;
}
void Load(World& world, const nlohmann::json& save) {
    std::string error;
    if (!world.Load(save, error)) { std::cerr << error << '\n'; Check(false, "fixture or snapshot reload"); }
}
void Step(World& world, float seconds) {
    for (int n = 0; n < static_cast<int>(seconds * 60); ++n) {
        world.Tick(1.0f / 60);
        if (world.NextSpawnRow() >= 0) world.CommitSpawn();
    }
}
}

void TestWorkshopCircuits() {
    // 同一装填口既是计量来源，又由计数器选择路线，连续出货不能漏计或多送一份。
    auto batch = Fixture(); auto& b = *batch;
    Check(b.Build(2, Machine::Store, 0) && b.Build(3, Machine::Feeder, 0)
        && b.Build(4, Machine::Counter, 0), "batch line");
    Check(b.SetRoute(3, 0, 0) && b.SetRoute(3, 1, 1) && b.SetCounterMode(4, CounterMode::Transferred)
        && b.SetCounterOutput(4, CounterOutput::Toggle) && b.SetSetting(4, 10)
        && b.Connect(3, 4, 0) && b.Connect(4, 3, 1), "closed-loop batch routing");
    auto save = b.Save(); save["state"]["cells"][2]["stock"] = 20; Load(b, save);
    Check(b.StartAssault(), "batch start"); Step(b, 7);
    Check(b.Lanes()[0].ammo == 10 && b.Lanes()[1].ammo == 10 && b.Cells()[4].count == 0
        && !b.Cells()[4].signal && b.Cells()[3].transferredItems[0] == 20, "exact 10 / 10 repeated batch");
    auto replay = Fixture(); Load(*replay, b.Save()); Step(*replay, 1); Step(b, 1);
    Check(replay->Save() == b.Save(), "batch reload does not replay old output");
    Check(b.SetCounterMode(4, CounterMode::Produced) && b.Cells()[4].inputs[0] == -1
        && !b.Connect(3, 4, 0), "incompatible source removed on counting mode change");

    auto split = Fixture(); auto& s = *split;
    Check(s.Build(2, Machine::Splitter, 0) && s.Build(3, Machine::Store, 0)
        && s.Build(22, Machine::Store, 0) && s.Build(4, Machine::Switch, 0)
        && s.Build(5, Machine::Switch, 0) && s.SetStrictSplit(2, true)
        && s.Connect(4, 2, 1) && s.Connect(5, 2, 0), "strict routing controls");
    save = s.Save(); save["state"]["cells"][2]["stock"] = 3; save["state"]["cells"][22]["stock"] = 60;
    Load(s, save); Check(s.StartAssault(), "strict start"); Step(s, 1);
    Check(s.Cells()[2].stock == 3 && s.Cells()[3].stock == 0, "blocked selected exit does not fall back");
    Check(s.Configure(4), "choose forward"); Step(s, 1);
    Check(s.Cells()[3].stock == 3 && s.Cells()[22].stock == 60, "B low selects forward only");
    Check(s.Configure(5), "disable output"); Step(s, .5f);
    save = s.Save(); save["state"]["cells"][2]["stock"] = 1; Load(s, save); Step(s, 1);
    Check(s.Cells()[2].stock == 1, "A low really stops splitter output");

    auto cooler = Fixture(); auto& c = *cooler;
    Check(c.Build(2, Machine::Cooler, 0) && c.Build(4, Machine::Switch, 0)
        && c.Configure(4) && c.Connect(4, 2, 1) && c.Build(5, Machine::Counter, 0)
        && c.SetCounterMode(5, CounterMode::Produced) && c.Connect(2, 5, 0), "recipe selection and production counting");
    save = c.Save(); save["state"]["cells"][2]["stock"] = 2; save["state"]["cells"][2]["extraStock"][1] = 2;
    Load(c, save); Check(c.StartAssault(), "cooler start"); Step(c, 1);
    Check(c.Cells()[2].processing && c.Cells()[2].recipe == 0 && c.Configure(4), "switch during processing");
    Step(c, .5f); Check(c.ActivePlan(2) == 1 && c.Cells()[2].recipe == 0, "finish old unit before changing recipe");
    Step(c, 4); Check(c.Cells()[2].iceStock == 1 && c.Cells()[2].Inventory(Ammo::IceMelon) == 1
        && c.Cells()[2].stock == 1 && c.Cells()[2].Inventory(Ammo::Melon) == 1 && c.Cells()[5].count == 2,
        "both recipes conserve all stocks and count finished units");
    Load(*replay, c.Save()); Step(c, 3); Step(*replay, 3);
    Check(c.Save() == replay->Save(), "in-process recipe and observation survive reload");
    Check(c.SetFilter(5, static_cast<int>(Ammo::IceMelon)), "filter baseline reset"); Step(c, 1);
    Check(c.Cells()[5].count == 0, "new filter does not retro-count old production");

    // 库存器 + 非门/与门 + 记忆器让一台冷凝机自动补两类库存，到量后停机。
    auto shared = Fixture(); auto& q = *shared;
    Check(q.Build(2, Machine::Cooler, 0), "shared cooler");
    for (int i : {4, 5}) Check(q.Build(i, Machine::StockSensor, 0) && q.Connect(2, i, 0) && q.SetSetting(i, 2), "quota sensor");
    Check(q.SetFilter(4, 1) && q.SetFilter(5, 4) && q.Build(6, Machine::Latch, 0)
        && q.Connect(4, 6, 0) && q.Connect(5, 6, 1) && q.Connect(6, 2, 1)
        && q.Build(7, Machine::And, 0) && q.Connect(4, 7, 0) && q.Connect(5, 7, 1)
        && q.Build(8, Machine::Not, 0) && q.Connect(7, 8, 0) && q.Connect(8, 2, 0), "dual stock feedback");
    save = q.Save(); save["state"]["cells"][2]["stock"] = 5; save["state"]["cells"][2]["extraStock"][1] = 5;
    Load(q, save); Check(q.StartAssault(), "feedback start"); Step(q, 25);
    Check(q.Cells()[2].iceStock >= 2 && q.Cells()[2].Inventory(Ammo::IceMelon) >= 2 && !q.Enabled(2), "both quotas met and shared production stops");
    const auto outputCount = q.Cells()[2].producedItems; Step(q, 10);
    Check(q.Cells()[2].producedItems == outputCount, "feedback remains stopped without consuming extras");

    auto cannon = Fixture(); auto& n = *cannon;
    for (int i = 2; i <= 7; ++i) Check(n.Build(i, Machine::Cannon, 0), "unlimited affordable cannons");
    Check(n.SetRoute(2, 0, 0) && n.SetRoute(2, 3, 1) && n.SetFilter(2, 4, 1), "dual cannon profiles");
    save = n.Save(); save["state"]["cells"][2]["extraStock"] = {0, 6, 6, 0}; Load(n, save);
    const auto planning = n.Save(); Step(n, 100);
    Check(n.Save() == planning && !n.BeginCannonVolley(2), "new cannon cannot charge or fire during preparation");
    Check(n.StartAssault(), "cannon start"); Step(n, 11.9f); Check(!n.BeginCannonVolley(2), "twelve seconds required");
    Step(n, .2f); Check(n.BeginCannonVolley(2) && n.Cells()[2].Inventory(Ammo::Melon) == 3
        && n.Cells()[2].volleyRemaining == 3 && !n.BeginCannonVolley(2), "reserve exactly three once");
    n.CommitCannonShot(2); Check(n.SetPlan(2, 1), "switch during volley");
    Load(*replay, n.Save()); Step(n, .4f); Step(*replay, .4f);
    while (n.CannonShotReady(2)) n.CommitCannonShot(2);
    while (replay->CannonShotReady(2)) replay->CommitCannonShot(2);
    Check(n.Save() == replay->Save() && n.Cells()[2].transferredItems[3] == 3 && n.Cells()[2].transferredItems[4] == 0
        && n.Cells()[2].volleyRoute == 0, "mid-volley load preserves remaining shots and locked profile");
    Step(n, 12); Check(n.BeginCannonVolley(2) && n.Cells()[2].volleyRoute == 3 && n.Cells()[2].volleyAmmo == Ammo::IceMelon,
        "next volley uses second profile");
    Step(n, 180); n.CompleteAssault(); Check(n.State() == Phase::Assault, "wave completion waits for reserved volley");
    while (n.CannonShotReady(2)) n.CommitCannonShot(2);
    n.CompleteAssault(); Check(n.State() == Phase::Planning, "wave completes once volley emitted");
    auto locked = Fixture(6); Check(locked->Build(2, Machine::Cannon, 0) && !locked->SetFilter(2, 4, 1), "ice cannon profile locked until eight");

    // v3 的分流 A 是优先口，迁移为 B；旧门电路按原价退款，新建按新价退款。
    auto legacy = Fixture(); auto& l = *legacy;
    Check(l.Build(2, Machine::Splitter, 0) && l.Build(3, Machine::Switch, 0) && l.Build(4, Machine::Counter, 0), "legacy machines");
    save = l.Save(); save["schemaVersion"] = 3; save["state"]["cells"][2]["inputs"][0] = 3;
    save["checkpoint"] = save["state"]; save["state"]["phase"] = 2; save["state"]["health"] = 0;
    Load(l, save); Check(l.Cells()[2].inputs[0] == -1 && l.Cells()[2].inputs[1] == 3 && !l.Cells()[2].strictSplit,
        "old splitter meaning retained");
    Check(l.Retry() && l.Cells()[2].inputs[1] == 3 && l.RemoveRefund(3) == 15 && l.RemoveRefund(4) == 40,
        "checkpoint and historic prices migrated");
    Check(l.Build(5, Machine::Switch, 0) && l.Build(6, Machine::Counter, 0) && l.RemoveRefund(5) == 5 && l.RemoveRefund(6) == 15,
        "new controls use reduced prices");
    const auto original = l.Save(); save = original; save["state"]["cells"][3]["paidCost"] = -1; std::string error;
    Check(!l.Load(save, error) && l.Save() == original, "invalid new state rejected transactionally");
}
