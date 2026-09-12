"""Extract approved amber atlas parts and fit them to the classic Caltrop leaf rig.

Source: built-in ImageGen, transparent 2x2 atlas: amber lichen rosette, crystal
chest drum, horizontal mallet, amber resin puddle. Classic PvZ painted outlines.
Only deterministic extraction, resampling and classic-track pivot calibration.
"""
from pathlib import Path
from copy import deepcopy
import hashlib
import json
import xml.etree.ElementTree as ET
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
RES=ROOT/'build/clang-release/resources'
SOURCE=ROOT/'scripts/assets/mine_fourth_source.png'
LEAVES=ROOT/'scripts/assets/amber_lichen_parts_source.png'


def generate():
    atlas=Image.open(SOURCE).convert('RGBA')
    assert atlas.getchannel('A').getextrema()[0]==0, 'Transparent atlas required'
    outputs=[]
    def extract(box):
        part=atlas.crop(box)
        bounds=part.getchannel('A').point(lambda a:255 if a>=128 else 0).getbbox()
        return part.crop(bounds)
    def save(im,path):
        target=RES/path
        im.save(target)
        outputs.append(target)
    w,h=atlas.size
    drum=extract((w//2,0,w,h//2)); drum.thumbnail((51,53),Image.Resampling.LANCZOS)
    save(drum,'image/reanim/CrystalDrummer_drum.png')
    mallet=extract((0,h//2,w//2,h)); mallet.thumbnail((39,19),Image.Resampling.LANCZOS)
    save(mallet,'image/reanim/CrystalDrummer_mallet.png')
    resin=extract((w//2,h//2,w,h)).resize((140,44),Image.Resampling.LANCZOS)
    save(resin,'image/reanim/AmberLichen_resin.png')
    # Anatomical leaf fans have generous root tabs hidden behind the central
    # bud. Cutting a composed plant into abutting strips caused visible cracks.
    atlas=Image.open(LEAVES).convert('RGBA'); w,h=atlas.size
    parts=[extract((w//2,h//2,w,h)).resize((40,52),Image.Resampling.LANCZOS),
           extract((w//2,0,w,h//2)).resize((52,57),Image.Resampling.LANCZOS),
           extract((0,h//2,w//2,h)).resize((52,57),Image.Resampling.LANCZOS),
           extract((0,0,w//2,h//2)).resize((68,51),Image.Resampling.LANCZOS),
           resin.resize((90,16),Image.Resampling.LANCZOS)]
    positions=[(31,0),(0,10),(48,10),(16,25),(5,64)]
    for index,part in enumerate(parts): save(part,f'image/reanim/AmberLichen_part{index}.png')
    card=Image.new('RGBA',(100,82))
    for index in (4,0,1,2,3): card.alpha_composite(parts[index],positions[index])
    save(card,'image/PlantImage/AmberLichen.png')
    rigpath=RES/'reanim/Caltrop.reanim'
    rig=ET.fromstring('<root>'+rigpath.read_text(encoding='utf-8-sig')+'</root>')
    drawable=[t for t in rig.findall('track') if t.findall('t/i')]
    for sector,track in enumerate(drawable):
        base=track.find('t')
        initial={key:float(base.findtext(key)) for key in ('x','y','sx','sy','kx','ky')}
        state=initial.copy()
        track.find('name').text=f'amber_part{sector}'
        for frame in track.findall('t'):
            for key in initial:
                node=frame.find(key)
                if node is not None: state[key]=float(node.text)
                else: node=ET.SubElement(frame,key)
                target={'x':positions[sector][0]*.8,'y':8+positions[sector][1]*.8,
                        'sx':.8,'sy':.8,'kx':0,'ky':0}[key]
                # Retain the original independent idle motion, but narrow each
                # local deformation so every concealed root stays under the bud.
                node.text=f'{target+(state[key]-initial[key])*.20:.4f}'
            for image in frame.findall('i'): image.text=f'IMAGE_REANIM_AMBERLICHEN_PART{sector}'
    for track in drawable: rig.remove(track)
    for index in (4,0,1,2,3): rig.append(drawable[index])
    target=RES/'reanim/AmberLichen.reanim'
    target.write_text('\n'.join(ET.tostring(c,encoding='unicode') for c in rig).rstrip()+'\n',encoding='utf-8')
    outputs.append(target)
    # A textureless copy of the body is an exact motion anchor after the tie.
    # Put the drumming forearm and hand in front of the drum, retaining every
    # original keyframe/event index and the shoulder's original rear layer.
    zombiepath=RES/'reanim/NormalZombie.reanim'
    zombie=ET.fromstring('<root>'+zombiepath.read_text(encoding='utf-8-sig')+'</root>')
    tracks={t.findtext('name'):t for t in zombie.findall('track')}
    mount=deepcopy(tracks['Zombie_body'])
    mount.find('name').text='crystal_drum_mount'
    for frame in mount.findall('t'):
        for image in list(frame.findall('i')): frame.remove(image)
    for name in ('anim_innerarm2','anim_innerarm3'): zombie.remove(tracks[name])
    insertion=list(zombie).index(tracks['Zombie_tie'])+1
    for i,track in enumerate((mount,tracks['anim_innerarm2'],tracks['anim_innerarm3'])):
        zombie.insert(insertion+i,track)
    target=RES/'reanim/CrystalDrummerZombie.reanim'
    target.write_text('\n'.join(ET.tostring(c,encoding='unicode') for c in zombie).rstrip()+'\n',encoding='utf-8')
    outputs.append(target)
    inputs=[SOURCE,LEAVES,rigpath,RES/'reanim/NormalZombie.reanim']
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs+outputs}
    lock=Path(__file__).with_suffix('.sha256.json')
    if lock.exists() and json.loads(lock.read_text())!=hashes:
        import sys
        if '--accept-reviewed' not in sys.argv: raise RuntimeError('Reviewed asset hash drift')
    lock.write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')
    print(f'Generated {len(outputs)} resources from reviewed atlas')


if __name__=='__main__':
    generate()
