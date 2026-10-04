"""Verify formal catapult portraits and real host/umbrella behavior in a controlled scene."""

import json
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1])
    reports = {name: json.loads((folder / (name + ".json")).read_text(encoding="utf-8"))
               for name in ("birth", "committed", "protected")}
    assert all(report["boardUnchanged"] for report in reports.values())
    cars = [o for o in reports["birth"]["legalOptions"] if o["type"] == "ZOMBIE_CATAPULT"]
    assert len(cars) == 5
    assert all(o["birthObjectX"] == 1140 and o["birthCenterX"] == 1203
               and o["boundsWidth"] == 150 and o["birthHealth"] == 850 for o in cars)
    birth = {p["name"]: p for p in reports["birth"]["candidatePlans"]}
    assert birth["ranged"]["legal"] and birth["ranged"]["catapultShots"] == 12
    assert birth["ranged"]["catapultHits"] == 12 and birth["wait"]["catapultShots"] == 0
    assert not birth["elite_ranged"]["legal"] and birth["elite_ranged"]["unavailableType"] == "ZOMBIE_ELITE_CATAPULT"
    assert birth["ranged"]["features"][0] > 0, "forecast must hit the host instead of spending all balls on its pumpkin"
    before = json.loads((folder / "committed_before.json").read_text(encoding="utf-8"))
    committed = reports["committed"]["candidatePlans"][0]
    assert before["basketballBulletCount"] == 1
    assert committed["catapultShots"] == 11 and committed["catapultHits"] == 12, "captured ball cannot be duplicated"
    protected = reports["protected"]["candidatePlans"][0]
    assert protected["catapultBlocks"] == 11 and protected["catapultHits"] == 0
    after = json.loads((folder / "protected_after.json").read_text(encoding="utf-8"))
    assert after["normalPlantsByCell"]["2_0"]["health"] == 225
    ice = after["coldStorage"]
    assert ice["spent"] == 0
    assert ice["enemyIce"] == ice["initialEnemyIce"] + ice["supplied"] + ice["workerIncome"] + ice["killIncome"] - ice["spent"]
    print("Birth/live ranged portraits, committed ball, pumpkin host and actual umbrella protection passed")


if __name__ == "__main__":
    main()
