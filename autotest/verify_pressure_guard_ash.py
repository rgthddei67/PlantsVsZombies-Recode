"""Verify ash protection from actual damage and complete explosion transactions."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else
            'build/clang-release/autotest/out/smoke_pressure_guard_ash')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


def units(name, kind, row=None):
    return sorted((z for z in read(name)['zombies'] if z['type'] == 'ZOMBIE_' + kind
                   and (row is None or z['row'] == row)), key=lambda z: z['id'])


assert read('status')['status'] == 'passed'
assert units('protected', 'BOILER', 1)[0]['bodyHealth'] == 280, 'Two hits must deal 720, without stacking or a 600 cap'
assert units('protected', 'COLD_CHAIN_GUARD')[0]['helmHealth'] == 940, 'Subtype ash must be reduced exactly once'
assert not units('protected', 'BUCKET'), '360 still exceeds the 270-body char threshold'
assert [z['helmHealth'] for z in units('protected', 'PRESSURE_GUARD')] == [2700, 2700]
assert all(z['pressureWindowUsed'] == 0 for z in units('protected', 'PRESSURE_GUARD')), 'Ash must not transfer or consume ordinary budget'
assert not units('range', 'BOILER', 4) and not units('range', 'BOILER', 2)
assert not units('controlled', 'BOILER', 3)
assert units('other_faction', 'BOILER')[0]['bodyHealth'] == 500
assert units('same_faction', 'BOILER')[0]['bodyHealth'] == 400
assert units('broken', 'PRESSURE_GUARD')[0]['helmHealth'] == 0
assert not units('broken_ally', 'BOILER')
assert not units('same_blast', 'PRESSURE_GUARD')
assert [z['bodyHealth'] for z in units('same_blast', 'BOILER')] == [640, 640], 'Source dying first must protect both allies'
assert not units('later_blast', 'BOILER', 2), 'Frozen protection must end with its attack'
assert units('later_blast', 'BOILER', 1)[0]['bodyHealth'] == 640
print('Pressure ash: actual damage, char thresholds, subtype, range, control, factions and transaction lifetime passed')
