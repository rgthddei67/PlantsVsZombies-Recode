"""Check direct protection evidence when overlapping engineers die in the same ash event."""

import json
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1])
    before = json.loads((folder / "before.json").read_text(encoding="utf-8"))
    episode = json.loads((folder / "protection.json").read_text(encoding="utf-8"))
    engineers = {z["id"] for z in before["zombies"] if z["type"] == "ZOMBIE_DISASTER_ENGINEER"}
    workers = {z["id"] for z in before["zombies"] if z["type"] == "ZOMBIE_ICE_WORKER"}
    events = episode["engineerProtectionEvents"]
    assert len(engineers) == 2 and len(workers) == 3
    assert len(events) == 1, "overlapping canisters must not count protection of the same workers twice"
    assert events[0]["engineerID"] == min(engineers) and events[0]["row"] == 2
    assert set(events[0]["workerIDs"]) == workers
    live = {z["id"] for z in episode["final"]["zombies"] if z["bodyHealth"] > 0}
    assert workers <= live and not engineers & live, "record must survive death of its source"
    assert episode["final"]["coldStorage"]["enemyIce"] == 100
    print("Same-blast engineer death, overlapping protection and direct event trace passed")


if __name__ == "__main__":
    main()
