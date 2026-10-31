#include "Game/Workshop/Workshop.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED workshop maintenance: " << message << '\n'; std::exit(1); }
}
}

void TestWorkshopMaintenance() {
    using namespace Workshop;
    auto world = std::make_unique<World>();
    auto fixture = world->Save();
    fixture["state"]["completedWaves"] = 9;
    fixture["state"]["energy"] = 9000;
    std::string error;
    Check(world->Load(fixture, error), "late factory fixture");
    Check(world->Build(2, Machine::MelonGrower, 0) && world->Build(3, Machine::Belt, 0)
        && world->Build(4, Machine::Splitter, 0) && world->Build(5, Machine::Feeder, 0)
        && world->Build(24, Machine::Belt, 3) && world->Build(25, Machine::Belt, 3)
        && world->Build(45, Machine::Store, 3) && world->Build(6, Machine::Belt, 0), "branched line with cycle and warehouse boundary");
    fixture = world->Save();
    for (int index : {2, 3, 4, 5, 6, 24, 25, 45}) fixture["state"]["cells"][index]["extraStock"] = {0, 1, 0, 0};
    fixture["state"]["cells"][5]["extraStock"] = {0, 2, 1, 0};
    Check(world->Load(fixture, error), "load old melon cargo");
    Check(world->SetFilter(5, static_cast<int>(Ammo::IceMelon)), "switch feeder to ice melon");
    Check(world->Cells()[5].Inventory(Ammo::Melon) == 2, "filter change preserves stock");
    const auto preview = world->ObsoleteCargo(5);
    Check(preview[3] == 6 && preview[4] == 0, "preview follows upstream belts and splitter once, excludes correct ammo");
    const auto before = world->Save();
    Check(world->ClearObsoleteCargo(5), "explicit cleanup succeeds");
    Check(world->Cells()[2].Inventory(Ammo::Melon) == 1 && world->Cells()[45].Inventory(Ammo::Melon) == 1
        && world->Cells()[6].Inventory(Ammo::Melon) == 1, "does not clear producer, warehouse, or outgoing belt");
    Check(world->Cells()[5].Inventory(Ammo::IceMelon) == 1 && world->Save()["state"]["lanes"] == before["state"]["lanes"], "correct stock and frontline unchanged");
    Check(!world->ClearObsoleteCargo(5) && world->Energy() == before["state"]["energy"].get<float>(), "repeat cleanup is no-op without refund or payment");
    Check(world->SetAmmoQuota(0, Ammo::IceMelon, 80) && world->StartAssault(), "allow new ammo delivery");
    world->Tick(.6f);
    Check(world->Lanes()[0].Inventory(Ammo::IceMelon) == 1, "line resumes with new ammo without rebuilding");

    world->Reset(); fixture = world->Save();
    for (auto& cell : fixture["state"]["cells"]) { cell["type"] = "empty"; cell["level"] = 1; }
    fixture["state"]["completedWaves"] = 9;
    for (int i = 0; i < 25; ++i) {
        const int index = i / 10 * MaxColumns + i % 10;
        fixture["state"]["cells"][index]["type"] = "solar";
        fixture["state"]["cells"][index]["level"] = 3;
    }
    Check(world->Load(fixture, error) && std::abs(world->SolarProduction() - 25.8f) < .001f, "25 level-three collectors match approved diminishing return");
    const float energy = world->Energy();
    world->Tick(2); Check(world->Energy() == energy, "planning still freezes solar");
    Check(world->StartAssault(), "start solar test"); world->Tick(2);
    Check(std::abs(world->Energy() - energy - 51.6f) < .02f, "effective rate is actual simulation income");
    for (auto& cell : fixture["state"]["cells"]) { cell["type"] = "empty"; cell["level"] = 1; }
    for (int i = 0; i < 7; ++i) {
        fixture["state"]["cells"][i]["type"] = "solar";
        fixture["state"]["cells"][i]["level"] = i == 0 ? 1 : 3;
    }
    Check(world->Load(fixture, error) && std::abs(world->SolarProduction() - 14.7f) < .001f, "six higher-level collectors receive full-rate slots regardless of build order");
    fixture["state"]["cells"][0]["type"] = "empty";
    Check(world->Load(fixture, error) && std::abs(world->SolarProduction() - 14.4f) < .001f, "first six collectors keep original rate");
}
