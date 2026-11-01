"""Check tutorial UI routing, isolated independent practice and actual branch behavior."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_teaching_clarity")


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
assert read("starter") == read("after_shortcut"), "selecting a catalog shortcut must not build or charge"
assert read("before_practice") == read("after_practice"), "practice changed the actual base"
b = read("supply_and_speed")["workshop"]["practice"]["world"]["cells"][41]["branch"]
assert b["demand"] == 12 and b["allocated"] == 10
assert abs(b["speed"] - (10 / 12) ** 2) < .0001
p = read("independent_passed")["workshop"]["practice"]
w = p["world"]
assert p["complete"] and not p["verifying"] and not p["running"]
assert p["highlightCell"] == -1 and p["highlightControl"] == ""
assert w["cells"][21]["powerSource"] == 41 and w["cells"][25]["powerSource"] == 40
assert w["lanes"][2]["ammo"] >= 5 and w["lanes"][1]["ammo"] > 0
initial, final = read("initial")["workshop"], read("final")["workshop"]
assert final["resourcesReady"] and not final["modalOpen"]
assert final["uiButtonCount"] == initial["uiButtonCount"], "modal controls leaked"
assert len(list(out.glob("*.png"))) == 11
print("Teaching clarity passed: named entries, paused signals, distinct percentages, real-click independent grouping, untouched base")
