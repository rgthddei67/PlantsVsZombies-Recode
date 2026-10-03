"""Verify forecast-only vacancies and actual legal ash placement after clearing a cell."""
import json
from pathlib import Path
import sys


def verify(root):
    """Compare identical live inputs around prediction, then check the independently executed clear."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
    for prefix in ('shovel', 'vacancy'):
        before, predicted, actual = (read(prefix + suffix) for suffix in ('_before', '_forecast', '_actual'))
        assert before['plants'] == predicted['plants']
        assert before['zombies'] == predicted['zombies']
        assert before['cards'] == predicted['cards']
        assert before['sun'] == predicted['sun']
        for field in ('enemyIce', 'playerIce', 'spent', 'workerIncome', 'killIncome'):
            assert before['coldStorage'][field] == predicted['coldStorage'][field], (prefix, field)
        assert predicted['coldStorage']['searchRawProduction'] <= 200
        assert actual['coldStorage']['hostileCount'] == 0
    assert read('shovel_forecast')['coldStorage']['searchCounterShovels'] >= 1
    assert read('vacancy_forecast')['coldStorage']['searchCounterShovels'] == 0
    print('PASS: optional shovel response and pending precision vacancy match actual ash clearing without changing live state.')


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_counter_vacancy'))
