"""Verify mower execution and untouched protection by stable entity identity."""
import json
from pathlib import Path

base = Path('build/clang-release/autotest/out/smoke_pressure_guard_mower')
before = json.loads((base / 'before.json').read_text(encoding='utf-8'))
after = json.loads((base / 'after.json').read_text(encoding='utf-8'))
assert json.loads((base / 'status.json').read_text(encoding='utf-8'))['status'] == 'passed'
units = {z['row']: z for z in before['zombies']}
assert len(units) == 4
assert units[0]['helmHealth'] == units[2]['helmHealth'] == 3900
assert units[0]['pressureWindowUsed'] == units[2]['pressureWindowUsed'] == 600
assert units[1]['helmHealth'] == 4500
assert units[1]['pressureProtectorID'] == units[2]['id']
assert units[3]['helmHealth'] == 1100
assert len(after['zombies']) == 1
survivor = after['zombies'][0]
assert survivor['id'] == units[2]['id']
assert survivor['bodyHealth'] == 600 and survivor['helmHealth'] == 3900
print('Mowers execute guards and protected allies without transferring damage to the untouched guard.')
