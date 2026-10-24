"""Check real landing/chewing poses, bite damage and frozen snapshot continuity."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_waterside_flood_dolphin"


def read(name):
    return json.loads((ROOT / (name + ".json")).read_text(encoding="utf-8"))


def verify():
    assert read("status")["status"] == "passed"
    land = read("land_eating")
    samples = [read(f"land_loop_{i:02}") for i in range(10)]
    wet, bitten = read("water_eating"), read("water_bites")
    for row in (0, 5):
        cell = f"{row}_1"
        assert samples[-1]["topPlantsByCell"][cell]["health"] < land["topPlantsByCell"][cell]["health"]
        assert bitten["topPlantsByCell"][cell]["health"] < wet["topPlantsByCell"][cell]["health"]
    for state in samples:
        for zombie in state["zombies"]:
            assert zombie["isEating"] and not zombie["inPool"]
            assert zombie["track"] == "anim_eat"
            assert zombie["dolphinLandBodyTracksVisible"] == 8
            assert zombie["dolphinWaterTracksVisible"] == 0
    for breed in ("ZOMBIE_DOLPHIN_RIDER", "ZOMBIE_ELITE_DOLPHIN_RIDER"):
        frames = [state["zombiesByType"][breed]["animFrame"] for state in samples]
        assert any(b < a for a, b in zip(frames, frames[1:])), "chewing samples missed the loop seam"
    for before_name, after_name in (("land_loop_09", "land_restored"), ("water_bites", "water_restored")):
        before, after = read(before_name), read(after_name)
        a, b = ({z["id"]: z for z in state["zombies"]} for state in (before, after))
        assert a.keys() == b.keys(), "restore replaced a dolphin"
        for identity in a:
            for key in ("isEating", "inPool", "track", "dolphinPhase", "successfulDolphinJumps",
                        "dolphinLandBodyTracksVisible", "dolphinWaterTracksVisible"):
                assert a[identity][key] == b[identity][key], (identity, key)
    print("Dolphin flood regression passed: dry landing, complete land chew loop, water bites, saves and walking.")


if __name__ == "__main__":
    verify()
