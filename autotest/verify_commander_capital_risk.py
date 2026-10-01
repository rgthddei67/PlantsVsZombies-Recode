"""Verify capital-risk fixtures, forecast isolation and actual battle ledgers."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_capital_risk')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


def ledger(state):
    ice = state['coldStorage']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
opening = read('opening')
before, after = read('growth-before'), read('growth-planned')
for state in (opening, before, after):
    ledger(state)
    assert state['testAudio']['muted']
assert opening['coldStorage']['searchRowStrikeCount'] == 1
assert opening['coldStorage']['commanderSpent'] <= 200
ice = after['coldStorage']
assert ice['searchGrowingPlants'] == 3 and ice['searchAttackAuraCount'] == 1
assert ice['planningApplied'] == 1 and ice['planningDiscarded'] == 0
assert ice['commanderSpent'] <= 200
assert ice['playerIce'] == before['coldStorage']['playerIce'], 'forecast must not buy real pineapple activations'
assert before['coldStorage']['enemyIce'] - ice['enemyIce'] == ice['commanderSpent']
for state in (before, after):
    elites = [p for p in state['plants'] if p['type'] == 'PLANT_ELITE_SCAREDYSHROOM']
    assert len(elites) == 3
    assert all(p['growthShots'] == 0 and p['puffDamage'] == 8 and p['shootIntervalMs'] == 1500 for p in elites)
for level in (83, 85):
    episode = read(f'battle-{level}')
    ledger(episode['initial'])
    ledger(episode['final'])
    assert not episode['externalSun']['enabled']
    assert episode['outcome'] in ('commander_win', 'player_win', 'timeout')
    print(f"{level}: {episode['outcome']}, first purchase={episode['initial']['coldStorage']['commanderSpent']}, "
          f"production={episode['final']['coldStorage']['workerIncome']}")
print('PASS: capital-risk fixtures, growing defense, unchanged live growth/wallet and battle accounting.')
