"""Compare completed-wall counterfactuals; does not estimate construction survival or alter AI."""
import json
import sys
from collections import Counter
from pathlib import Path

DEFAULT_OUT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/probe_mine_wall_choices"


def read(directory, name):
    return json.loads((directory / f"{name}.json").read_text(encoding="utf-8"))


def roster(state):
    """Compare stable IDs and initial combat state before any branch advances time."""
    return {
        "zombies": [(z["id"], z["type"], z["row"], z["x"], z["bodyHealth"], z["helmHealth"])
                    for z in state["zombies"]],
        "plants": [(p["id"], p["type"], p["row"], p["col"], p["health"])
                   for p in state["plants"]],
    }


def metrics(start, state):
    """Use living combatants, lost plants and consumed/moving mowers, not sampled absolute X."""
    initial = Counter(p["type"] for p in start["plants"])
    remaining = Counter(p["type"] for p in state["plants"]
                        if p["health"] > 0 and not p.get("squished", False))
    zombies = [z for z in state["zombies"] if not z["isDying"] and not z["mindControlled"]]
    return {
        "live_zombies": len(zombies),
        "remaining_zombie_hp": sum(max(0, z["bodyHealth"]) + max(0, z["helmHealth"])
                                   + max(0, z["shieldHealth"]) for z in zombies),
        "sunflowers_lost": initial["PLANT_SUNFLOWER"] - remaining["PLANT_SUNFLOWER"],
        "echoes_lost": initial["PLANT_ECHOSHROOM"] - remaining["PLANT_ECHOSHROOM"],
        "mowers_used": sum(m["state"] == "IDLE" for m in start["mowers"])
                       - sum(m["state"] == "IDLE" for m in state["mowers"]),
        "zombies_by_row": dict(sorted(Counter(z["row"] for z in zombies).items())),
    }


def verify(directory):
    """Validate paired starting states, mute persistence and the observed plant-layout effect."""
    assert read(directory, "status")["status"] == "passed"
    report = {"scope": "Outcome conditional on a wall already being opened; AI remains geometry-only.",
              "cases": {}}
    for layout, economic_branch in [("upper_economy", "upper"), ("lower_economy", "lower")]:
        ai = read(directory, f"{layout}_ai")
        chosen = ai["zombiesByType"]["ZOMBIE_EXCAVATOR"]["excavatorWall"]
        # Baseline observation, not the desired contract for a future plant-aware planner.
        assert chosen == 5
        start = read(directory, f"{layout}_none_start")
        case = {"ai_wall": [chosen // 9, chosen % 9], "economic_branch": economic_branch,
                "branches": {}}
        for branch in ("none", "upper", "lower"):
            branch_start = read(directory, f"{layout}_{branch}_start")
            assert roster(branch_start) == roster(start), (layout, branch, "unpaired roster")
            assert branch_start["mine"]["distances"][8] == (8 if branch == "upper" else 10)
            assert branch_start["mine"]["distances"][44] == (8 if branch == "lower" else 10)
            result = {}
            for suffix in ("start", "20s", "45s"):
                state = read(directory, f"{layout}_{branch}_{suffix}")
                assert state["adaptiveMusic"]["volumePct"] == 0
                assert state["mine"]["fogStrength1000"] == 0 and state["mine"]["pathValid"]
                if suffix != "start":
                    result[suffix] = metrics(start, state)
            case["branches"][branch] = result
        economy = case["branches"][economic_branch]["45s"]
        echo = case["branches"]["lower" if economic_branch == "upper" else "upper"]["45s"]
        assert economy["sunflowers_lost"] > echo["sunflowers_lost"]
        assert economy["remaining_zombie_hp"] > echo["remaining_zombie_hp"]
        report["cases"][layout] = case
        print(directory.name, layout, "AI opens", case["ai_wall"], "economy/echo outcomes:", economy, echo)
    (directory / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    for output in map(Path, sys.argv[1:]) if len(sys.argv) > 1 else [DEFAULT_OUT]:
        verify(output)
    print("PASS: paired wall choices, swapped plant layouts and default music mute")
