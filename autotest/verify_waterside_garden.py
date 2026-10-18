"""Verify fixed terrain, real ability cadence, and saved water transitions from visible AutoTests."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out"


def read(suite, name):
    return json.loads((ROOT / suite / name).read_text(encoding="utf-8"))


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def verify():
    """Check approved geometry and observable gameplay, without timing absolute moving positions."""
    main = "smoke_waterside_garden"
    actions = "smoke_waterside_actions"
    crossing = "smoke_waterside_crossing"
    typhoon = "smoke_waterside_typhoon"
    for suite in (main, actions, crossing, typhoon):
        check(read(suite, "status.json")["status"] == "passed", f"{suite}: script failed")

    empty = read(main, "empty.json")
    check("coldStorage" not in empty, "12-1 must use ordinary wave defense")
    check((empty["rows"], empty["columns"], empty["maxWave"]) == (6, 9, 20), "level contract")
    layout = ["......###", "..###.~~~", "..#~~~.##", "..#~~~.##", "..###.~~~", "......###"]
    fertile = {(1, 5), (2, 6), (3, 6), (4, 5)}
    for row, line in enumerate(layout):
        for column, terrain in enumerate(line):
            cell = empty["cells"][row][column]
            check(cell["water"] == (terrain == "~"), f"water {row},{column}")
            check(cell["requiresFlowerPot"] == (terrain == "#"), f"pot {row},{column}")
            check(cell["fertile"] == ((row, column) in fertile), f"fertile {row},{column}")
            check(cell["centerXInt"] == 282 + column * 80, f"cell X {row},{column}")
            check(cell["centerYInt"] == 128 + row * 85, f"cell Y {row},{column}")

    before = read(main, "rates_before.json")["topPlantsByCell"]
    after = read(main, "rates_after.json")["topPlantsByCell"]
    progress = {}
    for cell in ("1_5", "0_5", "4_5"):
        check(not before[cell]["productionGlowing"] and not after[cell]["productionGlowing"],
              "production fixture must measure a continuous interval")
        progress[cell] = after[cell]["produceTimerMs"] - before[cell]["produceTimerMs"]
    check(progress["0_5"] > 0, "production did not advance")
    check(abs(progress["1_5"] - 4 * progress["0_5"]) <= 4, "fertile production must advance x4")
    check(abs(progress["4_5"] - progress["0_5"]) <= 2, "flowerpot must block soil bonus")
    rain = read(main, "rain.json")["topPlantsByCell"]
    check(rain["2_6"]["attackRateMilli"] == 4400, "light rain must multiply soil bonus")
    restored = read(main, "restored.json")["topPlantsByCell"]
    check(restored["1_5"]["fertilityRateMilli"] == 4000
          and restored["4_5"]["fertilityRateMilli"] == 1000, "restore changed soil/support contract")

    start, end = read(actions, "before.json"), read(actions, "after.json")
    targets = {z["id"]: z for z in end["zombies"]}
    damage = {}
    for z in start["zombies"]:
        target = targets[z["id"]]
        damage[z["row"]] = sum(z[key] - target[key] for key in ("bodyHealth", "helmHealth", "shieldHealth"))
    # A finite window includes launch delay and in-flight projectiles. Check the real throughput
    # increase as well as the exact rate above; do not pretend finite damage counts are exactly 4:1.
    check(damage[0] > 0 and 3 * damage[0] <= damage[1] <= 6 * damage[0], "fertile firing throughput")
    heals = {cell: end["topPlantsByCell"][cell]["cottonHeals"]
             - start["topPlantsByCell"][cell]["cottonHeals"] for cell in ("0_1", "3_6")}
    check(heals["0_1"] > 0 and abs(heals["3_6"] - 4 * heals["0_1"]) <= 3, "fertile healing cadence")

    snapshots = [read(crossing, "land_start.json")]
    snapshots += [read(crossing, f"crossing_{i:02}.json") for i in range(1, 16)]
    for initial in snapshots[0]["zombies"]:
        states = []
        for snapshot in snapshots:
            for zombie in snapshot["zombies"]:
                if zombie["id"] == initial["id"] and (not states or states[-1] != zombie["inPool"]):
                    states.append(zombie["inPool"])
        check(states[:3] == [False, True, False], f"incomplete shore crossing: {initial['type']}")
    wet = {z["id"]: z for z in read(crossing, "crossing_02.json")["zombies"]}
    loaded = {z["id"]: z for z in read(crossing, "in_water_restored.json")["zombies"]}
    check(wet.keys() == loaded.keys(), "water reload changed identities")
    for ident, zombie in wet.items():
        check(zombie["inPool"] and loaded[ident]["inPool"], "water state not restored")
        check(zombie["type"] == loaded[ident]["type"], "water reload changed type")
    wave = read(crossing, "natural_wave.json")
    check(wave["wave"] == 6 and wave["zombies"], "ordinary waves did not spawn")
    for zombie in wave["zombies"]:
        if zombie["row"] in (1, 2, 3, 4):
            check(zombie["type"] not in ("ZOMBIE_NORMAL", "ZOMBIE_TRAFFIC_CONE", "ZOMBIE_BUCKET"),
                  "mixed route omitted terrain variant resolution")
    # Stable entity IDs prove supports and upper layers stayed together, including cob aliases.
    movements = {
        "front": {(2, 3): 1, (4, 4): 1, (5, 4): 1},
        "house": {(3, 7): -1, (4, 5): -1, (0, 1): -1},
        "cob": {(0, 2): -1},
    }
    for case, offsets in movements.items():
        before = read(typhoon, f"{case}_before.json")
        after = read(typhoon, f"{case}_after.json")
        survivors = {p["id"]: p for p in after["plants"]}
        check({p["id"] for p in before["plants"]} == survivors.keys(), f"{case}: split/lost stack")
        for plant in before["plants"]:
            current = survivors[plant["id"]]
            delta = offsets.get((plant["row"], plant["col"]), 0)
            check(current["row"] == plant["row"] and current["col"] == plant["col"] + delta,
                  f"{case}: wrong terrain move for {plant['type']} id={plant['id']}")
            if plant["type"] == "PLANT_COBCANNON":
                for col in (current["col"], current["col"] + 1):
                    check(after["cells"][current["row"]][col]["normalID"] == plant["id"],
                          "cob footprint lost its shared entity ID")
    result = {"passed": True, "productionProgressMs": progress, "damage": damage, "heals": heals,
              "fertileCells": 4, "waterCells": 12, "ordinaryWave": wave["wave"]}
    (ROOT / main / "verification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    verify()
