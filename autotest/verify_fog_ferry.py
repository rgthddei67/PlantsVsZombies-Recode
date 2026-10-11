"""Verify real entity movement and snapshot continuity by stable zombie IDs."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/clang-release/autotest/out/smoke_fog_ferry')
def read(name):
    return json.loads((root / name).read_text(encoding='utf-8'))
def entities(state):
    return {z['id']: z for z in state['zombies']}
def ferry(state):
    return max((z for z in state['zombies'] if z['type'] == 'ZOMBIE_FOG_FERRY'), key=lambda z: z['id'])

assert read('status.json')['status'] == 'passed'
before, after = read('clear_before.json'), read('clear_after.json')
a, b = entities(before), entities(after)
assert set(a) == set(b), 'Transport must preserve entity IDs and population'
owner = ferry(after)
passengers = [z for z in b.values() if z['type'] in ('ZOMBIE_NORMAL', 'ZOMBIE_GARGANTUAR')]
assert len(passengers) == 17
assert owner['ferryMovedCount'] == 17
assert all(owner['x'] - z['x'] > 120 for z in passengers), 'Whole group must arrive ahead of owner'
assert max(z['x'] for z in passengers) - min(z['x'] for z in passengers) < 12, 'Compact landing must not spread beyond the destination'
for z in b.values():
    if z['type'] in ('ZOMBIE_BALLOON', 'ZOMBIE_DIGGER', 'ZOMBIE_BUCKET', 'ZOMBIE_FOG_FERRY'):
        assert abs(z['x'] - a[z['id']]['x']) < 25, 'Excluded unit moved: ' + z['type']
    assert z['bodyHealth'] == a[z['id']]['bodyHealth'], 'Teleport must not add or remove health'
restored = ferry(read('clear_restored.json'))
assert restored['ferryUses'] == owner['ferryUses'] == 1
assert abs(restored['ferryRemainingMs'] - owner['ferryRemainingMs']) < 200
assert ferry(read('clear_repeat.json'))['ferryUses'] == 2

fog_before, fog_after = read('fog_before.json'), read('fog_after.json')
fb, fa = entities(fog_before), entities(fog_after)
fog_owner = ferry(fog_after)
assert fog_owner['ferryMovedCount'] == 2 and fog_owner['ferryUses'] == 1
for z in fa.values():
    if z['type'] in ('ZOMBIE_ZAMBONI', 'ZOMBIE_CATAPULT'):
        assert z['id'] in fb and fog_owner['x'] - z['x'] > 280
lit = ferry(read('illuminated_after.json'))
assert lit['ferryUses'] == 1 and lit['ferryChannelRatePct'] == 50
cancelled = read('cancelled.json')
assert not any(z['type'] == 'ZOMBIE_FOG_FERRY' for z in cancelled['zombies'])
assert all(z['x'] > 650 for z in cancelled['zombies']), 'Cancelled channel transported a passenger'
dead = read('death_finished.json')
assert not any(z['type'] == 'ZOMBIE_FOG_FERRY' for z in dead['zombies']), 'Death animation failed to recycle'
print('Fog ferry: stable IDs, 17 passengers, exclusions, vehicles, light, repeat, save and death verified')
