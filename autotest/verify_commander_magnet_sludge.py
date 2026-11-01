"""Compare live magnetic cooldowns with the commander snapshot while mud is active and after expiry."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_commander_magnet_sludge")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


assert read("status")["status"] == "passed"
for name in ("mud", "clear"):
    live = read(name + "_live")["normalPlantsByCell"]["2_5"]
    report = read(name)
    assert report["boardUnchanged"], "forecast changed live combat"
    sample = next(p for p in report["watersidePlants"] if p["id"] == live["id"])
    assert (sample["sludgeUntil"] > 0) == (name == "mud")
    assert abs(sample["magnetRemaining"] * 1000 - live["magnetRechargeMs"]) <= 1, "temporary slowdown was baked into remaining time"
    assert abs(sample["magnetRecharge"] - 15) < .001, "temporary slowdown persisted into later cycles"
print("Live magnet cooldown sampling passed: active mud applied once, normal recharge preserved after expiry")
