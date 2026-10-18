"""Verify real damage sharing under individual, overlapping and kelp controls."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else
            'build/clang-release/autotest/out/smoke_pressure_guard_control')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


def unit(state, kind, row):
    """Select an unambiguous type/row fixture without depending on array order."""
    matches = [z for z in state['zombies'] if z['type'] == 'ZOMBIE_' + kind and z['row'] == row]
    assert len(matches) == 1, (kind, row, matches)
    return matches[0]


assert read('status')['status'] == 'passed'
for name, controls in (
    ('frozen', ('frozen',)),
    ('butter', ('buttered',)),
    ('paralysis', ('paralyzed',)),
    ('combined', ('frozen', 'buttered', 'paralyzed')),
    ('kelp', ('tangleKelpTarget',)),
):
    before, after = read(name + '_before'), read(name + '_after')
    guard_before = unit(before, 'PRESSURE_GUARD', 2)
    guard = unit(after, 'PRESSURE_GUARD', 2)
    assert all(guard_before[field] and guard[field] for field in controls), name
    assert guard_before['pressureCanProtect'] and guard['pressureCanProtect'], name
    assert guard['id'] == guard_before['id'], name
    assert guard['helmHealth'] == guard_before['helmHealth'] - 80, name
    assert guard['bodyHealth'] == guard_before['bodyHealth'], name
    # Freezing itself deals damage; measure only the added shared damage against the controlled baseline.
    assert guard['pressureWindowUsed'] == guard_before['pressureWindowUsed'] + 80, name
    ally_before, ally = unit(before, 'BUCKET', 1), unit(after, 'BUCKET', 1)
    assert ally['id'] == ally_before['id'] and ally['pressureProtectorID'] == guard['id'], name
    assert ally['helmHealth'] == ally_before['helmHealth'] - 40, name
    assert ally['bodyHealth'] == ally_before['bodyHealth'], name
    outside_before, outside = unit(before, 'BUCKET', 4), unit(after, 'BUCKET', 4)
    assert outside['id'] == outside_before['id'], name
    assert outside['helmHealth'] == outside_before['helmHealth'] - 100, name

print('Pressure guard control: frozen, butter, paralysis, overlap and kelp sharing/ash/range passed')
