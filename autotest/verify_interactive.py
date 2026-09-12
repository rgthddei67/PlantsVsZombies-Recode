"""Verify the visible interactive_contract game; exits the game after successful checks."""

import json
from pathlib import Path
import sys
import time

from live import LiveClient


def verify(output):
    client = LiveClient(output)
    baseline = client.send([{"op": "observe"}])
    time.sleep(0.4)
    frozen = client.send([{"op": "observe"}])
    assert baseline["state"] == frozen["state"], "idle scene drifted"
    assert frozen["simulationSteps"] == 0
    assert frozen["gameTimeSeconds"] == baseline["gameTimeSeconds"]

    command = [{"op": "player_plant", "slot": 0, "row": 0, "col": 0}]
    first = client.send(command)
    assert first["results"][0]["ok"] and first["state"]["sun"] == 175
    assert first["state"]["plantCount"] == 1
    assert client.send(command, request_id=first["id"]) == first, "retry did not return original reply"

    rejected = client.send([
        {"op": "player_plant", "slot": 0, "row": 1, "col": 0},
        {"op": "player_plant", "slot": 1, "row": 1, "col": 0},
        {"op": "player_plant", "slot": 2, "row": 0, "col": 3},
        {"op": "player_plant", "slot": 99, "row": 0, "col": 0},
        {"op": "set_sun", "value": 9999},
        {"op": "advance", "steps": -1},
    ])
    assert [r["reason"] for r in rejected["results"]] == [
        "cooldown", "", "insufficient_sun", "invalid_slot", "unsupported_operation", "invalid_steps"]
    assert rejected["state"]["sun"] == 125 and rejected["state"]["plantCount"] == 2
    cooldown = rejected["state"]["cards"][0]["cooldownRemainingMs"]

    advanced = client.send([{"op": "advance", "steps": 120}])
    assert advanced["simulationSteps"] == 120
    assert abs(advanced["gameTimeSeconds"] - baseline["gameTimeSeconds"] - 2.0) < 0.001
    assert abs(advanced["state"]["cards"][0]["cooldownRemainingMs"] - (cooldown - 2000)) <= 2
    assert advanced["state"]["zombies"][0]["xInt"] < baseline["state"]["zombies"][0]["xInt"]
    time.sleep(0.4)
    observed = client.send([{"op": "observe"}])
    assert advanced["state"] == observed["state"], "combat or cooldown advanced while waiting"
    assert advanced["gameTimeSeconds"] == observed["gameTimeSeconds"]

    ready = client.send([{"op": "advance", "steps": 360}])
    assert ready["simulationSteps"] == 480 and ready["state"]["cards"][0]["ready"]
    blocked = client.send([
        {"op": "player_plant", "slot": 0, "row": 0, "col": 0},
        {"op": "player_plant", "slot": 0, "row": -1, "col": 0},
        {"op": "player_plant", "slot": 0, "row": 2, "col": 0},
    ])
    assert [r["reason"] for r in blocked["results"]] == ["invalid_cell", "invalid_cell", ""]
    assert blocked["state"]["sun"] == ready["state"]["sun"] - 100 and blocked["state"]["plantCount"] == 3

    suns = [s for s in blocked["state"]["suns"] if not s["collected"]]
    assert suns, "expected a natural sky sun after eight game seconds"
    collected = client.send([{"op": "collect_sun", "id": suns[0]["id"]},
                             {"op": "collect_sun", "id": suns[0]["id"]}])
    assert [r["reason"] for r in collected["results"]] == ["", "sun_unavailable"]
    assert collected["state"]["sun"] == blocked["state"]["sun"], "collection skipped travel animation"
    arrived = client.send([{"op": "advance", "steps": 180}])
    assert arrived["state"]["sun"] >= collected["state"]["sun"] + 25

    screenshot = client.send([{"op": "screenshot", "name": "interactive_contract.png"}])
    assert screenshot["simulationSteps"] == 660
    assert screenshot["results"][0]["ok"]
    assert (Path(output) / "interactive_contract.png").stat().st_size > 0
    assert screenshot["state"] == arrived["state"], "capture advanced gameplay"

    # A malformed published envelope must return an error and leave the mailbox usable.
    bad_id = client.last_id + 1
    bad_path = client.mailbox / f"request_{bad_id}.json"
    temporary = bad_path.with_suffix(".tmp")
    temporary.write_text('{"commands":', encoding="utf-8")
    temporary.rename(bad_path)
    deadline = time.monotonic() + 5
    reply_path = client.mailbox / f"response_{bad_id}.json"
    while not reply_path.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    malformed = json.loads(reply_path.read_text(encoding="utf-8"))
    assert not malformed["results"][0]["ok"] and malformed["simulationSteps"] == 660
    client.last_id = bad_id
    final = client.send([{"op": "quit"}])
    assert final["waiting"] is False
    summary = {"status": "passed", "requests": client.last_id, "simulationSteps": 660,
               "checks": ["idle freeze", "duplicate request", "sun cost", "cooldown", "occupied cell",
                          "invalid slot and cell", "fixture command rejection", "fixed steps",
                          "sun collection", "frozen screenshot", "malformed request recovery", "quit"]}
    (Path(output) / "interactive_verification.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary))


if __name__ == "__main__":
    verify(sys.argv[1])
