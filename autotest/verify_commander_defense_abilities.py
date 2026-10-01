"""Formal defense-unit eligibility and forecast-only repair transactions."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out/smoke_commander_defense_abilities')
assert json.loads((root/'status.json').read_text())['status']=='passed'
assert 'script finished OK' in (root/'run.log').read_text(encoding='utf-8')
for level in (86,87,2001):
    before=json.loads((root/f'before-{level}.json').read_text(encoding='utf-8'))
    after=json.loads((root/f'planned-{level}.json').read_text(encoding='utf-8'))
    b,a=before['coldStorage'],after['coldStorage']
    assert a['commanderStrategy']=='learned_search' and not a['trainingAllUnits']
    assert a['searchRepairOptions']==5 and a['searchRepairPlants']==1
    assert a['searchArmorRepairs']>0 and a['searchPlantRepairs']>0
    assert a['searchArmorRepairIce']==a['searchArmorRepairs']*4
    assert a['searchPlantRepairIce']==a['searchPlantRepairs']*20
    assert a['searchAbilityIce']==a['searchArmorRepairIce']+a['searchBurstActivations']*5
    assert b['enemyIce']-a['enemyIce']==a['commanderSpent']==a['spent']-b['spent']
    assert b['playerIce']==a['playerIce']
    assert after['zombiesByType']['ZOMBIE_COLD_CHAIN_GUARD']['helmHealth']==1400
    assert after['iceStorageNutsByCell']['0_6']['health']==7000
    print(f"{level}: forecast repairs {a['searchArmorRepairs']} guard / {a['searchPlantRepairs']} nut; real purchase {a['commanderSpent']} ice")
print('PASS: paid forecasts leave real health and ability wallets untouched.')
