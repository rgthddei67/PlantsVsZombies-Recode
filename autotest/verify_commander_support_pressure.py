"""Verify legal sniper suppression and independent temporal forecasting without live side effects."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_support_pressure')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
for prefix in ('open', 'wall', 'cherry', 'clock', 'dead'):
    before, after = read(prefix + '_before'), read(prefix + '_forecast')
    assert before['plants'] == after['plants'], prefix
    assert before['zombies'] == after['zombies'], prefix
    for field in ('enemyIce', 'playerIce', 'spent', 'workerIncome', 'killIncome', 'playerKillIncome'):
        assert before['coldStorage'][field] == after['coldStorage'][field], (prefix, field)
    assert before['sun'] == after['sun'], prefix
assert read('open_forecast')['coldStorage']['searchRawProduction'] > read('wall_forecast')['coldStorage']['searchRawProduction']
assert read('open_actual')['coldStorage']['hostileCount'] == 4
assert read('wall_actual')['coldStorage']['hostileCount'] == 0
assert read('cherry_actual')['coldStorage']['hostileCount'] == 2
assert read('dead_forecast')['coldStorage']['searchClockRevivals'] >= 3
assert read('clock_actual')['coldStorage']['hostileCount'] == 3
print('PASS: sniper suppresses exposed ash, a front wall protects ash, and committed clocks predict actual revival without mutating entities or money.')
