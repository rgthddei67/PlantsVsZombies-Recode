"""验证第二组单位的跨快照结果；运动比较使用相对变化，不绑定绝对位置。"""
import json
import sys
from pathlib import Path

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out/smoke_area11_pair2')
def read(name):
    return json.loads((root/(name+'.json')).read_text(encoding='utf-8'))
def cotton(s):return s['normalPlantsByCell']['2_2']
def gun(s):return s['zombiesByType']['ZOMBIE_PRESSURE_SHOOTER']

first,restored,second=(read(n) for n in ('cotton_first','cotton_restored','cotton_second'))
assert first['pumpkinPlantsByCell']['2_3']['health']==1060
assert second['pumpkinPlantsByCell']['2_3']['health']==1120
assert cotton(first)['cottonHeals']==cotton(restored)['cottonHeals']==1
assert abs(cotton(first)['cottonRemainingMs']-cotton(restored)['cottonRemainingMs'])<=50
assert first['normalPlantsByCell']['2_3']['health']==second['normalPlantsByCell']['2_3']['health']==2000
assert cotton(second)['health']==100, 'no self healing'
before,after=read('pressure_four'),read('pressure_restored')
assert gun(before)['pressureShots']==gun(after)['pressureShots']==4
assert abs(gun(before)['pressureReloadMs']-gun(after)['pressureReloadMs'])<=50
assert abs(gun(before)['pressureGunFrame']-gun(after)['pressureGunFrame'])<.01
assert before['pressureBulletCount']==4 and before['pressureMinimumGapMilli']>=40000
assert read('pressure_damage')['normalPlantsByCell']['2_0']['health']==3900
mirror=read('pressure_mirrors')['normalPlantsByCell']
assert mirror['2_1']['health']==250 and mirror['2_0']['health']==4000
walk0,walk1=read('pressure_walk_start'),read('pressure_walk_midburst_restored')
assert gun(walk1)['x']<gun(walk0)['x'] and gun(walk1)['pressureShots']==4
particles=read('pressure_headless')['particleEffectNameCounts']
assert particles['ZombieHeadOff']==particles['ZombieHeadLight']==0
assert 'script finished OK' in (root/'run.log').read_text(encoding='utf-8')
commander=root.parent/'smoke_commander_area11_pair2'
birth=json.loads((commander/'pressure_birth.json').read_text(encoding='utf-8'))
options=[o for o in birth['legalOptions'] if o['type']=='ZOMBIE_PRESSURE_SHOOTER']
assert options and all(o['cost']==20 and o['birthBodyHealth']==1000 for o in options)
protected=json.loads((commander/'pressure_live.json').read_text(encoding='utf-8'))['candidatePlans'][0]['features'][1]
unprotected=json.loads((commander/'pressure_live_unprotected.json').read_text(encoding='utf-8'))['candidatePlans'][0]['features'][1]
assert unprotected-protected>1, 'live cotton healing must materially reduce forecast damage credit'
assert 'script finished OK' in (commander/'run.log').read_text(encoding='utf-8')
print('Area 11 pair: healing layers, cooldown persistence, burst spacing/damage, mirrors, walking and midburst save passed')
