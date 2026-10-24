"""Deterministically fit ImageGen atlas parts to the existing Cabbage/normal rigs.

Retain the complete original articulated stems, limbs and event indices. The
card is composed from the same first pose; the water leaf and salvage equipment
are independent pieces, never flattened whole-character replacement sprites.
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
    inputs = [ROOT / 'scripts/assets/lotus_pult_source.png',
              ROOT / 'scripts/assets/salvage_zombie_source.png',
              RES / 'reanim/Cabbagepult.reanim', RES / 'reanim/NormalZombie.reanim']
    outputs = []

    def save(image, relative):
        path = RES / relative
        image.save(path)
        outputs.append(path)
        return image

    def piece(atlas, box, size):
        region = atlas.crop(tuple(round(v) for v in box))
        bounds = region.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        assert bounds, 'Empty atlas region'
        return region.crop(bounds).resize(size, Image.Resampling.LANCZOS)

    lotus = Image.open(inputs[0]).convert('RGBA')
    w, h = lotus.size
    for name, box, size in [
        ('head', (0, 0, w/2, h/2), (59, 47)),
        ('cup', (w/2, 0, w, h/2), (63, 40)),
        ('seed', (0, h/2, w/2, h), (30, 27)),
        ('floatleaf', (w/2, h/2, w, h), (108, 38))]:
        save(piece(lotus, box, size), f'image/reanim/LotusPult_{name}.png')
    seed = Image.open(RES / 'image/reanim/LotusPult_seed.png').convert('RGBA')
    cluster = Image.new('RGBA', (44, 44))
    small = seed.resize((12, 11), Image.Resampling.LANCZOS)
    for i in range(6):
        a = i * math.tau / 6
        cluster.alpha_composite(small, (round(16 + 15 * math.cos(a)), round(16 + 15 * math.sin(a))))
    cluster.alpha_composite(seed, (7, 8))
    save(cluster, 'image/reanim/LotusPult_cluster.png')

    salvage = Image.open(inputs[1]).convert('RGBA')
    sx, sy = salvage.width/1536, salvage.height/1024
    for name, box, size in [
        ('buoy', (0, 0, 760, 470), (75, 46)),
        ('reel', (780, 0, 1536, 480), (45, 48)),
        ('hook', (0, 470, 740, 1024), (36, 74)),
        ('hat', (740, 540, 1536, 1024), (74, 42))]:
        scaled = tuple(v * (sx if i % 2 == 0 else sy) for i, v in enumerate(box))
        save(piece(salvage, scaled, size), f'image/reanim/Salvage_{name}.png')

    rig = inputs[2].read_text(encoding='utf-8-sig')
    for old, new in [('HEAD', 'HEAD'), ('BASKET', 'CUP'), ('CABBAGE', 'SEED')]:
        # Exact image element prevents BASKET replacement from changing BASKET_OVERLAY.
        rig = rig.replace(f'<i>IMAGE_REANIM_CABBAGEPULT_{old}</i>',
                          f'<i>IMAGE_REANIM_LOTUSPULT_{new}</i>')
    for name, contents in [('LotusPult', rig), ('SalvageZombie', inputs[3].read_text(encoding='utf-8-sig'))]:
        path = RES / f'reanim/{name}.reanim'
        path.write_text(contents, encoding='utf-8', newline='')
        outputs.append(path)

    root = ET.fromstring('<root>' + rig + '</root>')
    canvas = Image.new('RGBA', (720, 560))
    hidden = {'Cabbagepult_blink', 'Cabbagepult_eyebrow1', 'Cabbagepult_eyebrow2', 'Cabbagepult_basket_overlay'}
    sprites = {p.stem.lower(): p for p in (RES / 'image/reanim').iterdir() if p.suffix.lower() in ('.png', '.jpg')}
    for track in root.findall('track'):
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
        a, b = sx*math.cos(math.radians(kx))*4, -sy*math.sin(math.radians(ky))*4
        c, d = sx*math.sin(math.radians(kx))*4, sy*math.cos(math.radians(ky))*4
        det, tx, ty = a*d-b*c, x*4+160, y*4+110
        inv = (d/det, -b/det, (b*ty-d*tx)/det, -c/det, a/det, (c*tx-a*ty)/det)
        canvas.alpha_composite(sprite.transform(canvas.size, Image.Transform.AFFINE, inv, Image.Resampling.BICUBIC))
    canvas = canvas.crop(canvas.getbbox())
    canvas.thumbnail((100, 100), Image.Resampling.LANCZOS)
    save(canvas, 'image/PlantImage/LotusPult.png')
    hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes and not update_lock:
        raise RuntimeError('Source/output hash drift; review changes before --update-lock')
    lock.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    print(f'Generated {len(outputs)} runtime assets with deterministic source/output hash lock.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--update-lock', action='store_true')
    generate(parser.parse_args().update_lock)
