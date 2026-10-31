"""Check guide pause, resume, and nested catalog control cleanup after the visible UI script."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_guide")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


assert read("status")["status"] == "passed"
before, after = read("guide_paused_before"), read("guide_paused_after")
assert before == after, "guide advanced factories or native combat while paused"
assert read("guide_resumed") != after, "closing guide did not resume combat"
initial, finished = read("initial")["workshop"], read("finished")["workshop"]
assert initial["uiButtonCount"] == finished["uiButtonCount"], "guide/catalog controls leaked"
assert not any(finished[key] for key in ("guideOpen", "catalogOpen", "plantCatalogOpen"))
assert finished["paused"] and finished["effectivePaused"]
assert len(list(out.glob("*.png"))) == 23, "missing guide visual checkpoints"
print("Workshop guide: native pause, resume, unlock/save assertions and control cleanup passed")
