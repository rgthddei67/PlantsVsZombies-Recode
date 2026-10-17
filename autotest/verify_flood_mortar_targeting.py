"""Verify synchronized mortar shots stay complete while targeting work is bounded."""

import json
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1])
    read = lambda name: json.loads((folder / (name + ".json")).read_text(encoding="utf-8"))
    assert read("status")["status"] == "passed"
    before, after = read("before"), read("after")
    guns = lambda state: {z["id"]: z for z in state["zombies"] if z["type"] == "ZOMBIE_FLOOD_MORTAR"}
    first, last = guns(before), guns(after)
    if "--fairness" in sys.argv[2:]:
        assert len(first) == len(last) == 64 and first.keys() == last.keys()
        assert all(z["floodShots"] == 0 for z in first.values())
        assert all(z["floodShots"] >= 1 and z["floodMonteCarlo"] for z in last.values())
        assert all(0 < z["floodRollouts"] <= 16 for z in last.values())
        print("all 64 mortars received targeting slots at 5x under repeated heavy-rain readiness")
        return
    assert len(first) == len(last) == 12 and first.keys() == last.keys()
    assert all(z["floodShots"] == 0 for z in first.values())
    assert all(z["floodShots"] == 1 and z["floodMonteCarlo"] for z in last.values())
    assert all(0 < z["floodRollouts"] <= 16 for z in last.values())
    assert all(1 <= z["floodTargetRow"] <= 3 and 0 <= z["floodTargetColumn"] <= 5 for z in last.values())
    # 装填余时来自真正发射边沿；不同余时确认队列分摊了发射，而不是同帧做完全部计算。
    remaining = [z["floodReloadMs"] for z in last.values()]
    assert max(remaining) - min(remaining) >= 400
    print("all 12 mortars fired once with Monte Carlo targeting and bounded staggered work")


if __name__ == "__main__":
    main()
