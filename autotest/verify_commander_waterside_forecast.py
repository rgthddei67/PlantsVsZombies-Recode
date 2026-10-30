"""Verify numerical portraits against the same paused live entities and committed projectiles."""
import json
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_commander_waterside_forecast")
    read = lambda name: json.loads((folder / (name + ".json")).read_text(encoding="utf-8"))
    reports = {name: read(name) for name in ("birth", "paid", "live", "flight", "orphan", "impact")}
    assert read("status")["status"] == "passed"
    assert all(r["boardUnchanged"] for r in reports.values()), "forecast must not mutate the Board"
    types = ("ZOMBIE_HEAVY_DIVER", "ZOMBIE_SALVAGE", "ZOMBIE_SLUDGE", "ZOMBIE_LOTUS_PARASOL")
    capabilities = ("diver", "salvage", "sludge", "parasol")
    options = reports["birth"]["legalOptions"]
    # Heavy divers retain the formal water-row purchase restriction in this dry test map.
    assert not any(o["type"] == types[0] for o in options)
    assert all(o["type"] != "ZOMBIE_ENGINEERING_SHIP" for o in options)
    paid = reports["paid"]["watersideCurrent"]
    assert len(paid) == 4 and all(u["spawnAt"] == 10 and u["id"] == 0 for u in paid)
    for i, (kind, capability) in enumerate(zip(types, capabilities)):
        assert paid[i][capability]
        for option in (o for o in options if o["type"] == kind):
            assert option["waterside"][capability]
            assert option["waterside"]["health"] == paid[i]["health"]
    assert {o["type"] for o in options} == set(types[1:])
    for name in ("live", "flight", "impact"):
        state = read(name + "_state")
        zombies = {z["id"]: z for z in state["zombies"]}
        for u in reports[name]["watersideCurrent"]:
            z = zombies[u["id"]]
            assert u["health"] == z["bodyHealth"] + z["helmHealth"] + z["shieldHealth"]
            if u["diver"]:
                assert abs(u["smashRemaining"] * 1000 - z["diverSmashMs"]) <= 1
                assert abs(u["airRemaining"] * 1000 - z["diverAirMs"]) <= 1
            if u["salvage"]:
                assert u["salvageTarget"] == z["salvageTargetID"] and u["hook"] == z["salvageHasHook"]
                assert abs(u["salvageWindup"] * 1000 - z["salvageWindupMs"]) <= 1
                assert abs(u["salvageCooldown"] * 1000 - z["salvageCooldownMs"]) <= 1
            if u["sludge"]:
                assert abs(u["sludgeWindup"] * 1000 - z["sludgeWindupMs"]) <= 1
                assert abs(u["sludgeReload"] * 1000 - z["sludgeReloadMs"]) <= 1
            if u["parasol"]:
                assert u["parasolHealth"] * 2 == z["parasolHealthOn2"]
    live = reports["live"]["candidatePlans"][0]
    assert live["diverSmashes"] > 0 and live["salvages"] == 1
    assert live["sludgeShots"] > 0 and live["sludgeHits"] > 0 and live["parasolBlocks"] > 0
    flight = reports["flight"]["candidatePlans"][0]
    assert reports["flight"]["sludgeRays"] == reports["orphan"]["sludgeRays"] == 1
    assert flight["sludgeHits"] == flight["sludgeShots"] + 1
    orphan = reports["orphan"]["candidatePlans"][0]
    assert orphan["sludgeHits"] == 1 and orphan["sludgeShots"] == orphan["sludgeRepaired"] == 0
    assert reports["impact"]["sludgeRays"] == 0
    impact = read("impact_state")
    assert impact["normalPlantsByCell"]["2_5"]["sludgeRemainingMs"] > 0
    assert any(p["row"] == 2 and p["column"] == 5 and p["sludgeUntil"] > 0 for p in reports["impact"]["watersidePlants"])
    assert all(z.get("diverResourcesReady", True) for z in impact["zombies"])
    assert impact["coldStorage"]["spent"] == 0
    assert "coldStorage" not in read("area12_unchanged"), "forecast integration must not enable Area 12 commander mode"
    print("Four portraits, formal row restriction, paid births, live timers, independent mud and unchanged Board passed")


if __name__ == "__main__":
    main()
