"""Verify background ownership/transactions and report decision latency separately from FPS."""
import json
from pathlib import Path

root = Path('build/clang-release/autotest/out')
smoke = root / 'smoke_commander_background'
stress = root / 'stress_commander_background'

def read(folder, name):
    """Read a completed state and check the real economy ledger, including rejected plans."""
    state = json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
    ice = state['coldStorage']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']
    assert state['testAudio']['musicPct'] == 0 and state['testAudio']['soundPct'] > 0
    return state

for folder in (smoke, stress):
    assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')

states = {name: read(smoke, name) for name in
          ('started', 'committed', 'once', 'stale', 'restored', 'new_board', 'paused', 'resumed')}
ice = {name: state['coldStorage'] for name, state in states.items()}
assert ice['started']['planning'] and ice['started']['spent'] == 24
assert ice['committed']['planningApplied'] == 1 and ice['committed']['spent'] > 24
assert ice['once']['spent'] == ice['stale']['spent'] == ice['committed']['spent']
assert ice['once']['decisions'] == ice['stale']['decisions'] == ice['committed']['decisions']
assert ice['stale']['planningDiscarded'] == 1
assert ice['restored']['spent'] == ice['stale']['spent'] and not ice['restored']['planning']
assert ice['restored']['pending'] == ice['stale']['pending']
assert ice['new_board']['spent'] == 0 and not ice['new_board']['planning']
assert ice['paused']['planning'] and ice['paused']['spent'] == 0
assert not ice['resumed']['planning'] and ice['resumed']['planningApplied'] == 1

sync = read(stress, 'synchronous')['coldStorage']
background = read(stress, 'background')['coldStorage']
assert background['planningApplied'] == 1 and background['planningDiscarded'] == 0
assert background['candidatesEvaluated'] == sync['candidatesEvaluated']
assert background['planningMainMaxMs'] < sync['planningMainMaxMs'] * .4
print('No early/duplicate payment; stale plans discarded; load/scene/pause boundaries passed.')
print(f"Dense scenario main-thread decision: {sync['planningMainMaxMs']:.3f} ms sync -> "
      f"{background['planningMainMaxMs']:.3f} ms background; "
      f"worker {background['planningWorkerMs']:.3f} ms. These are not whole-frame FPS measurements.")
