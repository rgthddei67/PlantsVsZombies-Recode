"""Verify natural wave progress and real Echo board-entry damage with stable IDs."""
import json
import sys
from pathlib import Path

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    'build/clang-release/autotest/out/smoke_commander_echo_forecast')


def load(name):
    """Read one state from the same visible AutoTest run."""
    return json.loads((folder / name).read_text(encoding='utf-8-sig'))


assert load('status.json')['status'] == 'passed'
assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')
states = [load(name + '.json') for name in ('initial', 'at5', 'at25', 'at60')]
initial, final = states[0], states[-1]
assert initial['coldStorage']['decisions'] == 12
assert initial['coldStorage']['enemyIce'] == 1014
assert not initial['zombies']
echoes = [p for p in initial['plants'] if p['type'] == 'PLANT_ECHOSHROOM']
assert len(echoes) == 15 and all(not p['sleeping'] for p in echoes)
assert final['coldStorage']['decisions'] >= 14, 'natural decisions must move beyond wave12'
assert final['coldStorage']['spent'] > initial['coldStorage']['spent']
assert final['coldStorage']['planningApplied'] >= 2
legal = {'ZOMBIE_NORMAL', 'ZOMBIE_TRAFFIC_CONE', 'ZOMBIE_BUCKET',
         'ZOMBIE_NEWSPAPER', 'ZOMBIE_ICE_WORKER'}
for state in states:
    c = state['coldStorage']
    assert c['enemyIce'] == (c['initialEnemyIce'] + c['supplied']
                            + c['workerIncome'] + c['killIncome'] - c['spent'])
    assert all(z['type'] in legal for z in state['zombies'])
    assert c['strikeTargetID'] < 0 and not c['strikeAdditionalTargetIDs']
assert any(state['zombies'] for state in states[1:]), 'paid troops must actually appear'
assert all(not state['devSpawnPaused'] for state in states[1:])

before, after = load('gate_before.json'), load('gate_after.json')
targets = {z['row']: z['id'] for z in before['zombies'] if z['type'] == 'ZOMBIE_ICE_WORKER'}
assert set(targets) == {0, 1}
survivors = {z['id']: z for z in after['zombies']}
outside = survivors[targets[0]]
assert outside['hasHead'] and not outside['isDying'] and outside['bodyHealth'] == 500
assert outside['iceBatches'] >= 2, 'a stationary outside worker must retain real production'
inside = survivors.get(targets[1])
assert inside is None or inside['isDying'] or inside['bodyHealth'] <= 0
report = {'passed': True, 'startWave': 12, 'finalWave': final['coldStorage']['decisions'],
          'actualSpent': final['coldStorage']['spent'] - initial['coldStorage']['spent'],
          'outsideWorkerHealth': outside['bodyHealth'], 'outsideWorkerBatches': outside['iceBatches'],
          'insideWorkerKilled': True}
(folder / 'echo_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
