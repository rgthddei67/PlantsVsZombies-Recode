"""Verify direction changes stay on the same mine edge and survive reloads."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out"


def zombie(suite, name):
    return json.loads((OUT / suite / name).read_text(encoding="utf-8"))["zombies"][0]


def check_turn(before, after, axis):
    """Compare relative movement and segment endpoints, independent of sample time."""
    other = "y" if axis == "x" else "x"
    offset = f"mineTargetOffset{axis.upper()}On1000"
    assert before["id"] == after["id"]
    assert before[offset] * after[offset] < 0, (before[offset], after[offset])
    assert after["mineTargetReturning"] and after["flipX"]
    assert abs(after[other] - before[other]) < 0.01
    # Dumps straddle the first moving step; an endpoint snap would jump tens of pixels.
    movement = after[axis] - before[axis]
    assert abs(movement) < 10, movement
    assert movement * after[offset] >= 0, movement
    assert abs(after["mineTargetCell"] - before["mineTargetCell"]) == (1 if axis == "x" else 9)


def check_reload(after, reloaded):
    assert after["id"] == reloaded["id"]
    assert after["mineTargetCell"] == reloaded["mineTargetCell"]
    assert reloaded["mineTargetReturning"] and reloaded["flipX"]


def main():
    suite = "smoke_mine_immediate_turn"
    for case, axis, row in (("horizontal", "x", 2),
                            ("vertical_before_midline", "y", 1),
                            ("vertical_after_midline", "y", 2)):
        before = zombie(suite, case + "_before.json")
        after = zombie(suite, case + "_after.json")
        assert before["row"] == row
        check_turn(before, after, axis)
        check_reload(after, zombie(suite, case + "_reloaded.json"))
    suite = "smoke_mine_third_retreat"
    before = zombie(suite, "before_turn.json")
    after = zombie(suite, "retreat_start.json")
    check_turn(before, after, "x")
    check_reload(after, zombie(suite, "retreat_reloaded.json"))
    assert before["theftCarried"] == 100 and after["theftCarried"] == 150
    assert after["theftEquipmentVisible"]
    print("Verified immediate theft/charm turns, both sides of the row midline, no teleport, and reloads.")


if __name__ == "__main__":
    main()
