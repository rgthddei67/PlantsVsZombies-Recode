"""Verify construction and follow-through with real troops; do not assert sampled positions."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_excavator_tactics"


def read(name):
    return json.loads((OUT / f"{name}.json").read_text(encoding="utf-8"))


def verify():
    """A lone worker declines; a supported worker commits, then the assault damages the defense."""
    assert read("status")["status"] == "passed"
    weak = read("weak_decision")["zombiesByType"]["ZOMBIE_EXCAVATOR"]
    assert weak["excavatorWall"] == -1 and weak["excavatorPhase"] == "RETRY"
    assert all(read("weak_20s")["mine"]["rocks"][cell] for cell in (5, 41))
    committed = read("strong_committed")
    assert committed["zombiesByType"]["ZOMBIE_EXCAVATOR"]["excavatorPhase"] == "SPENT"
    assert not committed["mine"]["rocks"][5]
    before, after = read("strong_decision"), read("strong_45s")
    living_plants = {p["id"] for p in after["plants"] if p["health"] > 0 and not p["squished"]}
    destroyed = [p for p in before["plants"] if p["id"] not in living_plants]
    assert any(p["type"] == "PLANT_WALLNUT" for p in destroyed)
    assert any(p["type"] == "PLANT_ECHOSHROOM" for p in destroyed)
    assert any(z["type"] == "ZOMBIE_REDEYE_GARGANTUAR" and not z["isDying"] for z in after["zombies"])
    assert after["adaptiveMusic"]["volumePct"] == 0 and after["mine"]["pathValid"]
    print("PASS: lone worker declines, supported construction commits, assault destroys",
          [(p["type"], p["row"], p["col"]) for p in destroyed])


if __name__ == "__main__":
    verify()
