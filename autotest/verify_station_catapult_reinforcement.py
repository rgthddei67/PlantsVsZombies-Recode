"""Check that game-time eligibility and the AI option pool cross the same boundary."""

import json
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1])

    def read(name):
        return json.loads((folder / (name + ".json")).read_text(encoding="utf-8"))

    assert read("status")["status"] == "passed"
    for name, unlocked in (("before_unlock", False), ("after_unlock", True)):
        report = read(name)
        assert report["boardUnchanged"]
        assert report["candidatePlans"][0]["legal"] == unlocked
        cars = [o for o in report["legalOptions"] if o["type"] == "ZOMBIE_CATAPULT"]
        assert len(cars) == (5 if unlocked else 0)
    before, after = read("paused_before"), read("paused_after")
    assert before["paused"] and after["paused"]
    assert before["coldStorage"]["elapsed"] == after["coldStorage"]["elapsed"]
    assert not after["coldStorage"]["purchaseUnlocked"]["ZOMBIE_CATAPULT"]
    print("station catapult eligibility, AI pool and pause checks passed")


if __name__ == "__main__":
    main()
