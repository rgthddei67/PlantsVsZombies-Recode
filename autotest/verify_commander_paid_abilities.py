"""Verify formal policy routing, capability projection and the real payment boundary."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_paid_abilities')
assert json.loads((root / 'status.json').read_text())['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
for level in (84, 85, 2001):
    before = json.loads((root / f'before-{level}.json').read_text(encoding='utf-8'))['coldStorage']
    after = json.loads((root / f'planned-{level}.json').read_text(encoding='utf-8'))['coldStorage']
    assert after['commanderStrategy'] == 'learned_search'
    assert not after['trainingAllUnits']
    assert after['searchBurstOptions'] == 5 and after['searchAttackAuraCount'] == 1
    assert after['searchBurstActivations'] > 0 and after['searchAuraActivations'] > 0
    assert after['searchAbilityIce'] == after['searchBurstActivations'] * 5 + after['searchArmorRepairIce'] + (60 if after['searchPrecisionTargetID'] else 0)
    assert before['enemyIce'] - after['enemyIce'] == after['commanderSpent']
    assert after['spent'] - before['spent'] == after['commanderSpent']
    assert after['playerIce'] == before['playerIce'], 'forecast aura payments must not touch the live wallet'
    assert after['planningApplied'] == 1 and after['planningDiscarded'] == 0
    print(f"{level}: forecast skill fee={after['searchAbilityIce']:.0f}, "
          f"real purchase={after['commanderSpent']}, worker={after['planningWorkerMs']:.2f} ms")
print('PASS: new abilities participate in the shipped policy without spending forecast fees in the live game.')
