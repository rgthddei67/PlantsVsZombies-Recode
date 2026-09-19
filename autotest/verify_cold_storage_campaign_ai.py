"""Verify adaptive spending, mixed formations and actual income after breaking a defense."""
from collections import Counter
import json
from pathlib import Path
import sys


def verify(root):
    """Compare equal-time development choices, a fortified board, and the resulting paid combat."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    names = ('development_race', 'development_blocked', 'fortified_attack', 'fortified_exact_budget',
             'breakthrough_followup', 'snowball_attack', 'poor_commander')
    states = {name: read(name)['coldStorage'] for name in names}
    for name, state in states.items():
        starting_ice = 20 if name == 'poor_commander' else 80 if name == 'fortified_exact_budget' else 450
        paid = sum(z['cost'] for z in state['pending'])
        assert state['enemyIce'] + paid == starting_ice, name
        assert paid == state['commanderSpent'] <= state['commanderBudget'], name
        assert state['killIncome'] == state['workerIncome'] == 0, 'Forecasts must not mint ice'

    race, blocked = states['development_race'], states['development_blocked']
    assert race['elapsed'] == blocked['elapsed']
    assert race['playerGrowthDpsOn100'] > blocked['playerGrowthDpsOn100']
    assert race['commanderStrategy'] == 'short_game' and race['commanderMode'] == 'assault'
    assert blocked['commanderStrategy'] == 'long_game' and blocked['commanderMode'] == 'economy'
    assert race['spendingHorizonMs'] < blocked['spendingHorizonMs']
    assert race['commanderSpent'] > blocked['commanderSpent']
    for name in ('fortified_attack', 'snowball_attack'):
        state = states[name]
        pending = state['pending']
        types = Counter(z['type'] for z in pending)
        assert state['commanderMode'] == 'assault' and types[34] >= 4, (name, types)
        assert types[56] == state['economicFollowups'] == 1, (name, types)
        worker = next(z for z in pending if z['type'] == 56)
        assert any(z['type'] == 34 and z['row'] == worker['row']
                   and z['remaining'] + 6 <= worker['remaining'] for z in pending), name
    assert states['fortified_attack']['commanderStrategy'] == 'siege'
    fortified = states['fortified_attack']
    assert sum(z['type'] == 34 and z['row'] == fortified['commanderFocusRow'] for z in fortified['pending']) >= 4
    exact = states['fortified_exact_budget']
    assert exact['commanderMode'] == 'assault' and exact['commanderSpent'] == 80
    assert all(z['type'] == 34 and z['row'] == exact['commanderFocusRow'] for z in exact['pending'])
    assert exact['economicFollowups'] == 0, 'Do not displace the core formation with a worker'
    follow = states['breakthrough_followup']
    assert follow['commanderMode'] == 'harvest'
    assert [(z['type'], z['row']) for z in follow['pending']] == [(56, 2)]
    snowball = states['snowball_attack']
    assert snowball['playerGrowthDpsOn100'] == 0 and snowball['commanderStrategy'] == 'short_game'
    assert snowball['predictedKillIncomeOn100'] > 0
    result = read('snowball_result')
    ice = result['coldStorage']
    assert ice['killIncome'] > 0 and ice['workerIncome'] > 0
    assert ice['enemyIce'] == snowball['enemyIce'] + ice['killIncome'] + ice['workerIncome'] + ice['supplied']
    assert result['plantCount'] < read('snowball_attack')['plantCount']
    print('Campaign AI verified: development race versus investment; mass giants plus worker; breakthrough followup; real kill and production income; no overdraft.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_campaign_ai'))
