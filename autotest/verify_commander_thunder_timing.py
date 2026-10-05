"""Verify real polling/windup stages and completed hit cadence against a stable target."""
import json
import sys
from pathlib import Path

folder=Path(sys.argv[1]) if len(sys.argv)>1 else Path("build/clang-release/autotest/out/smoke_commander_thunder_timing")
def load(name):
    return json.loads((folder/(name+".json")).read_text(encoding="utf-8-sig"))

assert load("status")["status"]=="passed"
birth,poll,windup,after=(load(name) for name in ("birth","polling","windup","after"))
flower=lambda s: next(p for p in s["plants"] if p["type"]=="PLANT_THUNDERFLOWER")
first=flower(birth)
assert all(flower(s)["id"]==first["id"] for s in (poll,windup,after))
timing=flower(poll)["thunderForecast"]
assert timing["cooldownRemaining"]==0 and .2<timing["checkRemaining"]<.6
assert timing["pendingRemaining"]<0 and not poll["bullets"], "completed cooldown must still wait for target polling"
timing=flower(windup)["thunderForecast"]
assert 0<timing["pendingRemaining"]<.78 and timing["cooldownRemaining"]>1
assert not windup["bullets"], "starting the head animation is not an immediate projectile release"
target=birth["zombies"][0]
end=next(z for z in after["zombies"] if z["id"]==target["id"])
assert target["bodyHealth"]-end["bodyHealth"]==40, "eight real seconds produce two 20-damage hits, not a 2-second forecast burst"
assert "script finished OK" in (folder/"run.log").read_text(encoding="utf-8-sig")
print(json.dumps({"passed":True,"hits":2,"damage":40,"independentPolling":True}))
