"""Check forecast-only deployment suppression and cumulative replacement quotas."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    "build/clang-release/autotest/out/smoke_commander_deployment_pressure")

def read(name):
    return json.loads((root / f"{name}.json").read_text(encoding="utf-8"))

assert read("status")["status"] == "passed"
assert "script finished OK" in (root / "run.log").read_text(encoding="utf-8")
before, planned, exhausted = (read(name) for name in ("before", "planned", "exhausted"))
for key in ("sun", "eliteScaredyShroomsPlanted", "eliteScaredyShroomPlantLimit"):
    assert before[key] == planned[key], key
for key in ("enemyIce", "playerIce", "workerIncome", "killIncome", "spent"):
    assert before["coldStorage"][key] == planned["coldStorage"][key], key
assert before["plants"] == planned["plants"], "forecast changed plant health/growth"
assert before["zombies"] == planned["zombies"], "forecast changed sniper state"
ice = planned["coldStorage"]
assert ice["searchEliteRemainingUses"] == 3
assert 0 < ice["searchPredictedPlantings"] <= 3
assert ice["searchDeploymentShots"] > 0 and ice["searchDeploymentHits"] > 0
assert exhausted["eliteScaredyShroomsPlanted"] == exhausted["eliteScaredyShroomPlantLimit"]
assert exhausted["coldStorage"]["searchEliteRemainingUses"] == 0
assert exhausted["coldStorage"]["searchEliteReplacementOptions"] == 0
assert exhausted["coldStorage"]["searchPredictedPlantings"] == 0
print(f"PASS: {ice['searchPredictedPlantings']} finite replacements, "
      f"{ice['searchDeploymentShots']} forecast shots; real health, growth and wallets unchanged.")
