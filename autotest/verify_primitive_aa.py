"""Check visible analytic primitives, coverage, clipping and serial/worker parity."""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from verify_mixed_geometry_order import verify as verify_order


def verify(directory: Path) -> bool:
    pixels = np.asarray(Image.open(directory / "primitives.png").convert("RGB"), dtype=np.int16)
    assert pixels.shape == (600, 1100, 3), pixels.shape
    left, right = pixels[80:570, 90:480], pixels[80:570, 610:1000]
    difference = np.max(np.abs(left - right), axis=2)
    gray = pixels[:, :, 0] / 255.0
    ring = gray[80:180, 290:390]
    disc = gray[180:280, 290:390]
    horizontal = gray[295:305, 98:432]
    diagonal = gray[315:355, 98:432]
    checks = {
        "serial_worker_parity": int(np.count_nonzero(difference > 5)) <= 20,
        "ring_has_partial_coverage": int(np.count_nonzero((ring > .02) & (ring < .98))) > 100,
        "ring_preserves_one_pixel_width": abs(float(ring.sum()) / (2 * math.pi * 45) - 1) < .12,
        "filled_disc_area": abs(float(disc.sum()) / (math.pi * 45**2) - 1) < .02,
        "disc_center_is_solid": bool(np.all(pixels[220:240, 330:350] == 255)),
        "odd_legacy_segments_no_missing_wedges": bool(np.all(pixels[218:242, 213:237] == 255)),
        "horizontal_line_coverage": abs(float(horizontal.sum()) / 330 - 1) < .03,
        "diagonal_line_coverage": abs(float(diagonal.sum()) / math.hypot(330, 30) - 1) < .05,
        "subpixel_disc_visible": float(gray[227:233, 417:423].sum()) > .05,
        "clip_inside_visible": bool(np.all(pixels[402:428, 390:410] == 255)),
        "clip_left_clean": bool(np.all(pixels[395:435, 380:385] == 0)),
        "clip_right_clean": bool(np.all(pixels[395:435, 415:420] == 0)),
    }
    # Fullscreen uses the same logical layout; check circle area after viewport scaling.
    full = np.asarray(Image.open(directory / "fullscreen.png").convert("RGB"))
    height, width = full.shape[:2]
    scale = min(width / 1100, height / 600)
    ox, oy = (width - 1100 * scale) / 2, (height - 600 * scale) / 2
    crop = full[round(oy + 180 * scale):round(oy + 280 * scale),
                round(ox + 290 * scale):round(ox + 390 * scale), 0] / 255.0
    checks["fullscreen_disc_area"] = abs(float(crop.sum()) / (math.pi * (45 * scale)**2) - 1) < .03
    checks["mixed_instance_batch_order"] = verify_order(directory)
    report = dict(passed=all(checks.values()), checks=checks,
                  differing_pixels=int(np.count_nonzero(difference > 5)),
                  ring_coverage=float(ring.sum()), disc_coverage=float(disc.sum()),
                  horizontal_coverage=float(horizontal.sum()), diagonal_coverage=float(diagonal.sum()))
    (directory / "primitive_aa_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    raise SystemExit(0 if all(verify(Path(arg)) for arg in sys.argv[1:]) else 1)
