"""Validate catalog modal lifecycle, build transactions and native pause restoration."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_catalog")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


assert read("status")["status"] == "passed"
for before, after in (("running_modal_before", "running_modal_after"), ("manual_before", "manual_after")):
    a, b = read(before), read(after)
    assert a == b, f"catalog advanced factory or native battle: {before}"
assert read("running_modal_after")["state"] != read("running_resumed")["state"], "closing catalog failed to resume simulation"
initial, recent, finished = (read(n)["workshop"] for n in ("initial", "recent", "finished"))
assert initial["uiButtonCount"] == recent["uiButtonCount"] == finished["uiButtonCount"], "catalog leaked controls across repeated opens"
assert not finished["catalogOpen"] and finished["paused"] and finished["effectivePaused"]
assert recent["energy"] == 2000 and recent["machineCount"] == 4, "selection paid or placed a machine"
for name in ("01_production", "02_locked", "03_logistics", "04_logic", "05_valid_preview", "06_occupied_preview", "07_unaffordable", "08_shortage_preview", "09_recent"):
    assert (out / (name + ".png")).stat().st_size > 0
print("Workshop catalog: categories, previews, transactions, recents and pause restoration passed")
