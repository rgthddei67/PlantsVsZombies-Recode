"""Verify paid commitments survive replanning, reinforcement, save/load and birth."""
import json
from pathlib import Path
import sys

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_rolling_queue')
names = ('paid', 'rechecked', 'reinforced', 'restored', 'automatic', 'born')
states = {name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8')) for name in names}
assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')
for name, state in states.items():
    ice = state['coldStorage']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent'], name
    assert ice['pendingCount'] + state['zombieCount'] <= 64, name
    assert state['testAudio']['musicPct'] == 0 and state['testAudio']['soundPct'] > 0
    revision = ice['searchQueue']
    assert revision['afterScore'] + .001 >= revision['beforeScore'], name
paid, checked, reinforced, restored, automatic, born = (states[n]['coldStorage'] for n in names)
assert paid['pending'] == checked['pending'] and paid['spent'] == checked['spent']
assert paid['decisions'] == checked['decisions'] == 20
assert checked['searchQueue']['committed'] == 1 and checked['searchQueue']['evaluated'] > 0
assert reinforced['pending'][0] == paid['pending'][0] and reinforced['pending'][0]['wave'] == 20
assert all(p['wave'] == 21 for p in reinforced['pending'][1:])
assert reinforced['spent'] - paid['spent'] == sum(p['cost'] for p in reinforced['pending'][1:])
assert reinforced['pending'] == restored['pending'] and reinforced['spent'] == restored['spent']
assert automatic['searchQueue']['committed'] > 0 and automatic['searchSerial'] > restored['searchSerial']
assert automatic['spent'] == restored['spent'] and automatic['decisions'] == 21
assert born['pendingCount'] == 0 and born['deployments'] == len(reinforced['pending'])
workers = [z for z in states['born']['zombies'] if z['type'] == 'ZOMBIE_ICE_WORKER']
assert any(z['spawnWave'] == 20 for z in workers)
assert all(z['spawnWave'] in (20, 21) for z in states['born']['zombies'])
legacy = json.loads((folder / 'legacy.json').read_text(encoding='utf-8'))['coldStorage']
assert legacy['pending'] == [{'type': 0, 'row': 0, 'cost': 4, 'remaining': 10, 'wave': 20}]
print('Rolling search, unchanged paid ledger, queue deadlines, saved purchase waves and automatic reevaluation passed.')
