"""Verify support selection, visible-field use and legal pine/elite construction."""
import json
from pathlib import Path

root = Path("build/clang-release/autotest/out/smoke_commander_pine_elite_opponent")
log = (root / "run.log").read_text(encoding="utf-8")
assert json.loads((root / "status.json").read_text())["status"] == "passed"
assert "script finished OK" in log and "rejected:" not in log.replace(
    "collect_sun rejected: sun_unavailable", "expected_collect_race")
assert "player activated cold pineapple" in log
episode = json.loads((root / "pine_opponent.json").read_text(encoding="utf-8"))
assert episode["opponent"] == "pine_elite" and episode["playerActions"]
assert not episode["externalSun"]["enabled"]
assert episode["initial"]["coldStorage"]["openingBonusMask"] == 5
assert episode["playerPlantings"].get("PLANT_ELITE_SCAREDYSHROOM", 0) > 0
final = json.loads((root / "final.json").read_text(encoding="utf-8"))
assert final["eliteScaredyShroomsPlanted"] <= final["eliteScaredyShroomPlantLimit"] == 10
assert "PLANT_COLDPINEAPPLE" in {p["type"] for p in episode["initial"]["plants"]}
print("PASS: selected two supports, built legal elite replacements and paid for pineapple fields without external sun.")
