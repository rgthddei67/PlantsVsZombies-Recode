"""Generate visible workshop challenge/trait checks, then verify native and saved evidence."""
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/clang-release/autotest/out/smoke_workshop_challenges"


def next_random(value):
    value ^= (value << 13) & 0xffffffff
    value ^= value >> 17
    return (value ^ (value << 5)) & 0xffffffff


def prize_seed(trait):
    for seed in range(1, 100000):
        first = next_random(seed)
        if trait == 7 and first % 100 == 29 and next_random(first) % 2 == 0:
            return seed
        if trait == 1 and first % 100 < 25 and next_random(first) % 5 == 1:
            return seed
    raise AssertionError("no deterministic prize seed")


def generate():
    commands = []

    def op(kind, **args):
        commands.append(dict(op=kind, **args))

    def wait(frames=5):
        op("wait_frames", value=frames)

    def act(action, index=None, type=None, value=None, expectedSuccess=None):
        op("workshop", action=action, **{key: val for key, val in locals().items()
            if key not in ("action", "op") and val is not None})

    def click(control):
        op("click", target="workshop_control", control=control, hold_frames=6, trace=True)
        wait()

    def check(key, value):
        op("assert_state", path="workshop." + key, equals=value)

    def snap(name):
        op("workshop_snapshot", action="save", name=name)

    def load(name, patches, settle=True):
        op("workshop_snapshot", action="load", name=name,
           patch=[dict(op="add", path=key, value=value) for key, value in patches])
        if settle:
            wait()

    def dump(name):
        op("dump_state", name=name + ".json")

    def guides():
        for _ in range(4):
            act("guide_close")
            wait(2)

    def offer(challenge, difficulty=2, sun=7200):
        values = dict(challenge=challenge, difficulty=difficulty, goal=5, target=1, progress=0,
                      timeLimit=0, remaining=0, status=0, targetWave=0, trait=9,
                      blueprint=6, legacy=False, sunReward=sun)
        return [("/state/commissions/offers/4/" + k, v) for k, v in values.items()]

    def base(name, patches=()):
        act("reset")
        wait()
        snap(name)
        load(name, [("/state/completedWaves", 15), ("/state/energy", 12000), ("/guideSeenWave", 15),
                    ("/battle/plants/0/health", 150)] + list(patches))
        guides()

    def finish(name, patches=()):
        # A saved final-clear edge, preserving the accepted task and frozen wave challenge.
        snap(name)
        load(name, [("/state/remainingSpawns", 0), ("/state/spawned", 0), ("/state/assaultTypes", []),
                    ("/battle/zombies", []), ("/battle/bullets", [])] + list(patches), settle=False)
        act("pause")  # caller is manually paused
        wait(6)

    wait(90)
    op("click", target="main_menu_workshop", hold_frames=6, trace=True)
    wait()
    base("ui_base", offer(17, sun=14400))
    click("commissions")
    op("screenshot", name="01_challenge_board.png")
    click("commission_details_4")
    op("screenshot", name="02_full_challenge_and_prize_pool.png")
    op("key", name="escape")
    wait()
    click("commission_accept_4")
    act("commission_accept", 3, expectedSuccess=False)
    click("commission_accept_0")
    check("commissions.active.0.targetWave", 16)
    click("commission_close")
    act("start")
    check("waveChallenge", 17)
    check("remainingSpawns", 70)
    op("wait_seconds", value=4)
    act("pause")
    dump("live_combination")
    op("screenshot", name="03_combined_assault.png")
    act("save")
    act("reload")
    wait()
    dump("restored_combination")
    act("commission_abandon", 0)
    check("waveChallenge", 17)
    dump("abandoned_combination")

    # Native birth health/armor and move-only multiplier; restored entities must not be rescaled.
    base("stats_base")
    act("start")
    for row, kind in enumerate(("ZOMBIE_TRAFFIC_CONE", "ZOMBIE_LADDER", "ZOMBIE_CATAPULT")):
        op("spawn_zombie", type=kind, row=row, x=950)
    wait(2)
    act("pause")
    dump("native_baseline")
    snap("native_stats")
    load("native_stats", [("/state/waveChallenge", 10)])
    dump("native_double_speed")
    load("native_stats", [("/state/waveChallenge", 9)])
    dump("restored_health_not_rescaled")
    for row, kind in ((3, "ZOMBIE_TRAFFIC_CONE"), (4, "ZOMBIE_LADDER")):
        op("spawn_zombie", type=kind, row=row, x=950)
    dump("native_double_health")

    # Formation lock and grid restriction stay active after abandon.
    base("locked_base", offer(18))
    act("commission_accept", 4)
    dump("power_preview")
    act("start")
    act("plant", 0, "pea", 1, expectedSuccess=False)
    act("commission_abandon", 0)
    act("plant", 0, "pea", 1, expectedSuccess=False)
    act("pause")
    dump("locked_after_abandon")
    finish("locked_clear")
    act("plant", 0, "pea", 1)
    check("phaseName", "planning")
    dump("restriction_ended")

    base("flawless_base", offer(7, difficulty=1, sun=1200))
    act("commission_accept", 4)
    act("start")
    op("spawn_zombie", type="ZOMBIE_NORMAL", row=0, x=-100)
    op("wait_seconds", value=1.2)
    act("pause")
    check("health", 2)
    check("commissions.active.0.status", 3)
    check("waveChallenge", 7)
    act("commission_claim", 0, expectedSuccess=False)
    dump("flawless_failed")

    # Reward outcome is rolled at completion, saved, and only applied after manual claim.
    for trait in (7, 1):
        tag = "legendary" if trait == 7 else "vitality"
        base(tag + "_base", offer(8) + [("/state/commissions/random", prize_seed(trait)),
             ("/state/commissions/offers/4/blueprint", 0)])
        click("commissions")
        click("commission_accept_4")
        click("commission_close")
        act("start")
        act("pause")
        finish(tag + "_clear")
        check("commissions.active.0.status", 2)
        check("commissions.active.0.trait", trait)
        click("commissions")
        op("screenshot", name="04_" + tag + "_reward.png")
        dump(tag + "_ready")
        click("commission_close")
        act("save")
        act("reload")
        wait()
        dump(tag + "_ready_restored")
        click("commissions")
        click("commission_claim_0")
        act("commission_claim", 0, expectedSuccess=False)
        check(f"commissions.traits.{trait}", 1)
        check("commissions.blueprints.0", True)
        check("commissions.freeUses.0", 1)
        dump(tag + "_claimed")
        click("commission_traits")
        op("screenshot", name="05_" + tag + "_owned_traits.png")
        op("key", name="escape")
        wait()
        click("commission_close")
        act("save")
        act("reload")
        wait()
        dump(tag + "_claimed_restored")
        if trait == 1:
            act("plant", 0, "pea", 1)
            dump("new_plant_vitality")
        else:
            act("start")
            snap("legendary_fire")
            load("legendary_fire", [("/state/remainingSpawns", 0), ("/state/spawned", 0), ("/state/assaultTypes", []),
                 ("/state/lanes/0/ammo", 1)], settle=False)
            op("spawn_zombie", type="ZOMBIE_GARGANTUAR", row=0, x=800, stationary=True)
            op("wait_seconds", value=4)
            act("pause")
            dump("legendary_native_damage")

    # Fifth-wave refresh and reward capacity use formal world and UI paths.
    base("refresh_base", [("/state/completedWaves", 4), ("/guideSeenWave", 4)])
    act("commission_accept", 0)
    snap("refresh_before")
    act("start")
    act("pause")
    finish("refresh_clear")
    check("completedWaves", 5)
    check("refreshWavesRemaining", 5)
    dump("refresh_after")
    base("legend_collection", offer(8) + [("/state/commissions/traits", [0, 0, 0, 0, 0, 0, 0, 1, 1])])
    click("commissions")
    op("screenshot", name="06_legendaries_collected.png")
    op("quit")
    path = ROOT / "build/clang-release/autotest/generated/smoke_workshop_challenges.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(name=path.stem, commands=commands), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(path)


def verify():
    def read(name):
        return json.loads((OUT / (name + ".json")).read_text(encoding="utf-8"))

    def state(name):
        return read(name)["workshop"]

    assert read("status")["status"] == "passed"
    before, after = state("live_combination"), state("restored_combination")
    for key in ("nativeZombies", "assaultTypes", "spawnRandom", "commissions", "waveChallenge"):
        assert before[key] == after[key], key
    assert before["spawned"] >= 5 and len(before["assaultTypes"]) == 70
    assert state("abandoned_combination")["waveChallenge"] == 17
    baseline = sorted(state("native_baseline")["nativeZombies"], key=lambda z: z["id"])
    speed = sorted(state("native_double_speed")["nativeZombies"], key=lambda z: z["id"])
    for a, b in zip(baseline, speed):
        assert a["moveSpeed"] > 0 and math.isclose(b["moveSpeed"], a["moveSpeed"] * 2, rel_tol=1e-5)
        assert a["track"] == b["track"] and a["hp"] == b["hp"]
    assert state("restored_health_not_rescaled")["nativeZombies"] == state("native_baseline")["nativeZombies"]
    armored = sorted(state("native_double_health")["nativeZombies"], key=lambda z: z["id"])
    for a, b in ((baseline[0], armored[3]), (baseline[1], armored[4])):
        for key in ("hp", "maxHp", "helmetHp", "helmetMaxHp", "shieldHp"):
            assert b[key] == a[key] * 2, (key, a, b)
    assert math.isclose(state("power_preview")["power"]["capacity"], state("restriction_ended")["power"]["capacity"] * .7, rel_tol=1e-5)
    for tag, trait in (("legendary", 7), ("vitality", 1)):
        assert state(tag + "_ready")["commissions"] == state(tag + "_ready_restored")["commissions"]
        assert state(tag + "_claimed")["commissions"] == state(tag + "_claimed_restored")["commissions"]
        for cell, plant in state(tag + "_claimed")["nativePlantsByCell"].items():
            restored_plant = state(tag + "_claimed_restored")["nativePlantsByCell"][cell]
            assert (plant["hp"], plant["maxHp"]) == (restored_plant["hp"], restored_plant["maxHp"])
        assert state(tag + "_ready")["commissions"]["traits"][trait] == 0
        assert math.isclose(state(tag + "_claimed")["energy"] - state(tag + "_ready")["energy"], 7200, abs_tol=.01)
    assert state("legendary_native_damage")["nativeZombies"][0]["hp"] == 2960
    p = state("vitality_claimed")["nativePlantsByCell"]["0_2"]
    assert p["hp"] == 165 and p["maxHp"] == 330, p
    p = state("new_plant_vitality")["nativePlantsByCell"]["0_1"]
    assert p["hp"] == p["maxHp"] == 330, p
    a, b = read("refresh_before")["state"]["commissions"], state("refresh_after")["commissions"]
    assert a["offers"][0]["id"] != b["offers"][0]["id"] and a["active"][0]["id"] == b["active"][0]["id"]
    print("Workshop challenges passed: native modifiers, restrictions, five-wave refresh, locked rare rewards, health ratio and final damage.")


if __name__ == "__main__":
    generate() if "--generate" in sys.argv else verify()
