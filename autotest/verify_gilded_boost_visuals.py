"""Verify frozen tail pixels, rearward geometry, and independent boost sources."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def verify(directory: Path) -> bool:
    def state(name):
        return json.loads((directory / (name + ".json")).read_text(encoding="utf-8"))

    a, b = [np.asarray(Image.open(directory / (name + ".png"))) for name in ("paused_a", "paused_b")]
    checks = {"pause_freezes_boost_pixels": bool(np.array_equal(a[270:390], b[270:390]))}
    for name, effect in (("boost_sustained", "GildedZamboniBoost"), ("overdrive", "GildedZamboniOverdrive")):
        particles = state(name)["particleEffectsByName"][effect]
        visible = [p for p in particles if p["renderProbeReady"]]
        checks[name + "_visible_tail"] = bool(visible) and all(
            p["worldBounds"]["widthInt"] >= 35 and 60 <= p["nearestZombie"]["centerDxInt"] <= 200
            for p in visible)
    independent = state("independent_boosts")
    survivor = next(z for z in independent["zombies"] if z["row"] == 3)
    tails = independent["particleEffectsByName"]["GildedZamboniBoost"]
    # Newly emitted effects can precede their first render probe; the emitted origin is still authoritative.
    checks["only_undamaged_car_keeps_tail"] = bool(tails) and all(
        abs(p["originYInt"] - survivor["y"] - 4) <= 1 for p in tails)
    checks["load_does_not_replay_kick"] = state("boost_loaded")["particleEffectNameCounts"]["GildedZamboniBoostKick"] == 0
    checks["damage_removes_all_tails"] = all(state("damaged_reset")["particleEffectNameCounts"][name] == 0
        for name in ("GildedZamboniBoost", "GildedZamboniOverdrive", "GildedZamboniBoostKick"))
    report = dict(passed=all(checks.values()), checks=checks)
    (directory / "boost_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    results = [verify(Path(arg)) for arg in sys.argv[1:]]
    raise SystemExit(0 if results and all(results) else 1)
