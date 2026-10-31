"""Compare independent workshop save/reload and recovery evidence after the visible smoke test."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop")


def read(name):
    return json.loads((out / name).read_text(encoding="utf-8"))


assert read("status.json")["status"] == "passed", "visible AutoTest did not pass"
assert read("before_reload.json") == read("after_reload.json"), "save/reload altered exported state"
native_before = read("native_before_reload.json")["workshop"]
native_after = read("native_after_reload.json")["workshop"]
for key in ("cells", "lanes", "nativePlants", "nativeZombies", "nativeBulletCount", "fired", "remainingSpawns", "hasShootingAnimation"):
    assert native_before[key] == native_after[key], f"native combat restore changed {key}"
combat = read("native_combat.json")["workshop"]
assert combat["nativeFrontline"] and combat["hasPeaHitParticles"]
assert all(p["shadow"] for p in combat["nativePlants"])
assert all(z["shadow"] for z in combat["nativeZombies"])
assert read("native_eating.json")["workshop"]["hasEatingZombie"]
tooltip = read("machine_tooltip.json")["workshop"]
assert tooltip["paused"] and tooltip["tooltipVisible"] and "2.0 秒/颗" in " ".join(tooltip["tooltipLines"])
recovered = read("recovered.json")["workshop"]
assert recovered["phaseName"] == "planning" and recovered["health"] == 3
assert recovered["machineCount"] == 2 and recovered["fired"] == 0
assert recovered["resourcesReady"] and not recovered["saveProtected"]
assert not recovered["saveFailed"]
assert read("menu_before.json")["adventureLevel"] == read("menu_after.json")["adventureLevel"], "workshop changed adventure progress"
assert read("menu_before.json")["haveCards"] == read("menu_after.json")["haveCards"], "workshop changed adventure unlocks"
for filename in ("01_main_menu.png", "02_new_base.png", "02_help.png", "03_supply.png", "03_machine_tooltip.png", "04_assault.png", "04_native_hit.png", "08_native_eating.png",
                 "04_factory_during_assault.png", "05_signals.png", "06_continued_base.png",
                 "07_reset_confirmation.png", "08_defeat.png"):
    assert (out / filename).stat().st_size > 0, f"missing screenshot: {filename}"
print("Workshop state round trip, independent progress, recovery and screenshot artifacts passed")
