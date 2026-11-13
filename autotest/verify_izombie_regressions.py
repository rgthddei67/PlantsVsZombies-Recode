"""Check pickup ownership and paused brain actions through real AutoTest state."""
import json
import math
from pathlib import Path


def read(folder, name):
    return json.loads((folder / name).read_text(encoding="utf-8"))


def verify(root):
    pickup = root / "smoke_izombie_pickup_input"
    paused = root / "smoke_izombie_brain_pause"
    for folder in (pickup, paused):
        status = read(folder, "status.json")
        assert status["status"] == "passed" and status["exitCode"] == 0, status
    state = read(pickup, "pickup.json")
    assert state["sun"] == 150 and state["zombieCount"] == 0
    assert state["izombie"]["selectedSlot"] == 0
    assert sum(sun["collected"] for sun in state["suns"]) == 1
    state = read(paused, "frozen.json")
    assert state["paused"]
    assert all(math.isclose(brain, 0.7, abs_tol=1e-6) for brain in state["izombie"]["brains"])
    zombies = {zombie["type"]: zombie for zombie in state["zombies"]}
    assert zombies["ZOMBIE_BALLOON"]["balloonPhase"] == "FLYING"
    assert zombies["ZOMBIE_POGO"]["hasPogo"]
    assert zombies["ZOMBIE_GARGANTUAR"]["gargantuarPhase"] == "WALKING"
    print("IZombie pickup ownership and paused brain actions passed")


if __name__ == "__main__":
    verify(Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out")
