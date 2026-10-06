"""从已生成的透明分件图集派生气压射手/缝补棉花运行资产；保留经典时间轴。"""
from pathlib import Path
import hashlib, json, re, xml.etree.ElementTree as ET
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
RES=ROOT/'build/clang-release/resources'
SOURCE=ROOT/'assets/source/area11_pair_parts.png'
OUT=RES/'image/reanim'
LOCK=ROOT/'assets/source/area11_pair_parts.lock.json'
refs=[SOURCE,RES/'reanim/Marigold.reanim',RES/'reanim/GatlingPea.reanim']
fingerprints={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in refs}
previous=json.loads(LOCK.read_text()) if LOCK.exists() else None
if previous:
    assert previous['inputs']==fingerprints,'Source/reference asset changed; review before regenerating'
sheet=Image.open(SOURCE).convert('RGBA'); w,h=sheet.size
pieces=[]
for row in range(3):
    for col in range(3):
        im=sheet.crop((col*w//3,row*h//3,(col+1)*w//3,(row+1)*h//3))
        box=im.getchannel('A').point(lambda x:255 if x>32 else 0).getbbox()
        pieces.append(im.crop(box))
outputs=[]
def save(im,path):
    im.save(path);outputs.append(path)
def fit_piece(i,name,reference=None,size=None):
    if reference:
        old=Image.open(OUT/(reference+'.png')).convert('RGBA'); size=old.size
        box=old.getchannel('A').getbbox()
    else: box=(0,0,*size)
    im=Image.new('RGBA',size)
    im.alpha_composite(pieces[i].resize((box[2]-box[0],box[3]-box[1]),Image.Resampling.LANCZOS),(box[0],box[1]))
    save(im,OUT/(name+'.png'))
for args in [(0,'MendingCotton_petals','Marigold_petals'),(1,'MendingCotton_head','Marigold_head'),
             (2,'Pressure_head','GatlingPea_head'),(3,'Pressure_mouth','GatlingPea_mouth'),
             (4,'Pressure_overlay','GatlingPea_mouth_overlay'),(5,'Pressure_barrel','GatlingPea_barrel'),
             (6,'Pressure_helmet','GatlingPea_helmet')]: fit_piece(*args)
fit_piece(7,'Pressure_tank',size=(56,78));fit_piece(8,'Pressure_projectile',size=(34,24))

cotton=(RES/'reanim/Marigold.reanim').read_text()
cotton=cotton.replace('IMAGE_REANIM_MARIGOLD_PETALS','IMAGE_REANIM_MENDINGCOTTON_PETALS').replace('IMAGE_REANIM_MARIGOLD_HEAD','IMAGE_REANIM_MENDINGCOTTON_HEAD')
# 旧眨眼包含黄色脸底；棉花用独立眼睛和原嘴/眉动作，不叠旧材质。
cotton=re.sub(r'<track>\s*<name>Marigold_blink</name>.*?</track>','',cotton,flags=re.S)
(RES/'reanim/MendingCotton.reanim').write_text(cotton.rstrip()+'\n',encoding='utf-8');outputs.append(RES/'reanim/MendingCotton.reanim')
head=(RES/'reanim/GatlingPea.reanim').read_text()
for old,new in [('HEAD','HEAD'),('MOUTH_OVERLAY','OVERLAY'),('MOUTH','MOUTH'),('BARREL','BARREL'),('HELMET','HELMET')]:
    head=head.replace('IMAGE_REANIM_GATLINGPEA_'+old,'IMAGE_REANIM_PRESSURE_'+new)
# 转换到僵尸头轨原点，完整保留枪管相对形变；贴图源朝右，渲染时在局部镜像。
root=ET.fromstring('<root>'+head+'</root>')
for tr in list(root.findall('track')):
    name=tr.findtext('name')
    if name in ('idle_shoot_blink','PeaShooter_eyebrow') or name.startswith(('backleaf','frontleaf','stalk')):
        root.remove(tr);continue
    for t in tr.findall('t'):
        # 新机械头比原僵尸头短：整组下移12px，让颈根在行走/啃食极值仍压住衣领。
        for axis,offset in [('x',19.2),('y',5.8)]:
            e=t.find(axis)
            if e is not None:e.text=f'{float(e.text)-offset:.4f}'
        # 新帽盖缩小后仍须压住头壳；只调整自身位置，不能为露眼睛把整顶帽子抬成悬空。
        if name=='GatlingPea_helmet':
            for axis,offset in [('x',5),('y',0)]:
                e=t.find(axis)
                if e is not None:e.text=f'{float(e.text)+offset:.4f}'
            for axis in ['sx','sy']:
                e=t.find(axis)
                if e is not None:e.text=f'{float(e.text)*.75:.4f}'
head='\n'.join(ET.tostring(e,encoding='unicode') for e in root)
(RES/'reanim/PressureShooterHead.reanim').write_text(head,encoding='utf-8');outputs.append(RES/'reanim/PressureShooterHead.reanim')

# 同一套棉花分件合成卡面，保持场上和卡槽身份；使用经典叶座，不缩放整份骨架。
card=Image.new('RGBA',(100,110)); leaf=Image.open(OUT/'PeaShooter_frontleaf.png').convert('RGBA')
leaf.thumbnail((75,30));card.alpha_composite(leaf,((100-leaf.width)//2,78))
stalk=Image.open(OUT/'PeaShooter_stalk_top.png').convert('RGBA').resize((17,40),Image.Resampling.LANCZOS)
card.alpha_composite(stalk,(44,51))
card.alpha_composite(pieces[0].resize((82,82),Image.Resampling.LANCZOS),(9,3))
card.alpha_composite(pieces[1].resize((46,49),Image.Resampling.LANCZOS),(27,21))
mouth=Image.open(OUT/'Marigold_mouth.png').convert('RGBA'); mouth.thumbnail((15,12));card.alpha_composite(mouth,(44,51))
save(card,RES/'image/PlantImage/MendingCotton.png')
result={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}
if previous:
    assert previous['outputs']==result,'Generated asset hash drift; review changed output before updating lock'
LOCK.write_text(json.dumps({'inputs':fingerprints,'outputs':result},indent=2),encoding='utf-8')
print('Generated',len(outputs),'assets')
