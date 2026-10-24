"""Check analytic lines/circles/rectangles, pixel alignment and serial/worker parity."""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from verify_mixed_geometry_order import verify as verify_order


def rectangle_checks(directory: Path) -> dict:
    """Validate coverage independently of the tessellation, including single-blend corners."""
    pixels = np.asarray(Image.open(directory / "rectangles.png").convert("RGB"), dtype=np.int16)
    assert pixels.shape == (600, 1100, 3), pixels.shape
    gray = pixels[:, :, 0] / 255.0
    difference = np.max(np.abs(pixels[80:570, 90:480] - pixels[80:570, 610:1000]), axis=2)
    aligned_frame = pixels[177:224, 107:174, 0]
    fractional_frame = pixels[177:224, 207:274, 0]
    camera = np.asarray(Image.open(directory / "rectangles_camera.png").convert("RGB"), dtype=np.int16)
    checks = {
        "rect_serial_worker_parity": int(np.count_nonzero(difference > 5)) <= 20,
        "aligned_fill_crisp": bool(np.all(pixels[100:140, 110:170] == 255)
                                   and np.all(pixels[100:140, 109] == 0)
                                   and np.all(pixels[140, 110:170] == 0)),
        "fractional_fill_area": abs(float(gray[98:143, 208:273].sum()) / 2400 - 1) < .01,
        "fractional_fill_edges": abs(int(pixels[110, 210, 0]) - 191) <= 2
                                 and abs(int(pixels[110, 270, 0]) - 64) <= 2,
        "fractional_fill_corner": abs(int(pixels[100, 210, 0]) - 143) <= 2,
        "subpixel_rect_area": abs(float(gray[98:103, 408:413].sum()) - .21) < .01,
        "aligned_frame_no_blur": bool(np.all((aligned_frame == 0) | (aligned_frame == 128))),
        "aligned_frame_square_corner": int(pixels[180, 110, 0]) == 128,
        "aligned_frame_area": abs(float(aligned_frame.sum()) / (200 * 128) - 1) < .01,
        "fractional_frame_single_alpha": int(fractional_frame.max()) <= 129,
        "fractional_frame_area": abs(float(fractional_frame.sum()) / (200 * 128) - 1) < .02,
        "collapsed_frame_area": abs(float(pixels[177:214, 307:315, 0].sum()) / (46.5 * 128) - 1) < .03,
        "zero_size_skipped": bool(np.all(pixels[177:224, 408:480] == 0)),
        "negative_fill_supported": bool(np.all(pixels[380:410, 110:170] == 255)),
        "negative_frame_supported": int(pixels[380, 210, 0]) == 128,
        "rect_clip_inside": bool(np.all(pixels[280:340, 385:415] == 255)),
        "rect_clip_outside": bool(np.all(pixels[280:340, 380:385] == 0)
                                  and np.all(pixels[280:340, 415:420] == 0)),
        "camera_pixel_alignment": abs(int(camera[100, 110, 0]) - 96) <= 2
                                  and abs(int(camera[110, 110, 0]) - 191) <= 2,
        "camera_serial_worker_parity": int(np.count_nonzero(np.max(np.abs(
            camera[80:570, 90:480] - camera[80:570, 610:1000]), axis=2) > 5)) <= 20,
        "rotated_frame_single_alpha": int(pixels[250:370, 100:220].max()) <= 129,
        "sheared_frame_single_alpha": int(pixels[250:370, 240:365].max()) <= 129,
    }
    full = np.asarray(Image.open(directory / "rectangles_fullscreen.png").convert("RGB"))
    height, width = full.shape[:2]
    scale = min(width / 1100, height / 600)
    ox, oy = (width - 1100 * scale) / 2, (height - 600 * scale) / 2
    crop = full[round(oy + 97 * scale):round(oy + 143 * scale),
                round(ox + 107 * scale):round(ox + 173 * scale), 0]
    checks["fullscreen_rect_area"] = abs(float(crop.sum()) / (2400 * scale**2 * 255) - 1) < .01
    if abs(scale - round(scale)) > .01:
        checks["fullscreen_fractional_edges"] = int(np.count_nonzero((crop > 0) & (crop < 255))) > 20
    return checks


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
    checks.update(rectangle_checks(directory))
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
