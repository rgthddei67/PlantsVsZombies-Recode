"""Check persisted defense-clock continuity and real withdrawal transactions."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_cold_storage_defense_victory"


def read(name):
    return json.loads((root / name).read_text(encoding="utf-8"))


assert read("status.json")["status"] == "passed"
paused = [read(name)["coldStorage"]["troopNoKillSeconds"] for name in ("pause_before.json", "pause_after.json")]
assert paused[0] == paused[1], paused
saved = read("before_reload.json")["coldStorage"]["troopNoKillSeconds"]
restored = read("after_reload.json")["coldStorage"]["troopNoKillSeconds"]
assert abs(restored - saved) < 0.1, (saved, restored)
before, after = read("before_surrender.json"), read("surrender.json")
a, b = before["coldStorage"], after["coldStorage"]
assert a["hostileCount"] > 0 and a["pending"] and a["strikeTargetID"] >= 0
assert b["defenseSurrendered"] and b["trophySpawned"] and b["troopNoKillSeconds"] == 360
assert b["hostileCount"] == 0 and not b["pending"] and b["strikeTargetID"] == -1
assert b["playerKillIncome"] == a["playerKillIncome"] == 0
assert b["spent"] == a["spent"] and b["enemyIce"] == a["enemyIce"], "withdrawal must not refund abandoned paid transactions"
target = next(p for p in before["plants"] if p["id"] == a["strikeTargetID"])
survivor = next(p for p in after["plants"] if p["id"] == target["id"])
assert survivor["health"] == target["health"], "cancelled commander strike must not kill its target"
print("Defense clock pause/save continuity, troop-only reset and paid withdrawal passed")
