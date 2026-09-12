"""Verify colored mine fog reaches both scene edges while menu buttons stay clear."""
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

OUT = Path(__file__).resolve().parents[1] / 'build/clang-release/autotest/out/smoke_mine_fog_edges'


def main():
    """Compare actual clear/fog captures in the former gaps and an opaque UI region."""
    assert json.loads((OUT / 'status.json').read_text())['status'] == 'passed'
    for color in ('blue', 'purple', 'gold'):
        clear = Image.open(OUT / f'{color}_clear.png').convert('RGB')
        fog = Image.open(OUT / f'{color}_fog.png').convert('RGB')
        assert clear.size == fog.size == (1100, 600)
        difference = ImageChops.difference(clear, fog)
        for edge, box in (('top', (982, 0, 995, 45)), ('bottom', (820, 590, 930, 600))):
            mean = sum(ImageStat.Stat(difference.crop(box)).mean) / 3
            assert mean > 5, (color, edge, mean)
        ui_mean = sum(ImageStat.Stat(difference.crop((1010, 10, 1080, 30))).mean) / 3
        assert ui_mean < 1, (color, ui_mean)
    print('Blue, purple and golden mine fog cover both scene edges; UI remains clear.')


if __name__ == '__main__':
    main()
