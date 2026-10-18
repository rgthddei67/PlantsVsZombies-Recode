"""Fit ImageGen equipment and anatomical lotus parts to existing classic rigs.

Only deterministic atlas extraction, resampling and rig calibration. Zombie
limbs and the original death/eat events retain their frame indices; the anchor
swing is drawn from the gameplay timer, without adding animation events.
"""
from pathlib import Path
from copy import deepcopy
import hashlib
import json
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate():
    paths = [ROOT / 'scripts/assets/heavy_diver_source.png',
             ROOT / 'scripts/assets/anchor_lotus_source.png',
             RES / 'reanim/NormalZombie.reanim', RES / 'reanim/Caltrop.reanim']
    outputs = []

    def save(im, relative):
        target = RES / relative
        im.save(target)
        outputs.append(target)
        return im

    def piece(atlas, box, size):
        # Atlas boundaries are normalized to the approved 1280px composition.
        box = tuple(round(v * atlas.width / 1280) for v in box)
        im = atlas.crop(box)
        bounds = im.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        assert bounds, 'Empty sprite component'
        return im.crop(bounds).resize(size, Image.Resampling.LANCZOS)

    diver = Image.open(paths[0]).convert('RGBA')
    lotus = Image.open(paths[1]).convert('RGBA')
    assert diver.getchannel('A').getextrema()[0] == lotus.getchannel('A').getextrema()[0] == 0
    save(piece(diver, (0, 0, 640, 620), (82, 86)), 'image/reanim/HeavyDiver_helmet.png')
    save(piece(diver, (710, 0, 1280, 650), (45, 70)), 'image/reanim/HeavyDiver_tanks.png')
    save(piece(diver, (0, 625, 710, 1280), (64, 64)), 'image/reanim/HeavyDiver_anchor.png')

    # Retain the original articulated suit and hands. Only brown cloth becomes
    # teal canvas; unchanged skin and bone remain valid detached limb artwork.
    rig = paths[2].read_text(encoding='utf-8-sig')
    for name in ('Zombie_body',):
        source = RES / f'image/reanim/{name}.png'
        if not source.exists(): continue
        paths.append(source)
        im = Image.open(source).convert('RGBA')
        data = []
        for r, g, b, a in im.getdata():
            if a and r > g * 1.06 and r > b * 1.14 and max(r, g, b) > 40:
                value = (r + g + b) / 3
                r, g, b = int(value * .52), min(255, int(value * 1.03)), min(255, int(value * .95))
            data.append((r, g, b, a))
        im.putdata(data)
        stem = name.replace('Zombie_', 'HeavyDiver_')
        save(im, f'image/reanim/{stem}.png')
        rig = rig.replace('IMAGE_REANIM_' + name.upper(), 'IMAGE_REANIM_' + stem.upper())
    target = RES / 'reanim/HeavyDiverZombie.reanim'
    target.write_text(rig, encoding='utf-8'); outputs.append(target)

    parts = [piece(lotus, (0, 0, 660, 600), (63, 46)),
             piece(lotus, (645, 0, 1280, 605), (47, 48)),
             piece(lotus, (0, 610, 650, 1280), (47, 48)),
             piece(lotus, (650, 640, 1280, 1280), (67, 42))]
    names = ['body', 'leftleaf', 'rightleaf', 'roots']
    positions = [(9, 23), (0, 0), (35, 0), (6, 47)]
    for name, im in zip(names, parts): save(im, f'image/reanim/AnchorLotus_{name}.png')
    # The leaf stems and root crown overlap beneath the body even at the idle
    # sway extrema; retain independent classic Caltrop leaf motion at 15%.
    original = ET.fromstring('<root>' + paths[3].read_text(encoding='utf-8-sig') + '</root>')
    result = ET.Element('root'); ET.SubElement(result, 'fps').text = original.findtext('fps')
    result.append(deepcopy(next(t for t in original.findall('track') if t.findtext('name') == 'anim_idle')))
    drawable = [t for t in original.findall('track') if t.findall('t/i')]
    for index in (3, 1, 2, 0):
        track = deepcopy(drawable[index]); track.find('name').text = 'anchor_' + names[index]
        state = {'x': 0., 'y': 0., 'sx': 1., 'sy': 1., 'kx': 0., 'ky': 0.}
        first = None
        for frame in track.findall('t'):
            for key in state:
                node = frame.find(key)
                if node is not None: state[key] = float(node.text)
            if first is None: first = state.copy()
            for key in state:
                node = frame.find(key)
                if node is None: node = ET.SubElement(frame, key)
                base = {'x': positions[index][0], 'y': positions[index][1], 'sx': 1., 'sy': 1., 'kx': 0., 'ky': 0.}[key]
                node.text = f'{base + (state[key] - first[key]) * .15:.4f}'
            for image in frame.findall('i'): image.text = 'IMAGE_REANIM_ANCHORLOTUS_' + names[index].upper()
        result.append(track)
    target = RES / 'reanim/AnchorLotus.reanim'
    target.write_text('\n'.join(ET.tostring(c, encoding='unicode') for c in result).rstrip() + '\n', encoding='utf-8')
    outputs.append(target)
    card = Image.new('RGBA', (84, 90))
    for i in (3, 1, 2, 0): card.alpha_composite(parts[i], positions[i])
    save(card, 'image/PlantImage/AnchorLotus.png')
    hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Art changed: review inputs and runtime outputs before updating the hash lock')
    lock.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    print('Exported waterside pair assets with source/output hashes.')


if __name__ == '__main__':
    generate()
