"""Verify read-only candidate legality, common forecast window and worker income trace."""

import json
import math
import sys
from pathlib import Path


def main():
    folder = Path(sys.argv[1])
    report = json.loads((folder / "legal_plans.json").read_text(encoding="utf-8"))
    plans = {plan["name"]: plan for plan in report["candidatePlans"]}
    assert report["boardUnchanged"]
    assert plans["wait"]["legal"] and plans["guarded"]["legal"]
    assert not plans["unavailable"]["legal"]
    assert not plans["too_expensive"]["legal"]
    assert plans["wait"]["searchVersion"] == plans["guarded"]["searchVersion"] == 2
    for branch in report["branches"]:
        assert branch["expanded"]
        assert branch["baselineFeatures"] == plans["wait"]["features"]
    assert plans["guarded"]["baselineFeatures"] == plans["wait"]["features"]
    income = sum(point["income"] for point in plans["guarded"]["workerTrace"] if point["time"] < 60)
    assert income > 0 and plans["guarded"]["engineerBlocks"] > 0
    assert math.isclose(income, plans["guarded"]["rawProduction"], abs_tol=0.001)
    background = json.loads((folder / "background_plans.json").read_text(encoding="utf-8"))
    assert background["boardUnchanged"]
    assert background["backgroundComputingBefore"] and not background["backgroundComputingAfter"]
    print("Read-only legal candidate diagnosis, shared long forecast and income trace passed")


if __name__ == "__main__":
    main()
