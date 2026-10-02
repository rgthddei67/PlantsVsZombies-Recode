"""Check generic route/combination comparisons, unchanged ledger and explicitly labelled external sun fixtures."""
import json
from pathlib import Path

root = Path('build/clang-release/autotest/out/smoke_commander_investment')
assert json.loads((root / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
for name in ('wide', 'narrow'):
    state = json.loads((root / (name + '.json')).read_text(encoding='utf-8'))
    ice = state['coldStorage']
    compare = ice['searchCombination']
    assert 0 < compare['routeEvaluated'] <= 512
    assert 0 < compare['evaluated'] <= 160
    assert 0 < compare['cohortEvaluated'] <= compare['evaluated']
    assert abs(ice['searchPreferenceScore']) <= ice['searchFeatures'][5] * abs(ice['searchEffectiveWeights'][5]) * .25 + .01
    assert 'searchRawPreferenceScore' in ice
    assert compare['bestBreach'] or not compare['baseBreach']
    assert (compare['bestBreach'] and not compare['baseBreach']) or compare['bestScore'] + .002 >= compare['baseScore']
    assert ice['planningApplied'] == 1 and not ice['planning']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']
    assert ice['commanderSpent'] <= ice['commanderBudget']
    workers = [x for x in ice['searchUnitCandidates'] if x['type'] == 'ZOMBIE_ICE_WORKER']
    assert workers and all(x['standalone'] > 0 for x in workers)
    assert ice['searchUnitCandidatesScope'] == 'final_formation_before_precision'
    for candidate in ice['searchUnitCandidates']:
        assert candidate['evaluated'] == candidate['allowed'] + candidate['regroupRejected'] + candidate['capitalRejected']
    assert state['testAudio']['muted']
for name in ('refill-1', 'refill-2', 'no-refill'):
    episode = json.loads((root / (name + '.json')).read_text(encoding='utf-8'))
    extra = episode['externalSun']
    assert episode['playerActions'] is False
    if name == 'no-refill':
        assert not extra['enabled'] and extra['events'] == []
        assert episode['final']['sun'] == 1000
    else:
        assert extra['enabled'] and len(extra['events']) == 1
        assert extra['events'][0] == {'seconds': 0.0, 'before': 1000, 'after': 9990, 'added': 8990}
        assert episode['final']['sun'] == 9990
print('Generic route/pair/batch exploration, bounded learned priors, outcome ordering, ledger and external-sun diagnostics passed.')
