"""Derive the Area 11 rigs from classic parts without changing their pivots."""
from pathlib import Path
import colorsys
import hashlib
import json
import re
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
ART = RES / 'image/reanim'
inputs = {}
outputs = []

# 在写入前核对来源和现有派生图，避免重跑脚本覆盖后期手绘修订。
record_path=ROOT/'scripts/area11_unit_assets.sha256.json'
if record_path.exists() and '--accept-hashes' not in sys.argv:
    previous=json.loads(record_path.read_text())
    for group in ('inputs','outputs'):
        for relative, expected in previous[group].items():
            path=ROOT/relative
            assert path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()==expected, f'Asset changed: {relative}; inspect before accepting hashes.'

def load(path):
    inputs[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    im = Image.open(path).convert('RGBA')
    if path.suffix.lower() == '.jpg':
        mask = path.with_name(path.stem+'_.png')
        if mask.exists():
            inputs[str(mask.relative_to(ROOT))] = hashlib.sha256(mask.read_bytes()).hexdigest()
            im.putalpha(Image.open(mask).convert('L'))
    return im

def save(im, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)
    outputs.append(path)

def tint(im, hue, clothes=False):
    result=im.copy(); pixels=[]
    for r,g,b,a in im.get_flattened_data():
        h,s,v=colorsys.rgb_to_hsv(r/255,g/255,b/255)
        if s>.16 and v>.15 and (not clothes or h<.14 or h>.88):
            r,g,b=(round(c*255) for c in colorsys.hsv_to_rgb(hue,max(.3,min(.65,s)),v))
        pixels.append((r,g,b,a))
    result.putdata(pixels)
    return result

def derive_rig(source, target, plant=False):
    src=RES/'reanim'/source
    inputs[str(src.relative_to(ROOT))]=hashlib.sha256(src.read_bytes()).hexdigest()
    text=src.read_text()
    keys=set(re.findall(r'<i>([^<]+)</i>',text))
    for key in sorted(keys):
        stem=key.removeprefix('IMAGE_REANIM_')
        if plant and not (stem.startswith('PEASHOOTER_') or stem=='ANIM_SPROUT'): continue
        if not plant and not stem.startswith('ZOMBIE_'): continue
        candidates=[p for p in ART.iterdir() if p.stem.upper()==stem and p.suffix.lower() in ('.png','.jpg')]
        if not candidates: continue
        candidates.sort(key=lambda p:p.suffix!='.png')
        im=load(candidates[0]); im=tint(im,.64 if plant else .59, not plant)
        newstem=('ThunderFlower_' if plant else 'DisasterEngineer_')+stem.split('_',1)[1].lower()
        # Keep the original head canvas/pivot; petals fit inside its silhouette and move with it.
        if plant and stem.endswith('_HEAD'):
            d=ImageDraw.Draw(im); w,h=im.size
            for y in [h*.25,h*.43,h*.61]:
                d.ellipse((w*.03,y-h*.10,w*.23,y+h*.10),fill=(133,85,196,255),outline=(46,32,76,255),width=2)
            d.line([(w*.15,h*.3),(w*.23,h*.4),(w*.16,h*.49),(w*.25,h*.59)],fill=(248,218,119,255),width=3)
        if not plant and stem in ('ZOMBIE_HEAD','ZOMBIE_HEAD2'):
            d=ImageDraw.Draw(im); w,h=im.size
            d.pieslice((w*.04,0,w*.83,h*.46),180,360,fill=(239,187,62,255),outline=(68,60,39,255),width=2)
            d.rounded_rectangle((0,h*.16,w*.86,h*.25),radius=2,fill=(252,213,86,255),outline=(68,60,39,255),width=2)
        save(im,ART/(newstem+'.png'))
        text=text.replace(key,'IMAGE_REANIM_'+newstem.upper())
    dst=RES/'reanim'/target; dst.write_text(text,encoding='utf-8');outputs.append(dst)

derive_rig('PeaShooter.reanim','ThunderFlower.reanim',True)
derive_rig('NormalZombie.reanim','DisasterEngineerZombie.reanim')

for stage in range(5):
    im=Image.new('RGBA',(88,116));d=ImageDraw.Draw(im)
    d.rounded_rectangle((12,13,73,104),radius=17,fill=(65,81,100),outline=(30,37,51),width=4)
    d.rounded_rectangle((20,19,65,98),radius=11,fill=(118,143,159),outline=(39,59,74),width=3)
    d.rounded_rectangle((29,29,57,86),radius=7,fill=(30,51,70),outline=(214,224,225),width=3)
    if stage:
        top=82-stage*12
        d.rounded_rectangle((33,top,53,82),radius=3,fill=(61,218,231),outline=(121,252,253),width=2)
        d.line((36,top+5,36,78),fill=(223,255,255),width=2)
    d.rectangle((28,7,56,16),fill=(45,61,79),outline=(20,29,43),width=3)
    d.ellipse((62,41,80,59),fill=(232,190,75),outline=(56,56,50),width=3)
    d.line((70,49,75,45),fill=(47,54,61),width=2)
    d.line([(14,83),(3,80),(4,35),(13,29)],fill=(27,37,47),width=7)
    d.line([(14,83),(3,80),(4,35),(13,29)],fill=(104,128,147),width=3)
    save(im,ART/f'Disaster_canister_{stage}.png')

im=Image.new('RGBA',(32,36));d=ImageDraw.Draw(im)
d.ellipse((6,7,27,30),fill=(99,140,237),outline=(33,44,99),width=2)
d.polygon([(18,0),(8,17),(17,16),(11,36),(28,13),(19,14)],fill=(221,252,255),outline=(104,175,241))
save(im,RES/'image/Thunder_seed.png')
im=Image.new('RGBA',(32,44));d=ImageDraw.Draw(im)
d.rounded_rectangle((5,8,26,41),radius=7,fill=(45,70,86),outline=(211,239,244),width=2)
d.rounded_rectangle((10,14,21,35),radius=3,fill=(66,217,232),outline=(173,252,255),width=2)
d.rectangle((11,2,21,8),fill=(211,220,192),outline=(39,59,73),width=2)
save(im,ART/'Disaster_worker_badge.png')
# The card keeps the same framing as the classic pea, with the identical palette and ornament.
pea_card=next(p for p in (RES/'image/PlantImage').iterdir() if p.stem.lower()=='peashooter')
im=tint(load(pea_card),.64);d=ImageDraw.Draw(im);w,h=im.size
d.line([(w*.35,h*.18),(w*.43,h*.32),(w*.33,h*.42),(w*.45,h*.53)],fill=(248,218,119,255),width=3)
save(im,RES/'image/PlantImage/ThunderFlower.png')

xml=RES/'resources.xml';s=xml.read_text()
if 'name="ThunderFlower"' not in s:
    s=s.replace('<GameImages>','<GameImages>\n    <Image>./resources/image/PlantImage/ThunderFlower.png</Image>\n    <Image>./resources/image/Thunder_seed.png</Image>')
    s=s.replace('<Reanimations>','<Reanimations>\n    <Reanimation name="ThunderFlower">./resources/reanim/ThunderFlower.reanim</Reanimation>\n    <Reanimation name="DisasterEngineerZombie">./resources/reanim/DisasterEngineerZombie.reanim</Reanimation>')
    xml.write_text(s,encoding='utf-8')
manifest=RES/'manifest.txt'; lines=set(manifest.read_text().splitlines())
lines.update('./resources/'+str(p.relative_to(RES)).replace('\\','/') for p in outputs)
manifest.write_text('\n'.join(sorted(lines))+'\n',encoding='utf-8')
record={'inputs':inputs,'outputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}}
if record_path.exists() and '--accept-hashes' not in sys.argv:
    assert json.loads(record_path.read_text())==record,'Source or deterministic output changed; inspect before accepting hashes.'
else: record_path.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(f'Generated {len(outputs)} rig assets with preserved canvas sizes.')
