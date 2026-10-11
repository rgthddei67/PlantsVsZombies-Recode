"""Bake complete detached parts from the existing rig; preserve classic stump pivots."""
from pathlib import Path
import colorsys
import hashlib
import json
import math
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
LOCK = ROOT / 'scripts/fog_ferry_limbs.sha256.json'
inputs, outputs = {}, {}


def source(path):
    inputs[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return path


def save(image, path):
    image.save(path)
    outputs[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()


if LOCK.exists():
    previous = json.loads(LOCK.read_text())
    for group in ('inputs', 'outputs'):
        for name, digest in previous[group].items():
            assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, name

# Match the existing Area 11 suit generator, retaining bone/skin pixels and the classic canvas.
stump = Image.open(source(RES / 'image/reanim/Zombie_outerarm_upper2.png')).convert('RGBA')
pixels = []
for r, g, b, a in stump.get_flattened_data():
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    if s > .16 and v > .15 and (h < .14 or h > .88):
        r, g, b = (round(c * 255) for c in colorsys.hsv_to_rgb(.59, max(.3, min(.65, s)), v))
    pixels.append((r, g, b, a))
stump.putdata(pixels)
save(stump, RES / 'image/reanim/Fog_ferry_outerarm_upper2.png')

rig = source(RES / 'reanim/DisasterEngineerZombie.reanim').read_text()
tracks = []
for block in re.findall(r'<track>(.*?)</track>', rig, re.S):
    name = re.search(r'<name>(.*?)</name>', block).group(1)
    state = dict(x='0', y='0', sx='1', sy='1', kx='0', ky='0', f='0')
    for frame, transform in enumerate(re.findall(r'<t>(.*?)</t>', block, re.S)):
        state.update(dict(re.findall(r'<(\w+)>(.*?)</\1>', transform)))
        if frame == 44:
            tracks.append((name, state.copy()))
            break


def bake(names, stem, anchor):
    """Use the rig's ordered affine transforms so jaws and hands form one rigid particle."""
    canvas = Image.new('RGBA', (512, 512))
    anchor_pos = None
    for name, t in tracks:
        if name not in names or float(t['f']) < 0:
            continue
        key = t['i'].removeprefix('IMAGE_REANIM_')
        path = next(p for p in (RES / 'image/reanim').glob('*.png') if p.stem.upper() == key)
        im = Image.open(source(path)).convert('RGBA')
        x, y, sx, sy, kx, ky = (float(t[k]) for k in ('x', 'y', 'sx', 'sy', 'kx', 'ky'))
        a, b = math.cos(math.radians(kx)) * sx, math.sin(math.radians(kx)) * sx
        c, d = -math.sin(math.radians(ky)) * sy, math.cos(math.radians(ky)) * sy
        determinant = a * d - b * c
        inverse = (d / determinant / 4, -c / determinant / 4, (c*y-d*x)/determinant,
                   -b / determinant / 4, a / determinant / 4, (b*x-a*y)/determinant)
        canvas.alpha_composite(im.transform(canvas.size, Image.Transform.AFFINE, inverse, Image.Resampling.BICUBIC))
        if name == anchor:
            anchor_pos = (x, y)
    canvas = canvas.resize((128, 128), Image.Resampling.LANCZOS)
    bounds = canvas.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()
    image = canvas.crop(bounds)
    save(image, RES / f'particles/textures/fog_ferry_{stem}.png')
    center = ((bounds[0]+bounds[2])/2-anchor_pos[0], (bounds[1]+bounds[3])/2-anchor_pos[1])
    return {'anchor': anchor, 'centerOffset': center, 'size': image.size}


geometry = {
    'head': bake({'anim_head1', 'anim_head2', 'anim_tongue', 'anim_hair'}, 'head', 'anim_head1'),
    'arm': bake({'Zombie_outerarm_lower', 'Zombie_outerarm_hand'}, 'arm', 'Zombie_outerarm_lower'),
}
LOCK.write_text(json.dumps({'inputs': inputs, 'outputs': outputs, 'geometry': geometry}, indent=2)+'\n')
print(json.dumps(geometry, indent=2))
