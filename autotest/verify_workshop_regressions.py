"""Check raw-save recovery and repeated native healer decisions on one workshop Board."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out")


def read(folder, name):
    return json.loads((folder / (name + ".json")).read_text(encoding="utf-8"))


recovery = root / "smoke_workshop_recovery"
assert read(recovery, "status")["status"] == "passed"
contents = ['{"基地":\x00', '', '{"schemaVersion":7,']
backups = sorted(recovery.glob("workshop.json.before-power*.json"), key=lambda p: p.stat().st_mtime_ns)[-3:]
assert len(backups) == 3
for i, content in enumerate(contents):
    assert backups[i].read_bytes() == content.encode("utf-8"), "backup must preserve even invalid or empty bytes"
    restored = read(recovery, f"recovered_{i}")
    assert restored["schemaVersion"] == 7 and restored["state"]["completedWaves"] == 0

healer = root / "smoke_workshop_healer"
assert read(healer, "status")["status"] == "passed"
identities = None
for i in range(2):
    before = {z["id"]: z for z in read(healer, f"wounded_{i}")["battle"]["zombies"]}
    after = {z["id"]: z for z in read(healer, f"healed_{i}")["battle"]["zombies"]}
    assert before.keys() == after.keys()
    if identities is None:
        identities = set(before)
    assert set(before) == identities, "reloading/recreating actors must not reset the shared budget"
    medics = [z for z in after.values() if "healCooldown" in z.get("extraData", {})]
    patients = [z for z in before.values() if "healCooldown" not in z.get("extraData", {})]
    assert len(medics) == len(patients) == 2
    assert all(z["extraData"]["mcRollouts"] > 0 for z in medics), "every healer must claim a decision slot"
    assert all(after[z["id"]]["bodyHealth"] > z["bodyHealth"] for z in patients), "both lanes must heal again"
print("Workshop regressions passed: raw/empty/truncated backups, cancel/confirm, two healers healing twice")
