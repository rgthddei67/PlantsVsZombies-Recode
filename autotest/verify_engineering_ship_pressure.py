"""Generate and check natural repeated reinforcements, volleys and occupied-lane selection."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'smoke_engineering_ship_pressure'
SHIP = 'ZOMBIE_ENGINEERING_SHIP'
OUT = ROOT / 'build/clang-release/autotest/out' / NAME


def generate():
    commands = []

    def add(op, **kw):
        commands.append(dict(op=op, **kw))

    def check(path, value):
        add('assert_state', path=path, equals=value)

    def ship(key, value):
        check('engineeringShip.' + key, value)

    def pause():
        add('set_timescale', value=0)

    def wait(key, value):
        add('set_timescale', value=2)
        add('wait_value', path='engineeringShip.' + key, equals=value, timeout=45)
        pause()

    def dump(name):
        add('dump_state', name=name + '.json')

    def reload(name):
        add('save_level_snapshot', name=name)
        add('reload_level_snapshot', name=name)
        add('wait_state', state='GAME', timeout=30)
        add('set_spawn_paused', value=True)
        pause()
        dump(name + '_reload')

    def plant(row, col, kind='PLANT_WALLNUT'):
        if row in (2, 3) and 3 <= col <= 5:
            add('plant', type='PLANT_LILYPAD', row=row, col=col)
        add('plant', type=kind, row=row, col=col)

    def setup(final=True):
        add('goto_level', level=108, resetTestState=True)
        add('choose_cards', cards=[])
        add('wait_state', state='GAME', timeout=30)
        add('set_spawn_paused', value=True)
        add('set_weather', intensity='CLEAR', duration=600)
        pause()
        if final:
            add('set_wave_spawn_list', types=['ZOMBIE_NORMAL'])
            add('summon_next_wave', count=40, clearPrevious=True)
            check('zombieCount', 7)
            plant(3, 1)

    setup()
    for shot, guards in ((1, 6), (2, 12), (3, 17), (4, 18), (5, None)):
        wait('shipShotCount', shot)
        if guards is not None:
            ship('shipReinforcementCount', guards)
            check('zombieCount', 7 + guards)
        else:
            # Mowers may have killed earlier guards by now; the cap is simultaneous, not cumulative.
            add('assert_state', path='engineeringShip.shipReinforcementCount', atLeast=18)
            add('assert_state', path='zombieCount', atMost=25)
        ship('row', 3)  # Empty adjacent lanes must not pull the ship away from its target.
        dump('regular_' + str(shot))
        if shot == 2:
            reload('regular_2')
    add('screenshot', name='repeated_guards.png')

    setup()
    plant(3, 4, 'PLANT_SUNFLOWER')
    add('damage_zombie', type=SHIP, damage=24002)
    ship('shipReinforcementCount', 6)
    wait('shipPhase', 'CHARGING')
    ship('shipVolleyShotsRemaining', 2)
    add('screenshot', name='double_cannon_warning.png')
    wait('shipShotCount', 1)
    ship('shipVolleyShotsRemaining', 1)
    ship('shipCoolingMs', 850)
    ship('shipReinforcementCount', 6)
    dump('first_shot')
    reload('first_shot')
    # First hit destroys the sunflower; remove its exposed lily to make the second lock observable.
    add('shovel_plant_at', row=3, col=4)
    wait('shipPhase', 'CHARGING')
    ship('shipTargetColumn', 1)
    ship('shipVolleyShotsRemaining', 1)
    add('assert_state', path='engineeringShip.shipPhaseRemainingMs', atLeast=3900)
    dump('followup_warning')
    add('screenshot', name='followup_warning.png')
    reload('followup_warning')
    add('set_timescale', value=1)
    add('wait_seconds', value=3.3)
    pause()
    ship('shipShotCount', 1)
    wait('shipShotCount', 2)
    ship('shipVolleyShotsRemaining', 0)
    ship('shipCoolingMs', 6000)
    ship('shipReinforcementCount', 14)
    dump('second_shot')
    reload('second_shot')
    wait('shipPhase', 'CHARGING')
    ship('shipVolleyShotsRemaining', 2)
    add('interrupt_zombie_special_action', type=SHIP, expectedChanged=True)
    ship('shipVolleyShotsRemaining', 0)
    ship('shipCoolingMs', 6000)
    add('set_timescale', value=1)
    add('wait_seconds', value=4.5)
    pause()
    ship('shipShotCount', 2)
    ship('shipReinforcementCount', 14)
    dump('interrupted')
    # A separate branch from the saved first impact verifies cancellation of the followup itself.
    add('reload_level_snapshot', name='first_shot')
    add('wait_state', state='GAME', timeout=30)
    add('set_spawn_paused', value=True)
    wait('shipPhase', 'CHARGING')
    add('interrupt_zombie_special_action', type=SHIP, expectedChanged=True)
    ship('shipVolleyShotsRemaining', 0)
    reload('followup_interrupted')
    add('set_timescale', value=1)
    add('wait_seconds', value=4.5)
    pause()
    ship('shipShotCount', 1)
    ship('shipReinforcementCount', 6)

    setup(False)
    plant(0, 1)
    for _ in range(24):
        add('spawn_zombie', type='ZOMBIE_NORMAL', row=5, x=1000, stationary=True)
    add('spawn_zombie', type=SHIP, row=0, x=915)
    wait('shipShotCount', 1)
    ship('shipReinforcementCount', 0)
    add('kill_zombie', row=5, index=0)
    wait('shipShotCount', 2)
    ship('shipReinforcementCount', 1)
    check('zombieCount', 25)
    dump('cap_refilled')

    setup(False)
    plant(1, 1)
    add('spawn_zombie', type=SHIP, row=0, x=915)
    wait('shipLaneSwitchCount', 1)
    ship('shipLaneDestinationRow', 1)
    wait('shipPhase', 'CHARGING')
    ship('row', 1)
    ship('shipTargetColumn', 1)
    dump('occupied_lane')
    add('quit')
    path = ROOT / 'autotest/scripts' / (NAME + '.json')
    path.write_text('{\n  "commands": [\n' + ',\n'.join(
        '    ' + json.dumps(c, ensure_ascii=False) for c in commands) + '\n  ]\n}\n', encoding='utf-8')


def verify():
    def read(name):
        return json.loads((OUT / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    assert 'script finished OK' in (OUT / 'run.log').read_text(encoding='utf-8')
    for name in ('regular_2', 'first_shot', 'followup_warning', 'second_shot'):
        before, after = read(name), read(name + '_reload')
        assert {z['id'] for z in before['zombies']} == {z['id'] for z in after['zombies']}, name
        for key in ('shipShotCount', 'shipVolleyShotsRemaining', 'shipReinforcementCount', 'shipTargetColumn'):
            assert before['engineeringShip'][key] == after['engineeringShip'][key], (name, key)
    first, second = read('first_shot'), read('second_shot')
    prior_ids = {z['id'] for z in first['zombies']}
    guards = [z for z in second['zombies'] if z['id'] not in prior_ids]
    assert len(guards) == 8
    assert all(z['type'] not in ('ZOMBIE_NORMAL', 'ZOMBIE_POOL_NORMAL', 'ZOMBIE_GARGANTUAR',
                                'ZOMBIE_REDEYE_GARGANTUAR') for z in guards)
    assert sum(z['type'] == 'ZOMBIE_FOOTBALL' for z in guards) == 2
    assert sum(z['type'] == 'ZOMBIE_HEAVY_DIVER' for z in guards) == 2
    for state in (read('regular_5'), first, second, read('cap_refilled')):
        assert not any('GARGANTUAR' in z['type'] for z in state['zombies'])
    print('Engineering ship pressure: repeated guards, cap/refill, double cannon, cancellation, saves and occupied lanes passed.')


if __name__ == '__main__':
    generate() if '--generate' in sys.argv else verify()
