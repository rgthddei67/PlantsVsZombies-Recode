"""Check the shipped policy path, formal budgets/rosters and paid queue restoration."""
import json
import re
from pathlib import Path
import sys

folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_cold_storage_learned')
policy = json.loads((folder.parents[2] / 'resources/ai/cold_storage_policy.json').read_text(encoding='utf-8'))
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
        comparison = ice['searchFormation']
        assert comparison['tested'] == 31 and ice['candidatesEvaluated'] == 101
        # 日志分数按百分之一取整；集中对照不能降低自由搜索的结果。
        score = ice['lastBestScoreOn100'] / 100
        # 核对实际决策使用当前运行资源的权重，而不只是看到 learned_search 标签。
        effective = policy['weights'][:]
        if policy.get('stateModel'):
            effective = [max(-500,min(500,b+sum(x*row[j] for x,row in zip(ice['searchStateInputs'],policy['stateModel']['coefficients']))))
                         for j,b in enumerate(effective)]
        if policy.get('netEconomy'):
            value=max(.01,min(500,abs(effective[4])))
            effective[0]=max(-500,min(500,effective[0]+value))
            effective[3]=value*max(0,min(1,effective[3]))
            effective[4],effective[5]=value,-value
            effective[6]=-abs(effective[6])
        assert all(abs(a-b)<max(.001,abs(b)*.000002) for a,b in zip(ice['searchEffectiveWeights'],effective))
        expected = ice['searchPreferenceScore'] + sum(a*b for a,b in zip(ice['searchFeatures'],effective))
        assert abs(score-expected) < max(.03,abs(expected)*.000002)
        assert score + .02 >= comparison['baseScore']
        for row, value in enumerate(comparison['scores']):
            if comparison['tested'] & (1 << row) and not comparison['rejected'] & (1 << row):
                assert score + .02 >= value
opening, restored = (states[n]['coldStorage'] for n in ('opening', 'restored'))
assert opening['enemyIce'] == restored['enemyIce'] and opening['spent'] == restored['spent']
assert opening['pending'] == restored['pending']
assert states['dispatched']['coldStorage']['deployments'] > 0
assert states['disabled']['coldStorage']['commanderStrategy'] != 'learned_search'
print('Published policy, formal purchase ledger, queue save/load and AI-off fallback passed.')
