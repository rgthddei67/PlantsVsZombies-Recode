"""Compare equal four-shroom investments across two-lane and three-lane deployment."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/probe_mine_strategy_defenses"


def read(name):
    return json.loads((OUT / f"{name}.json").read_text(encoding="utf-8"))


def verify():
    """These are established-defense probes, not tests of the opening economy or all nine levels."""
    assert read("status")["status"] == "passed"
    for name in ("two_lanes_start", "three_lanes_start"):
        start = read(name)
        assert start["mine"]["layoutRevision"] == 1
        assert len(start["plants"]) == 4
        assert all(p["type"] == "PLANT_ELITE_SCAREDYSHROOM" and p["growthShots"] == 0 for p in start["plants"])
    assert read("two_lanes_start")["mine"]["rocks"] == read("three_lanes_start")["mine"]["rocks"]
    lost, finished = read("two_lanes_result"), read("three_lanes_360s")
    assert lost["boardState"] == "LOSE_GAME"
    assert finished["boardState"] != "LOSE_GAME" and finished["wave"] == finished["maxWave"]
    assert finished["zombieCount"] == 0 and len(finished["plants"]) >= 3
    print("PASS: two-lane loss at wave", lost["wave"], "; three-lane completion with",
          len(finished["plants"]), "plants and", len(finished["mowers"]), "mowers remaining")


if __name__ == "__main__":
    verify()
