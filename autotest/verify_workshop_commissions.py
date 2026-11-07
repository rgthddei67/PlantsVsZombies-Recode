"""Generate and verify visible commission transactions and isolated native reward combat."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/clang-release/autotest/out/smoke_workshop_commissions"


def generate():
    commands = []

    def op(kind, **kwargs):
        commands.append(dict(op=kind, **kwargs))

    def wait(frames=5):
        op("wait_frames", value=frames)

    def act(action, index=None, type=None, value=None, expectedSuccess=None):
        op("workshop", action=action, **{k: v for k, v in locals().items()
            if k not in ("action", "op") and v is not None})

    def check(path, value):
        op("assert_state", path="workshop." + path, equals=value)

    def click(control):
        op("click", target="workshop_control", control=control, hold_frames=6, trace=True)
        wait()

    def snap(name):
        op("workshop_snapshot", action="save", name=name)

    def load(name, patch, settle=True):
        op("workshop_snapshot", action="load", name=name,
           patch=[dict(op="add", path=p, value=v) for p, v in patch])
        if settle:
            wait()

    def dump(name):
        op("dump_state", name=name + ".json")

    def clear_guides():
        for _ in range(4):
            act("guide_close")
            wait(2)

    def old_business_offer(slot):
        return [(f"/state/commissions/offers/{slot}/" + key, value) for key, value in
            {"difficulty": 2, "challenge": 0, "legacy": True, "timeLimit": 720, "remaining": 720, "goal": 0, "sunReward": 450}.items()]

    def tasks_two(a, b):
        click("commissions")
        click(f"commission_accept_{a}")
        click(f"commission_accept_{b}")
        click("commission_close")

    wait(90)
    op("click", target="main_menu_workshop", hold_frames=6, trace=True)
    wait()
    check("resourcesReady", True)
    snap("starter")
    load("starter", [("/state/completedWaves", 14), ("/state/energy", 3000), ("/guideSeenWave", 14),
        ("/state/commissions/offers/0/goal", 0), ("/state/commissions/offers/0/target", 2),
        ("/state/commissions/offers/1/goal", 1), ("/state/commissions/offers/1/target", 2)])
    clear_guides()
    act("build", 1, "grower", 0)
    act("wire_power", 1, value=40)
    act("build", 2, "belt", 0)
    act("build", 3, "feeder", 0)
    click("commissions")
    op("screenshot", name="01_task_board.png")
    click("commission_accept_0")
    click("commission_accept_1")
    act("commission_accept", 2, expectedSuccess=False)
    dump("accepted")
    wait(90)
    dump("paused_tasks")
    op("screenshot", name="02_active_tasks.png")
    click("commission_close")
    act("start")
    op("wait_seconds", value=8)
    click("commissions")
    check("commissions.active.0.status", 2)
    check("commissions.active.1.status", 2)
    dump("ready")
    op("screenshot", name="03_ready_rewards.png")
    click("commission_claim_0")
    click("commission_claim_0")
    act("commission_claim", 0, expectedSuccess=False)
    dump("claimed")
    click("commission_close")
    act("pause")
    snap("earned")
    load("earned", old_business_offer(4) + old_business_offer(5) + [("/state/commissions/offers/4/blueprint", 0), ("/state/commissions/offers/4/goal", 0),
        ("/state/commissions/offers/4/target", 1), ("/state/commissions/offers/5/blueprint", 1),
        ("/state/commissions/offers/5/goal", 0), ("/state/commissions/offers/5/target", 1)])
    tasks_two(4, 5)
    act("pause")
    op("wait_seconds", value=3)
    click("commissions")
    op("screenshot", name="04_blueprint_rewards.png")
    click("commission_claim_0")
    click("commission_claim_0")
    dump("blueprints")
    click("commission_close")
    act("pause")
    check("commissions.blueprints", [True, True, False, False, False, False])
    check("commissions.freeUses", [1, 1, 0, 0, 0, 0])
    dump("before_free_machine")
    act("build", 4, "pressure_grower", 0)
    act("wire_power", 4, value=40)
    check("cells.4.paidCost", 0)
    check("commissions.freeUses.0", 0)
    dump("free_machine")
    act("remove", 4)
    dump("removed_free_machine")
    act("build", 4, "pressure_grower", 0)
    check("cells.4.paidCost", 200)
    act("wire_power", 4, value=40)
    act("plant", 0, "heavy_gatling", 2)
    check("commissions.freeUses.1", 0)
    act("plant", 0, "heavy_gatling", 2, expectedSuccess=False)
    snap("two_rewards")
    load("two_rewards", [("/state/commissions/nextId", 100), ("/state/commissions/offers/4/id", 99),
        ("/state/commissions/offers/4/status", 0), ("/state/commissions/offers/4/blueprint", 2)])
    click("commissions")
    click("commission_accept_4")
    click("commission_close")
    act("pause")
    op("wait_seconds", value=3)
    click("commissions")
    click("commission_claim_0")
    click("commission_close")
    act("pause")
    check("commissions.blueprints", [True, True, True, False, False, False])
    check("commissions.freeUses.2", 1)
    dump("all_rewards")
    act("view_front")
    click("plant_catalog")
    op("screenshot", name="05_reward_plant_catalog.png")
    op("key", name="escape")
    wait()
    act("plant", 0, "blaze_torch", 3)
    check("commissions.freeUses.2", 0)
    snap("reward_roundtrip")
    act("reload")
    wait()
    dump("restored_rewards")

    # Isolate one four-shot volley; blueprint earning is covered by the real production above.
    for flame in (False, True):
        tag = "flame" if flame else "plain"
        act("reset")
        wait()
        snap("clean_" + tag)
        load("clean_" + tag, [("/state/completedWaves", 14), ("/state/energy", 6000), ("/guideSeenWave", 14),
            ("/state/commissions/blueprints", [True, True, True, False, False, False]), ("/state/commissions/freeUses", [1, 1, 1, 0, 0, 0]),
            ("/battle/plants/0/extraData/workshopUpgradeLevel", 3), ("/battle/plants/0/health", 150)])
        clear_guides()
        act("plant", 0, "heavy_gatling", 2)
        wait()
        check("nativePlantsByCell.0_2.level", 3)
        check("nativePlantsByCell.0_2.hp", 150)
        for row in range(1, 5):
            act("remove_plant", row, value=2)
        if flame:
            act("plant", 0, "blaze_torch", 3)
            act("plant", 0, "blaze_torch", 4)
        act("start")
        snap("battle_" + tag)
        load("battle_" + tag, [("/state/remainingSpawns", 0), ("/state/spawned", 0),
            ("/state/assaultTypes", []), ("/state/lanes/0/ammo", 4 if flame else 3)], settle=False)
        for x in (850, 930, 1010):
            op("spawn_zombie", type="ZOMBIE_GARGANTUAR", row=0, x=x, stationary=True)
        if not flame:
            op("wait_seconds", value=2)
            check("fired", 0)
            check("lanes.0.ammo", 3)
            snap("short_ammo")
            load("short_ammo", [("/state/lanes/0/ammo", 4)])
        op("wait_seconds", value=0.6)
        act("pause")
        dump(tag + "_mid")
        snap(tag + "_mid_saved")
        op("screenshot", name="06_" + tag + "_volley.png")
        act("save")
        act("reload")
        wait()
        dump(tag + "_mid_restored")
        act("pause")
        op("wait_seconds", value=1.25)
        act("pause")
        dump(tag + "_piercing")
        act("save")
        snap(tag + "_piercing_saved")
        act("reload")
        wait()
        dump(tag + "_piercing_restored")
        act("pause")
        op("wait_seconds", value=3.25)
        act("pause")
        dump(tag + "_hits")
        check("fired", 4)
        check("lanes.0.ammo", 0)
        check("nativePlantsByCell.0_2.reservedShots", 0)
        op("screenshot", name="07_" + tag + "_hits.png")
        if flame:
            snap("pool_reuse")
            load("pool_reuse", [("/state/lanes/1/ammo", 1)])
            act("plant", 1, "pea", 2)
            op("spawn_zombie", type="ZOMBIE_GARGANTUAR", row=1, x=1040, stationary=True)
            act("pause")
            op("wait_seconds", value=2.2)
            act("pause")
            dump("pool_reused")
    op("quit")
    path = ROOT / "build/clang-release/autotest/generated/smoke_workshop_commissions.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(name=path.stem, commands=commands), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(path)


def verify():
    def read(name):
        return json.loads((OUT / (name + ".json")).read_text(encoding="utf-8"))

    def state(name):
        return read(name)["workshop"]

    assert read("status")["status"] == "passed"
    assert state("accepted")["commissions"] == state("paused_tasks")["commissions"]
    assert abs(state("claimed")["energy"] - state("ready")["energy"] - 150) < 0.01
    assert state("before_free_machine")["energy"] == state("free_machine")["energy"] == state("removed_free_machine")["energy"]
    assert state("blueprints")["commissions"]["blueprints"] == [True, True, False, False, False, False]
    assert state("all_rewards")["commissions"]["blueprints"] == [True, True, True, False, False, False]
    assert read("reward_roundtrip")["state"]["commissions"] == state("restored_rewards")["commissions"]
    for tag, damage in (("plain", 40), ("flame", 120)):
        before, after = state(tag + "_mid"), state(tag + "_mid_restored")
        reserved = before["nativePlantsByCell"]["0_2"]["reservedShots"]
        assert 0 < reserved <= 4, (tag, reserved)
        assert reserved == after["nativePlantsByCell"]["0_2"]["reservedShots"]
        assert before["lanes"][0]["ammo"] == after["lanes"][0]["ammo"] == 0
        hits = state(tag + "_hits")
        targets = sorted(hits["nativeZombies"], key=lambda z: z["id"])
        assert [z["hp"] for z in targets] == [3000 - damage * 4, 3000 - damage * 2, 3000], (tag, targets)
        assert hits["fired"] == 4
        assert state(tag + "_piercing")["nativeBullets"] == state(tag + "_piercing_restored")["nativeBullets"]
    reused = [b for b in state("pool_reused")["nativeBullets"] if b["row"] == 1]
    assert reused and all(b["damage"] == 20 and not b["heavy"] and not b["flamed"] and not b["hitIDs"] for b in reused)
    print("Workshop commissions passed: real transactions, pause/save, free-use safety, native volley reserve, piercing and non-stacking triple flame.")


if __name__ == "__main__":
    generate() if "--generate" in sys.argv else verify()
