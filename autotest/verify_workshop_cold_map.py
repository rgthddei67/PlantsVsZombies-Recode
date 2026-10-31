"""Verify factory camera input, typed production and native cold combat evidence."""
import json
import math
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_cold_map")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


def state(name):
    return read(name)["workshop"]


assert read("status")["status"] == "passed"
a, b = (state(n)["camera"] for n in ("zoom_before", "zoom_after"))
assert b["zoom"] > a["zoom"] and a["probeCell"] == b["probeCell"] == 84
assert abs(a["probeX"] - b["probeX"]) < 1 and abs(a["probeY"] - b["probeY"]) < 1, "wheel anchor drift"
a, b = (state(n)["camera"] for n in ("drag_before", "drag_after"))
assert math.isclose(b["x"] - a["x"], 40, abs_tol=1)
assert math.isclose(b["y"] - a["y"], 20, abs_tol=1)
assert read("cold") == read("cold_restored"), "cold factory round trip"
p = state("production")
assert p["processed"] >= 2 and p["iceFired"] > 0 and p["lanes"][0]["ammo"] == 0
c = state("ice_combat")
assert c["iceFired"] > 0 and c["fired"] == c["iceFired"], "ordinary plants consumed ice supply"
assert c["hasChilledZombie"] and c["hasIceHitParticles"]
assert all(p["shadow"] for p in c["nativePlants"]) and all(z["shadow"] for z in c["nativeZombies"])
for key in ("cells", "lanes", "nativePlants", "nativeZombies", "fired", "iceFired", "nativeBulletCount"):
    assert c[key] == state("ice_restored")[key], f"combat restore changed {key}"
assert read("windup") == read("windup_restored"), "cold windup restore changed snapshot"
for name in ("01_grid", "02_zoom_drag", "03_cold_tooltip", "04_expanded", "05_ice_combat"):
    assert (out / (name + ".png")).stat().st_size > 0

assert read("planning_before")["state"] == read("planning_after")["state"] == read("planning_restored")["state"], "preparation or reload advanced cold factory"
print("Factory camera, cold logistics, native combat and snapshot contracts passed")
