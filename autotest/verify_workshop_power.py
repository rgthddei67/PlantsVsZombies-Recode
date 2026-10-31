"""Verify power teaching, real Board pause/restore and v5 grace without touching player saves."""
import json
from pathlib import Path
import sys

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_power")


def canonical(value):
    if isinstance(value, dict):
        return {key: canonical(item) for key, item in value.items()}
    if isinstance(value, list):
        if value and all(isinstance(item, dict) and "id" in item for item in value):
            value = sorted(value, key=lambda item: item["id"])
        return [canonical(item) for item in value]
    return value


def read(name):
    return canonical(json.loads((out / (name + ".json")).read_text(encoding="utf-8")))


assert read("status")["status"] == "passed"
assert read("demo_before") == read("demo_after"), "teaching altered the actual base or battle"
amounts = [read(name)["workshop"]["powerDemo"]["lanes"][0]["ammo"] for name in ("demo_all", "demo_manual", "demo_auto")]
assert amounts[1] > amounts[0] and amounts[2] == amounts[1], amounts
assert read("paused_before") == read("paused_after"), "paused simulation advanced factories or combat"
assert read("resume_before") == read("resume_after"), "native restore altered in-flight state"
before, legacy = read("resume_before"), read("legacy_loaded")
assert before["battle"] == legacy["battle"] and before["battleCheckpoint"] == legacy["battleCheckpoint"]
for section in ("state", "checkpoint"):
    expected = dict(before[section], gridLevel=4, legacyAssault=True)
    assert legacy[section] == expected, f"v5 migration changed {section} beyond its grid grant and grace"
running = read("native_running")["workshop"]
zombies = before["battle"]["zombies"]
assert len(zombies) >= 8 and all(z["row"] == running["focusRoutes"][0] for z in zombies[:3])
assert any(z["id"] > 7 and z["row"] == running["focusRoutes"][1] for z in zombies), "native Board did not follow the next announced route"
assert running["cells"][2]["producedItems"][3] >= 2, "sensor-controlled line never produced"
assert running["cells"][22]["producedItems"][3] == 0, "manually powered-off line produced"
assert running["cells"][42]["producedItems"][3] >= 2, "independent powered line stopped"
assert running["cells"][2]["power"] == 0, "full target magazine did not switch off A"
finished = read("finished")["workshop"]
assert not finished["modalOpen"] and not finished["guideOpen"] and not finished["gridOpen"]
assert finished["powerGuideSeen"] and finished["uiButtonCount"] == read("initial")["workshop"]["uiButtonCount"]
assert len(list(out.glob("*.png"))) == 14
print(f"Workshop power passed: teaching {amounts}, native dispatch, pause/restore, v5 grace, and UI cleanup")
