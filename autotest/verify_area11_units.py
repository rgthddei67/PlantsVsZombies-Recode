"""Verify stable entity relationships from the Area 11 capability smoke."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out/smoke_area11_units')
def read(name): return json.loads((out/(name+'.json')).read_text(encoding='utf-8-sig'))
def units(state, kind): return {z['id']:z for z in state['zombies'] if z['type']=='ZOMBIE_'+kind and not z['isDying']}
def engineer(state): return next(iter(units(state,'DISASTER_ENGINEER').values()))

before=read('same_blast_before'); after=read('same_blast_after')
assert len(units(before,'ICE_WORKER'))==4
survivors=units(after,'ICE_WORKER')
assert len(survivors)==3 and not units(after,'DISASTER_ENGINEER'), 'same blast must kill engineer and protect only its nearest three workers'
assert all(z['bodyHealth']==500 for z in survivors.values()), 'protected workers take no ash damage'

a=read('reload_before_control'); b=read('reload_controlled'); c=read('reload_restored')
assert a['coldStorage']['enemyIce']==90
assert 0 <= engineer(a)['engineerReloadMs']-engineer(b)['engineerReloadMs'] <= 70, 'short paralysis pauses reload without clearing it'
assert abs(engineer(b)['engineerReloadMs']-engineer(c)['engineerReloadMs']) <= 70
assert c['coldStorage']['enemyIce']==b['coldStorage']['enemyIce'], 'restoring paid reload cannot charge twice'
assert engineer(c)['engineerProtectionUses']==1 and engineer(c)['engineerReloadPaid']
assert engineer(read('reload_complete'))['engineerFull']
assert len(units(read('second_blast_after'),'ICE_WORKER'))==3

a=units(read('first_arc'),'NORMAL'); b=units(read('second_arc'),'NORMAL')
assert len(a)==9 and all(z['bodyHealth']==250 for z in a.values()), 'all nine splash recipients take exactly twenty damage'
controlled={key:z for key,z in a.items() if z['paralyzed']}
assert len(controlled)==6, 'each attack controls at most six eligible recipients'
assert all(b[key]['paralysisTimerMs']<=z['paralysisTimerMs'] for key,z in controlled.items()), 'a second source cannot extend earlier control'
assert all(not z['paralyzed'] and z['thunderResistanceMs']>0 for z in units(read('arc_recovered'),'NORMAL').values())
assert next(iter(units(read('charge_not_shortened'),'ICE_WORKER').values()))['paralysisTimerMs']>=1850
assert next(iter(units(read('resistance_expired'),'ICE_WORKER').values()))['paralyzed']

# 跟踪同一颗自然发射的雷种，以相对位置证明平射，不断言运动对象绝对坐标。
rays=lambda state: {bullet['id']:bullet for bullet in state['bullets'] if bullet['typeName']=='BULLET_THUNDER_SEED'}
first=rays(read('flat_first')); second=rays(read('flat_second'))
assert first and first.keys() & second.keys(), 'both snapshots must contain the same naturally fired projectile'
for key in first.keys() & second.keys():
    start=first[key]; end=second[key]
    assert not start['lobbedMotion'] and not end['lobbedMotion']
    assert end['x']>start['x'] and abs(end['y']-start['y'])<0.001
    assert start['baseVelocityX']==290 and start['baseVelocityY']==0
assert any(z['bodyHealth']<500 for z in units(read('flat_hit'),'ICE_WORKER').values()), 'straight projectile must actually hit'

result={'passed':True,'protectedWorkers':len(survivors),'firstArcTargets':len(a),'firstArcControlled':len(controlled),'reloadCost':10,'sameAttackEngineerDeath':True,'straightShot':True}
if len(sys.argv)>2:
    overlap=Path(sys.argv[2])
    load_overlap=lambda name: json.loads((overlap/(name+'.json')).read_text(encoding='utf-8-sig'))
    initial=units(load_overlap('before'),'DISASTER_ENGINEER')
    once=units(load_overlap('first'),'DISASTER_ENGINEER')
    twice=units(load_overlap('second'),'DISASTER_ENGINEER')
    assert len(initial)==2 and all(z['engineerFull'] for z in initial.values())
    assert sum(z['engineerProtectionUses'] for z in once.values())==1
    assert sum(z['engineerFull'] for z in once.values())==1, 'overlap cannot consume a redundant canister'
    assert sum(z['engineerProtectionUses'] for z in twice.values())==2
    assert not any(z['engineerFull'] for z in twice.values())
    assert all(z['bodyHealth']==500 for name in ('first','second') for z in units(load_overlap(name),'ICE_WORKER').values())
    result['overlapOneCanisterPerEvent']=True
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
