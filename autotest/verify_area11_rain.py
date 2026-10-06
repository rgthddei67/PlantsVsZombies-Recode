"""Check actual rain-weapon damage, persistence and player-disabled targeting."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]/'build/clang-release/autotest/out'

def read(test,name):
    return json.loads((ROOT/test/(name+'.json')).read_text(encoding='utf-8'))

def plant(state,row,col,layer='normal'):
    return state[layer+'PlantsByCell'][f'{row}_{col}']

def mortar(state):
    return state['zombiesByType']['ZOMBIE_FLOOD_MORTAR']

bamboo='smoke_area11_bamboo'
first=read(bamboo,'bamboo_five')
health=[z['bodyHealth'] for z in sorted(first['zombies'],key=lambda z:z['id'])]
assert health==[5500,5600,5700,5800,5900,6000],health
restored=read(bamboo,'bamboo_restored')
assert plant(first,2,0)['bambooShots']==plant(restored,2,0)['bambooShots']==1
assert abs(plant(first,2,0)['bambooChargeMilli']-plant(restored,2,0)['bambooChargeMilli'])<=15
for rain in ['light','medium','heavy']:
    assert plant(read(bamboo,'bamboo_'+rain),2,0)['bambooShots']==1

for mode in ['greedy','mc']:
    test='smoke_area11_flood_'+mode
    launch,restore,hit,second=[read(test,n) for n in ['flood_launched','flood_restored','flood_hit','flood_second']]
    gun=mortar(launch)
    assert gun['floodShots']==1 and gun['floodMonteCarlo']==(mode=='mc')
    assert (gun['floodRollouts']>0)==(mode=='mc')
    assert 3650<=gun['floodReloadMs']<=3900,gun['floodReloadMs']
    assert mortar(restore)['floodShots']==1
    assert abs(gun['floodReloadMs']-mortar(restore)['floodReloadMs'])<=80
    assert plant(hit,2,3,'pumpkin')['health']==3250,plant(hit,2,3,'pumpkin')['health']
    assert plant(second,2,3,'pumpkin')['health']==2500
    assert plant(hit,2,3)['health']==4000 and plant(hit,2,3)['floodSlowMs']==0
    assert all(p['health']==3750 for key,p in hit['normalPlantsByCell'].items() if key!='2_3'), 'pumpkin protects only its own cell'
    affected=[p for p in hit['normalPlantsByCell'].values() if p['floodSlowMs']>0]
    assert affected and all(7700<=p['floodSlowMs']<=8000 for p in affected)
    assert all(p['attackMultiplierMilli']==round(400*hit['weather']['plantActionSpeedPct']/100) for p in affected)

test='smoke_area11_flood_counters'
launch,hit,expired,umbrella=[read(test,n) for n in ['clear_launched','clear_after_death','slow_expired','umbrella']]
assert 9650<=mortar(launch)['floodReloadMs']<=9900
assert plant(hit,2,3)['health']==3900 and plant(hit,2,3)['floodSlowMs']>0
assert plant(expired,2,3)['floodSlowMs']==0
assert plant(umbrella,2,3)['health']==4000 and plant(umbrella,2,3)['floodSlowMs']==0
assert plant(umbrella,2,2)['health']==300 and plant(umbrella,2,2)['floodSlowMs']==0
edges='smoke_area11_rain_edges'
mid,restore,complete=[read(edges,n) for n in ['pierce_midflight','pierce_restored','pierce_complete']]
assert mid['bullets'][0]['bambooHitIDs']==restore['bullets'][0]['bambooHitIDs']==[1,2]
assert [z['bodyHealth'] for z in sorted(complete['zombies'],key=lambda z:z['id'])]==[5500,5600,5700,5800,5900,6000]
assert not complete['bullets']
broken,after=[read(edges,n) for n in ['shell_broken','after_shell']]
assert '2_3' not in broken['pumpkinPlantsByCell']
assert plant(broken,2,3)['health']==4000 and plant(broken,2,3)['floodSlowMs']==0
assert plant(broken,2,4)['health']==3750 and plant(broken,2,4)['floodSlowMs']>0
assert plant(after,2,3)['health']==3750 and plant(after,2,3)['floodSlowMs']>0
commander='smoke_commander_area11_rain'
clear,heavy=[read(commander,'flood_'+rain) for rain in ['clear','heavy']]
options=[o for o in clear['legalOptions'] if o['type']=='ZOMBIE_FLOOD_MORTAR']
assert len(options)==5 and all(o['cost']==45 and o['birthBodyHealth']==1800 for o in options)
assert heavy['candidatePlans'][1]['features'][1]>clear['candidatePlans'][1]['features'][1]
assert all(any(b['experiencedEvaluated']>0 for b in state['branches']) for state in [clear,heavy]), 'formal search must compare experienced formations'
definition=read(commander,'definitions')['plantDefinitions']['PLANT_RAINBAMBOO']
assert definition['sunCost']==325 and definition['cooldownMs']==20000 and definition['simulationBaseHealth']==300
for mode in ['greedy','mc']:
    effect=read('smoke_area11_flood_'+mode,'flood_second')['particleEffectsByName']['FloodMortarExplosion'][0]
    assert effect['activeParticleCount']>=40 and effect['worldBounds']['widthInt']>=200
for name in [bamboo,'smoke_area11_flood_greedy','smoke_area11_flood_mc',test,edges,commander,'smoke_area11_rain_reward','smoke_pumpkin_zombie_area_damage']:
    assert read(name,'status')['exitCode']==0
    assert 'script finished OK' in (ROOT/name/'run.log').read_text(encoding='utf-8')
print('Rain weapons: five-hit falloff, four rain cadences, targeting toggle, shell damage, slow, umbrella and saves passed')
