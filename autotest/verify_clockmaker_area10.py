"""Compare real time-anchor records with restored local abilities and unchanged AI inputs."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/clang-release/autotest/out')


def read(folder, name):
    return json.loads((root / folder / (name + '.json')).read_text(encoding='utf-8'))


def near_ms(actual, seconds):
    # The restored entity can advance by one fixed tick after Board resolves its anchor.
    assert abs(actual - round(seconds * 1000)) <= 50, (actual, seconds)


folder = 'smoke_clockmaker_area10_states'
for mode in ('survivor', 'revival'):
    saved = read(folder, 'snapshots/' + mode + '_anchor')['temporalAnchors'][0]
    before = read(folder, mode + '_before_plan')
    planned = read(folder, mode + '_planned')
    restored = read(folder, mode + '_restored')
    for key in ('enemyIce', 'playerIce', 'workerIncome'):
        assert before['coldStorage'][key] == planned['coldStorage'][key], key
    assert before['zombies'] == planned['zombies'], 'planning mutated live entities'
    records = {t['zombieID']: t for t in saved['targets']}
    entities = restored['zombiesByType']
    for name in ('ICE_WORKER', 'BOILER', 'COLD_CHAIN_GUARD'):
        z = entities['ZOMBIE_' + name]
        target = records[z['id']]
        assert target['abilityStateValid']
        assert z['bodyHealth'] == target['bodyHealth']
        if name == 'ICE_WORKER':
            assert z['iceBatches'] == target['abilityReleaseCount']
            assert z['nextIceYieldOn1000'] == round(target['abilityAuxiliaryValue'] * 1000)
            near_ms(z['iceRemainingMs'], target['abilityRemaining'])
        elif name == 'BOILER':
            assert z['boilerPhase'] == target['abilityPhase']
            assert z['boilerSpent'] == bool(target['abilityReleaseCount'])
            near_ms(z['boilerRemainingMs'], target['abilityRemaining'])
        else:
            assert z['helmHealth'] == target['helmHealth']
            near_ms(z['guardRepairMs'], target['abilityRemaining'])
    # Paid overdrive restoration does not pay again; production already credited stays credited.
    earned = restored['coldStorage']['workerIncome'] - before['coldStorage']['workerIncome']
    assert earned >= 0
    assert restored['coldStorage']['enemyIce'] == before['coldStorage']['enemyIce'] + earned

for mode in ('ready', 'preheat', 'retry', 'venting', 'spent'):
    folder = 'smoke_clockmaker_boiler_phases'
    before = read(folder, mode + '_recorded')['zombiesByType']['ZOMBIE_BOILER']
    after = read(folder, mode + '_restored')['zombiesByType']['ZOMBIE_BOILER']
    assert before['id'] == after['id']
    for key in ('boilerPhase', 'boilerSpent'):
        assert before[key] == after[key], (mode, key)
    for key in ('boilerRemainingMs', 'boilerRetryMs'):
        assert abs(before[key] - after[key]) <= 50, (mode, key, before[key], after[key])

print('PASS: all three Area 10 abilities survive rewind/save-load; boiler phases, economy and AI isolation agree.')
