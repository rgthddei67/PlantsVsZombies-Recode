"""Check real three-target fees, stable identities, save/load and map eligibility."""
import json
import sys
from pathlib import Path

folder=Path(sys.argv[1]) if len(sys.argv)>1 else Path("build/clang-release/autotest/out/smoke_commander_multi_precision")
def read(name):
    return json.loads((folder/(name+".json")).read_text(encoding="utf-8-sig"))
def ids(state):
    c=state["coldStorage"]
    return [c["strikeTargetID"]]+c["strikeAdditionalTargetIDs"] if c["strikeTargetID"]>=0 else []
def ledger(state):
    c=state["coldStorage"]
    assert c["enemyIce"]==c["initialEnemyIce"]+c["supplied"]+c["workerIncome"]+c["killIncome"]-c["spent"]
assert read("status")["status"]=="passed"
assert "script finished OK" in (folder/"run.log").read_text(encoding="utf-8-sig")
before,loaded,done,again=(read(n) for n in ("before","loaded","done","done_loaded"))
for state in (before,loaded,done,again):ledger(state)
targets=ids(loaded)
assert len(targets)==len(set(targets))==3
original={p["id"]:p for p in before["plants"]}
assert loaded["coldStorage"]["enemyIce"]==20 and loaded["coldStorage"]["spent"]==180
assert loaded["normalPlantsByCell"]["1_6"]["id"]==before["normalPlantsByCell"]["1_3"]["id"]
refund=sum(before["coldStorage"]["plantCosts"][original[i]["placementType"]]*3//4 for i in targets)
assert done["coldStorage"]["killIncome"]==refund
assert done["coldStorage"]["enemyIce"]==20+refund
assert not set(targets)&{p["id"] for p in done["plants"]}
assert done["plantCount"]==again["plantCount"]==3 and not ids(done) and not ids(again)
assert done["coldStorage"]["spent"]==again["coldStorage"]["spent"]==180
assert done["coldStorage"]["killIncome"]==again["coldStorage"]["killIncome"]
missingBefore,missingLoaded,missingDone=(read(n) for n in ("missing_before","missing_loaded","missing_done"))
for state in (missingBefore,missingLoaded,missingDone):ledger(state)
missingTargets=ids(missingLoaded)
assert len(missingTargets)==3 and missingTargets[0] not in {p["id"] for p in missingLoaded["plants"]}
replacement=missingLoaded["normalPlantsByCell"]["0_1"]["id"]
assert replacement not in missingTargets
assert missingDone["normalPlantsByCell"]["0_1"]["id"]==replacement and missingDone["plantCount"]==1
original={p["id"]:p for p in missingBefore["plants"]}
refund=sum(missingBefore["coldStorage"]["plantCosts"][original[i]["placementType"]]*3//4 for i in missingTargets[1:])
assert missingDone["coldStorage"]["enemyIce"]==20+refund and missingDone["coldStorage"]["killIncome"]==refund
assert missingDone["coldStorage"]["spent"]==180 and not ids(missingDone)
for level in (96,2001):
    state=read("map_"+str(level));ledger(state)
    assert state["coldStorage"]["spent"]==180 and not ids(state) and state["plantCount"]==0
for name in ("three_pending","missing_primary"):
    snapshot=json.loads((folder/"snapshots"/(name+".json")).read_text(encoding="utf-8-sig"))
    # v26开始保存完整目标名单；后续关卡schema升级仍必须保留这些已付款身份。
    assert snapshot["schemaVersion"]>=26 and len(snapshot["coldStorage"]["strikeAdditionalTargetIDs"])==2
capacityBefore,capacityAfter=read("capacity_before"),read("capacity_after")
for state in (capacityBefore,capacityAfter):ledger(state)
b=capacityBefore["coldStorage"];a=capacityAfter["coldStorage"]
assert b["deploymentLimit"]==66 and len(b["pending"])==62
assert a["deploymentLimit"]==64 and len(a["pending"])==64
assert a["spent"]-b["spent"]==180+sum(unit["cost"] for unit in a["pending"][-2:])
assert b["enemyIce"]-a["enemyIce"]==a["spent"]-b["spent"]
report={"passed":True,"threeTargetFee":180,"missingPrimaryDoesNotCancelOthers":True,"noRetargetOrDuplicateFees":True,"maps":[88,96,2001]}
(folder/"multi_precision_verification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
print(json.dumps(report))
