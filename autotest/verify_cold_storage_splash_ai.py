"""Compare paid decisions and actual melon hits with and without a side target."""
import json
from pathlib import Path
import sys


def verify(root):
    """Require conditional avoidance, legal payment, and a runtime collateral counterfactual."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    protected = read('protected_economy')['coldStorage']
    assert protected['splashRiskByRowOn100'][1] > 0
    assert protected['pending'] and all(z['row'] != 1 for z in protected['pending'])
    for name in ('no_allies', 'distant_allies'):
        state = read(name)['coldStorage']
        assert state['splashRiskByRowOn100'][1] == 0, name
        assert any(z['row'] == 1 for z in state['pending']), name
    worthwhile = read('worthwhile_side_attack')['coldStorage']
    assert 0 < worthwhile['splashRiskByRowOn100'][1] < protected['splashRiskByRowOn100'][1]
    assert any(z['row'] == 1 for z in worthwhile['pending']), 'Small collateral cost can be worth paying'
    for name in ('protected_economy', 'no_allies', 'distant_allies', 'worthwhile_side_attack'):
        state = read(name)['coldStorage']
        paid = sum(z['cost'] for z in state['pending'])
        assert state['enemyIce'] + paid == 8 and paid == state['commanderSpent']

    exposed = read('actual_side_target')
    safe = read('actual_no_side_target')
    worker = next(z for z in safe['zombies'] if z['type'] == 'ZOMBIE_ICE_WORKER')
    hit_worker = next(z for z in exposed['zombies'] if z['id'] == worker['id'])
    assert hit_worker['bodyHealth'] < worker['bodyHealth']
    assert hit_worker['slowed'] and not worker['slowed']
    print('Splash AI verified: nearby production protected; empty/distant lane and worthwhile risky lane still attacked; '
          'actual side target causes extra worker damage and slow; payments balanced.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_splash_ai'))
