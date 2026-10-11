"""Validate real combat event causality and optionally actual pre-payment captures.

Controlled fixture checks prove recording/lifecycle contracts, never win rate.
Normal diagnostic matches can use the same generic checks without --fixture.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def read(folder, name):
    return json.loads((folder / name).read_text(encoding="utf-8"))


def verify(folder, fixture=False, require_capture=False, allow_truncated=False, row_ash_fixture=False):
    status = read(folder, "status.json")
    assert status["status"] == "passed", status
    report = read(folder, "commander_combat_trace.json")
    assert report["schema"] == 1 and report["diagnosticOnly"]
    assert not report["recordingFailed"], "capture/write failure must not be mistaken for a complete trace"
    assert allow_truncated or not report["truncated"], "bounded trace was truncated"
    events = report["events"]
    assert [e["seq"] for e in events] == list(range(1, len(events) + 1))
    by_attack, by_epoch, last_clock = defaultdict(list), defaultdict(list), {}
    for event in events:
        epoch = event["epoch"]
        assert event["dt"] >= 0 and event["timeScale"] >= 0
        assert event["gameTime"] >= last_clock.get(epoch, float("-inf"))
        last_clock[epoch] = event["gameTime"]
        by_epoch[epoch].append(event)
        if event.get("attackID"):
            by_attack[event["attackID"]].append(event)
        hits = event.get("targets", [])
        assert len({h["id"] for h in hits}) == len(hits), "one impact must not damage the same receiver twice"
        for hit in hits:
            assert hit["requestedDamage"] > 0
            assert hit["healthLost"] == max(0, hit["before"]) - max(0, hit["after"])
            assert hit["becameInactive"] == (hit["activeBefore"] and not hit["activeAfter"])
            assert hit["becameDying"] == (not hit["dyingBefore"] and hit["dyingAfter"])
    for group in by_attack.values():
        names = [e["event"] for e in group]
        start = group[0]
        if names[0] == "jalapeno_impact":
            assert names == ["jalapeno_impact"], "a direct row blast cannot reuse a Jack/Cob attack ID"
            assert all(h["kind"] == "zombie" and h["row"] == start["sourceRow"] for h in start["targets"])
            continue
        if start.get("observedWithoutStart"):
            assert names[0] in ("jack_impact", "cob_impact", "cob_launch"), names
            continue  # Real damage is retained; original launch/lock time remains unknown
        assert names[0] in ("jack_throw", "cob_lock"), names
        if names[0] == "jack_throw":
            assert names.count("jack_impact") + names.count("jack_cancel") <= 1
            for impact in (e for e in group if e["event"] == "jack_impact"):
                error = abs((impact["gameTime"] - start["gameTime"]) - start["flightSeconds"])
                assert error <= 2 * max(start["dt"], impact["dt"]) + .002, (error, group)
        else:
            assert names.count("cob_launch") <= 1 and names.count("cob_impact") <= 1
            if "cob_impact" in names:
                assert "cob_launch" in names and names.index("cob_launch") < names.index("cob_impact")
                launch = next(e for e in group if e["event"] == "cob_launch")
                impact = next(e for e in group if e["event"] == "cob_impact")
                assert launch["projectileID"] == impact["projectileID"]
                assert launch["sourceID"] == impact["sourceID"]
                error = abs((impact["gameTime"] - launch["gameTime"]) - launch["flightSeconds"])
                assert error <= 2 * max(launch["dt"], impact["dt"]) + .002, (error, group)
            for event in group:
                if event.get("reason") == "source_removed_before_launch":
                    assert "cob_launch" not in names and "cob_impact" not in names
    committed = [c for c in report["captures"] if c["status"] == "committed"]
    if require_capture:
        assert committed, "no qualifying actual paid purchase was captured"
    births = {(e["epoch"], e["ticket"]): e for e in events if e["event"] == "paid_birth"}
    for capture in committed:
        assert capture["snapshotSaved"] and capture["actionTicketMappingComplete"]
        assert capture["actualPaid"] == capture["selectedTotalCost"] > 0
        assert capture["enemyIceBefore"] - capture["enemyIceAfter"] == capture["actualPaid"]
        assert capture["spentAfter"] - capture["spentBefore"] == capture["actualPaid"]
        assert abs(capture["sampleAt"] + capture["age"] - capture["commitAt"]) < .001
        snapshot = read(folder, capture["snapshotPath"])
        assert snapshot["coldStorage"]["enemyIce"] == capture["enemyIceBefore"]
        assert snapshot["coldStorage"]["spent"] == capture["spentBefore"]
        actions = capture["selectedActions"]
        assert [a["actionIndex"] for a in actions] == list(range(len(actions)))
        for paid in capture["paidTickets"]:
            action = actions[paid["actionIndex"]]
            assert (action["type"], action["row"], action["cost"]) == (paid["type"], paid["row"], paid["cost"])
            birth = births.get((capture["epoch"], paid["ticket"]))
            if birth:
                assert birth["type"] == paid["type"] and birth["cost"] == paid["cost"]
                assert birth["at"] >= capture["commitAt"]
        assert not capture["selectedOriginalForecast"]["truncated"]
    if row_ash_fixture:
        throws = [e for e in events if e["event"] == "jack_throw"]
        flames = [e for e in events if e["event"] == "jalapeno_impact"]
        assert len(throws) == len(flames) == 1
        assert throws[0]["sourceID"] == flames[0]["sourceID"], "fixture must exercise equal IDs in different entity namespaces"
        assert throws[0]["attackID"] != flames[0]["attackID"]
        assert len(flames[0]["targets"]) == 1 and flames[0]["targets"][0]["becameInactive"]
        assert flames[0]["targets"][0]["typeName"] == "ZOMBIE_ELITE_JACK_IN_THE_BOX"
        survivors = read(folder, "row_ash_after.json")["zombies"]
        assert len(survivors) == 1 and survivors[0]["row"] == 4
    if fixture:
        assert len(by_epoch) == 6, sorted(by_epoch)
        phases = [by_epoch[k] for k in sorted(by_epoch)]
        first = [e for e in phases[0] if e["event"] == "jack_impact"]
        assert len(first) == 2 and first[0]["sourceID"] == first[1]["sourceID"]
        assert first[0]["attackID"] != first[1]["attackID"]
        assert all(len(e["targets"]) == 1 and e["targets"][0]["healthLost"] == 50 for e in first)
        shell = [e for e in phases[1] if e["event"] == "jack_impact"]
        assert len(shell) == 1 and len(shell[0]["targets"]) == 1
        assert shell[0]["targets"][0]["layer"] == "pumpkin" and shell[0]["targets"][0]["healthLost"] == 250
        assert [e["event"] for e in phases[2] if e["event"].startswith("jack_")] == ["jack_throw", "jack_cancel"]
        dying = [e for e in phases[3] if e["event"] == "jack_impact"]
        assert len(dying) == 1 and dying[0]["sourceDying"] and dying[0]["targets"][0]["healthLost"] == 50
        assert [e["event"] for e in phases[4] if e["event"].startswith("cob_")] == ["cob_lock", "cob_cancel"]
        independent = [e for e in phases[5] if e["event"].startswith("cob_")]
        assert [e["event"] for e in independent] == ["cob_lock", "cob_launch", "cob_source_removed", "cob_impact"]
        assert all(e["timeScale"] == 5 for e in independent)
        assert any(h["becameInactive"] for h in independent[-1]["targets"]), "ash casualties must survive source death in the trace"
        assert read(folder, "repeat_after.json")["plants"][0]["health"] == 200
        assert read(folder, "cancel_after.json")["plants"][0]["health"] == 300
        assert read(folder, "dying_after.json")["plants"][0]["health"] == 250
        assert not read(folder, "cob_independent_after.json")["zombies"]
    print(f"PASS: {len(events)} real events, {len(by_attack)} attacks, {len(committed)} actual pre-payment captures; diagnostic evidence only")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--fixture", action="store_true")
    parser.add_argument("--require-capture", action="store_true")
    parser.add_argument("--allow-truncated", action="store_true")
    parser.add_argument("--row-ash-fixture", action="store_true")
    options = parser.parse_args()
    verify(options.folder, options.fixture, options.require_capture, options.allow_truncated, options.row_ash_fixture)
