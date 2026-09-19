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
    # A ready bomb can now defer the worker instead of forcing it into the paid assault's blast zone.
    fortified_types = Counter(z['type'] for z in states['fortified_attack']['pending'])
    assert fortified_types[34] >= 4 and fortified_types[56] == 0
    assert states['fortified_attack']['attackDeferred']
    assert 0 < states['fortified_attack']['formationBlastLossOn100'] <= 4000
    # Without an available bomb, the thick front is opened by a first echelon and a profitable worker follows.
    snowball = states['snowball_attack']
    assert snowball['commanderMode'] == 'assault' and snowball['attackDeferred']
    assert sum(z['type'] == 34 for z in snowball['pending']) >= 2
    assert sum(z['type'] == 56 for z in snowball['pending']) == snowball['economicFollowups'] == 1
    worker = next(z for z in snowball['pending'] if z['type'] == 56)
    assert any(z['type'] == 34 and z['row'] == worker['row']
               and z['remaining'] + 6 <= worker['remaining'] for z in snowball['pending'])
    assert states['fortified_attack']['commanderStrategy'] == 'siege'
    fortified = states['fortified_attack']
    assert sum(z['type'] == 34 and z['row'] == fortified['commanderFocusRow'] for z in fortified['pending']) >= 4
    exact = states['fortified_exact_budget']
    # A small treasury must not be emptied just to complete a formation exposed to a ready bomb.
    assert exact['commanderMode'] == 'assault' and 0 < exact['commanderSpent'] < 80
    assert exact['attackDeferred'] and 0 < exact['formationBlastLossOn100'] <= 2400
    assert all(z['type'] == 34 and z['row'] == exact['commanderFocusRow'] for z in exact['pending'])
    assert exact['economicFollowups'] == 0, 'Do not displace the core formation with a worker'
    follow = states['breakthrough_followup']
    assert follow['commanderMode'] == 'harvest'
    assert [(z['type'], z['row']) for z in follow['pending']] == [(56, 2)]
    assert snowball['playerGrowthDpsOn100'] == 0 and snowball['commanderStrategy'] == 'short_game'
    assert snowball['predictedKillIncomeOn100'] > 0
    result = read('snowball_result')
    ice = result['coldStorage']
    assert ice['killIncome'] > 0 and ice['workerIncome'] > 0
    assert ice['enemyIce'] == snowball['enemyIce'] + ice['killIncome'] + ice['workerIncome'] + ice['supplied']
    assert result['plantCount'] < read('snowball_attack')['plantCount']
    print('Campaign AI verified: development race versus investment; staged giants and conditional worker followup; breakthrough followup; real kill and production income; no overdraft.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_campaign_ai'))
