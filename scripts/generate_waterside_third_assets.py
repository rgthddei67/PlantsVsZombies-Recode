"""Fit ImageGen leaf/equipment parts to the original lily and zombie animation rigs."""
from pathlib import Path
import hashlib
import json
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
inputs = [ROOT / f'scripts/assets/{name}_source.png' for name in
          ('thorn_lily', 'sludge_gear', 'sludge_splat')]
outputs = []


def fit(source, size):
    bounds = source.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
    return source.crop(bounds).resize(size, Image.Resampling.LANCZOS)


def save(image, relative):
    path = RES / relative
    image.save(path)
    outputs.append(path)


leaf, gear, mud = [Image.open(p).convert('RGBA') for p in inputs]
# The original lily is one flat animated leaf; the upgraded leaf keeps its 100px canvas/pivot.
body = Image.new('RGBA', (100, 100))
body.alpha_composite(fit(leaf, (100, 70)), (0, 12))
save(body, 'image/reanim/ThornLily_body.png')
save(fit(leaf, (100, 70)), 'image/PlantImage/ThornLily.png')
save(fit(gear, (100, 90)), 'image/reanim/Sludge_gear.png')
save(fit(mud, (70, 45)), 'image/reanim/Sludge_splat.png')
save(fit(mud, (26, 20)), 'image/reanim/Sludge_projectile.png')
# Extract one complete perimeter thorn, including the fleshy root, for the emerging-root effect.
w, h = leaf.size
root = leaf.crop((round(w*.55), 0, round(w*.64), round(h*.23)))
save(fit(root, (20, 36)), 'image/reanim/ThornLily_root.png')
for source, destination in [('Lilypad', 'ThornLily'), ('ConeZombie', 'SludgeZombie')]:
    original = RES / f'reanim/{source}.reanim'
    inputs.append(original)
    text = original.read_text(encoding='utf-8-sig')
    if source == 'Lilypad':
        text = text.replace('IMAGE_REANIM_LILYPAD_BODY', 'IMAGE_REANIM_THORNLILY_BODY')
    target = RES / f'reanim/{destination}.reanim'
    target.write_text(text, encoding='utf-8', newline='')
    outputs.append(target)
hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in inputs + outputs}
Path(__file__).with_suffix('.sha256.json').write_text(json.dumps(hashes, indent=2)+'\n', encoding='utf-8')
print(f'Generated {len(outputs)} runtime assets.')
