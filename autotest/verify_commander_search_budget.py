"""Check the read-only same-snapshot probe; wall-clock candidate counts are not deterministic."""
import json
import math
import sys
from pathlib import Path


def verify(output):
    """Verify real command success, unchanged paid commitments and valid complete probe results."""
    status = json.loads((output / 'status.json').read_text(encoding='utf-8'))
    assert status['status'] == 'passed' and status['exitCode'] == 0
    log = (output / 'run.log').read_text(encoding='utf-8')
    assert 'script finished OK' in log and 'FAIL' not in log
    report = json.loads((output / 'comparison.json').read_text(encoding='utf-8'))
    assert report['boardUnchanged']
    branches = report['branches']
    assert {(b['budgetMs'], b['relaxCumulativeRisk']) for b in branches} == {
        (budget, relax) for budget in (0, 300, 600, 900, 1800) for relax in (False, True)}
    for branch in branches:
        assert branch['evaluated'] > 0 and math.isfinite(branch['score'])
        assert all(math.isfinite(x) for x in branch['features'] + branch['baselineFeatures'])
        assert sum(action['cost'] for action in branch['actions']) <= report['budget']
        assert all(action['delay'] >= 0 for action in branch['actions'])
        if branch['budgetMs'] == 0:
            assert not branch['timeLimited']
    state = json.loads((output / 'unchanged.json').read_text(encoding='utf-8'))
    ice = state['coldStorage']
    assert ice['enemyIce'] == 329 and ice['spent'] == 24 and ice['pendingCount'] == 1
    assert ice['planningStarted'] == 0 and ice['planningApplied'] == 0
    print('Same-snapshot budget/risk probes preserved the board and paid queue')


if __name__ == '__main__':
    verify(Path(sys.argv[1]))
