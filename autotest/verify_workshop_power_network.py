"""Verify isolated power tutorials, native repair transactions and explicit old-save reset."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_power_network")
def canonical(value):
    if isinstance(value, dict):
        return {k: canonical(v) for k, v in value.items()}
    if isinstance(value, list):
        if value and all(isinstance(v, dict) and "id" in v for v in value):
            value = sorted(value, key=lambda v: v["id"])
        return [canonical(v) for v in value]
    return value

def read(name):
    return canonical(json.loads((out / (name + ".json")).read_text(encoding="utf-8")))

assert read("status")["status"] == "passed"
for before, after in (("starter", "after_basics"), ("demo_before", "demo_after"), ("paused_before", "paused_after")):
    assert read(before) == read(after), f"{before}: tutorial or pause changed the actual base/battle"
amounts = [read(f"demo_{i}")["workshop"]["powerDemo"]["lanes"][0]["ammo"] for i in range(3)]
assert amounts[1] > amounts[0] and amounts[2] == amounts[1], amounts
base = read("before_repair")["workshop"]
for name in ("after_manual", "after_auto"):
    state = read(name)["workshop"]
    assert abs(base["energy"] - state["energy"] - 100) < .01, name
    assert state["nativePlantsByCell"]["2_5"]["hp"] == base["nativePlantsByCell"]["2_5"]["hp"] + 1000
    assert state["lanes"][2]["extraAmmo"][3] == base["lanes"][2]["extraAmmo"][3] - 1
assert read("no_kit")["workshop"]["energy"] == base["energy"]
assert read("no_kit")["workshop"]["nativePlantsByCell"]["2_5"]["hp"] == 4000
assert read("wired")["state"]["cells"][21]["powerSource"] == 40
backup = json.loads((out / "workshop.json.before-power.json").read_text(encoding="utf-8"))
assert backup["schemaVersion"] == 6 and read("reset_new")["schemaVersion"] == 7
final = read("final")["workshop"]
assert final["phase"] == 0 and not final["modalOpen"]
assert final["uiButtonCount"] == read("initial")["workshop"]["uiButtonCount"], "modal controls leaked"
assert len(list(out.glob("*.png"))) == 14
print(f"Power network passed: four real-click lessons, isolated teaching {amounts}, power save, manual/auto repair, old-save backup/reset")
