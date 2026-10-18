"""Check pressure guard contracts by stable identity/row, including the paid AI queue."""
import json
import sys
from pathlib import Path

BASE = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/clang-release/autotest/out')


def read(suite, name):
    return json.loads((BASE / suite / (name + '.json')).read_text(encoding='utf-8'))


def zombies(state, kind, row=None):
    return sorted((z for z in state['zombies'] if z['type'] == 'ZOMBIE_' + kind
                   and (row is None or z['row'] == row)), key=lambda z: z['id'])


def check_entity():
    """Verify actual damage, lifecycle and persistence, rather than constructor-only values."""
    load = lambda name: read('smoke_pressure_guard', name)
    assert load('status')['status'] == 'passed'
    birth = zombies(load('birth'), 'PRESSURE_GUARD')
    assert len(birth) == 2
    assert all(z['bodyHealth'] == 600 and z['helmHealth'] == 3500
               and z['pressureResourcesReady'] and z['pressureEquipmentVisible'] for z in birth)
    shared = load('shared')
    guards = zombies(shared, 'PRESSURE_GUARD')
    assert [z['helmHealth'] for z in guards] == [2900, 2900], guards
    assert guards[0]['pressureWindowUsed'] == 600
    for row in (1, 3):
        z = zombies(shared, 'BUCKET', row)[0]
        assert z['helmHealth'] == 1000 and z['pressureProtectorID'] == guards[0]['id'], z
    assert zombies(shared, 'BUCKET', 2)[0]['helmHealth'] == 1000
    for name in ('reloaded', 'same_window'):
        restored = zombies(load(name), 'PRESSURE_GUARD')
        assert [(z['id'], z['helmHealth']) for z in restored] == [(z['id'], z['helmHealth']) for z in guards]
        assert restored[0]['pressureWindowUsed'] == 600
    assert zombies(load('next_window'), 'PRESSURE_GUARD')[0]['helmHealth'] == 2500
    assert zombies(load('tiny_hits'), 'PRESSURE_GUARD')[0]['helmHealth'] == 2496
    assert zombies(load('tiny_hits'), 'BUCKET', 1)[0]['helmHealth'] == 949
    controlled = zombies(load('controlled'), 'PRESSURE_GUARD')
    assert controlled[0]['pressureCanProtect'] and controlled[0]['paralyzed']
    assert controlled[1]['helmHealth'] == zombies(load('tiny_hits'), 'PRESSURE_GUARD')[1]['helmHealth'] - 50
    assert zombies(load('controlled'), 'BUCKET', 3)[0]['helmHealth'] == zombies(shared, 'BUCKET', 3)[0]['helmHealth'] - 50
    survivor = zombies(load('ash_survivor'), 'PRESSURE_GUARD')[0]
    assert survivor['helmHealth'] == 2900 and survivor['bodyHealth'] == 600
    assert survivor['pressureWindowUsed'] == 600 and survivor['pressureCanProtect']
    assert zombies(load('ash_followup'), 'PRESSURE_GUARD')[0]['helmHealth'] == 2900
    assert zombies(load('ash_followup'), 'BUCKET')[0]['helmHealth'] == 1000
    assert zombies(load('ash_ally'), 'BUCKET')[0]['helmHealth'] == 640
    assert zombies(load('ash_ally'), 'BUCKET')[0]['bodyHealth'] == 270
    assert zombies(load('almost_broken'), 'PRESSURE_GUARD')[0]['helmHealth'] == 100
    broken = zombies(load('broken'), 'PRESSURE_GUARD')[0]
    assert broken['helmHealth'] == 0 and broken['bodyHealth'] == 550 and broken['pressureStage'] == 3
    assert not broken['pressureCanProtect']
    assert zombies(load('broken_damage'), 'PRESSURE_GUARD')[0]['bodyHealth'] == 350
    assert zombies(load('broken_damage'), 'PRESSURE_GUARD')[0]['pressureWindowUsed'] == 550
    assert not zombies(load('ash_dead'), 'PRESSURE_GUARD')
    healer = load('healer_ignores_shield')
    assert zombies(healer, 'PRESSURE_GUARD')[0]['helmHealth'] == 2900
    assert zombies(load('charmed_other_faction'), 'BUCKET')[0]['helmHealth'] == 1000
    assert zombies(load('charmed_same_faction'), 'BUCKET')[0]['helmHealth'] == 980
    assert zombies(load('charmed_same_faction'), 'PRESSURE_GUARD')[0]['helmHealth'] == 2820


def check_commander():
    """Check formal eligibility, shared-wallet payment and all three forecast origins."""
    load = lambda name: read('smoke_commander_pressure_guard', name)
    assert load('status')['status'] == 'passed'
    assert not any(o['type'] == 'ZOMBIE_PRESSURE_GUARD' for o in load('wave7')['legalOptions'])
    options = [o for o in load('wave8')['legalOptions'] if o['type'] == 'ZOMBIE_PRESSURE_GUARD']
    assert options and all(o['cost'] == 35 and o['birthPressureGuard'] and o['birthBodyHealth'] == 600
                           and o['birthHelmHealth'] == 3500 for o in options)
    before, after = load('before_paid')['coldStorage'], load('after_paid')['coldStorage']
    assert before['enemyIce'] - after['enemyIce'] == 35 and after['spent'] - before['spent'] == 35
    queued = load('queued')['pressureGuards']
    assert len(queued) == 1 and queued[0]['spawnAt'] > 0 and queued[0]['shield'] == 3500
    live = load('live')['pressureGuards']
    assert len(live) == 1 and live[0]['spawnAt'] == 0 and live[0]['shield'] == 2900 and live[0]['windowUsed'] == 600


check_entity()
check_commander()
print('Pressure guard: shared cap, rolling/save boundaries, range, hard control, ash, broken armor, heal, charm and AI purchase/projection passed')
