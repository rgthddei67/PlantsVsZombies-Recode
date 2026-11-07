"""Generate and verify native clockmaker scaling and workshop tutorial batching regressions."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = "smoke_workshop_review_fixes"
OUT = ROOT / "build/clang-release/autotest/out" / NAME


def generate():
    commands = []

    def op(kind, **args):
        commands.append(dict(op=kind, **args))

    def wait(frames=6):
        op("wait_frames", value=frames)

    def act(action):
        op("workshop", action=action)

    def check(path, value):
        op("assert_state", path="workshop." + path, equals=value)

    def snap(name):
        op("workshop_snapshot", action="save", name=name)

    def load(name, patches=()):
        op("workshop_snapshot", action="load", name=name,
           patch=[dict(op="add", path=key, value=value) for key, value in patches])
        wait()

    def close_guide():
        op("key", name="escape")
        wait()

    wait(90)
    op("click", target="main_menu_workshop", hold_frames=6)
    wait()
    act("practice_close")
    wait()
    act("guide_close")
    wait()
    snap("starter")
    op("workshop_tutorial_history", action="clear")
    load("starter", [("/state/completedWaves", 5), ("/guideSeenWave", 5), ("/powerGuideSeen", True)])
    check("guideTopic", 43)
    close_guide()
    check("guideOpen", False)
    for wave in (6, 8, 10):
        load("starter", [("/state/completedWaves", wave), ("/guideSeenWave", wave - 1),
                         ("/powerGuideSeen", True)])
        check("guideUnlockWave", wave)
        op("dump_state", name=f"guide_{wave}.json")
        op("screenshot", name=f"guide_{wave}.png")
        close_guide()
        check("guideOpen", False)
        check("guideSeenWave", wave)

    # Already acknowledged old waves still receive each newly added circuit exactly once.
    op("workshop_tutorial_history", action="clear")
    load("starter", [("/state/completedWaves", 10), ("/guideSeenWave", 10), ("/powerGuideSeen", True)])
    check("guideTopic", 43)
    close_guide()
    for topic in (25, 26, 27):
        check("guideTopic", topic)
        check("guideUnlockWave", -1)
        close_guide()
    check("guideOpen", False)

    for tag, challenge in (("normal", 0), ("armor", 2), ("heavy", 9)):
        act("reset")
        wait()
        snap("fresh")
        load("fresh", [("/state/completedWaves", 33), ("/guideSeenWave", 33),
                       ("/powerGuideSeen", True), ("/battle/plants", []), ("/battle/nextZombieID", 1)])
        act("start")
        act("pause")
        snap("assault")
        # Keep one pending spawn until actors exist, so an empty fixture cannot complete the wave.
        load("assault", [("/state/waveChallenge", challenge), ("/state/remainingSpawns", 1),
                         ("/state/spawned", 0), ("/state/assaultTypes", [0]), ("/state/spawnClock", 3)])
        kinds = ("ZOMBIE_BUCKET", "ZOMBIE_DOOR", "ZOMBIE_NORMAL", "ZOMBIE_POLAR_CLOCKMAKER")
        for kind, row in zip(kinds, (1, 2, 3, 2)):
            op("spawn_zombie", type=kind, row=row, x=900, stationary=True)
        for kind, damage in zip(kinds, (100, 100, 20)):
            op("damage_zombie", type=kind, damage=damage, source="OTHER")
        snap("populated")
        load("populated", [("/state/remainingSpawns", 0), ("/state/assaultTypes", [])])
        snap(tag + "_baseline")
        act("pause")
        op("wait_value", path="workshop.temporalAnchorCount", equals=1)
        act("pause")
        check("temporalAnchorTargetCount", 4)
        op("damage_zombie", type=kinds[0], damage=100000, source="OTHER")
        act("pause")
        op("wait_seconds", value=3)
        act("pause")
        # One target is fully removed, another is still a dying shell, and the third survives.
        op("damage_zombie", type=kinds[1], damage=100000, source="OTHER")
        op("damage_zombie", type=kinds[2], damage=20, source="OTHER")
        act("pause")
        op("wait_value", path="workshop.nativeZombiesByID.2.dying", equals=True)
        act("pause")
        snap(tag + "_before")
        load(tag + "_before", [("/battle/temporalAnchors/0/timer", 0.2)])
        snap(tag + "_roundtrip")
        act("pause")
        op("wait_value", path="workshop.temporalAnchorCount", equals=0)
        act("pause")
        for identity in (1, 2, 3, 4):
            check(f"nativeZombiesByID.{identity}.dying", False)
        snap(tag + "_restored")
        act("save")
        act("reload")
        wait()
        snap(tag + "_reloaded")
    op("quit")
    path = ROOT / "build/clang-release/autotest/generated" / (NAME + ".json")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(name=NAME, workshopTutorials=True, commands=commands), indent=2), encoding="utf-8")
    print(path)


def verify():
    def read(name):
        return json.loads((OUT / (name + ".json")).read_text(encoding="utf-8"))

    assert read("status")["status"] == "passed"
    for wave, topics in ((6, {21, 25}), (8, {26, 106}), (10, {23, 27, 107})):
        guide = read(f"guide_{wave}")["workshop"]
        assert topics <= set(guide["guideTopics"]), (wave, guide["guideTopics"])
        assert guide["guideUnlockWave"] == wave
        assert (OUT / f"guide_{wave}.png").exists()

    fields = ("bodyHealth", "bodyMaxHealth", "helmHealth", "helmMaxHealth", "shieldHealth", "shieldMaxHealth")
    for tag in ("normal", "armor", "heavy"):
        baseline = {z["id"]: z for z in read(tag + "_baseline")["battle"]["zombies"]}
        before = read(tag + "_before")
        actors = {z["id"]: z for z in before["battle"]["zombies"]}
        assert 1 not in actors and actors[2]["isDying"] and not actors[3]["isDying"], tag
        assert len(before["battle"]["temporalAnchors"]) == 1
        assert read(tag + "_roundtrip")["state"] == before["state"], "restore must not change factory accounts"
        for phase in ("restored", "reloaded"):
            document = read(tag + "_" + phase)
            restored = {z["id"]: z for z in document["battle"]["zombies"]}
            assert restored.keys() == baseline.keys(), (tag, phase)
            for identity in baseline:
                assert [restored[identity][f] for f in fields] == [baseline[identity][f] for f in fields], (tag, phase, identity)
            assert document["battle"]["temporalAnchors"] == []
    print("Workshop review regressions passed: batched/catch-up tutorials; normal, 1.5x and 2x temporal restoration across reload.")


if __name__ == "__main__":
    generate() if "--generate" in sys.argv else verify()
