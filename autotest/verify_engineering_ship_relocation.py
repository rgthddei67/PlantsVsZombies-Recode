"""Verify a frozen moving ship shares physical/visible position and retains its route on load."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/clang-release/autotest/out"
SUITE = OUT / "smoke_engineering_ship_relocation"


def read(name):
    return json.loads((SUITE / f"{name}.json").read_text(encoding="utf-8"))


def displacement(ship):
    return round((ship["y"] - ship["shipLaneFromY"]) * 1000)


def verify():
    assert read("status")["status"] == "passed"
    log = (SUITE / "run.log").read_text(encoding="utf-8")
    assert "script finished OK" in log and "FAIL" not in log
    for prefix, row, low, high in (("early", 0, 100, 400), ("late", 1, 600, 900)):
        values = {
            name: read(f"{prefix}_{name}")["engineeringShip"]
            for name in ("moving", "frozen", "restored", "wrong_lane", "hit", "completed")
        }
        moving = values["moving"]
        assert low <= moving["shipLaneProgressOn1000"] <= high, (prefix, moving)
        assert moving["row"] == row and moving["shipLaneDestinationRow"] == 1
        assert displacement(moving) > 0, prefix
        # This compares relative displacement at a paused point, never a moving object's absolute Y.
        for name in ("frozen", "restored", "wrong_lane", "hit"):
            ship = values[name]
            assert ship["id"] == moving["id"] and ship["row"] == row
            assert ship["shipPhase"] == "RELOCATING" and ship["frozen"]
            assert ship["shipLaneDestinationRow"] == 1 and ship["shipLaneSwitchCount"] == 1
            assert abs(ship["shipLaneProgressOn1000"] - moving["shipLaneProgressOn1000"]) <= 20
            assert abs(displacement(ship) - displacement(moving)) <= 1700
            assert abs(ship["shipColliderVisualYDeltaOn1000"]
                       - moving["shipColliderVisualYDeltaOn1000"]) <= 1
        assert values["frozen"]["bodyHealth"] == moving["bodyHealth"] - 10
        assert values["restored"]["bodyHealth"] == values["frozen"]["bodyHealth"]
        assert values["wrong_lane"]["bodyHealth"] == values["restored"]["bodyHealth"]
        assert values["hit"]["bodyHealth"] == values["wrong_lane"]["bodyHealth"] - 20
        completed = values["completed"]
        assert completed["shipPhase"] == "CHARGING" and completed["row"] == 1
        assert completed["shipLaneSwitchCount"] == 1 and completed["shipLaneDestinationRow"] == -1
        assert abs(completed["terrainYOffsetOn1000"]) <= 1
    moving = read("anchor_moving")["engineeringShip"]
    settled = read("anchor_settled")["engineeringShip"]
    charging = read("anchor_charging")["engineeringShip"]
    assert moving["row"] == 1 and moving["shipLaneDestinationRow"] == 1
    assert 550 <= moving["shipLaneProgressOn1000"] <= 850
    for ship in (settled, charging):
        assert ship["id"] == moving["id"] and ship["row"] == 1
        assert ship["anchorRooted"] and ship["shipLaneDestinationRow"] == -1
        assert abs(ship["terrainYOffsetOn1000"]) <= 1
    assert charging["shipPhase"] == "CHARGING" and charging["shipLaneSwitchCount"] == 1
    print("Engineering ship relocation: continuous position, both frozen halves, save/load and real projectile hits passed.")


if __name__ == "__main__":
    verify()
