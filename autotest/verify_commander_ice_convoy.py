"""Check normal/gilded ice-vehicle forecast wiring and independently observed crush/block behavior."""
import json
from pathlib import Path

root = Path('build/clang-release/autotest/out/smoke_commander_ice_convoy')
vehicle_types = {'ZOMBIE_ZAMBONI', 'ZOMBIE_GILDED_ZAMBONI'}
assert json.loads((root / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
plan = json.loads((root / 'planned.json').read_text(encoding='utf-8'))['coldStorage']
assert set(plan['searchInstantCrushTypes']) == vehicle_types
assert plan['enemyIce'] == (plan['initialEnemyIce'] + plan['supplied']
                            + plan['workerIncome'] + plan['killIncome'] - plan['spent'])
before = json.loads((root / 'before.json').read_text(encoding='utf-8'))
after = json.loads((root / 'after.json').read_text(encoding='utf-8'))
assert len(before['plants']) == 4
assert not [p for p in after['plants'] if p['row'] < 2 and p['health'] > 0 and not p['squished']]
for row in (2,3):
    nuts = [p for p in after['plants'] if p['type'] == 'PLANT_ICESTORAGENUT' and p['row'] == row]
    assert len(nuts) == 1 and 0 < nuts[0]['health'] < 8000
assert len([z for z in after['zombies'] if z['type'] in vehicle_types]) == 4
print('Both ice-vehicle types are projected; actual ordinary plants are crushed while storage nuts survive impacts.')
