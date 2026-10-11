"""Verify Area 11 ferry availability, paid birth projection and real transport by ID.

This deliberately seeded fixture checks contracts, not autonomous strength.
"""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else
            'build/clang-release/autotest/out/smoke_commander_fog_ferry')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


def ferries(state):
    return {z['id']: z for z in state['zombies'] if z['type'] == 'ZOMBIE_FOG_FERRY'}


assert read('status')['status'] == 'passed'
assert not any(o['type'] == 'ZOMBIE_FOG_FERRY' for o in read('before_unlock')['legalOptions'])
birth = read('birth_options')
options = [o for o in birth['legalOptions'] if o['type'] == 'ZOMBIE_FOG_FERRY']
assert options and all(o['cost'] == 40 and o['birthFerry'] and
                       o['birthFerryRemaining'] == 5 and o['birthBodyHealth'] == 2200 for o in options)
proposal = next(p for p in birth['candidatePlans'] if p['name'] == 'ferry_vehicle')
assert proposal['legal'], proposal
assert proposal['ferryPassengers'] > 0, 'Purchased transport must affect a complete combat world'

before, after = read('before_paid')['coldStorage'], read('after_paid')['coldStorage']
assert before['enemyIce'] - after['enemyIce'] == 40, 'Real queue entry must cost exactly forty'
assert after['spent'] - before['spent'] == 40
assert after['pendingCount'] == before['pendingCount'] + 1
pending = read('pending_ferry')['ferryCurrent']
assert len(pending) == 1 and pending[0]['spawnAt'] == 10 and pending[0]['remaining'] == 5
assert pending[0]['enabled'] and not pending[0]['winding']
live = read('live_ferry')['ferryCurrent']
assert any(u['spawnAt'] == 0 and 4 < u['remaining'] <= 5 for u in live), live

channel, transported = read('channel'), read('transported')
source_id = max(ferries(channel))
source = ferries(channel)[source_id]
assert source['ferryPhase'] == 1, source
projection = next(u for u in read('channel_ferry')['ferryCurrent'] if u['id'] == source_id)
assert projection['winding'] and abs(projection['remaining'] * 1000 - source['ferryRemainingMs']) < 80
owner = ferries(transported)[source_id]
assert owner['ferryUses'] == source['ferryUses'] + 1 and owner['ferryMovedCount'] == 2
assert abs(owner['x'] - source['x']) < 1, 'Stationary source is not a passenger'
passengers = [z for z in channel['zombies'] if z['row'] == source['row'] and
              z['type'] in ('ZOMBIE_ZAMBONI', 'ZOMBIE_GARGANTUAR')]
arrivals = {z['id']: z for z in transported['zombies']}
assert len(passengers) == 2
for passenger in passengers:
    arrived = arrivals[passenger['id']]
    assert passenger['x'] - arrived['x'] > 300
    assert arrived['bodyHealth'] == passenger['bodyHealth']
cooldown = next(u for u in read('cooldown_ferry')['ferryCurrent'] if u['id'] == source_id)
assert not cooldown['winding'] and 7 < cooldown['remaining'] <= 10
print('Fog ferry: unlock, real price, unborn/live/channel/cooldown projection and transport passed')
