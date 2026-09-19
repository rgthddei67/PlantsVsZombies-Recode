"""Verify that real escort movement affects income forecasts and paid formations."""
import json
from pathlib import Path
import sys


def verify(root):
    """Compare the same saved formation under control durations, then check actual overtaking."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    names = ('moving_guard', 'brief_slow', 'long_slow', 'brief_stop', 'long_stop',
             'eating_guard', 'plain_lane', 'slow_lane', 'splash_slow_lane', 'mixed_slow_lane')
    states = {name: read(name) for name in names}
    income = {name: s['coldStorage']['predictedProductionOn100'] for name, s in states.items()}
    assert income['moving_guard'] >= income['brief_slow'] > income['long_slow'], income
    assert income['moving_guard'] >= income['brief_stop'] > income['long_stop'], income
    assert income['eating_guard'] < income['moving_guard'], income
    assert states['eating_guard']['zombiesByType']['ZOMBIE_BUCKET']['isEating']
    before = states['long_slow']['zombiesByType']
    after = read('actual_overtake')['zombiesByType']
    for kind in ('ZOMBIE_BUCKET', 'ZOMBIE_ICE_WORKER'):
        assert before[kind]['id'] == after[kind]['id'], kind
    assert before['ZOMBIE_BUCKET']['x'] < before['ZOMBIE_ICE_WORKER']['x']
    assert after['ZOMBIE_BUCKET']['x'] > after['ZOMBIE_ICE_WORKER']['x']
    for name, state in states.items():
        ice = state['coldStorage']
        assert ice['enemyIce'] + sum(z['cost'] for z in ice['pending']) == 400, name
        assert sum(z['cost'] for z in ice['pending']) <= ice['commanderBudget'], name
    plain = states['plain_lane']['coldStorage']
    slow = states['slow_lane']['coldStorage']
    assert slow['economyValueOn100'] < plain['economyValueOn100']
    mixed = states['mixed_slow_lane']['coldStorage']
    assert mixed['commanderMode'] == 'economy', mixed
    worker = next(z for z in mixed['pending'] if z['type'] == 56)
    guard = next(z for z in mixed['pending'] if z['row'] == worker['row'] and z['type'] == 8)
    chosen_delay = mixed['economyRows'][worker['row']]['entryDelayMs']
    assert chosen_delay in (6000, 12000)
    assert worker['remaining'] - guard['remaining'] >= chosen_delay / 1000, mixed
    # Once the worker is hit it can also slow down, so a faster escort can keep protection with the shorter delay.
    splashed = read('splash_followup_actual')
    assert splashed['zombiesByType']['ZOMBIE_ICE_WORKER']['slowed']
    assert splashed['coldStorage']['workerIncome'] >= 3
    print('Escort AI verified: temporary control recovery, reduced forecast before actual overtaking, eating risk, faster escort and delayed paid entry.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_escort_ai'))
