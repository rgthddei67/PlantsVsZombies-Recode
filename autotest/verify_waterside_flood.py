"""Verify live flood rescue, water timing, route changes and snapshot continuity."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out"


def read(suite, name):
    return json.loads((ROOT / suite / name).read_text(encoding="utf-8"))


def plants(state):
    return {p["id"]: p for p in state["plants"]}


def verify():
    main, weather = "smoke_waterside_flood", "smoke_waterside_flood_weather"
    for suite in (main, weather):
        assert read(suite, "status.json")["status"] == "passed", suite
    initial = read(main, "initial.json")
    saved, restored = read(main, "saved.json"), read(main, "restored.json")
    assert saved["watersideFlood"] == restored["watersideFlood"], "water changed during frozen restore"
    assert saved["cells"] == restored["cells"], "restored flooding/plant occupancy differs"
    a, b = plants(saved), plants(restored)
    for identity, plant in a.items():
        assert identity in b, "restored plant identity missing"
        for key in ("health", "watersideHazard", "watersideElapsedMs"):
            assert plant[key] == b[identity][key], (identity, key)
    full = read(main, "drowned.json")
    assert full["cells"][0][2]["normal"] == "NONE", "unrescued land plant survived drowning"
    assert len(full["watersideFlood"]["fullRows"]) == 6
    retreat = read(main, "retreated.json")
    for row, col in ((1, 5), (0, 6), (0, 7), (5, 0), (5, 1)):
        assert retreat["cells"][row][col]["under"] == "NONE", "stranded lily survived"
        assert retreat["cells"][row][col]["normalID"] == initial["cells"][row][col]["normalID"], "upper plant replaced or lost"
    finished = read(main, "finished.json")
    assert finished["cells"][0][6]["normalID"] == initial["cells"][0][6]["normalID"]
    assert finished["cells"][0][6]["under"] == "PLANT_FLOWERPOT"
    assert finished["cells"][0][7]["normal"] == "NONE", "missed pot deadline"
    assert finished["cells"][0][8]["normal"] == "NONE", "stranded aquatic plant survived"
    assert finished["cells"][5][0]["normalID"] == initial["cells"][5][0]["normalID"]
    natural = read(main, "natural.json")
    enemies = [z for z in natural["zombies"] if z["type"] == "ZOMBIE_CATAPULT"]
    assert enemies and len({z["row"] for z in enemies}) >= 2, "full-flood natural land spawns missing"

    light_start, light_end = read(weather, "light_start.json"), read(weather, "light_end.json")
    clear_start, clear_end = read(weather, "clear_start.json"), read(weather, "clear_end.json")
    water = lambda state: state["watersideFlood"]["waterMilli"]
    gain, drain = water(light_end) - water(light_start), water(clear_start) - water(clear_end)
    assert 950 <= gain <= 1100 and abs(gain - drain) < 100, ("fivefold drain duration", gain, drain)
    assert light_start["watersideFlood"]["rateMilli"] == 200
    assert clear_start["watersideFlood"]["rateMilli"] == -40
    before, after = read(weather, "shore_before.json"), read(weather, "shore_after.json")
    assert 0 < before["cells"][1][5]["floodCoverageMilli"] < 1000
    assert after["cells"][1][5]["floodCoverageMilli"] > before["cells"][1][5]["floodCoverageMilli"]
    storm, restore_storm = read(weather, "storm_saved.json"), read(weather, "storm_restored.json")
    assert storm["weather"]["intensity"] == restore_storm["weather"]["intensity"] == "TORRENT"
    assert storm["watersideFlood"] == restore_storm["watersideFlood"]
    assert storm["weather"]["typhoonStrength"] == restore_storm["weather"]["typhoonStrength"] == "SUPER"
    world, zombies = "smoke_waterside_flood_world", "smoke_waterside_flood_zombies"
    for suite in (world, zombies, "smoke_waterside_flood_pogo"):
        assert read(suite, "status.json")["status"] == "passed", suite
    floating, restored = read(world, "floating.json"), read(world, "floating_restored.json")
    assert all(floating["watersideFlood"]["outerWater"].values())
    assert floating["watersideFlood"] == restored["watersideFlood"]
    by_id = lambda state, key: {entity["id"]: entity for entity in state[key]}
    assert by_id(floating, "mowers") == by_id(restored, "mowers"), "floating mower snapshot changed"
    dry = read(world, "dry.json")
    assert set(by_id(dry, "mowers")) == set(by_id(floating, "mowers")), "water consumed idle mowers"
    for effect in read(world, "land_splashes.json")["particleEffectsByName"]["RainGroundSplash"]:
        assert effect["floodDepthMilli"] < 0, "rain ground splash appeared on water"
    submerged, restored = read(zombies, "submerged.json"), read(zombies, "submerged_restored.json")
    a, b = by_id(submerged, "zombies"), by_id(restored, "zombies")
    assert a.keys() == b.keys(), "flood restore replaced or duplicated zombies"
    for identity, zombie in a.items():
        assert zombie["inPool"] == b[identity]["inPool"], identity
        if zombie["inPool"]:
            assert not zombie["groundShadowVisible"] and not zombie["groundShadowDrawn"], zombie["type"]
            assert not b[identity]["groundShadowVisible"], (zombie["type"], "restored shadow")
    retreat = read(zombies, "retreated.json")
    for zombie in retreat["zombies"]:
        if zombie["type"] == "ZOMBIE_BOBSLED_TEAM":
            assert zombie["inPool"] == (zombie["row"] == 1), "scatter member must follow its own landing water"
    print("Waterside flood: rescue, weather, four outer shores, mowers, special zombies, shadows and saves passed.")


if __name__ == "__main__":
    verify()
