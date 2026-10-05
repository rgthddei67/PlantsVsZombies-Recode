"""Check quota-specific same-cell replacement and real card payment/cooldown."""
import json
import sys
from pathlib import Path
folder=Path(sys.argv[1]) if len(sys.argv)>1 else Path("build/clang-release/autotest/out/smoke_commander_mixed_replacement")
def read(name): return json.loads((folder/(name+".json")).read_text(encoding="utf-8-sig"))
assert read("status")["status"]=="passed"
before,last,vacant,after=(read(name) for name in ("before","last_elite","vacant","after"))
ready=read("replacement_ready")
first,last_episode=read("last_elite_episode"),read("replacement_episode")
assert before["eliteScaredyShroomsPlanted"]==before["eliteScaredyShroomTotalPlantLimit"]-1
assert last["normalPlantsByCell"]["2_0"]["type"]=="PLANT_ELITE_SCAREDYSHROOM"
assert "2_0" not in vacant["normalPlantsByCell"]
assert after["normalPlantsByCell"]["2_0"]["type"]=="PLANT_THUNDERFLOWER"
assert last["normalPlantsByCell"]["2_1"]["type"]=="PLANT_THUNDERFLOWER"
assert last["eliteScaredyShroomsPlanted"]==after["eliteScaredyShroomsPlanted"]==after["eliteScaredyShroomTotalPlantLimit"]
# 半秒响应允许本段用完最后一份名额后再补另一格；分别核对两笔真实付款，不假定一秒只有一株。
assert first["playerPlantings"]=={"PLANT_ELITE_SCAREDYSHROOM":1,"PLANT_THUNDERFLOWER":1}
assert before["sun"]-last["sun"]==sum(c["sunCost"] for c in before["cards"])
assert before["coldStorage"]["playerIce"]-last["coldStorage"]["playerIce"]==sum(before["coldStorage"]["plantCosts"][c["gameplayType"]] for c in before["cards"])
assert not next(c for c in vacant["cards"] if c["gameplayType"]=="PLANT_THUNDERFLOWER")["ready"]
assert "2_0" not in ready["normalPlantsByCell"] and ready["eliteScaredyShroomsPlanted"]==16
assert last_episode["playerPlantings"]=={"PLANT_THUNDERFLOWER":1}
for old,new,kind in ((ready,after,"PLANT_THUNDERFLOWER"),):
    card=next(c for c in old["cards"] if c["gameplayType"]==kind)
    next_card=next(c for c in new["cards"] if c["gameplayType"]==kind)
    assert old["sun"]-new["sun"]==card["sunCost"]
    assert old["coldStorage"]["playerIce"]-new["coldStorage"]["playerIce"]==old["coldStorage"]["plantCosts"][kind]
    assert card["ready"] and not next_card["ready"] and next_card["cooldownRemainingMs"]>0
assert "script finished OK" in (folder/"run.log").read_text(encoding="utf-8-sig")
print(json.dumps({"passed":True,"lastEliteUsedBeforeFallback":True,"sameCell":"2_0","normalPaymentAndCooldown":True}))
