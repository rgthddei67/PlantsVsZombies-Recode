"""Extract reviewed crystal/tool art into classic rigs; lock source and output hashes."""
from pathlib import Path
from copy import deepcopy
import hashlib
import json
import math
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / 'build/clang-release/resources'
SOURCE = ROOT / 'scripts/assets/prism_theft_source.png'
EQUIPMENT = ROOT / 'scripts/assets/sun_thief_equipment_source.png'


def extract(image, box):
    """Trim very faint outer glow without discarding the original alpha inside the cutout."""
    part = image.crop(box).convert('RGBA')
    bounds = part.getchannel('A').point(lambda a: 255 if a >= 160 else 0).getbbox()
    if bounds is None:
        raise ValueError('Empty atlas part')
    return part.crop(bounds)


def generate():
    atlas = Image.open(SOURCE).convert('RGBA')
    if atlas.getchannel('A').getextrema()[0] != 0:
        raise ValueError('The reviewed atlas must have true alpha')
    outputs = []

    def save(image, relative):
        path = RES / relative
        image.save(path)
        outputs.append(path)

    blossom = extract(atlas, (0, 0, 780, 590)).resize((74, 74), Image.Resampling.LANCZOS)
    # Independent petals follow the original petal timeline, opening around a fixed core.
    for sector in range(7):
        part = blossom.copy()
        pixels = part.load()
        for y in range(74):
            for x in range(74):
                dx, dy = x-37, y-37
                angle = (math.atan2(dy, dx) + math.pi/2 + math.pi/6) % math.tau
                selected = 6 if dx*dx+dy*dy < 12*12 else int(angle/(math.pi/3))
                if selected != sector:
                    pixels[x, y] = (0, 0, 0, 0)
        save(part, f'image/reanim/PrismFlower_part{sector}.png')

    rig_path = RES / 'reanim/Marigold.reanim'
    rig = ET.fromstring('<root>'+rig_path.read_text(encoding='utf-8-sig')+'</root>')
    petal = next(t for t in rig.findall('track') if t.findtext('name') == 'Marigold_petals')
    insertion = list(rig).index(petal)
    rig.remove(petal)
    for sector in range(7):
        track = deepcopy(petal)
        track.find('name').text = f'prism_part{sector}'
        for image in track.findall('t/i'):
            image.text = f'IMAGE_REANIM_PRISMFLOWER_PART{sector}'
        rig.insert(insertion+sector, track)
    for track in rig.findall('track'):
        if track.findtext('name') in ('anim_face','Marigold_mouth','Marigold_eyebrow1','Marigold_eyebrow2','Marigold_blink'):
            for frame in track.findall('t'):
                flag = frame.find('f')
                if flag is None:
                    flag = ET.SubElement(frame, 'f')
                flag.text = '-1'
    target = RES/'reanim/PrismFlower.reanim'
    target.write_text('\n'.join(ET.tostring(child,encoding='unicode') for child in rig).rstrip()+'\n',encoding='utf-8')
    outputs.append(target)

    # Card uses the same blossom and the classic rig's green base, independent of world scale.
    card = Image.new('RGBA',(100,100))
    for filename, position, size in (
        ('Peashooter_stalk_bottom.png',(43,55),(13,31)),
        ('Peashooter_stalk_top.png',(39,38),(18,34)),
        ('Peashooter_backleaf.png',(18,70),(63,25)),
        ('Peashooter_frontleaf.png',(15,79),(60,19))):
        image=Image.open(RES/'image/reanim'/filename).convert('RGBA').resize(size,Image.Resampling.LANCZOS)
        card.alpha_composite(image,position)
    card.alpha_composite(blossom,(13,2))
    save(card,'image/PlantImage/PrismFlower.png')

    tank = extract(atlas, (790,0,1536,590))
    tank.thumbnail((54,78),Image.Resampling.LANCZOS)
    for stage in range(4):
        filled=tank.copy()
        pixels=filled.load()
        width,height=filled.size
        for y in range(height):
            for x in range(width):
                r,g,b,a=pixels[x,y]
                if stage and y >= height*(0.80-0.18*stage) and y < height*0.83 and x > width*0.47 and b > r*1.08 and g > r:
                    pixels[x,y]=(min(255,int(b*1.1)),min(255,int(g*0.91)),max(35,int(r*0.42)),a)
        save(filled,f'image/reanim/SunThief_tank{stage}.png')
    nozzle=extract(atlas,(0,590,820,1024))
    nozzle.thumbnail((58,38),Image.Resampling.LANCZOS)
    save(nozzle,'image/reanim/SunThief_nozzle.png')
    mark=extract(atlas,(900,610,1536,1024))
    mark.thumbnail((48,48),Image.Resampling.LANCZOS)
    save(mark,'image/reanim/Prism_mark.png')

    equipment = Image.open(EQUIPMENT).convert('RGBA')
    apron = extract(equipment, (0,0,equipment.width//2,equipment.height))
    apron.thumbnail((53,72),Image.Resampling.LANCZOS)
    save(apron,'image/reanim/SunThief_apron.png')
    hose = extract(equipment, (equipment.width//2,0,equipment.width,equipment.height))
    hose = hose.resize((62,15),Image.Resampling.LANCZOS)
    save(hose,'image/reanim/SunThief_hose.png')

    inputs=[SOURCE,EQUIPMENT,rig_path]+[RES/'image/reanim'/name for name in ('Peashooter_stalk_bottom.png','Peashooter_stalk_top.png','Peashooter_backleaf.png','Peashooter_frontleaf.png')]
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs+outputs}
    lock=Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text()) != hashes:
        raise RuntimeError('Asset hash drift: inspect before accepting revised assets')
    lock.write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')
    print(f'Generated {len(outputs)} resources; {len(rig.findall("track"))} classic rig tracks')


if __name__ == '__main__':
    generate()
