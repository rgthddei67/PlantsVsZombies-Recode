"""Compare serial and worker replay occlusion after smoke_mixed_geometry_order."""

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def verify(directory: Path) -> bool:
    """Require the same visible layers with no glow, during glow, and after glow."""
    with Image.open(directory / "mixed_geometry.png") as image:
        assert image.size == (1100, 600), f"unexpected framebuffer: {image.size}"
        pixels = np.asarray(image.convert("RGB"), dtype=np.int16)
    results = []
    for row in range(3):
        y = 140 + row * 120
        serial = pixels[y:y + 100, 160:400]
        replay = pixels[y:y + 100, 640:880]
        different = int(np.count_nonzero(np.max(np.abs(serial - replay), axis=2) > 12))
        # Check actual foreground colors too: two equally blank columns must fail.
        red = np.array([230, 30, 80])
        blue = np.array([30, 100, 240])
        foreground = all(
            np.max(np.abs(column[45:55, 35:45] - red)) <= 3
            and np.max(np.abs(column[45:55, 125:135] - blue)) <= 3
            for column in (serial, replay)
        )
        results.append(dict(row=row, different_pixels=different,
                            foreground=bool(foreground), passed=bool(foreground and different <= 30)))
    report = dict(passed=all(row["passed"] for row in results), rows=results)
    (directory / "order_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return report["passed"]


if __name__ == "__main__":
    raise SystemExit(0 if verify(Path(sys.argv[1])) else 1)
