"""Check the mixed legal sparring defense and real resource ledger; one run is not a win rate."""
import json
import sys
from pathlib import Path
from verify_commander_frontline import inspect

folder=Path(sys.argv[1]) if len(sys.argv)>1 else Path("build/clang-release/autotest/out/battle_commander_mixed_96")
assert json.loads((folder/"status.json").read_text())["status"]=="passed"
assert "script finished OK" in (folder/"run.log").read_text(encoding="utf-8-sig")
result=inspect(folder)
plantings={}
coexisting=0
for phase in ("opening","middle","late"):
    episode=json.loads((folder/(phase+".json")).read_text(encoding="utf-8-sig"))
    assert episode["opponent"]=="ice_bunker_mixed" and episode["playerActions"]
    for kind,count in episode["playerPlantings"].items():
        plantings[kind]=plantings.get(kind,0)+count
    for sample in episode["trace"]:
        kinds=sample["plantTypes"]
        if kinds.get("PLANT_ELITE_SCAREDYSHROOM",0)>0 and kinds.get("PLANT_THUNDERFLOWER",0)>0:
            coexisting+=1
assert plantings.get("PLANT_ELITE_SCAREDYSHROOM",0)>0 and plantings.get("PLANT_THUNDERFLOWER",0)>0
assert coexisting>0, "the two real outputs must coexist rather than only replace each other after quota exhaustion"
result["playerPlantings"]=plantings
result["mixedDefenseSamples"]=coexisting
(folder/"mixed_verification.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps({"passed":True,"mixedDefenseSamples":coexisting,"playerPlantings":plantings,
                  "frontlineIntervals":result["frontlineIntervals"],"episodes":result["episodes"]},ensure_ascii=False,indent=2))
