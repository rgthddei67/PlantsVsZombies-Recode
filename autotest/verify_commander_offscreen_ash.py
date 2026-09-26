"""Compare actual offscreen ash deaths with commander income predictions and later births."""
import json
from pathlib import Path
import sys

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_offscreen_ash')
states = {name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
          for name in ('forecast', 'burned', 'delayed')}
assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')
before, burned, delayed = (states[name] for name in ('forecast', 'burned', 'delayed'))
assert len(before['zombies']) == 2 and all(z['xInt'] > 1100 for z in before['zombies'])
assert before['coldStorage']['searchBaselineFeatures'][4] == 0
assert burned['zombieCount'] == 0 and burned['coldStorage']['workerIncome'] == 0
assert burned['coldStorage']['pendingCount'] == 1
assert delayed['zombieCount'] == 1 and delayed['coldStorage']['pendingCount'] == 0
assert delayed['coldStorage']['workerIncome'] >= 4
for state in states.values():
    ice = state['coldStorage']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']
    assert state['testAudio']['musicPct'] == 0 and state['testAudio']['soundPct'] > 0
print('Offscreen ash forecast matches actual deaths; unborn paid workers survive and later produce normally.')
