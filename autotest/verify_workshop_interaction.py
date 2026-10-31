"""Check real mouse gestures preserve the factory while moving its viewport."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/clang-release/autotest/out/smoke_workshop_interaction")


def read(name):
    return json.loads((out / (name + ".json")).read_text(encoding="utf-8"))


assert read("status")["status"] == "passed"
for tag in ("build", "remove", "wire", "cancel", "outside"):
    assert read(tag + "_before") == read(tag + "_after"), f"{tag} gesture mutated the factory or battle"
for tag in ("build", "remove", "wire"):
    before, drag, after = (read(tag + suffix)["workshop"] for suffix in ("_camera_before", "_camera_drag", "_camera_after"))
    assert drag["draggingMap"] and not after["draggingMap"]
    assert before["tool"] == drag["tool"] == after["tool"]
    for axis, delta in (("x", 50), ("y", 30)):
        assert abs(after["camera"][axis] - before["camera"][axis] - delta) < 0.1, f"{tag} drag did not follow mouse {axis}"
        assert after["camera"][axis] == drag["camera"][axis], f"{tag} release jumped the camera"
middle_before, middle_after = (read(n)["workshop"]["camera"] for n in ("middle_before", "middle_after"))
assert abs(middle_after["x"] - middle_before["x"] - 30) < 0.1
assert abs(middle_after["y"] - middle_before["y"] - 25) < 0.1
assert read("keyboard_after")["workshop"]["camera"]["x"] < read("keyboard_before")["workshop"]["camera"]["x"], "paused keyboard pan was frozen"
finished = read("finished")["workshop"]
assert finished["resourcesReady"] and not finished["saveFailed"] and finished["tool"] == 0
for name in ("01_wrong_outlet", "02_connected_outlet", "03_drag_build", "03_drag_remove", "03_drag_wire", "04_remove_warning", "05_wire_target", "06_finished"):
    assert (out / (name + ".png")).stat().st_size > 0
print("Workshop input: held left drag, cancellation, hover rotation, removal, wiring and mode transitions passed")
