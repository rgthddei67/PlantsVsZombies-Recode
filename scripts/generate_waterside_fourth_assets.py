"""Fit ImageGen equipment to the complete classic sunflower and zombie rigs.

Only deterministic atlas extraction, sizing and first-pose card composition are
performed here. The original articulated stem, petals, face and limbs survive.
Input/output hashes are locked; reruns reject unreviewed source drift.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate(update_lock=False):
    inputs = [ROOT / 'scripts/assets/lotus_parasol_source.png',
              ROOT / 'scripts/assets/float_sunflower_leaves_source.png',
              RES / 'reanim/Sunflower.reanim', RES / 'reanim/NormalZombie.reanim']
    outputs = []

    def save(image, relative):
        path = RES / relative
        image.save(path)
        outputs.append(path)
        return image

    def fit(image, size):
        box = image.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        assert box, 'Empty source silhouette'
        return image.crop(box).resize(size, Image.Resampling.LANCZOS)

    atlas = Image.open(inputs[0]).convert('RGBA')
    for i, name in enumerate(('intact', 'damaged', 'broken')):
        region = atlas.crop((i*atlas.width//3, 0, (i+1)*atlas.width//3, atlas.height))
        save(fit(region, (128, 154)), f'image/reanim/LotusParasol_{name}.png')
    leaf = save(fit(Image.open(inputs[1]).convert('RGBA'), (160, 60)),
                'image/reanim/FloatSunflower_leaves.png')
    for source, name in [(inputs[2], 'FloatSunflower'), (inputs[3], 'LotusParasolZombie')]:
        destination = RES / f'reanim/{name}.reanim'
        destination.write_text(source.read_text(encoding='utf-8-sig'), encoding='utf-8', newline='')
        outputs.append(destination)

    rig = ET.fromstring('<root>' + inputs[2].read_text(encoding='utf-8-sig') + '</root>')
    hidden = {'backleaf', 'backleaf_left_tip', 'backleaf_right_tip',
              'frontleaf', 'frontleaf_left_tip', 'frontleaf_right_tip'}
    canvas = Image.new('RGBA', (720, 560))
    sprites = {p.stem.lower(): p for p in (RES / 'image/reanim').iterdir()
               if p.suffix.lower() in ('.png', '.jpg')}

    def composite(sprite, x, y, sx, sy, kx, ky):
        a, b = sx*math.cos(math.radians(kx))*4, -sy*math.sin(math.radians(ky))*4
        c, d = sx*math.sin(math.radians(kx))*4, sy*math.cos(math.radians(ky))*4
        det, tx, ty = a*d-b*c, x*4+160, y*4+110
        inv = (d/det, -b/det, (b*ty-d*tx)/det, -c/det, a/det, (c*tx-a*ty)/det)
        canvas.alpha_composite(sprite.transform(canvas.size, Image.Transform.AFFINE, inv, Image.Resampling.BICUBIC))

    for track in rig.findall('track'):
        frame = track.find('t')
        key = frame.findtext('i') if frame is not None else None
        if not key or track.findtext('name') in hidden or frame.findtext('f') == '-1':
            continue
        path = sprites[key.removeprefix('IMAGE_REANIM_').lower()]
        inputs.append(path)
        sprite = Image.open(path).convert('RGBA')
        if path.suffix.lower() == '.jpg':
            mask = path.with_name(path.stem + '_.png')
            if mask.exists():
                inputs.append(mask)
                sprite.putalpha(Image.open(mask).convert('L'))
        x, y, sx, sy, kx, ky = [float(frame.findtext(k, str(v))) for k, v in
                               [('x', 0), ('y', 0), ('sx', 1), ('sy', 1), ('kx', 0), ('ky', 0)]]
        composite(sprite, x, y, sx, sy, kx, ky)
        if track.findtext('name') == 'stalk_bottom':
            composite(leaf, x-80*sx, y+14*sy, sx, sy, kx, ky)
    canvas = canvas.crop(canvas.getbbox())
    canvas.thumbnail((100, 100), Image.Resampling.LANCZOS)
    save(canvas, 'image/PlantImage/FloatSunflower.png')
    # Git may materialize reanim with CRLF; lock its UTF-8/LF content, not checkout line endings.
    hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(
                  p.read_text(encoding='utf-8-sig').encode('utf-8') if p.suffix == '.reanim' else p.read_bytes()).hexdigest()
              for p in inputs + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes and not update_lock:
        raise RuntimeError('Source/output hash drift; review before --update-lock')
    lock.write_text(json.dumps(hashes, indent=2)+'\n', encoding='utf-8')
    print(f'Generated {len(outputs)} runtime assets; source/output hashes locked.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--update-lock', action='store_true')
    generate(parser.parse_args().update_lock)
