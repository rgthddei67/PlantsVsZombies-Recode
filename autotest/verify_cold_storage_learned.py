"""Check the shipped policy path, formal budgets/rosters and paid queue restoration."""
import json
import re
from pathlib import Path
import sys

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_cold_storage_learned')
enum = (Path(__file__).resolve().parents[1] / 'PlantVsZombies/Game/Zombie/ZombieType.h').read_text(encoding='utf-8')
enum = re.sub(r'//[^\n]*', '', enum)
names = re.findall(r'^\s*(ZOMBIE_\w+)\s*,', enum.split('enum class ZombieType')[1].split('NUM_ZOMBIE_TYPES')[0], re.M)
states = {name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
          for name in ('opening', 'restored', 'late', 'dispatched', 'disabled')}
assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')
for name, state in states.items():
    ice = state['coldStorage']
    assert not ice['trainingAllUnits']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent'], name
    assert all(0 <= p['row'] < 5 and names[p['type']] in ice['availableUnits'] for p in ice['pending'])
    assert ice['pendingCount'] + len(ice['refundableCosts']) <= 64
    if name in ('opening', 'late'):
        assert ice['commanderStrategy'] == 'learned_search' and ice['pendingCount'] > 0
        assert ice['commanderSpent'] <= ice['commanderBudget']
opening, restored = (states[n]['coldStorage'] for n in ('opening', 'restored'))
assert opening['enemyIce'] == restored['enemyIce'] and opening['spent'] == restored['spent']
assert opening['pending'] == restored['pending']
assert states['dispatched']['coldStorage']['deployments'] > 0
assert states['disabled']['coldStorage']['commanderStrategy'] != 'learned_search'
print('Published policy, formal purchase ledger, queue save/load and AI-off fallback passed.')
