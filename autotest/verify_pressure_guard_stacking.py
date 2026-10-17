"""Check uncapped diminishing protection, per-guard budgets and save identity."""
import json
from pathlib import Path

root = Path('build/clang-release/autotest/out/smoke_pressure_guard_stacking')


def read(name):
    """Load one completed fixture checkpoint."""
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


def guards(name):
    """Use stable identity instead of entity-array order."""
    return sorted((z for z in read(name)['zombies'] if z['type'] == 'ZOMBIE_PRESSURE_GUARD'),
                  key=lambda z: z['id'])


def bucket(name):
    """Select the sole protected armored ally."""
    return next(z for z in read(name)['zombies'] if z['type'] == 'ZOMBIE_BUCKET')


assert read('status')['status'] == 'passed'
assert len(guards('birth')) == 8
assert all(z['bodyHealth'] == 600 and z['helmHealth'] == 3000 for z in guards('birth'))
# 1000 -> transfers 800,100,50,25,12,6,3,2; the first guard accepts only 350.
losses = [350, 100, 50, 25, 12, 6, 3, 2]
assert [z['helmHealth'] for z in guards('ordinary')] == [3000 - n for n in losses]
assert bucket('ordinary')['helmHealth'] == 1098
# 1800 -> 360 -> 180 -> 90 -> 45 -> 23 -> 12 -> 6 -> 3; ash never transfers.
assert bucket('ash')['helmHealth'] == 1095 and bucket('ash')['bodyHealth'] == 270
assert [z['helmHealth'] for z in guards('ash')] == [3000 - n for n in losses]
# Hitting the first guard excludes itself; guard 2 caps at 350, the others share once.
assert [z['helmHealth'] for z in guards('peer')] == [2650, 2650, 2850, 2925, 2963, 2982, 2991, 2995]
assert [z['pressureWindowUsed'] for z in guards('peer')] == [350, 350, 150, 75, 37, 18, 9, 5]
fields = ('id', 'bodyHealth', 'helmHealth', 'pressureWindowUsed')
assert [tuple(z[k] for k in fields) for z in guards('restored')] == [
    tuple(z[k] for k in fields) for z in guards('peer')]
print('Eight-guard diminishing shares, independent caps, ash counts and save round-trip passed.')
