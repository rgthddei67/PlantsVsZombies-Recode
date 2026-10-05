"""Verify current-wallet capital projection and real paid wave advancement."""
import argparse
import json
from pathlib import Path
from verify_commander_league import capital_utility_scale, effective_weights


def verify(folder, policy_path):
    """Recompute weights from independent inputs, then check actual troop eligibility and payment."""
    read = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')
    before, after = read('before'), read('after')
    b, a = before['coldStorage'], after['coldStorage']
    assert b['initialEnemyIce'] == b['enemyIce'] == 1875 and not b['pending']
    assert b['decisions'] == 0 and a['decisions'] == 1 and a['planningApplied'] == 1
    inputs = a['searchCapitalInputs']
    assert inputs['budget'] == 1875 and inputs['highestAffordableTroopCost'] == 4
    assert inputs['fundableUnlockTroopCost'] == 35
    scale = capital_utility_scale(inputs)
    assert scale == 1, 'the cheap startup pool must not devalue capital needed for affordable unlocks'
    policy = json.loads(policy_path.read_text(encoding='utf-8'))
    expected = effective_weights(policy, a['searchStateInputs'], scale)
    assert all(abs(x-y) < .002 for x, y in zip(expected, a['searchEffectiveWeights']))
    assert a['pending'] and all(p['typeName'] == 'ZOMBIE_NORMAL' for p in a['pending'])
    assert sum(p['cost'] for p in a['pending']) > 0
    assert b['enemyIce'] - a['enemyIce'] == a['spent'] - b['spent']
    for c in (b, a):
        assert c['enemyIce'] == c['initialEnemyIce'] + c['supplied'] + c['workerIncome'] + c['killIncome'] - c['spent']
    report = {'passed': True, 'cashScaleIndependentlyVerified': scale,
              'actualPaidWaveAdvanced': True, 'futureTroopsNotPrematurelyPurchased': True,
              'battleStrengthVerified': False}
    (folder / 'opening_capital_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--policy', type=Path, default=Path('build/clang-release/resources/ai/cold_storage_policy.json'))
    args = parser.parse_args()
    verify(args.folder, args.policy)
