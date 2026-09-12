"""Compare queued HP glyphs with their complete-atlas reference on the same frame.

Run smoke_glyph_atlas_rebuild.json, then pass its output directory here.
Requires Pillow and numpy. The first capture must immediately follow the probe
command: later frames have already collected every digit and hide the regression.
"""

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def verify(directory: Path) -> bool:
    """Check both rebuild and steady frames, including visible green ink."""
    results = []
    for name in ("rebuild.png", "stable.png"):
        with Image.open(directory / name) as image:
            # The fixture explicitly uses windowed 1100 x 600 logical coordinates.
            assert image.size == (1100, 600), f"unexpected framebuffer: {image.size}"
            pixels = np.asarray(image.convert("RGB"), dtype=np.int16)
        for row in range(4):
            y = 170 + row * 60
            queued = pixels[y:y + 55, 140:400]
            reference = pixels[y:y + 55, 620:880]
            green = lambda p: (p[:, :, 1] > 120) & (p[:, :, 1] > p[:, :, 0] + 60)
            ink = int(np.count_nonzero(green(queued)))
            reference_ink = int(np.count_nonzero(green(reference)))
            different = int(np.count_nonzero(np.max(np.abs(queued - reference), axis=2) > 12))
            # Allow a few edge-rounding pixels, but reject missing/black/misplaced glyphs.
            passed = min(ink, reference_ink) > 300 and different <= 30
            results.append(dict(image=name, row=row, green=ink,
                                reference_green=reference_ink,
                                different_pixels=different, passed=passed))
    report = dict(passed=all(r["passed"] for r in results), rows=results)
    (directory / "glyph_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    raise SystemExit(0 if verify(Path(sys.argv[1])) else 1)
