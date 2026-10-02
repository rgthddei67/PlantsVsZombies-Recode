"""Verify ice-defense fixtures and paired worker ablations without prescribing a winning composition."""
import argparse
import json
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def verified_output(folder):
    """Require a complete game run and inspect controller failures before interpreting victories."""
    assert read(folder / 'status.json')['status'] == 'passed'
    log = (folder / 'run.log').read_text(encoding='utf-8')
    assert 'script finished OK' in log and 'FAIL' not in log
    failures = [line for line in log.splitlines() if 'rejected:' in line and not (
        line.endswith('collect_sun rejected: sun_unavailable') or line.endswith('player_shovel rejected: no_shovel_target'))]
    assert not failures, failures[:1]
    return log


def verify(setup, ablation):
    """Check legal inputs, actual payment and unchanged ledger; report whether workers changed outcomes."""
    log = verified_output(setup)
    assert 'player repaired ice storage nut' in log
    repair = read(setup / 'repair_input.json')
    before, after = repair['initial'], repair['final']
    nut = next(p for p in after['plants'] if p['type'] == 'PLANT_ICESTORAGENUT')
    assert nut['health'] == nut['maxHealth'] and nut['nutCooldownMs'] > 0
    planting_cost = sum(before['coldStorage']['plantCosts'][kind] * count
                        for kind, count in repair['playerPlantings'].items())
    assert before['coldStorage']['playerIce'] - after['coldStorage']['playerIce'] == 20 + planting_cost
    verified_output(ablation)
    rows = []
    opponents = ['ice_fortifier', 'ice_pine', 'ice_bunker']
    if (ablation / 'case_3_full.json').exists(): opponents.append('ice_pine')
    for index, opponent in enumerate(opponents):
        modes = {}
        for mode in ('full', 'no_workers'):
            result = read(ablation / f'case_{index}_{mode}.json')
            initial, final = result['initial'], result['final']
            ice = final['coldStorage']
            assert result['opponent'] == opponent and not result['externalSun']['enabled']
            assert final['testAudio']['muted'] and len(initial['cards']) <= 11
            assert sum(p['type'] == 'PLANT_ICESTORAGENUT' for p in initial['plants']) == 5
            if index == 3:
                grown = [p for p in initial['plants'] if p['type'] == 'PLANT_ELITE_SCAREDYSHROOM']
                assert len(grown) == 4 and all(p['growthShots'] >= 39 and p['shootIntervalMs'] <= 250 for p in grown)
                assert not initial['zombies'], 'Preparation targets must not remain in the battle'
            assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['killIncome'] + ice['workerIncome'] - ice['spent']
            workers = ice['deploymentTypes'].get('ZOMBIE_ICE_WORKER', 0)
            if mode == 'no_workers':
                assert workers == 0 and 'ZOMBIE_ICE_WORKER' not in initial['coldStorage']['availableUnits']
            modes[mode] = {'outcome': result['outcome'], 'seconds': result['seconds'], 'workers': workers,
                           'production': ice['workerIncome'], 'reserve': ice['enemyIce'], 'spent': ice['spent']}
        rows.append({'opponent': opponent, **modes,
                     'economicDependenceSeen': modes['full']['outcome'] == 'commander_win'
                     and modes['full']['workers'] > 0 and modes['no_workers']['outcome'] != 'commander_win'})
    report = {'cases': rows, 'economicDependenceCases': sum(r['economicDependenceSeen'] for r in rows)}
    (ablation / 'verified_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('setup', type=Path)
    parser.add_argument('ablation', type=Path)
    args = parser.parse_args()
    verify(args.setup, args.ablation)
