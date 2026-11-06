"""Visible native debut waves and save-stable random workshop compositions."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/clang-release/autotest/out/smoke_workshop_spawn_pool"
WAVES = (11, 16, 20, 24, 28, 30)


def generate():
    commands = [dict(op="wait_frames", value=90),
                dict(op="click", target="main_menu_workshop", hold_frames=6, trace=True),
                dict(op="wait_frames", value=5)]
    for wave in WAVES:
        commands += [dict(op="workshop", action="reset"), dict(op="wait_frames", value=5),
            dict(op="workshop_snapshot", action="save", name="starter"),
            dict(op="workshop_snapshot", action="load", name="starter", patch=[
                dict(op="add", path="/state/completedWaves", value=wave - 1),
                dict(op="add", path="/guideSeenWave", value=wave - 1)]),
            dict(op="wait_frames", value=4)]
        for _ in range(4):
            commands += [dict(op="workshop", action="guide_close"), dict(op="wait_frames", value=2)]
        commands += [dict(op="assert_state", path="workshop.resourcesReady", equals=True),
            dict(op="workshop", action="start"),
            dict(op="workshop_snapshot", action="save", name=f"plan_{wave}"),
            dict(op="wait_seconds", value=10), dict(op="workshop", action="pause"),
            dict(op="assert_state", path="workshop.fired", equals=0),
            dict(op="dump_state", name=f"wave_{wave}.json"),
            dict(op="screenshot", name=f"wave_{wave}.png"), dict(op="workshop", action="save"),
            dict(op="workshop", action="reload"), dict(op="wait_frames", value=4),
            dict(op="dump_state", name=f"restored_{wave}.json")]
    commands.append(dict(op="quit"))
    path = ROOT / "build/clang-release/autotest/generated/smoke_workshop_spawn_pool.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(name=path.stem, commands=commands), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(path)


def verify():
    def read(name):
        return json.loads((OUT / (name + ".json")).read_text(encoding="utf-8"))
    assert read("status")["status"] == "passed"
    newcomers = []
    for wave in WAVES:
        plan = read(f"plan_{wave}")["state"]["assaultTypes"]
        before, after = read(f"wave_{wave}")["workshop"], read(f"restored_{wave}")["workshop"]
        spawned = before["spawned"]
        actual = [z["type"] for z in sorted(before["nativeZombies"], key=lambda z: z["id"])]
        assert spawned >= 5 and actual == plan[:spawned], (wave, spawned, actual, plan)
        assert plan.count(plan[4]) == 1 and 0 in actual, (wave, plan)
        newcomers.append(plan[4])
        for key in ("assaultTypes", "spawnRandom", "spawned", "remainingSpawns", "nativeZombies"):
            assert before[key] == after[key], (wave, key)
    assert len(set(newcomers)) == len(WAVES), newcomers
    print("Workshop native spawn pools passed: six distinct guaranteed debuts, ordinary zombies, exact rolled order and save restoration.")


if __name__ == "__main__":
    generate() if "--generate" in sys.argv else verify()
