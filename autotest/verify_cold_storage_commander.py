"""Verify commander decisions against paid formations and unchanged resource ledgers."""
import json
import sys
from pathlib import Path


def verify(output):
    names = ['probe', 'pressure', 'assault', 'recover', 'observe', 'counter_block', 'resume',
             'last_stand', 'escort', 'doom_without_coffee', 'doom_with_coffee', 'banked_doom']
    states = {name: json.loads((output / (name + '.json')).read_text(encoding='utf-8'))['coldStorage']
              for name in names}
    for name, state in states.items():
        paid = sum(z['cost'] for z in state['pending'])
        assert paid == state['commanderSpent'] <= state['commanderBudget'], (name, state)
        assert state['enemyIce'] + paid == (4 if name == 'last_stand' else 600), name
        assert state['enemyIce'] >= state['commanderReserve'], name
    assert states['probe']['commanderSpent'] < states['pressure']['commanderSpent']
    assert states['pressure']['commanderSpent'] < states['assault']['commanderSpent']
    assert states['recover']['commanderSpent'] <= states['pressure']['commanderBudget']
    assert len({z['row'] for z in states['probe']['pending']}) == len(states['probe']['pending'])
    assert states['observe']['decisions'] == 20 and not states['observe']['pending']
    assert states['counter_block']['commanderMode'] == 'pressure' and states['counter_block']['pending']
    assert states['resume']['pending'] and states['last_stand']['enemyIce'] == 0
    # This fixture starts with two live gargantuars in row 2; support must follow them.
    assert any(z['type'] == 51 and z['row'] == 2 for z in states['escort']['pending'])
    assert all(z['row'] == 2 for z in states['escort']['pending'] if z['type'] in (40, 50, 51))
    assert states['doom_without_coffee']['responseWindowMs'] > states['doom_with_coffee']['responseWindowMs']
    assert states['banked_doom']['responseWindowMs'] == 0
    print('Commander verified: budgets, assault recovery, finite observation, escorted clockmaker, real bomb availability.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_commander'))
