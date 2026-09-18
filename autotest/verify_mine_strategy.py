"""Validate current adventure maps, frozen forecasts, legal formations and save restoration."""
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/clang-release/autotest/out/smoke_mine_strategy"
DATA = json.loads((ROOT / "build/clang-release/resources/gamedata.json").read_text(encoding="utf-8"))


def read(name):
    return json.loads((OUT / f"{name}.json").read_text(encoding="utf-8"))


def verify():
    """Check all nine levels; formation must preserve the announced roster and per-level pools."""
    assert read("status")["status"] == "passed"
    before, after = read("legacy1_before"), read("legacy1_after")
    for field in ("layoutRevision", "rocks", "entrances", "wavePlan"):
        assert before["mine"][field] == after["mine"][field], ("legacy1", field)
    assert before["mine"]["layoutRevision"] == 1
    assert [(p["row"], p["col"]) for p in after["plants"]] == [(2, 4)]
    configs = json.loads((ROOT / "build/clang-release/resources/spawnlists.json").read_text(encoding="utf-8"))
    names, index = {}, 0
    for line in (ROOT / "PlantVsZombies/Game/Zombie/ZombieType.h").read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*(ZOMBIE_\w+)\s*(?:=\s*(\d+))?\s*,", line)
        if match:
            index = int(match[2]) if match[2] else index
            names[index] = match[1]
            index += 1
    main_forecasts = supported_groups = multi_lane_waves = 0
    for config in configs:
        level = config["level"]
        if not 73 <= level <= 81:
            continue
        initial, restored = read(f"level{level}_initial"), read(f"level{level}_restored")
        assert initial["maxWave"] == config["waves"] and initial["sun"] == config["sun"]
        assert initial["spawnList"] == [names[t] for t in config["zombies"]]
        assert initial["mine"]["layoutRevision"] == 2 and initial["mine"]["pathValid"]
        if level >= 77:
            assert initial["mine"]["rockCount"] == 12
        assert sum(initial["mine"]["entrances"]) == (3 if level < 79 else 4)
        for field in ("layoutRevision", "rocks", "entrances", "wavePlan"):
            assert initial["mine"][field] == restored["mine"][field], (level, field)
        saved, loaded = read(f"level{level}_saved_wave"), read(f"level{level}_restored_wave")
        for field in ("layoutRevision", "rocks", "entrances", "wavePlan", "forecastMainMask"):
            assert saved["mine"][field] == loaded["mine"][field], (level, field)
        for wave in range(1, min(config["waves"], 32) + 1):
            state = read(f"level{level}_plan{wave:02}")
            mine = state["mine"]
            plan = mine["wavePlanDetails"]
            points = Counter()
            for unit in plan:
                assert unit["type"] in initial["spawnList"]
                assert mine["entrances"][unit["row"]]
                if unit["type"] == "ZOMBIE_ZAMBONI":
                    assert unit["row"] == 0
                points[unit["row"]] += DATA["zombies"][unit["type"]]["weight"]
            multi_lane_waves += len(points) >= 3
            total, largest = sum(points.values()), max(points.values(), default=0)
            expected_mask = sum(1 << row for row, value in points.items() if value == largest) if total and largest * 5 >= total * 2 else 0
            assert mine["forecastMainMask"] == expected_mask
            main_forecasts += bool(expected_mask)
            supported_groups += any(u["role"] == 2 and any(v["role"] == 1 and u["row"] == v["row"] for v in plan) for u in plan)
            if wave in (3, 5, 15, 30):
                actual = read(f"level{level}_spawn{wave:02}")
                roster = [(z["type"], z["row"]) for z in actual["zombies"] if z["spawnWave"] == wave and not z["isDying"]]
                assert Counter(roster) == Counter((u["type"], u["row"]) for u in plan), (level, wave, "forecast mismatch")
                # Original picker may round its final remainder up to the cheapest unit; formation adds none.
                cheapest = min(DATA["zombies"][name]["weight"] for name in initial["spawnList"])
                if level in (75, 79) and wave == 3:
                    # Existing fixed teaching groups intentionally replace the ordinary third-wave budget.
                    theme = "ZOMBIE_CRYSTAL_HORN_MINER" if level == 75 else "ZOMBIE_CRYSTAL_DRUMMER"
                    assert Counter(u["type"] for u in plan) == Counter({theme: 1, "ZOMBIE_NORMAL": 2})
                else:
                    assert total <= actual["waveZombiePoints"] + cheapest, (level, wave, total, actual["waveZombiePoints"])
        print(f"PASS level {level}: {sum(initial['mine']['entrances'])} entrances, {config['waves']} waves, pool and saves intact")
    assert main_forecasts > 0 and supported_groups > 0 and multi_lane_waves > 0
    print("PASS: main forecasts", main_forecasts, "supported formations", supported_groups, "multi-lane waves", multi_lane_waves)


if __name__ == "__main__":
    verify()
