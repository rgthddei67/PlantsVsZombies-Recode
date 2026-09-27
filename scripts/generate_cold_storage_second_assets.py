"""Export the reviewed boiler accessory and pineapple head on classic animated rigs."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate():
    """Keep alpha and the sunflower stalk/leaves; lock source and output hashes."""
    inputs, outputs = [], []
    for source, name, size in [('boiler_pack', 'Boiler_pack', (72, 86)),
                               ('cold_pineapple', 'ColdPineapple_head', (64, 100))]:
        path = ROOT / f'scripts/assets/{source}_source.png'
        inputs.append(path)
        im = Image.open(path).convert('RGBA')
        assert im.getchannel('A').getextrema()[0] == 0
        im = im.crop(im.getchannel('A').point(lambda a: 255 if a >= 100 else 0).getbbox())
        im.thumbnail(size, Image.Resampling.LANCZOS)
        path = RES / f'image/reanim/{name}.png'
        im.save(path)
        outputs.append(path)
    source = RES / 'reanim/Sunflower.reanim'
    inputs += [source, RES / 'reanim/NormalZombie.reanim']
    rig = ET.fromstring('<root>' + source.read_text() + '</root>')
    for track in list(rig.findall('track')):
        name = track.findtext('name')
        if 'petal' in name.lower():
            rig.remove(track)
        elif name == 'anim_idle':
            # The fruit bottom overlaps the original stalk through its whole squash cycle.
            for frame in track.findall('t'):
                if frame.find('y') is not None:
                    frame.find('y').text = str(float(frame.findtext('y')) - 40)
                if frame.find('i') is not None:
                    frame.find('i').text = 'IMAGE_REANIM_COLDPINEAPPLE_HEAD'
    target = RES / 'reanim/ColdPineapple.reanim'
    target.write_text(''.join(ET.tostring(c, encoding='unicode') for c in rig), encoding='utf-8')
    outputs.append(target)
    # Card composition uses the same first-frame parts as the actual field character.
    canvas = Image.new('RGBA', (220, 220))
    images = {p.stem.upper(): p for p in (RES / 'image/reanim').iterdir() if p.suffix.lower() in ('.png', '.jpg')}
    for track in rig.findall('track'):
        f = track.find('t')
        key = f.findtext('i') if f is not None else None
        if not key:
            continue
        part_path = images[key.removeprefix('IMAGE_REANIM_')]
        inputs.append(part_path)
        part = Image.open(part_path).convert('RGBA')
        part = part.resize((round(part.width * float(f.findtext('sx', '1'))),
                            round(part.height * float(f.findtext('sy', '1')))), Image.Resampling.LANCZOS)
        canvas.alpha_composite(part, (round(float(f.findtext('x', '0'))) + 60,
                                     round(float(f.findtext('y', '0'))) + 60))
    card = canvas.crop(canvas.getchannel('A').getbbox())
    target = RES / 'image/PlantImage/ColdPineapple.png'
    card.save(target)
    outputs.append(target)
    hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Reviewed source/output hash drift; inspect before updating lock')
    lock.write_text(json.dumps(hashes, indent=2) + '\n')
    print('Exported', len(outputs), 'assets')


if __name__ == '__main__':
    generate()
