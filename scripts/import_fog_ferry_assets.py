"""Import the ImageGen backpack and generate simple square/soft-dot particle primitives.

The weather-station protective suit deliberately shares the engineer's classic rig;
the purple crystal pack gives the transporter its own silhouette. No timeline or
frame event is changed. Input and output hashes guard reproducible re-imports.
"""
from pathlib import Path
import hashlib
import json
import math
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
SRC = ROOT / 'assets/source/fog_ferry/pack.png'
LOCK = SRC.parent / 'import.lock.json'
outputs = []

def save_image(image, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    outputs.append(path)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

inputs = {str(p.relative_to(ROOT)): digest(p) for p in
          [SRC, RES / 'reanim/DisasterEngineerZombie.reanim']}
accept = '--accept-hashes' in sys.argv
if LOCK.exists() and not accept:
    previous = json.loads(LOCK.read_text())
    assert previous['inputs'] == inputs, 'Source changed; review before regenerating'
    for path, expected in previous['outputs'].items():
        assert digest(ROOT / path) == expected, 'Runtime asset changed: ' + path

pack = Image.open(SRC).convert('RGBA')
pack = pack.crop(pack.getchannel('A').point(lambda a: 255 if a > 24 else 0).getbbox())
pack.thumbnail((95, 122), Image.Resampling.LANCZOS)
save_image(pack, RES / 'image/reanim/Fog_ferry_pack.png')
save_image(Image.new('RGBA', (8, 8), (255, 255, 255, 255)),
           RES / 'particles/textures/fog_ferry_square.png')
mist = Image.new('RGBA', (64, 64))
mist.putdata([(255, 255, 255, round(180 * max(0, 1 - math.hypot(x-31.5, y-31.5)/32)**2))
              for y in range(64) for x in range(64)])
save_image(mist, RES / 'particles/textures/fog_ferry_mist.png')

def emitter(name, count, radius, speed, duration, scale, image='SQUARE', extra=''):
    alpha = '.25,40 .18,75 0' if image == 'MIST' else '.9,45 .65,75 0'
    return (f'<Emitter><Name>{name}</Name><Image>PARTICLE_FOG_FERRY_{image}</Image>'
            f'<SpawnMinActive>{count}</SpawnMinActive><SystemDuration>{duration}</SystemDuration>'
            f'<ParticleDuration>{duration}</ParticleDuration><EmitterType>Circle</EmitterType>'
            f'<EmitterRadius>{radius}</EmitterRadius><LaunchSpeed>{speed}</LaunchSpeed>'
            f'<RandomLaunchSpin>1</RandomLaunchSpin><ParticleScale>{scale}</ParticleScale>'
            '<ParticleRed>[.55 .95]</ParticleRed><ParticleGreen>[.12 .35]</ParticleGreen>'
            f'<ParticleBlue>1</ParticleBlue><ParticleAlpha>{alpha}</ParticleAlpha>'
            f'{extra}</Emitter>')

effects = {
    'FogFerryCharge': emitter('FogFerryCharge', 8, '[15 30]', '[-28 -12]', '.42', '[.35 .65] .1'),
    'FogFerryMarker': emitter('FogFerryMarker', 10, '[18 25]', '[1 5]', '.45', '[.4 .65] .1'),
    'FogFerryCancel': emitter('FogFerryCancel', 16, '[3 15]', '[20 50]', '.55', '[.35 .7] 0'),
}
for name, count, radius, speed in [('FogFerryDepart', 24, '[18 42]', '[-45 -20]'),
                                  ('FogFerryDepartWide', 32, '[30 65]', '[-60 -25]'),
                                  ('FogFerryArrive', 26, '[2 12]', '[35 90]')]:
    effects[name] = emitter(name, count, radius, speed, '.75', '[.35 .8] .1', extra='<ParticleGravity>-24</ParticleGravity>')
    effects[name] += emitter('mist', 1, '[5 20]', '[2 12]', '.65', '.6 1.1', 'MIST')
for name, xml in effects.items():
    path = RES / 'particles/config' / (name + '.xml')
    path.write_text(xml + '\n', encoding='utf-8')
    outputs.append(path)
record = {'inputs': inputs, 'outputs': {str(p.relative_to(ROOT)): digest(p) for p in outputs}}
if LOCK.exists() and not accept:
    assert json.loads(LOCK.read_text()) == record, 'Generated output drifted'
else:
    LOCK.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Imported', len(outputs), 'fog ferry assets')
