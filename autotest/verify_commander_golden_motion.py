"""Verify moving golden sources, residual fields and pure forecast sampling in the real game."""
import json
from pathlib import Path

root = Path('build/clang-release/autotest/out/smoke_commander_golden_motion')
assert json.loads((root / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')

for phase in ('live', 'residual'):
    before = json.loads((root / f'{phase}_before.json').read_text(encoding='utf-8'))
    after = json.loads((root / f'{phase}_after.json').read_text(encoding='utf-8'))
    state = after['coldStorage']
    assert state['searchGoldenDrumSteps'] > 0, phase
    assert state['spent'] == 0 and state['enemyIce'] == 0, phase
    previous = {z['id']: z for z in before['zombies']}
    current = {z['id']: z for z in after['zombies']}
    assert previous.keys() == current.keys()
    for entity_id, zombie in previous.items():
        # Commands run in one logical frame: prediction must not mutate bodies, buffs, positions or clocks.
        for field in ('bodyHealth', 'x', 'goldenIceEffectStacks', 'drumStacks',
                      'drumRemainingMs', 'drumBeatCount', 'gildedUndamagedMs',
                      'gildedAccelerationStage'):
            assert zombie.get(field) == current[entity_id].get(field), (phase, entity_id, field)
    if phase == 'live':
        assert state['searchGoldenMaxStacks'] == 2
        assert state['searchGoldenAccelerationSteps'] > 0
        assert any(z['goldenIceEffectStacks'] == 2 and z['drumStacks'] > 0 for z in before['zombies'])
    else:
        assert state['searchGoldenMaxStacks'] == 1
        assert state['searchGoldenAccelerationSteps'] == 0
        assert state['searchGoldenResidualSteps'] > 0
        assert all(z['type'] != 'ZOMBIE_GILDED_ZAMBONI' for z in before['zombies'])
        assert any(z['goldenIceEffectStacks'] == 1 and z['drumStacks'] > 0 for z in before['zombies'])

print('Live golden sources and residual trails amplify forecast drum buffs; acceleration advances without mutating real entities or ice.')
