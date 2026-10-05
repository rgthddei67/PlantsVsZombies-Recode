"""Check the mixed legal sparring defense and real resource ledger; one run is not a win rate."""
import argparse
import json
from pathlib import Path
from verify_commander_frontline import inspect

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("folder",nargs="?",type=Path,default=Path("build/clang-release/autotest/out/battle_commander_mixed_96"))
parser.add_argument("--require-win",action="store_true",help="Require an actual commander victory, rather than only legal transactions.")
parser.add_argument("--require-profitable",action="store_true",help="Require real earned income to exceed all actual commander spending.")
parser.add_argument("--require-economy",action="store_true",help="Require actual worker income to repay worker purchases; military attacks may lose money.")
args=parser.parse_args()
folder=args.folder
assert json.loads((folder/"status.json").read_text())["status"]=="passed"
assert "script finished OK" in (folder/"run.log").read_text(encoding="utf-8-sig")
result=inspect(folder)
plantings={}
coexisting=0
fogClears=blindDoomCasts=0
precisionCasts={1:0,2:0,3:0}
precisionSerials=set()
final=None
won=False
for phase in ("opening","middle","late"):
    episode=json.loads((folder/(phase+".json")).read_text(encoding="utf-8-sig"))
    assert episode["opponent"]=="ice_bunker_mixed" and episode["playerActions"]
    fogClears+=episode.get("sparringFogClears",0)
    blindDoomCasts+=episode.get("sparringBlindDoomCasts",0)
    won|=episode["outcome"]=="commander_win"
    final=episode["final"]["coldStorage"]
    for kind,count in episode["playerPlantings"].items():
        plantings[kind]=plantings.get(kind,0)+count
    for decision in episode["decisions"]:
        skill=decision.get("specialAbilities",{})
        target=skill.get("precisionTarget",0)
        if target>0:
            targets=[target]+skill.get("precisionAdditionalTargets",[])
            assert 1<=len(targets)<=3 and len(set(targets))==len(targets)
            if decision["serial"] not in precisionSerials:
                precisionSerials.add(decision["serial"])
                precisionCasts[len(targets)]+=1
    for sample in episode["trace"]:
        kinds=sample["plantTypes"]
        if kinds.get("PLANT_ELITE_SCAREDYSHROOM",0)>0 and kinds.get("PLANT_THUNDERFLOWER",0)>0:
            coexisting+=1
assert plantings.get("PLANT_ELITE_SCAREDYSHROOM",0)>0 and plantings.get("PLANT_THUNDERFLOWER",0)>0
assert coexisting>0, "the two real outputs must coexist rather than only replace each other after quota exhaustion"
if args.require_win:
    assert won, "the actual board must end in commander victory"
if args.require_profitable:
    assert final["workerIncome"]>final["initialEnemyIce"], "workers must develop beyond the initial wallet"
    assert final["workerIncome"]+final["killIncome"]>final["spent"], "real earned income must pay back all actual spending"
workerCount=final["deploymentTypes"].get("ZOMBIE_ICE_WORKER",0)
workerInvestment=workerCount*final.get("zombieCosts",{}).get("ZOMBIE_ICE_WORKER",0)
workerInvestment+=sum(paid["cost"] for paid in final["pending"] if paid.get("typeName")=="ZOMBIE_ICE_WORKER")
if args.require_economy:
    assert workerCount>0 and workerInvestment>0, "real worker purchases and formal prices are required"
    assert final["workerIncome"]>workerInvestment, "actual production must repay the worker investment"
result["workerInvestment"]=workerInvestment
result["workerProductionMargin"]=final["workerIncome"]-workerInvestment
result["commanderWon"]=won
result["actualFogClears"]=fogClears
result["actualBlindDoomCasts"]=blindDoomCasts
result["precisionCastsByTargetCount"]=precisionCasts
result["earnedNetIce"]=final["workerIncome"]+final["killIncome"]-final["spent"]
result["playerPlantings"]=plantings
result["mixedDefenseSamples"]=coexisting
(folder/"mixed_verification.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps({"passed":True,"mixedDefenseSamples":coexisting,"playerPlantings":plantings,
                  "frontlineIntervals":result["frontlineIntervals"],"episodes":result["episodes"],
                  "commanderWon":won,"earnedNetIce":result["earnedNetIce"],
                  "actualFogClears":fogClears,"actualBlindDoomCasts":blindDoomCasts,
                  "precisionCastsByTargetCount":precisionCasts,
                  "workerInvestment":workerInvestment,"workerProductionMargin":result["workerProductionMargin"]},ensure_ascii=False,indent=2))
