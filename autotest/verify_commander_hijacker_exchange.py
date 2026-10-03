"""Check forecast/live hijacker exchange against stable entities and plant layers."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    'build/clang-release/autotest/out/smoke_commander_hijacker_exchange')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
before, forecast, actual = read('before'), read('forecast'), read('actual')
assert before['plants'] == forecast['plants'], 'planning must not apply execution to live plants'
assert before['zombies'] == forecast['zombies'], 'planning must not apply friendly fire to live zombies'
assert before['sun'] == forecast['sun']
for wallet in ('enemyIce', 'playerIce'):
    assert before['coldStorage'][wallet] == forecast['coldStorage'][wallet]
survivors = {p['id'] for p in actual['plants']}
for plant in before['plants']:
    assert (plant['id'] in survivors) == (plant['row'] == 1), 'only the pumpkin-protected group survives'
remaining = {z['id']: z for z in actual['zombies']}
for zombie in before['zombies']:
    assert zombie['id'] not in remaining or remaining[zombie['id']]['bodyHealth'] == 0
predicted = forecast['coldStorage']['searchBaselineFeatures'][0]
earned = actual['coldStorage']['killIncome'] - before['coldStorage']['killIncome']
assert predicted == earned and earned > 0, (predicted, earned)
assert forecast['coldStorage']['searchBaselineFeatures'][1] > 0, 'real plant destruction still has tactical value'
assert forecast['coldStorage']['searchBaselineFeatures'][3] == 0
print(f'PASS: forecast/live kill income {earned}; protected group survives, worker and caster die, planning is read-only.')
