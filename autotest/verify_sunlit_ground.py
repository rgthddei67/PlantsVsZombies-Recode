"""Generate visible first-area regressions and verify stable plot/entity snapshots."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/clang-release/autotest/out"
SCRIPTS = ROOT / "autotest/scripts"


def op(operation, **kwargs):
    return {"op": operation, **kwargs}


def check(path, value):
    return op("assert_state", path=path, equals=value)


def wait(seconds):
    return op("wait_seconds", value=seconds, timeout=30)


def dump(name):
    return op("dump_state", name=name + ".json")


def start(level):
    return [op("set_timescale", value=1), op("goto_level", level=level),
            op("choose_cards", cards=["PLANT_SUNFLOWER", "PLANT_PEASHOOTER", "PLANT_WALLNUT"]),
            op("wait_state", state="GAME", timeout=20), op("set_spawn_paused", value=True),
            op("set_sun", value=9000)]


def write(name, commands):
    lines = ["    " + json.dumps(command, ensure_ascii=False) for command in commands + [op("quit")]]
    (SCRIPTS / (name + ".json")).write_text('{\n  "commands": [\n' + ',\n'.join(lines) + '\n  ]\n}\n',
                                           encoding="utf-8")


def generate():
    commands = [op("set_difficulty", value=3), op("set_auto_collect", value=False)]
    counts = [0, 0, 2, 2, 3, 3, 4, 4, 4]
    waves = [3, 10, 10, 10, 10, 10, 15, 15, 30]
    suns = [50, 50, 50, 50, 50, 150, 300, 400, 300]
    pools = [["NORMAL"], ["NORMAL"], ["NORMAL", "TRAFFIC_CONE"], ["NORMAL", "TRAFFIC_CONE"],
             ["NORMAL", "TRAFFIC_CONE", "POLEVAULTER"], ["NORMAL", "TRAFFIC_CONE", "POLEVAULTER"],
             ["NORMAL", "TRAFFIC_CONE", "BUCKET"], ["NORMAL", "TRAFFIC_CONE", "FASTBUCKET"],
             ["NORMAL", "TRAFFIC_CONE", "POLEVAULTER", "BUCKET", "FASTBUCKET"]]
    for level in range(1, 10):
        commands += [op("goto_level", level=level), check("background", "GROUND_DAY"),
                     check("sunlitGround.count", counts[level - 1]), check("sun", suns[level - 1]),
                     check("maxWave", waves[level - 1]), check("supportsWeather", False),
                     check("spawnList", ["ZOMBIE_" + name for name in pools[level - 1]]), dump(f"level_{level}")]
    for level in [10, 1000]:
        commands += [op("goto_level", level=level), check("sunlitGround.active", False), check("sunlitGround.count", 0)]
    commands += [op("goto_level", level=3), op("show_crazy_dave_dialog"), op("advance_crazy_dave_dialog"),
                 op("wait_frames", value=3), check("crazyDave.messageCount", 4),
                 op("screenshot", name="tutorial.png"), op("skip_crazy_dave_dialog")]
    commands += start(3)
    commands += [check("sunlitGround.resourcesReady", True), op("set_timescale", value=4), wait(10),
                 op("set_timescale", value=0), dump("growing"), op("screenshot", name="growing.png"),
                 op("plant", type="PLANT_WALLNUT", row=1, col=3), op("wait_frames", value=3),
                 dump("occupied_begin"), op("set_timescale", value=4), wait(8), op("set_timescale", value=0),
                 dump("occupied_end"), check("sunlitGround.plots.0.occupied", True),
                 op("save_level_snapshot", name="occupied"), op("reload_level_snapshot", name="occupied"),
                 op("wait_frames", value=3), dump("occupied_reload"),
                 op("shovel_plant_at", row=1, col=3), op("wait_frames", value=3),
                 op("set_timescale", value=4), wait(17), op("set_timescale", value=0), dump("bloom"),
                 op("screenshot", name="bloom.png"), op("save_level_snapshot", name="bloom"),
                 op("reload_level_snapshot", name="bloom"), op("wait_frames", value=3), dump("bloom_reload"),
                 op("set_timescale", value=4), wait(3.2), op("set_timescale", value=0), dump("harvest"),
                 op("screenshot", name="harvest.png"), op("save_level_snapshot", name="harvest"),
                 op("reload_level_snapshot", name="harvest"), op("wait_frames", value=3), dump("harvest_reload"),
                 op("spawn_zombie", type="ZOMBIE_NORMAL", row=1, x=522, stationary=True),
                 op("set_timescale", value=1), wait(0.3), op("set_timescale", value=0), dump("trampled"),
                 check("sunlitGround.plots.0.elapsedMillis", 0), check("sunlitGround.plots.0.trampled", True),
                 op("screenshot", name="trampled.png"), op("save_level_snapshot", name="trampled"),
                 op("reload_level_snapshot", name="trampled"), op("wait_frames", value=3), dump("trampled_reload"),
                 op("set_sun", value=0), op("click", x=522, y=218),
                 op("set_timescale", value=1), wait(2), check("sunlitGround.plots.0.elapsedMillis", 0),
                 check("sun", 25),
                 op("charm_zombie", row=1), wait(1), op("set_timescale", value=0), dump("charmed"),
                 check("sunlitGround.plots.0.trampled", False), op("kill_zombie", row=1),
                 op("set_timescale", value=1), wait(1), op("set_timescale", value=0), dump("recovered")]
    # A real vault begins over a flower plot: the short initial grounded contact may reset it,
    # but progress must increase while the zombie is airborne, including after reload.
    commands += start(5)
    commands += [op("plant", type="PLANT_WALLNUT", row=0, col=2),
                 op("spawn_zombie", type="ZOMBIE_POLEVAULTER", row=0, x=522),
                 op("wait_value", path="zombies.0.vaultState", equals="JUMPING", timeout=8),
                 op("set_timescale", value=0), dump("jump_begin"),
                 check("sunlitGround.plots.0.trampled", False),
                 op("set_timescale", value=1), wait(0.65), op("set_timescale", value=0), dump("jump_mid"),
                 check("zombies.0.vaultState", "JUMPING"), op("screenshot", name="jump.png"),
                 op("save_level_snapshot", name="jump"), op("reload_level_snapshot", name="jump"),
                 op("wait_frames", value=3), dump("jump_reload"),
                 check("sunlitGround.plots.0.trampled", False), op("set_timescale", value=1),
                 op("wait_value", path="zombies.0.vaultState", equals="WALKING", timeout=8),
                 wait(0.2), op("set_timescale", value=0), dump("jump_landed")]
    # Plant death releases the slot without losing the saved progress.
    commands += [op("plant", type="PLANT_WALLNUT", row=2, col=4), op("wait_frames", value=3),
                 dump("death_before"), op("damage_plant", row=2, col=4, damage=100000),
                 op("set_timescale", value=1), wait(1), op("set_timescale", value=0), dump("death_after")]
    write("smoke_sunlit_ground", commands)

    commands = start(9) + [op("set_timescale", value=0), dump("finale_initial")]
    for wave in range(1, 31):
        commands += [op("summon_next_wave", clearPrevious=True), op("wait_frames", value=2), dump(f"wave_{wave:02}")]
    commands += [op("save_level_snapshot", name="final_wave"), op("reload_level_snapshot", name="final_wave"),
                 op("wait_frames", value=3), dump("final_wave_reload")]
    write("smoke_sunlit_finale", commands)

    commands = [op("set_auto_collect", value=False), op("goto_level", level=9),
                op("wait_value", path="chooseCardReady", equals=True, timeout=20),
                op("screenshot", name="preview.png")]
    commands += start(9) + [op("set_sun", value=300)]
    for row in range(5):
        commands += [op("plant", type="PLANT_SUNFLOWER", row=row, col=0),
                     op("plant", type="PLANT_PEASHOOTER", row=row, col=1)]
    commands += [op("set_timescale", value=4), wait(26), op("set_timescale", value=0),
                 op("plant", type="PLANT_WALLNUT", row=1, col=3),
                 op("spawn_zombie", type="ZOMBIE_NORMAL", row=3, x=682, stationary=True),
                 op("apply_zombie_control", row=3, effect="PARALYSIS", duration=100),
                 op("set_timescale", value=1), wait(0.1), op("set_timescale", value=0),
                 dump("overview"), op("screenshot", name="overview.png"),
                 op("set_timescale", value=1), wait(5), op("set_timescale", value=0),
                 dump("cycle_one"), op("screenshot", name="cycle_one.png"),
                 op("set_timescale", value=4), wait(30), op("set_timescale", value=0), dump("cycle_two")]
    write("visual_sunlit_ground", commands)


def read(suite, name):
    return json.loads((OUT / suite / (name + ".json")).read_text(encoding="utf-8"))


def plots(state):
    return {(p["row"], p["col"]): p for p in state["sunlitGround"]["plots"]}


def verify():
    suite = "smoke_sunlit_ground"
    for name in ["occupied", "bloom", "harvest", "trampled", "jump"]:
        before = {"occupied": "occupied_end", "jump": "jump_mid"}.get(name, name)
        a, b = read(suite, before), read(suite, name + "_reload")
        assert plots(a) == plots(b), (name, "plot snapshot changed")
        assert {s["id"] for s in a["suns"]} == {s["id"] for s in b["suns"]}, (name, "sun identities changed")
    a, b = plots(read(suite, "occupied_begin")), plots(read(suite, "occupied_end"))
    assert abs(a[1, 3]["elapsedMillis"] - b[1, 3]["elapsedMillis"]) < 100
    assert b[3, 4]["elapsedMillis"] - a[3, 4]["elapsedMillis"] >= 7900
    before, after = read(suite, "bloom"), read(suite, "harvest")
    assert 26000 <= plots(before)[1, 3]["elapsedMillis"] < 30000
    assert plots(after)[1, 3]["elapsedMillis"] < 2000
    previous_ids = {s["id"] for s in before["suns"]}
    assert any(s["id"] not in previous_ids and not s["small"] and s["xInt"] == 522
               and s["yInt"] == 218 for s in after["suns"]), "missing real 25-point plot sun"
    assert plots(read(suite, "charmed"))[1, 3]["elapsedMillis"] >= 900
    assert plots(read(suite, "recovered"))[1, 3]["elapsedMillis"] >= 1900
    assert plots(read(suite, "jump_mid"))[0, 3]["elapsedMillis"] - plots(read(suite, "jump_begin"))[0, 3]["elapsedMillis"] >= 600
    assert plots(read(suite, "jump_landed"))[0, 3]["elapsedMillis"] > plots(read(suite, "jump_mid"))[0, 3]["elapsedMillis"]
    assert plots(read(suite, "death_after"))[2, 4]["elapsedMillis"] > plots(read(suite, "death_before"))[2, 4]["elapsedMillis"]
    for level, count in enumerate([0, 0, 2, 2, 3, 3, 4, 4, 4], 1):
        state = read(suite, f"level_{level}")
        ps = plots(state)
        assert len(ps) == count
        assert all(0 <= r < 5 and 3 <= c <= 5 and p["elapsedMillis"] == 0 for (r, c), p in ps.items())

    suite = "smoke_sunlit_finale"
    stages = [set(), set(), set()]
    for wave in range(1, 31):
        state = read(suite, f"wave_{wave:02}")
        assert state["wave"] == wave
        types = {z["type"] for z in state["zombies"]}
        stages[(wave - 1) // 10].update(types)
        if wave <= 10:
            assert types <= {"ZOMBIE_NORMAL", "ZOMBIE_TRAFFIC_CONE"}, (wave, types)
        elif wave <= 20:
            assert "ZOMBIE_FASTBUCKET" not in types, wave
    assert {"ZOMBIE_POLEVAULTER", "ZOMBIE_BUCKET"} <= stages[1]
    assert "ZOMBIE_FASTBUCKET" in stages[2]
    assert read(suite, "final_wave_reload")["wave"] == 30
    a, b = read("visual_sunlit_ground", "cycle_one"), read("visual_sunlit_ground", "cycle_two")
    for square in [(0, 4), (4, 4)]:
        assert abs(plots(a)[square]["elapsedMillis"] - plots(b)[square]["elapsedMillis"]) < 300
        x, y = 602, 138 + square[0] * 100 - 20
        suns_a = {s["id"] for s in a["suns"] if s["xInt"] == x and s["yInt"] == y}
        suns_b = {s["id"] for s in b["suns"] if s["xInt"] == x and s["yInt"] == y}
        assert suns_a and suns_b and suns_a.isdisjoint(suns_b), "second cycle did not produce a new sun"
    print("Sunlit ground: layouts, occupation, repeated production, trampling, charm, vault, death, saves and 30-wave pacing passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    generate() if args.generate else verify()
