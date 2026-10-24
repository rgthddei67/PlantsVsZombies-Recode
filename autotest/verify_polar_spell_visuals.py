"""Check frozen spell pixels, separated rifts, and clock marks following a moving target."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def verify(directory: Path) -> bool:
    def frame(name):
        image = np.asarray(Image.open(directory / (name + ".png")).convert("RGB"), dtype=np.int16)
        assert image.shape == (600, 1100, 3)
        return image

    def state(name):
        return json.loads((directory / (name + ".json")).read_text(encoding="utf-8"))

    a, b = frame("aurora_paused_a"), frame("aurora_paused_b")
    checks = {"pause_freezes_shader_and_particles": bool(np.array_equal(a[210:410, 800:1000], b[210:410, 800:1000]))}
    rifts = frame("rifts_open")
    # The fixture selects three successive rows in column 4. Their tips must not form one solid black column.
    gaps = [rifts[y - 2:y + 3, 598:607] for y in (163, 263)]
    checks["adjacent_rifts_have_visible_gaps"] = all(float(np.mean(pixels)) > 100 for pixels in gaps)
    centers, positions = [], []
    for name in ("clock_marks", "clock_follow", "clock_loaded"):
        zombie = state(name)["zombiesByType"]["ZOMBIE_NORMAL"]
        x, y = zombie["xInt"], zombie["yInt"] + 7
        pixels = frame(name)[y - 36:y + 37, x - 36:x + 37]
        red, green, blue = (pixels[:, :, i] for i in range(3))
        gold = (red > 130) & (green > 90) & (red > blue * 1.20) & (green > blue * 1.04)
        yy, xx = np.nonzero(gold)
        checks[name + "_has_visible_clock"] = len(xx) > 40
        centers.append(float(np.mean(xx)) + x - 36 if len(xx) else -1000.0)
        positions.append(zombie["x"])
    checks["target_really_moves"] = abs(positions[1] - positions[0]) > 5
    checks["clock_follows_target"] = abs((centers[1] - centers[0]) - (positions[1] - positions[0])) < 8
    checks["loaded_clock_stays_with_target"] = abs((centers[2] - centers[1]) - (positions[2] - positions[1])) < 8
    checks["loading_does_not_replay_bursts"] = all(
        state(name)["polarFinaleVisuals"][effect] == 0
        for name in ("rifts_loaded", "clock_loaded") for effect in ("arrivals", "rewinds", "trails"))
    rewind, finished = frame("clock_rewind"), frame("clock_finished")
    changed = np.max(np.abs(rewind[300:400, 580:1000] - finished[300:400, 580:1000]), axis=2) > 25
    checks["rewind_has_visible_transient"] = int(np.count_nonzero(changed)) > 400
    checks["interrupted_channel_is_removed"] = state("interrupted")["polarFinaleVisuals"]["auroraChannels"] == 0
    report = dict(passed=all(checks.values()), checks=checks, mark_centers=centers)
    (directory / "polar_spell_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    results = [verify(Path(arg)) for arg in sys.argv[1:]]
    raise SystemExit(0 if results and all(results) else 1)
