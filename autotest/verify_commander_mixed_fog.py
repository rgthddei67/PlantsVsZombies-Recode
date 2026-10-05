"""Verify paid clear-fog and public-information blind ash transactions."""
import json
import sys
from pathlib import Path

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_commander_mixed_fog")
def read(name):
    return json.loads((folder / (name + ".json")).read_text(encoding="utf-8-sig"))

assert read("status")["status"] == "passed"
assert "script finished OK" in (folder / "run.log").read_text(encoding="utf-8-sig")
before, after = read("before_blind"), read("after_blind")
blind = read("blind")
assert before["zombieCount"] == after["zombieCount"] == 0
assert before["weatherStation"]["controls"][1]["protection"] > 0
assert blind["sparringFogClears"] == 0 and blind["sparringBlindDoomCasts"] == 1
assert blind["playerPlantings"] == {"PLANT_DOOMSHROOM": 1}
assert after["coldStorage"]["enemyIce"] > before["coldStorage"]["enemyIce"]
card = next(card for card in before["cards"] if card["gameplayType"] == "PLANT_DOOMSHROOM")
assert before["sun"] - after["sun"] == card["sunCost"]
assert before["coldStorage"]["playerIce"] - after["coldStorage"]["playerIce"] == before["coldStorage"]["plantCosts"]["PLANT_DOOMSHROOM"]
assert after["cards"][0]["cooldownRemainingMs"] > 0
# 半秒观察后毁灭可能已经起爆，不能要求取证时仍存活；正式新弹坑保留实际落点。
locations = [(plant["row"], plant["col"]) for plant in after["plants"] if plant["type"] == "PLANT_DOOMSHROOM"]
old_craters = {(crater["row"], crater["col"]) for crater in before["craters"]}
locations += [(crater["row"], crater["col"]) for crater in after["craters"]
              if (crater["row"], crater["col"]) not in old_craters]
assert len(locations) == 1 and before["fog"]["cellAlpha"][locations[0][0]][locations[0][1]] > 0
before, after = read("before_clear"), read("after_clear")
clear = read("clear")
assert clear["sparringFogClears"] == 1 and clear["sparringBlindDoomCasts"] == 0
assert before["coldStorage"]["playerIce"] - after["coldStorage"]["playerIce"] == 40
assert before["sun"] == after["sun"]
control = after["weatherStation"]["controls"][1]
assert control["value"] == 1 and control["pending"] == 0 and control["player"]
assert 0 < control["warning"] <= 8
print(json.dumps({"passed": True, "blindDoomCasts": 1, "paidClearFog": 1, "hiddenEnemies": 0}))
