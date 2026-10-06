"""核对三条绘制路径中的同一动作序列；不把等待后的绝对运动坐标当作固定预期。"""
import json
import sys
from pathlib import Path

base=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out')
roots=[base/('smoke_pressure_head_attachment'+suffix) for suffix in ('','_noinstance','_opengl')]
signatures=('selected=vulkan noInstance=no','selected=vulkan noInstance=yes','selected=opengl')
for root,signature in zip(roots,signatures):
    log=(root/'run.log').read_text(encoding='utf-8')
    assert signature in log and 'script finished OK' in log,root
    almanac=json.loads((root/'almanac_head.json').read_text(encoding='utf-8'))
    assert almanac['zombieAlmanacSelected']=='ZOMBIE_PRESSURE_SHOOTER'

keys=('row','track','animFrame','pressureGunFrame','pressureShots','isEating','flipX')
last={}
for i in range(17):
    cases=[json.loads((root/f'pose_{i:02d}.json').read_text(encoding='utf-8')) for root in roots]
    for ident,track in ((1,'anim_walk'),(2,'anim_walk2'),(3,'anim_eat'),(4,'anim_walk2')):
        poses=[next(z for z in case['zombies'] if z['id']==ident) for case in cases]
        assert poses[0]['track']==track
        assert all(z['pressureResourcesReady'] for z in poses)
        for other in poses[1:]:
            for key in keys:
                a,b=poses[0][key],other[key]
                assert abs(a-b)<.02 if isinstance(a,float) else a==b,(i,ident,key,a,b)
        assert poses[0]['pressureShots']>=last.get(ident,0)
        last[ident]=poses[0]['pressureShots']
    # 旧机枪植物仍用默认附件语义，两条路径与实际后端均应保留其完整实体。
    assert all(case['normalPlantsByCell']['4_0']['type']=='PLANT_GATLINGPEA' for case in cases)
assert all(count>=8 for count in last.values()),last
for root in roots:
    previous=json.loads((root/'pose_16.json').read_text(encoding='utf-8'))
    restored=json.loads((root/'restored.json').read_text(encoding='utf-8'))
    for ident in (1,2,3,4):
        a=next(z for z in previous['zombies'] if z['id']==ident)
        b=next(z for z in restored['zombies'] if z['id']==ident)
        assert b['pressureResourcesReady'] and a['pressureShots']<=b['pressureShots']<=a['pressureShots']+4
print('Both walks, eating, charm, almanac and animation timing match across Vulkan, NoInstance and OpenGL')
