"""Verify overflow commits, stable target/plant identity and complete foundation stacks."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out"


def read(suite, name):
    return json.loads((ROOT / suite / (name + ".json")).read_text(encoding="utf-8"))


def verify():
    suite = "smoke_waterside_overflow"
    assert read(suite, "status")["status"] == "passed"
    built = read(suite, "built")
    saved, restored = read(suite, "warning_saved"), read(suite, "warning_restored")
    assert saved["watersideFlood"] == restored["watersideFlood"], "warning reset or retargeted on load"
    assert saved["cells"] == restored["cells"], "layer ownership changed on load"
    targets = saved["watersideFlood"]["surgeTargets"]
    assert len(set(targets)) == len(targets) == 2
    assert set(targets) == {built["cells"][0][col]["underID"] for col in (4, 7)}
    impact = read(suite, "first_impact")
    assert impact["cells"][0][4]["under"] == "NONE"
    for col in (6, 7):
        assert impact["cells"][0][col]["underID"] == built["cells"][0][col]["underID"]
    for col in (4, 6, 7):
        assert impact["cells"][0][col]["normalID"] == built["cells"][0][col]["normalID"]
    warning = read(suite, "second_warning")
    replacement, second = read(suite, "replacement"), read(suite, "second_impact")
    new_lily = replacement["cells"][0][4]["underID"]
    assert new_lily not in warning["watersideFlood"]["surgeTargets"]
    assert second["cells"][0][4]["underID"] == new_lily, "old warning attacked a replacement lily"
    assert second["watersideFlood"]["surgeCount"] == 2
    drained = read(suite, "drained")
    for col in (4, 6, 7):
        assert drained["cells"][0][col]["normalID"] == built["cells"][0][col]["normalID"]
        assert drained["cells"][0][col]["under"] == "NONE"
        assert drained["cells"][0][col]["foundationID"] == impact["cells"][0][col]["foundationID"]

    suite = "smoke_waterside_foundation"
    assert read(suite, "status")["status"] == "passed"
    before = read(suite, "stack_before")["cells"][0][7]
    for name, row, col in (("wind_moved", 0, 6), ("relocated", 5, 7), ("stack_restored", 5, 7)):
        cell = read(suite, name)["cells"][row][col]
        for layer in ("foundation", "under", "normal", "pumpkin"):
            assert cell[layer + "ID"] == before[layer + "ID"], (name, layer)
    assert read(suite, "shoveled")["plantCount"] == 0
    before, after = read(suite, "cob_before"), read(suite, "cob_moved")
    assert {p["id"] for p in before["plants"]} == {p["id"] for p in after["plants"]}
    for source in (7, 8):
        for layer in ("foundation", "under", "normal"):
            assert before["cells"][5][source][layer + "ID"] == after["cells"][5][source - 1][layer + "ID"]
    five, smashed = read(suite, "five_layers"), read(suite, "smash_all_layers")
    for layer in ("foundation", "under", "normal", "pumpkin", "overlay"):
        assert five["cells"][0][6][layer] != "NONE"
        assert smashed["cells"][0][6][layer] == "NONE"
    print("Overflow and foundation passed: committed warnings, rescue, replacement, retreat, complete stacks and multicell motion.")


if __name__ == "__main__":
    verify()
