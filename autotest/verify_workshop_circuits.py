"""Verify the visible workshop profile/counter/cannon scenario and real save boundaries."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_circuits")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


assert read("status")["status"] == "passed"
assert read("config_paused_before") == read("config_paused_after"), "configuration advanced the simulation"
mid = read("mid_volley")
assert mid == read("mid_volley_loaded"), "mid-volley restore changed factory or native combat"
cell = mid["state"]["cells"][42]
assert cell["volleyRemaining"] == 2 and cell["transferredItems"][3] == 1
assert cell["extraStock"][1:3] == [3, 6], "volley reservation must be paid once"
bullets = mid["battle"]["bullets"]
assert len(bullets) == 1 and bullets[0]["lobbedMotion"] and bullets[0]["plantOriginKind"] == 1

batch = read("batch")["workshop"]
assert batch["cells"][3]["transferredItems"][0] == 20
assert batch["cells"][4]["count"] == 0 and not batch["cells"][4]["signal"]

melon = {z["row"]: z for z in read("melon_hit")["workshop"]["nativeZombies"]}
ice = {z["row"]: z for z in read("ice_hit")["workshop"]["nativeZombies"]}
assert melon[2]["helmetHp"] == 740 and melon[3]["helmetHp"] == 980, "three native direct and adjacent splash hits"
assert ice[3]["helmetHp"] < melon[3]["helmetHp"] and ice[2]["helmetHp"] < melon[2]["helmetHp"]
assert ice[2]["chilled"] and ice[3]["chilled"], "native ice direct and splash status"
assert all(ice[row]["shadow"] for row in (2, 3))

initial, finished = read("initial")["workshop"], read("finished")["workshop"]
assert initial["uiButtonCount"] == finished["uiButtonCount"], "configuration or nested guide leaked buttons"
assert not finished["configOpen"] and not finished["guideOpen"] and finished["effectivePaused"]
assert finished["cannonCount"] == 5 and finished["cannonShots"] == 6
assert len(list(out.glob("*.png"))) == 14, "missing visual checkpoint"
print("Workshop circuits: profiles, real batch feedback, native cannon hits, exact restore and UI cleanup passed")
