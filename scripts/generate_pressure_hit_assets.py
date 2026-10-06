"""按气弹主色派生豌豆命中贴图，原尺寸、透明轮廓、明暗层次和运动参数保持不变。"""
from pathlib import Path
import hashlib
import json
from collections import Counter
from PIL import Image, ImageOps

ROOT=Path(__file__).resolve().parents[1]
RES=ROOT/'build/clang-release/resources'
SOURCE=RES/'image/reanim/Pressure_projectile.png'
LOCK=ROOT/'assets/source/pressure_hit_parts.lock.json'
inputs=[SOURCE,RES/'particles/pea_splats.png',RES/'particles/pea_particles.png',RES/'particles/config/PeaBulletHit.xml']
hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
old=json.loads(LOCK.read_text()) if LOCK.exists() else None
if old: assert old['inputs']==hashes,'Input changed; review before regenerating'
# 量化到8阶后按不透明度计票，排除描边与边缘透明像素；主色来自实际运行子弹。
palette=Counter()
projectile=Image.open(SOURCE).convert('RGBA')
for red,green,blue,alpha in [projectile.getpixel((x,y)) for y in range(projectile.height) for x in range(projectile.width)]:
    if alpha>=192 and max(red,green,blue)>=128:
        palette[tuple(min(255,round(c/8)*8) for c in (red,green,blue))]+=alpha
primary=palette.most_common(1)[0][0]
outputs=[]
for name in ['splats','particles']:
    reference=Image.open(RES/f'particles/pea_{name}.png').convert('RGBA')
    # 单调调色保持原图明暗次序；灰白中间色提亮绿色的中间调，深色轮廓仍保留。
    strip=ImageOps.colorize(ImageOps.grayscale(reference),black=(16,27,29),
        white=tuple(min(255,c+8) for c in primary),mid=primary,midpoint=140).convert('RGBA')
    strip.putalpha(reference.getchannel('A'))
    assert strip.getchannel('A').tobytes()==reference.getchannel('A').tobytes()
    target=RES/f'particles/pressure_{name}.png';strip.save(target);outputs.append(target)
xml=(RES/'particles/config/PeaBulletHit.xml').read_text(encoding='utf-8')
xml=xml.replace('PeaBulletHit','PressureHit').replace('PARTICLE_PEA_','PARTICLE_PRESSURE_')
target=RES/'particles/config/PressureHit.xml';target.write_text(xml.rstrip()+'\n',encoding='utf-8');outputs.append(target)
result={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}
if old: assert old['outputs']==result,'Output hash drift; review changed assets'
LOCK.write_text(json.dumps({'inputs':hashes,'outputs':result},indent=2),encoding='utf-8')
print('Generated original pea masks with pressure projectile dominant color',primary)
