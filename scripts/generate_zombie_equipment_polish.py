"""从四张透明母图派生五种僵尸的装备、离体头和粒子分件。"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
SOURCE = ROOT / 'assets/source'
LOCK = Path(__file__).with_suffix('.sha256.json')


def generate():
    inputs = []
    outputs = {}

    def read(path):
        inputs.append(path)
        return Image.open(path).convert('RGBA')

    def save(image, relative):
        png = BytesIO()
        image.save(png, format='PNG')
        outputs[RES / relative] = png.getvalue()

    def fit(sheet, crop, canvas, bounds=None, rotation=0, alpha=32):
        region = sheet.crop(crop)
        if rotation:
            region = region.rotate(rotation, Image.Resampling.BICUBIC, expand=True)
        box = region.getchannel('A').point(lambda value: 255 if value >= alpha else 0).getbbox()
        assert box, f'Empty sprite in {crop}'
        x, y, right, bottom = bounds or (0, 0, *canvas)
        sprite = Image.new('RGBA', canvas)
        sprite.alpha_composite(region.crop(box).resize((right-x, bottom-y), Image.Resampling.LANCZOS), (x, y))
        return sprite

    water = read(SOURCE / 'water_zombie_equipment.png')
    mechanical = read(SOURCE / 'mechanical_zombie_equipment.png')
    equipment = {}
    for sheet, pieces in [
        (water, [
            ('HeavyDiver_helmet', (0, 0, 425, 415), (82, 86), None),
            ('HeavyDiver_tanks', (425, 0, 840, 420), (45, 70), None),
            ('HeavyDiver_anchor', (840, 0, 1280, 415), (64, 64), None),
            ('Salvage_hat', (0, 430, 425, 825), (74, 42), None),
            ('Salvage_reel', (425, 430, 825, 825), (45, 48), None),
            ('Salvage_hook', (840, 430, 1280, 825), (36, 74), None),
            ('Salvage_buoy', (0, 855, 425, 1280), (75, 46), None),
        ]),
        (mechanical, [
            ('Pressure_barrel', (0, 0, 425, 415), (43, 27), None),
            ('Pressure_tank', (425, 0, 820, 415), (56, 78), None),
            ('Pressure_helmet', (850, 0, 1280, 415), (82, 87), None),
            ('Zombie_thermal_goggles', (0, 430, 425, 800), (74, 38), (2, 4, 73, 33)),
            ('Zombie_thermal_pack', (425, 430, 800, 800), (58, 76), (6, 3, 53, 74)),
            ('Zombie_thermal_rifle', (800, 430, 1280, 800), (120, 44), (0, 5, 119, 38)),
            ('Flood_mortar_pack', (0, 850, 425, 1280), (133, 103), None),
        ]),
    ]:
        for name, crop, canvas, bounds in pieces:
            equipment[name] = fit(sheet, crop, canvas, bounds)
            save(equipment[name], f'image/reanim/{name}.png')

    save(fit(water, (425, 855, 820, 1280), (16, 24), rotation=18), 'image/reanim/HeavyDiver_chain.png')
    fx = read(SOURCE / 'zombie_equipment_fx.png')
    for name, crop, size, alpha in [
        ('bubble', (55, 70, 365, 370), (28, 28), 64),
        ('vapor', (435, 52, 815, 383), (68, 59), 50),
        ('ember', (859, 73, 1216, 385), (44, 38), 55),
        ('flash', (0, 418, 416, 840), (80, 80), 28),
    ]:
        save(fit(fx, crop, size, alpha=alpha), f'particles/textures/equipment_{name}.png')
    save(fit(fx, (467, 472, 793, 767), (40, 33), alpha=72), 'image/reanim/Flood_mortar_shell.png')
    splash = read(SOURCE / 'zombie_splash_variants.png')
    for i, (suffix, height) in enumerate(zip('abc', (58, 50, 67))):
        x0, x1 = i * splash.width // 3, (i + 1) * splash.width // 3
        save(fit(splash, (x0, 0, x1, splash.height), (100, 67), (0, 67-height, 100, 67)),
             f'particles/textures/equipment_splash_{suffix}.png')

    head = read(RES / 'image/reanim/Zombie_head.png')
    jaw = read(RES / 'image/reanim/Zombie_jaw.png')
    hair = read(RES / 'image/reanim/Zombie_hair.png')
    for name, gear, offset, scale in [
        ('HeavyDiver', 'HeavyDiver_helmet', (-8, -12), 1),
        ('Salvage', 'Salvage_hat', (-3, -18), 1),
        ('ThermalSniper', 'Zombie_thermal_goggles', (-3, 7), .58),
    ]:
        # 同一装备贴图，使用在体 follower 的安装点和倍率合成离体正反面。
        canvas = Image.new('RGBA', (112, 112))
        canvas.alpha_composite(head, (20, 32))
        canvas.alpha_composite(jaw, (29, 72))
        canvas.alpha_composite(hair, (15, 29))
        part = equipment[gear]
        part = part.resize((round(part.width * scale), round(part.height * scale)), Image.Resampling.LANCZOS)
        canvas.alpha_composite(part, (20 + offset[0], 32 + offset[1]))
        for suffix, image in [('', canvas), ('_right', ImageOps.mirror(canvas))]:
            save(image, f'particles/{name}HeadOff{suffix}.png')
    hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    hashes.update({p.relative_to(ROOT).as_posix(): hashlib.sha256(data).hexdigest() for p, data in outputs.items()})
    return hashes, outputs


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--update-lock', action='store_true')
    args = parser.parse_args()
    hashes, outputs = generate()
    if LOCK.exists() and json.loads(LOCK.read_text()) != hashes and not args.update_lock:
        raise SystemExit('Equipment art drift; inspect inputs and outputs before using --update-lock')
    for path, data in outputs.items():
        path.write_bytes(data)
    LOCK.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
