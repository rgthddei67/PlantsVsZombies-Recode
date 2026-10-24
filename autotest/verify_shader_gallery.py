"""Check registered source art, three distinct materials, time variation and scene restoration."""

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def verify(directory: Path) -> bool:
    if (directory / "controls_reset_a.png").exists():
        return verify_controls(directory)
    frames = [np.asarray(Image.open(directory / name).convert("RGB"), dtype=np.int16)
              for name in ("materials_a.png", "materials_b.png")]
    assert all(frame.shape == (600, 1100, 3) for frame in frames)
    checks, panels = {}, []
    for index in range(3):
        x = 40 + index * 355
        a, b = [frame[175:475, x:x + 300] for frame in frames]
        changed = np.max(np.abs(a - b), axis=2) > 16
        panels.append((a, b))
        checks[f"material_{index}_changes_with_time"] = int(np.count_nonzero(changed)) > 250
    shield = panels[0][0]
    cyan = (shield[:, :, 1] > 90) & (shield[:, :, 2] > 120) & (shield[:, :, 0] < shield[:, :, 1] * .7)
    nut = (shield[:, :, 0] > 65) & (shield[:, :, 1] > 35) & (shield[:, :, 2] < shield[:, :, 0] * .65)
    checks["shield_cyan_energy"] = int(np.count_nonzero(cyan)) > 500
    checks["shield_keeps_source_art"] = int(np.count_nonzero(nut)) > 500
    ice = panels[1][1]
    frozen = (ice[:, :, 2] > 110) & (ice[:, :, 2] > ice[:, :, 0] * 1.2)
    checks["frost_is_blue_and_visible"] = int(np.count_nonzero(frozen)) > 1500
    fire = panels[2][0]
    embers = (fire[:, :, 0] > 140) & (fire[:, :, 0] > fire[:, :, 1] * 1.4) & (fire[:, :, 1] > 20)
    checks["dissolve_has_glowing_edge"] = int(np.count_nonzero(embers)) > 60
    restored = np.asarray(Image.open(directory / "restored.png").convert("RGB"), dtype=np.int16)
    grass = (restored[:, :, 1] > restored[:, :, 0] * 1.3) & (restored[:, :, 1] > restored[:, :, 2] * 1.3)
    checks["normal_scene_restored"] = int(np.count_nonzero(grass)) > 20000
    state = json.loads((directory / "renderer_state.json").read_text(encoding="utf-8"))
    checks["no_gameplay_zombies"] = state.get("zombieCount", len(state.get("zombies", []))) == 0
    report = dict(passed=all(checks.values()), checks=checks,
                  renderer=state.get("graphics", {}).get("renderer"),
                  shield_cyan_pixels=int(np.count_nonzero(cyan)),
                  frost_blue_pixels=int(np.count_nonzero(frozen)),
                  ember_pixels=int(np.count_nonzero(embers)))
    (directory / "shader_gallery_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


def verify_controls(directory: Path) -> bool:
    images = [np.asarray(Image.open(directory / name).convert("RGB"), dtype=np.int16)
              for name in ("controls_reset_a.png", "controls_reset_b.png",
                           "controls_hit.png", "controls_resumed.png")]
    a, b, hit, resumed = [frame[175:475, 40:1050] for frame in images]
    checks = {
        "pause_holds_materials": bool(np.array_equal(a, b)),
        "hit_changes_shield": int(np.count_nonzero(np.max(np.abs(hit[:, :300] - b[:, :300]), axis=2) > 16)) > 100,
        "hit_keeps_other_materials": bool(np.array_equal(hit[:, 355:], b[:, 355:])),
        "resume_advances_materials": int(np.count_nonzero(np.max(np.abs(resumed - hit), axis=2) > 16)) > 500,
        "reset_clears_frost_progress": bool(np.max(images[0][507:510, 400:690]) < 90),
    }
    report = dict(passed=all(checks.values()), checks=checks)
    (directory / "shader_gallery_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    results = [verify(Path(arg)) for arg in sys.argv[1:]]
    raise SystemExit(0 if results and all(results) else 1)
