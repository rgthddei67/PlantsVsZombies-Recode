"""Verify real workshop upgrade transactions and native battle snapshot evidence."""
import json
import math
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_upgrades")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


def state(name):
    return read(name)["workshop"]


def plants(document):
    return {(p["row"], p["column"]): p for p in document["battle"]["plants"]}


assert read("status")["status"] == "passed"
for name in ("level_two", "level_three"):
    before, after = state(name + "_before"), state(name + "_after")
    for key in ("cells", "lanes", "nativePlantsByCell", "energy", "produced", "fired", "completedWaves"):
        assert before[key] == after[key], f"{name} restore changed {key}"
    assert all(p["hp"] == 123 for p in after["nativePlants"]), "upgrade healed plants"
    assert not after["saveFailed"] and not after["saveProtected"]
assert state("level_two_after")["energy"] == 645
assert state("level_three_after")["energy"] == 1370
assert "200 颗" in " ".join(state("warehouse_tooltip")["tooltipLines"])
assert "0.9 秒" in " ".join(state("shooter_tooltip")["tooltipLines"])

shots = {}
for tier in ("base", "upgraded"):
    before, after = state(tier + "_rate_before"), state(tier + "_rate_after")
    shots[tier] = after["fired"] - before["fired"]
    reserved = sum(l["ammo"] for l in before["lanes"]) - sum(l["ammo"] for l in after["lanes"])
    assert shots[tier] <= reserved <= shots[tier] + 5, "one ammo per shot plus at most one pending shot per plant"
assert shots["base"] > 0 and shots["upgraded"] >= shots["base"] * 1.35, shots

before, upgraded, restored = (read(name) for name in ("windup_before", "windup_upgraded", "windup_restored"))
p0, p1 = plants(before)[(0, 2)], plants(upgraded)[(0, 2)]
assert p0["id"] == p1["id"] and p0["health"] == p1["health"]
e0, e1 = p0["extraData"], p1["extraData"]
assert e0["headAnimTrack"] == e1["headAnimTrack"] == "anim_shooting"
assert e0["headAnimFrame"] == e1["headAnimFrame"] and e0["shootTimer"] == e1["shootTimer"]
assert math.isclose(e1["headAnimClipSpeed"] / e0["headAnimClipSpeed"], 1.5 / 0.9, rel_tol=1e-5)
assert before["state"]["lanes"] == upgraded["state"]["lanes"], "upgrade charged reserved ammunition twice"
assert before["state"]["fired"] == upgraded["state"]["fired"], "upgrade replayed a shot"
assert plants(upgraded) == plants(restored), "windup restore changed native plant state"
finished = read("windup_finished")
assert sum(b["row"] == 0 for b in finished["battle"]["bullets"]) == 1, "upgraded windup did not emit exactly one pea"

defeat = state("defeat")
assert defeat["phaseName"] == "dead" and len(defeat["nativePlants"]) < 5
recovered = state("recovered_upgrades")
assert recovered["phaseName"] == "planning"
assert all(recovered["nativePlantsByCell"][f"{r}_2"]["level"] == 3 for r in range(5))
assert recovered["nativePlantsByCell"]["0_1"]["level"] == 1
assert read("workshop")["battle"]["plants"][0]["extraData"]["workshopUpgradeLevel"] == 4, "rejected file was overwritten"
for name in ("01_factory_upgrade.png", "02_shooter_upgrade.png", "03_upgraded_frontline.png"):
    assert (out / name).stat().st_size > 0
print(f"Workshop upgrades passed: payments, refunds, legacy saves, windup, recovery; six-second shots {shots}")
