"""Check workshop usability evidence after the visible input/cooldown scenario."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_usability")


def canonical(value):
    if isinstance(value, dict):
        return {key: canonical(item) for key, item in value.items()}
    if isinstance(value, list):
        if value and all(isinstance(item, dict) and "id" in item for item in value):
            value = sorted(value, key=lambda item: item["id"])
        return [canonical(item) for item in value]
    return value


def read(name):
    return canonical(json.loads((out / (name + ".json")).read_text(encoding="utf-8")))


assert read("status")["status"] == "passed"
first = read("warning_first")["workshop"]
held = read("warning_held")["workshop"]
assert first["materialNoticeCount"] == held["materialNoticeCount"], "persistent mismatch repeatedly notified"
assert first["materialWarnings"][3] and first["materialWarnings"][3] == held["materialWarnings"][3]
cooldown = read("cooldown")
for name in ("cooldown_paused", "cooldown_restored", "rejected_plant"):
    assert read(name) == cooldown, f"{name} advanced cooldown, charged sun, or changed native combat"
assert cooldown["state"]["plantCooldowns"]["wallnut"] == 25
assert cooldown["state"]["plantCooldowns"]["pea"] == 7.5
after = read("expired_pea")
assert after["state"]["plantCooldowns"]["pea"] == 7.5
assert 0 < after["state"]["plantCooldowns"]["wallnut"] < 25
finished = read("finished")["workshop"]
assert finished["resourcesReady"] and not finished["saveFailed"]
assert not finished["configOpen"] and not finished["modalOpen"]
assert not finished["plantCooldowns"]
assert len(list(out.glob("*.png"))) == 11
print("Workshop usability: card clicks, double-click, mismatch deduplication, combat cooldown and exact restore passed")
