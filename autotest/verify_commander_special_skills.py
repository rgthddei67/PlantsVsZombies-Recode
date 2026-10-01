"""Check actual precision payment/identity and forecast-only specialist side effects."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_special_skills')
def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
assert read('low-value')['coldStorage']['searchPrecisionTargetID'] == 0
before, planned, resolved = (read(name) for name in ('strike-before','strike-after','strike-resolved'))
b, p, r = (d['coldStorage'] for d in (before, planned, resolved))
target = p['searchPrecisionTargetID']
assert target > 0 and p['strikeTargetID'] == target and p['searchPrecisionGain'] > 0
plant = next(plant for plant in before['plants'] if plant['id'] == target)
assert plant['type'] in ('PLANT_MELONPULT', 'PLANT_DAWNLOTUS')
assert b['enemyIce'] - p['enemyIce'] == p['commanderSpent'] == p['spent'] - b['spent'] == 60
assert p['playerIce'] == b['playerIce']
assert any(plant['id'] == target for plant in planned['plants']), 'aim must not delete immediately'
assert not any(plant['id'] == target for plant in resolved['plants'])
assert r['strikeTargetID'] == -1 and r['spent'] == p['spent'], 'impact must not charge again'
before, after = read('abilities-before'), read('abilities-after')
b, a = before['coldStorage'], after['coldStorage']
assert a['searchDrumOptions'] > 0 and a['searchAdaptationOptions'] > 0 and a['searchRitualOptions'] > 0
assert a['searchDrumBeats'] > 0 and a['searchDrumRecipients'] > 0
assert a['searchRitualReleases'] == 3 and a['searchRiftSummons'] == 9
assert a['searchArmorRepairs'] > 0 and a['searchArmorRepairIce'] == a['searchArmorRepairs']
assert (b['enemyIce'], b['playerIce'], b['spent']) == (a['enemyIce'], a['playerIce'], a['spent'])
assert before['zombies'] == after['zombies'], 'forecast must not change live units or cast abilities'
print('PASS: valuable strike uses one paid transaction; specialist forecasts preserve the live world.')
