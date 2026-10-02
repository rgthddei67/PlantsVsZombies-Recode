"""Verify actual walk variants match sampled speeds and future births carry motion uncertainty."""
import json
from pathlib import Path

folder = Path('build/clang-release/autotest/out/smoke_commander_movement')
assert json.loads((folder/'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (folder/'run.log').read_text(encoding='utf-8')
walking = json.loads((folder/'walking.json').read_text(encoding='utf-8'))
units = walking['zombies']
assert len(units) == 39
profiles=walking["zombieBirthMovement"]
assert len(profiles)==59
assert all(p["valid"] and 0<=p["minimum"]<=p["mean"]<=p["maximum"] for p in profiles.values()), profiles
ordinary=units[:20]
assert {z['track'] for z in ordinary} == {'anim_walk', 'anim_walk2'}
for unit in ordinary:
    assert abs(unit['simulationMoveSpeedOn1000']-unit['horizontalMoveSpeedOn1000']) <= 2, unit
for unit in units:
    p=profiles[unit['type']]
    assert p['minimum']*1000-2<=unit['simulationMoveSpeedOn1000']<=p['maximum']*1000+2, unit
assert profiles['ZOMBIE_FOOTBALL']['mean']>profiles['ZOMBIE_NORMAL']['mean']
assert profiles['ZOMBIE_POLAR_CLOCKMAKER']['mean']<profiles['ZOMBIE_NORMAL']['mean']
projected = json.loads((folder/'projected.json').read_text(encoding='utf-8'))
ice = projected['coldStorage']
assert ice['searchMovementBoundsApplied'] == 2
assert ice['spent'] == 36 and ice['enemyIce'] == 0
assert ice['enemyIce'] == ice['initialEnemyIce']+ice['supplied']+ice['workerIncome']+ice['killIncome']-ice['spent']
assert projected['zombieCount'] == walking['zombieCount'] and ice['pendingCount'] == 2
print('Both walk variants match actual speeds; paid future motion bounds apply without spawning or additional payment.')

travel=json.loads((folder/'travel.json').read_text(encoding='utf-8'))
delta=travel['coldStorage']['elapsed']-projected['coldStorage']['elapsed']
assert .3<delta<1
previous={z['id']:z for z in projected['zombies']}
matched=set()
linear={'ZOMBIE_ZAMBONI','ZOMBIE_GILDED_ZAMBONI','ZOMBIE_CATAPULT','ZOMBIE_DIGGER','ZOMBIE_POGO','ZOMBIE_BALLOON'}
for z in travel['zombies']:
    if z['type'] not in linear or z['id'] not in previous: continue
    matched.add(z['type'])
    old=previous[z['id']]
    measured=(old['x']-z['x'])/delta
    expected=old['simulationMoveSpeedOn1000']/1000
    assert abs(measured-expected)<1, (z['type'],measured,expected)
assert matched==linear
print('Independent observed displacement matches special linear movement projections.')
