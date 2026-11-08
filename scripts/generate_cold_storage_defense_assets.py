"""Export the storage walnut; shield art belongs to generate_zombie_identity_assets.ps1."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate():
    """Preserve classic transforms and alpha, then verify all source/output hashes."""
    inputs, outputs = [], []

    def source(path):
        inputs.append(path)
        return Image.open(path).convert('RGBA')

    def save(im, path):
        im.save(path)
        outputs.append(path)

    def trimmed(im):
        assert im.getchannel('A').getextrema()[0] == 0, 'Source must have genuine alpha'
        return im.crop(im.getchannel('A').point(lambda a: 255 if a >= 160 else 0).getbbox())

    compartment = trimmed(source(ROOT / 'scripts/assets/ice_storage_compartment_source.png'))
    compartment = compartment.resize((64, 26), Image.Resampling.LANCZOS)
    for suffix in ('body', 'cracked1', 'cracked2'):
        walnut = source(RES / f'image/reanim/Wallnut_{suffix}.png')
        # The compartment moves with the original body, below its face; its art does not replace the rig.
        walnut.alpha_composite(compartment, (18, 72))
        save(walnut, RES / f'image/reanim/IceStorageNut_{suffix}.png')

    rig_source = RES / 'reanim/Wallnut.reanim'
    inputs.append(rig_source)
    rig = rig_source.read_text().replace('IMAGE_REANIM_WALLNUT_BODY', 'IMAGE_REANIM_ICESTORAGENUT_BODY')
    target = RES / 'reanim/IceStorageNut.reanim'
    target.write_text(rig, encoding='utf-8')
    outputs.append(target)
    # Card and field share exactly the same decorated core part, with separate card framing.
    card = source(RES / 'image/reanim/IceStorageNut_body.png')
    card = card.crop(card.getchannel('A').getbbox())
    save(card, RES / 'image/PlantImage/IceStorageNut.png')

    hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs + outputs}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Reviewed source/output drift; inspect before updating lock')
    lock.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    print('Exported', len(outputs), 'reviewed assets')


if __name__ == '__main__':
    generate()
