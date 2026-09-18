"""Check route benefit under echo fire and the same giant's actual smash animation."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_mine_pressure"


def read(name):
    return json.loads((OUT / f"{name}.json").read_text(encoding="utf-8"))


def giant(name):
    return read(name)["zombiesByType"]["ZOMBIE_GARGANTUAR"]


def verify():
    """Compare saved states; position samples are not used as timing assertions."""
    assert read("status")["status"] == "passed"
    before, after = read("team_shortcut_selected"), read("team_shortcut_opened")
    assert before["mine"]["rocks"][11] and not after["mine"]["rocks"][11]
    assert before["mine"]["distances"][17] - after["mine"]["distances"][17] == 1
    assert before["mine"]["distances"][2] == after["mine"]["distances"][2]
    assert read("approach_restored")["zombies"][0]["animExtraSpeedPct"] == 300
    drilling, opened = read("under_fire_drilling"), read("under_fire_opened")
    worker = opened["zombies"][0]
    assert worker["excavatorPhase"] == "SPENT" and opened["mine"]["pathValid"]
    assert 0 < worker["bodyHealth"] < drilling["zombies"][0]["bodyHealth"] < worker["bodyMaxHealth"]
    assert worker["animExtraSpeedPct"] == 100

    plain, buffed = giant("smash_plain"), giant("smash_buffed")
    assert plain["id"] == buffed["id"] and buffed["drumStacks"] == 1
    assert abs(buffed["effectiveAnimSpeed"] / plain["effectiveAnimSpeed"] - 1.75) < 0.001
    normal_frames = giant("smash_plain_progress")["animFrame"] - plain["animFrame"]
    faster_frames = giant("smash_buffed_progress")["animFrame"] - buffed["animFrame"]
    assert normal_frames > 0 and 1.60 < faster_frames / normal_frames < 1.90
    assert giant("smash_restored")["animExtraSpeedPct"] == buffed["animExtraSpeedPct"]
    # One golden-ice layer halves the giant's sub-1 base speed and doubles the >1 drum factor.
    golden = giant("smash_golden")
    assert golden["goldenIceEffectStacks"] == 1
    assert abs(golden["effectiveAnimSpeed"] / buffed["effectiveAnimSpeed"] - 1.0) < 0.001
    walking = giant("walking_after_smash")
    assert walking["animExtraSpeedPct"] == plain["animExtraSpeedPct"]
    assert giant("drum_expired")["drumStacks"] == 0
    print("PASS: team shortcut, construction under two echo mushrooms, approach save/load, 1.75x smash and recovery")


if __name__ == "__main__":
    verify()
