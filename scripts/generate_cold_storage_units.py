"""Export reviewed ImageGen accessories and a matching card without replacing classic rigs."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate():
    """Preserve source alpha, resize attachments, and lock inputs and final exports."""
    inputs = []
    outputs = []
    for stem, filename, size in (
        ('ice_mint_crystal', 'Ice_mint_crystal.png', (44, 44)),
        ('ice_worker_machine', 'Ice_worker_machine.png', (72, 85)),
    ):
        source = ROOT / f'scripts/assets/{stem}_source.png'
        inputs.append(source)
        image = Image.open(source).convert('RGBA')
        assert image.getchannel('A').getextrema()[0] == 0, 'Expected real transparency'
        bounds = image.getchannel('A').point(lambda a: 255 if a >= 100 else 0).getbbox()
        image = image.crop(bounds)
        image.thumbnail(size, Image.Resampling.LANCZOS)
        target = RES / f'image/reanim/{filename}'
        image.save(target)
        outputs.append(target)
    source = RES / 'image/PlantImage/Umbrellaleaf.png'
    inputs.append(source)
    body = Image.open(source).convert('RGBA')
    body = body.crop(body.getchannel('A').getbbox())
    body.thumbnail((85, 65), Image.Resampling.LANCZOS)
    card = Image.new('RGBA', (100, 100))
    card.alpha_composite(body, ((100-body.width)//2, 95-body.height))
    crystal = Image.open(outputs[0]).convert('RGBA')
    card.alpha_composite(crystal, ((100-crystal.width)//2, 12))
    target = RES / 'image/PlantImage/IceMint.png'
    card.crop(card.getchannel('A').getbbox()).save(target)
    outputs.append(target)
    # The original umbrella expands across adjacent cells. Preserve its timing and
    # independent leaves, but retain only 8% of the expanded pose excursion.
    rig_source = RES / 'reanim/Umbrellaleaf.reanim'
    root = ET.fromstring('<root>' + rig_source.read_text(encoding='utf-8') + '</root>')
    for track in root.findall('track'):
        name = track.findtext('name')
        if name in ('anim_idle', 'anim_block'):
            continue
        state = {}
        baseline = {}
        for index, frame in enumerate(track.findall('t')):
            state.update({item.tag: item.text for item in frame})
            if index == 0:
                baseline = state.copy()
            if index < 15:
                continue
            for axis in ('x', 'y', 'kx', 'ky', 'sx', 'sy'):
                if axis not in baseline or axis not in state:
                    continue
                value = float(baseline[axis]) + (float(state[axis]) - float(baseline[axis])) * 0.08
                element = frame.find(axis)
                if element is None:
                    element = ET.SubElement(frame, axis)
                element.text = f'{value:.5f}'
    target = RES / 'reanim/IceMint.reanim'
    target.write_text(''.join(ET.tostring(child, encoding='unicode') for child in root), encoding='utf-8')
    outputs.append(target)
    inputs += [RES / 'reanim/Umbrellaleaf.reanim', RES / 'reanim/NormalZombie.reanim']
    hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Asset hash drift: review inputs and outputs before updating the lock')
    lock.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    print('Exported', len(outputs), 'assets with source/output hash checks')


if __name__ == '__main__':
    generate()
