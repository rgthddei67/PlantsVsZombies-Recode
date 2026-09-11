"""Deterministically extract reviewed local parts, preserving classic rigs and attachment sizes."""
from pathlib import Path
from collections import deque
import hashlib
import json
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT/'build/clang-release/resources'
SOURCE = ROOT/'scripts/assets'

def extract_cap(image):
    """Remove edge-connected neutral checkerboard only; retain enclosed highlights and dark outline."""
    image = image.convert('RGBA')
    if image.getchannel('A').getextrema()[0] == 0:
        return image
    w,h=image.size
    pixels=image.load()
    queue=deque([(x,0) for x in range(w)]+[(x,h-1) for x in range(w)]+[(0,y) for y in range(h)]+[(w-1,y) for y in range(h)])
    seen=set()
    while queue:
        x,y=queue.popleft()
        if (x,y) in seen or not (0<=x<w and 0<=y<h): continue
        seen.add((x,y))
        r,g,b,a=pixels[x,y]
        if max(r,g,b)-min(r,g,b)>28 or min(r,g,b)<115: continue
        pixels[x,y]=(0,0,0,0)
        queue.extend(((x-1,y),(x+1,y),(x,y-1),(x,y+1)))
    return image

def generate():
    outputs=[]
    atlas=Image.open(SOURCE/'crystal_horn_source.png').convert('RGBA')
    w,h=atlas.size
    for i,name in enumerate(('intact','cracked','broken')):
        part=atlas.crop((i*w//3,0,(i+1)*w//3,h))
        part=part.crop(part.getchannel('A').point(lambda a:255 if a>=160 else 0).getbbox())
        part.thumbnail((86,70),Image.Resampling.LANCZOS)
        path=RES/f'image/reanim/CrystalHorn_{name}.png'; part.save(path); outputs.append(path)
    cap=extract_cap(Image.open(SOURCE/'echo_cap_source.png'))
    cap=cap.crop(cap.getchannel('A').getbbox())
    original=Image.open(RES/'image/reanim/FumeShroom_head.png')
    cap=cap.resize(original.size,Image.Resampling.LANCZOS)
    path=RES/'image/reanim/EchoShroom_head.png'; cap.save(path); outputs.append(path)
    reanim=RES/'reanim/FumeShroom.reanim'
    path=RES/'reanim/EchoShroom.reanim'
    path.write_text(reanim.read_text().replace('IMAGE_REANIM_FUMESHROOM_HEAD','IMAGE_REANIM_ECHOSHROOM_HEAD'),encoding='utf-8')
    outputs.append(path)
    # Independent card composition from the same cap and original body, without changing the battlefield rig.
    card=Image.new('RGBA',(100,100))
    body=Image.open(RES/'image/reanim/FumeShroom_body.png').convert('RGBA')
    body.thumbnail((65,55),Image.Resampling.LANCZOS)
    card.alpha_composite(body,(18,43))
    card.alpha_composite(cap,(3,0))
    path=RES/'image/PlantImage/EchoShroom.png';card.save(path);outputs.append(path)
    head=Image.open(RES/'particles/ZombieHead.png').convert('RGBA')
    helmet=Image.open(RES/'image/reanim/CrystalHorn_intact.png').convert('RGBA')
    composite=Image.new('RGBA',(105,115))
    composite.alpha_composite(head,(35,47));composite.alpha_composite(helmet,(3,2))
    path=RES/'particles/CrystalHornHead.png';composite.save(path);outputs.append(path)
    # Match the reduced battlefield helmet and classic head at the final render scale.
    xml=(RES/'particles/config/ExcavatorHeadOff.xml').read_text().replace('<ParticleScale>0.8</ParticleScale>','<ParticleScale>0.6</ParticleScale>')
    path=RES/'particles/config/CrystalHornHeadOff.xml'
    path.write_text(xml.replace('ExcavatorHeadOff','CrystalHornHeadOff').replace('PARTICLE_EXCAVATORHEAD','PARTICLE_CRYSTALHORNHEAD'),encoding='utf-8');outputs.append(path)
    path=RES/'particles/config/CrystalHornBreak.xml'
    path.write_text(xml.replace('ExcavatorHeadOff','CrystalHornBreak').replace('PARTICLE_EXCAVATORHEAD','IMAGE_CRYSTALHORN_BROKEN'),encoding='utf-8');outputs.append(path)
    inputs=[SOURCE/'crystal_horn_source.png',SOURCE/'echo_cap_source.png',reanim,RES/'particles/ZombieHead.png',RES/'particles/config/ExcavatorHeadOff.xml',RES/'image/reanim/FumeShroom_body.png',RES/'image/reanim/FumeShroom_head.png']
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs+outputs}
    lock=Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text())!=hashes:
        raise RuntimeError('Asset hash drift; review before updating lock')
    lock.write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')
    print('Generated',len(outputs),'resources; source and output hashes locked')

if __name__=='__main__': generate()
