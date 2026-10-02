"""Check paid replacement growth and exact interference snapshot preservation."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    'build/clang-release/autotest/out/smoke_cold_storage_player_recovery')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
replacement = [p for p in read('paid_replacement')['plants']
               if p['type'] == 'PLANT_ELITE_SCAREDYSHROOM'
               and p['row'] == 0 and p['col'] == 0 and p['health'] > 0]
assert len(replacement) == 1
assert replacement[0]['growthProgressTenths'] == 0
assert replacement[0]['growthShots'] == 0
before = read('interference_before_save')['coldStorage']
after = read('interference_restored')['coldStorage']
for field in ('interferenceRemaining', 'interferenceCooldownRemaining', 'playerIce'):
    assert before[field] == after[field], field
print('PASS: paid replacement starts with no growth; interference timers and wallet survive save/load exactly.')
