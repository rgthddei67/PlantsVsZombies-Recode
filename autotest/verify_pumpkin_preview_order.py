"""Verify stable drawing, slot ownership and paused pixels after real preview input."""
import json
import sys
from pathlib import Path

from PIL import Image, ImageChops


def verify(directory):
    """Compare stable entity IDs and the unaffected adjacent pumpkin seam."""
    names = ["before", "after_add", "after_replace"] + [
        f"{phase}_{cycle}" for cycle in range(4) for phase in ("hover", "cancel")]
    states = {name: json.loads((directory / (name + ".json")).read_text(encoding="utf-8"))
              for name in names}
    status = json.loads((directory / "status.json").read_text(encoding="utf-8"))
    assert status["status"] == "passed", status
    before = states["before"]
    original_ids = set(before["plantDrawOrder"])
    assert len(states["after_add"]["plants"]) == len(original_ids) + 3
    assert len(states["after_replace"]["plants"]) == len(original_ids) + 4
    for name, state in states.items():
        plants = {p["id"]: p for p in state["plants"]}
        orders = [p["renderOrder"] for p in plants.values()]
        assert len(orders) == len(set(orders)), (name, "live plants share a slot")
        actual = [plants[pid]["renderOrder"] for pid in state["plantDrawOrder"]]
        assert actual == sorted(actual), (name, "draw list is stale")
        for key, normal in state["normalPlantsByCell"].items():
            top = state["topPlantsByCell"][key]
            if top["type"] == "PLANT_PUMPKINSHELL":
                assert normal["renderOrder"] < top["renderOrder"], (name, key)
        if name.startswith(("hover_", "cancel_")):
            assert state["plantDrawOrder"] == before["plantDrawOrder"], name
            old_orders = {p["id"]: p["renderOrder"] for p in before["plants"]}
            assert {pid: plants[pid]["renderOrder"] for pid in original_ids} == old_orders

    # Paused animations and no HP labels: only pixels of the untouched pair are compared.
    # Crop is the row-0 col-1/2 seam, away from the col-0 preview and mouse cursor.
    crop = (365, 100, 495, 184)
    with Image.open(directory / "before.png") as image:
        reference = image.convert("RGB").crop(crop)
    for name in states:
        if name.startswith(("hover_", "cancel_")):
            with Image.open(directory / (name + ".png")) as image:
                difference = ImageChops.difference(reference, image.convert("RGB").crop(crop))
            assert difference.getbbox() is None, (name, "unaffected pumpkin pixels changed")
    print(f"{directory.name}: stable draw IDs, unique live slots, stack order and 8 pixel comparisons passed")


if __name__ == "__main__":
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else
           Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_pumpkin_preview_order")
