"""Import ImageGen transparent art into deterministic, jointed rain-unit assets.

Built-in ImageGen prompts: jade bamboo cannon with separate head/stalk/root leaves
and spear; teal brass backpack water mortar; hand-painted water mist, droplet spray,
and radial explosive splash. Source PNGs retain the original alpha and artwork.
Only crop/resize/composition for the game import is performed here.
"""
from pathlib import Path
import hashlib
import json
import math
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'assets/source/area11_rain'
RES=ROOT/'build/clang-release/resources'
OUT=RES/'image/reanim'
outputs=[]

def trim(im):
    box=im.getchannel('A').point(lambda a:255 if a>24 else 0).getbbox()
    return im.crop(box) if box else im

def save(im,path):
    path.parent.mkdir(parents=True,exist_ok=True)
    im.save(path);outputs.append(path)

sheet=Image.open(SRC/'bamboo_parts.png').convert('RGBA')
# The generated stalk extends below the sheet midpoint; use visible empty gutters.
boxes=[(0,0,750,760),(750,0,1280,760),(0,760,670,1280),(670,760,1280,1280)]
pieces=[trim(sheet.crop(box)) for box in boxes]
sizes=[(72,51),(23,49),(86,37),(76,19)]
names=['RainBamboo_head','RainBamboo_stalk','RainBamboo_leaves','Rain_bamboo_dart']
for im,size,name in zip(pieces,sizes,names):
    save(im.resize(size,Image.Resampling.LANCZOS),OUT/(name+'.png'))
card=Image.new('RGBA',(100,108))
for i,xy in [(1,(35,36)),(0,(18,9)),(2,(6,68))]:
    card.alpha_composite(pieces[i].resize(sizes[i],Image.Resampling.LANCZOS),xy)
save(card,RES/'image/PlantImage/RainBamboo.png')
pack=trim(Image.open(SRC/'mortar_pack.png').convert('RGBA'));pack.thumbnail((133,105),Image.Resampling.LANCZOS)
save(pack,OUT/'Flood_mortar_pack.png')
for name,size in [('water_mist',160),('water_droplets',96),('water_blast',256)]:
    im=trim(Image.open(SRC/(name+'.png')).convert('RGBA'));im.thumbnail((size,size),Image.Resampling.LANCZOS)
    save(im,RES/'particles/textures'/(name+'.png'))

# Connected head and stalk share their joint translation. The leaf base hides the
# lower stalk throughout the small bend, including the repeat seam.
tracks=['<fps>12</fps>','<track><name>anim_idle</name>'+''.join('<t><f>0</f></t>' for _ in range(25))+'</track>']
for name,key,x,y in [('stalk','STALK',33,32),('head','HEAD',18,6),('leaves','LEAVES',0,69)]:
    frames=[]
    for frame in range(25):
        phase=2*math.pi*frame/24
        dx=0 if name=='leaves' else 1.4*math.sin(phase)
        dy=0 if name=='leaves' else .8*math.cos(phase)
        frames.append(f'<t><x>{x+dx:.3f}</x><y>{y+dy:.3f}</y><sx>1</sx><sy>1</sy><kx>0</kx><ky>0</ky><f>0</f><i>IMAGE_REANIM_RAINBAMBOO_{key}</i></t>')
    tracks.append('<track><name>'+name+'</name>'+''.join(frames)+'</track>')
anim=RES/'reanim/RainBamboo.reanim';anim.write_text('\n'.join(tracks),encoding='utf-8');outputs.append(anim)

def emitter(name,image,count,scale,duration,speed='0',extra=''):
    return f'<Emitter><Name>{name}</Name><Image>{image}</Image><SpawnMinActive>{count}</SpawnMinActive><ParticleDuration>{duration}</ParticleDuration><SystemDuration>{duration}</SystemDuration><ParticleScale>{scale}</ParticleScale><ParticleAlpha>1,25 0</ParticleAlpha><LaunchSpeed>{speed}</LaunchSpeed><RandomLaunchSpin>1</RandomLaunchSpin>{extra}</Emitter>'
effects={
    'RainBambooTrail':emitter('RainBambooTrail','PARTICLE_WATER_DROPLETS',2,'.13 .04','.3','[8 24]'),
    'RainBambooMuzzle':emitter('RainBambooMuzzle','PARTICLE_WATER_MIST',4,'.12 .28','.35','[20 80]')+emitter('drops','PARTICLE_WATER_DROPLETS',8,'.2 .05','.35','[50 130]'),
    'RainBambooHit':emitter('RainBambooHit','PARTICLE_WATER_BLAST',1,'.17 .32','.3')+emitter('drops','PARTICLE_WATER_DROPLETS',9,'.24 .08','.45','[80 160]','<ParticleGravity>100</ParticleGravity>'),
    'RainBambooFinish':emitter('RainBambooFinish','PARTICLE_WATER_BLAST',1,'.25 .5','.4')+emitter('mist','PARTICLE_WATER_MIST',6,'.25 .4','.5','[40 100]')+emitter('drops','PARTICLE_WATER_DROPLETS',18,'.3 .06','.65','[100 230]','<ParticleGravity>170</ParticleGravity>'),
    'FloodMortarExplosion':emitter('FloodMortarExplosion','PARTICLE_WATER_BLAST',1,'.3 1.12,30 1.35','.65')+emitter('mist','PARTICLE_WATER_MIST',14,'.32 .65','1.05','[35 125]','<EmitterType>Circle</EmitterType><EmitterRadius>16</EmitterRadius>')+emitter('drops','PARTICLE_WATER_DROPLETS',35,'.4 .12','.85','[120 300]','<ParticleGravity>230</ParticleGravity><EmitterType>Circle</EmitterType><EmitterRadius>20</EmitterRadius>'),
}
for name,xml in effects.items():
    path=RES/'particles/config'/(name+'.xml');path.write_text(xml,encoding='utf-8');outputs.append(path)
lock={'inputs':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(SRC.glob('*.png'))},
      'outputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}}
lockpath=SRC/'import.lock.json'
if lockpath.exists():
    assert json.loads(lockpath.read_text())==lock,'Generated input/output changed; review before updating lock'
else: lockpath.write_text(json.dumps(lock,indent=2),encoding='utf-8')
print('Imported',len(outputs),'rain assets')
