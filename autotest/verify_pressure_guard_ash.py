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
assert units('protected', 'BOILER', 1)[0]['bodyHealth'] == 640, 'Two guards reduce each 1800 ash to 180, without charging their caps'
assert units('protected', 'COLD_CHAIN_GUARD')[0]['helmHealth'] == 1120, 'Subtype ash must be reduced exactly once'
assert units('protected', 'BUCKET')[0]['bodyHealth'] == 270
assert units('protected', 'BUCKET')[0]['helmHealth'] == 920
assert units('protected', 'DOOR')[0]['bodyHealth'] == units('birth', 'DOOR')[0]['bodyHealth']
assert units('protected', 'DOOR')[0]['shieldHealth'] == units('birth', 'DOOR')[0]['shieldHealth'] - 180
assert units('armor_overflow', 'BUCKET')[0]['helmHealth'] == 0
assert units('armor_overflow', 'BUCKET')[0]['bodyHealth'] == 170
assert not units('armor_lethal', 'BUCKET'), 'Exact lethal overflow must still char the body'
assert [z['helmHealth'] for z in units('protected', 'PRESSURE_GUARD')] == [2900, 2900]
assert all(z['pressureWindowUsed'] == 600 for z in units('protected', 'PRESSURE_GUARD')), 'Each guard ash consumes its own shared budget'
assert units('capped_huge', 'PRESSURE_GUARD')[0]['helmHealth'] == 2900
assert units('cap_expired', 'PRESSURE_GUARD')[0]['helmHealth'] == 2300
assert units('cap_reloaded', 'PRESSURE_GUARD')[0]['helmHealth'] == 2300
assert units('cap_reloaded', 'PRESSURE_GUARD')[0]['pressureWindowUsed'] == 600
assert not units('range', 'BOILER', 4) and not units('range', 'BOILER', 2)
assert not units('controlled', 'BOILER', 3)
assert units('other_faction', 'BOILER')[0]['bodyHealth'] == 500
assert units('same_faction', 'BOILER')[0]['bodyHealth'] == 400
assert units('broken', 'PRESSURE_GUARD')[0]['helmHealth'] == 0
assert not units('broken_ally', 'BOILER')
assert all(not z['pressureCanProtect'] for z in units('same_blast', 'PRESSURE_GUARD'))
assert units('same_blast', 'BUCKET')[0]['helmHealth'] == 740
assert units('same_blast', 'BUCKET')[0]['bodyHealth'] == 270
assert not units('later_blast', 'BUCKET'), 'Without protection, 500 ash must bypass the remaining helmet'
assert [z['bodyHealth'] for z in units('same_blast', 'BOILER')] == [640, 640], 'Source losing its shield must still protect both allies in that blast'
assert not units('later_blast', 'BOILER', 2), 'Frozen protection must end with its attack'
assert units('later_blast', 'BOILER', 1)[0]['bodyHealth'] == 640
print('Pressure ash: actual damage, char thresholds, subtype, range, control, factions and transaction lifetime passed')
