"""Export the pressure apparatus atlas onto the untouched classic zombie timeline.

The torso becomes a teal protective vest; original limbs/head intentionally remain
classic, including their detached particles. No animation event or pivot changes.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'


def generate():
    """Preserve source alpha and rig transforms, and lock reproducible inputs/outputs."""
    atlas_path = ROOT / 'scripts/assets/pressure_guard_equipment_source.png'
    torso_path = RES / 'image/reanim/Zombie_body.png'
    rig_path = RES / 'reanim/NormalZombie.reanim'
    paths = [atlas_path, torso_path, rig_path]
    atlas = Image.open(atlas_path).convert('RGBA')
    assert atlas.getchannel('A').getextrema()[0] == 0
    for i, suffix in enumerate(('', '_damaged1', '_damaged2', '_spent')):
        x, y = i % 2 * atlas.width // 2, i // 2 * atlas.height // 2
        piece = atlas.crop((x, y, x+atlas.width//2, y+atlas.height//2))
        box = piece.getchannel('A').point(lambda a: 255 if a >= 160 else 0).getbbox()
        piece = piece.crop(box).resize((65, 68), Image.Resampling.LANCZOS)
        target = RES / f'image/reanim/Pressure_guard_equipment{suffix}.png'
        piece.save(target)
        paths.append(target)
    torso = Image.open(torso_path).convert('RGBA')
    pixels = []
    for r, g, b, a in torso.getdata():
        # Keep the classic dark outlines and light shirt while changing only brown jacket fabric.
        if a and r > g * 1.05 and r > b * 1.15 and max(r, g, b) > 45:
            value = (r+g+b)/3
            r, g, b = int(value*.55), min(255, int(value*1.1)), min(255, int(value*1.06))
        pixels.append((r, g, b, a))
    torso.putdata(pixels)
    target = RES / 'image/reanim/Pressure_guard_body.png'
    torso.save(target)
    paths.append(target)
    target = RES / 'reanim/PressureGuardZombie.reanim'
    target.write_text(rig_path.read_text().replace('IMAGE_REANIM_ZOMBIE_BODY', 'IMAGE_REANIM_PRESSURE_GUARD_BODY'), encoding='utf-8')
    paths.append(target)
    hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    lock = Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Pressure guard art changed: review source/output before updating hash lock')
    lock.write_text(json.dumps(hashes, indent=2)+'\n', encoding='utf-8')
    print('Exported pressure guard equipment and unchanged classic timeline')


if __name__ == '__main__':
    generate()
