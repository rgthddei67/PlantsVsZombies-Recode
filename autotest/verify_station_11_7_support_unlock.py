"""Check chapter eligibility, shared wave gates and AI candidate visibility."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    'build/clang-release/autotest/out/smoke_station_11_7_support_unlock')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
types = {'ZOMBIE_THERMAL_SNIPER', 'ZOMBIE_POLAR_CLOCKMAKER'}
for level in (96, 97, 98, 99):
    state = read(f'level_{level}')
    pool = state['spawnList']
    ice = state['coldStorage']
    assert len(pool) == len(set(pool))
    assert (types & set(pool)) == (types if level >= 97 else set())
    assert set(ice['availableUnits']) == set(pool)
    expected = sum(ice['zombieCosts'][t] for t in types) if level >= 97 else 0
    assert ice['enemyIce'] == 1000 - expected
    assert ice['spent'] == expected
    assert ice['pendingCount'] == (2 if level >= 97 else 0)
gates = read('wave_gates')['coldStorage']
assert gates['unlockRounds']['ZOMBIE_THERMAL_SNIPER'] == 4
assert gates['unlockRounds']['ZOMBIE_POLAR_CLOCKMAKER'] == 9
assert gates['pendingCount'] == 2
ai = read('ai_candidates')['coldStorage']
assert ai['commanderStrategy'] == 'learned_search'
evaluated = {c['type'] for c in ai['searchUnitCandidates'] if c['evaluated'] > 0}
assert types <= evaluated, evaluated
print('PASS: excluded before 11-7, both retained through 11-9, real payment/wave gates and AI evaluation verified.')
