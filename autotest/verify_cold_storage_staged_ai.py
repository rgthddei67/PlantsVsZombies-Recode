"""Verify paid staging and an actual bomb/counterattack on the same fortified field."""
import json
from pathlib import Path
import sys


def verify(root):
    """Check bounded blast exposure, preserved reserves, live reaction and queue reload."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    ready, cooldown = (read(n)['coldStorage'] for n in ('bomb_ready', 'bomb_cooldown'))
    for state in (ready, cooldown):
        paid = sum(z['cost'] for z in state['pending'])
        assert paid == state['commanderSpent'] <= state['commanderBudget']
        assert state['enemyIce'] + paid == 600
    assert ready['attackDeferred'] and ready['formationBlastLossOn100'] <= 4000
    assert any(z['type'] == 34 for z in ready['pending']), 'Use a front line that can survive the available bomb'
    assert ready['commanderSpent'] < cooldown['commanderSpent']
    assert cooldown['formationBlastLossOn100'] == 0
    assert not cooldown['attackDeferred'], 'A completed budget must not falsely bypass assault recovery'
    for name in ('breacher_far', 'breacher_near'):
        state = read(name)['coldStorage']
        assert state['commanderFocusRow'] == 2
        # Existing breachers may need reinforcement; do not freeze a randomized tactical choice to zero giants.
        giants_here = sum(z['row'] == 2 and z['type'] == 34 for z in state['pending'])
        giants_elsewhere = sum(z['row'] != 2 and z['type'] == 34 for z in state['pending'])
        assert giants_here < giants_elsewhere, 'Existing breachers should free most heavy investment for other lanes'
        assert any(z['row'] == 2 and z['type'] == 56 for z in state['pending']), 'Follow existing breachers with production'
        assert sum(z['cost'] for z in state['pending']) + state['enemyIce'] == 600
    held, restored = (read(n)['coldStorage'] for n in ('held_bomb', 'held_bomb_reloaded'))
    assert held['attackDeferred'] and held['enemyIce'] == ready['enemyIce']
    for key in ('spent', 'enemyIce', 'deployments', 'decisions'):
        assert held[key] == restored[key], (key, held[key], restored[key])
    assert held['deployments'] == len(ready['pending'])

    before, after, counter = (read(n) for n in ('held_bomb', 'after_bomb', 'counterattack'))
    survivors = {z['id']: z for z in after['zombies']}
    assert any(z['id'] in survivors and z['bodyHealth'] - survivors[z['id']]['bodyHealth'] >= 1700
               for z in before['zombies']), 'Real bomb must hit and leave surviving front-line units'
    assert counter['coldStorage']['deployments'] > after['coldStorage']['deployments']
    assert counter['coldStorage']['spent'] > after['coldStorage']['spent']
    assert counter['cards'][0]['cooldownRemainingMs'] > 0, 'Counterattack must happen inside the bomb cooldown'
    for state in (held, counter['coldStorage']):
        assert state['enemyIce'] + state['spent'] == 600 + state['supplied'] + state['killIncome'] + state['workerIncome']
    print('Staged AI verified: limited shared blast exposure; reserves held while blocked; '
          'real bomb survivors and renewed deployments during cooldown; paid queue survives reload.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_staged_ai'))
