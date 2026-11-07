"""Generate and verify severed-arm death, drummer layering and door removal cases."""
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/clang-release'
GROUPS = [
    ['NORMAL', 'CRYSTAL_DRUMMER', 'CRYSTAL_DRUMMER', 'COLD_CHAIN_GUARD', 'SUN_THIEF'],
    ['ICE_WORKER', 'BOILER', 'HEALER', 'SLUDGE', 'PRESSURE_GUARD'],
    ['WEATHER_JAMMER', 'EXCAVATOR', 'LOTUS_PARASOL'],
]


def command(op, **args):
    return dict(op=op, **args)


def setup():
    return [command('goto_level', level=1, resetTestState=True, background='GROUND_NIGHT'),
            command('choose_cards', cards=[]), command('wait_state', state='GAME', timeout=30),
            command('set_spawn_paused', value=True), command('set_difficulty', value=1),
            command('set_timescale', value=0)]


def capture(commands, name):
    commands += [command('screenshot', name=name+'.png'), command('dump_state', name=name+'.json')]


def advance(commands, seconds):
    commands += [command('set_timescale', value=.25), command('wait_seconds', value=seconds, timeout=30),
                 command('set_timescale', value=0)]


def reload(commands, name):
    commands += [command('save_level_snapshot', name=name), command('reload_level_snapshot', name=name),
                 command('wait_state', state='GAME', timeout=30), command('set_spawn_paused', value=True),
                 command('set_timescale', value=0)]
    capture(commands, name+'_reload')


def generate():
    cases = []
    for group, types in enumerate(GROUPS):
        cases += setup()
        for row, kind in enumerate(types):
            cases.append(command('spawn_zombie', type='ZOMBIE_'+kind, row=row, x=650,
                                 stationary=True, slowed=True, slowDuration=30))
        if group == 0:
            cases += [command('assert_state', path='mineFourthResourcesReady', equals=True),
                      command('charm_zombie', row=2)]
            advance(cases, 1.3)
            capture(cases, 'drummer_windup')
            for row in (1, 2):
                cases.append(command('damage_zombie_body', row=row, damage=600))
            capture(cases, 'drummer_severed')
            reload(cases, 'drummer_alive')
        for row in range(len(types)):
            cases.append(command('damage_zombie_body', row=row, damage=100000))
        delays = [.1, .7, .8, .8, .5, .2, .2, .2, .2, .3] if group == 0 else [.1, 3.4, .4]
        for stage, seconds in enumerate(delays):
            advance(cases, seconds)
            name = f'group{group}_fall{stage}'
            capture(cases, name)
            if group == 0 and stage == 3:
                reload(cases, name)
        advance(cases, 1)
        capture(cases, f'group{group}_removed')
    cases.append(command('quit'))

    doors = []
    for kind, shield in [('DOOR', 1100), ('REINFORCED_DOOR', 1030)]:
        doors += setup()
        doors.append(command('spawn_zombie', type='ZOMBIE_'+kind, row=2, x=650, stationary=True))
        doors.append(command('damage_zombie_body', row=2, damage=181))
        capture(doors, kind+'_headless')
        doors.append(command('damage_zombie', row=2, damage=shield))
        capture(doors, kind+'_unshielded')
        reload(doors, kind+'_unshielded')
        doors.append(command('damage_zombie_body', row=2, damage=100000))
        advance(doors, 2.2)
        capture(doors, kind+'_fall')
        advance(doors, 1)
        capture(doors, kind+'_removed')
        # Healthy disarming reveals an intact arm; reinforced doors keep magnetic immunity.
        for method in ('damage', 'magnet'):
            doors += setup()
            doors.append(command('spawn_zombie', type='ZOMBIE_'+kind, row=2, x=650, stationary=True))
            if method == 'damage':
                doors.append(command('damage_zombie', row=2, damage=shield))
            else:
                doors.append(command('plant', type='PLANT_MAGNETSHROOM', row=2, col=4))
                advance(doors, .15)
            capture(doors, kind+'_healthy_'+method)
    doors.append(command('quit'))
    generated = BUILD / 'autotest/generated'
    generated.mkdir(parents=True, exist_ok=True)
    for name, commands in [('smoke_zombie_arm_death', cases), ('smoke_door_arm_removal', doors)]:
        path = generated / (name+'.json')
        path.write_text(json.dumps({'commands': commands}, indent=2)+'\n', encoding='utf-8')
        print(path)


def verify_rigs():
    resources = BUILD / 'resources/reanim'
    def tracks(name):
        root = ET.fromstring('<root>'+(resources/name).read_text(encoding='utf-8-sig')+'</root>')
        return {track.findtext('name'): track for track in root.findall('track')}
    original = tracks('NormalZombie.reanim')
    drummer = tracks('CrystalDrummerZombie.reanim')
    def content(node):
        return node.tag, (node.text or '').strip(), [content(child) for child in node]
    # Every source track/pose/event range stays unchanged; only layering is duplicated.
    for name, track in original.items():
        assert content(track) == content(drummer[name]), name
    for source, front in [('anim_innerarm2', 'crystal_drum_arm_lower'),
                          ('anim_innerarm3', 'crystal_drum_arm_hand')]:
        assert [ET.tostring(t) for t in drummer[source].findall('t')] == [
            ET.tostring(t) for t in drummer[front].findall('t')]
    names = list(drummer)
    assert names.index('Zombie_body') < names.index('crystal_drum_mount') < names.index('crystal_drum_arm_lower')
    count = 0
    for path in resources.glob('*.reanim'):
        current = list(tracks(path.name))
        if all(name in current for name in ('Zombie_body', 'anim_innerarm2', 'anim_innerarm3', 'Zombie_outerarm_hand')):
            count += 1
            assert current.index('anim_innerarm2') < current.index('Zombie_body'), path.name
            assert current.index('anim_innerarm3') < current.index('Zombie_body'), path.name
    print(f'{count} normal-style rigs retain rear inner arms; drummer copies preserve all original frames')


def verify():
    verify_rigs()
    def read(suite, name):
        return json.loads((BUILD/'autotest/out'/suite/(name+'.json')).read_text(encoding='utf-8'))
    suite = 'smoke_zombie_arm_death'
    assert read(suite, 'status')['status'] == 'passed'
    for name in ('drummer_windup', 'drummer_severed', 'drummer_alive_reload'):
        zombies = {z['row']: z for z in read(suite, name)['zombies']}
        for row in (1, 2):
            z = zombies[row]
            assert z['drumFrontArmVisible'] and z['drumFrontMalletVisible'], name
            assert not z['drumRearArmVisible'] and not z['drumRearMalletVisible'], name
            if name != 'drummer_windup':
                assert not z['hasArm'] and not z['armVisible'], name
        if name == 'drummer_windup':
            assert zombies[1]['drumWindingUp']
    for group, types in enumerate(GROUPS):
        samples = range(10) if group == 0 else range(3)
        ids = None
        for stage in samples:
            name = f'group{group}_fall{stage}'
            state = read(suite, name)
            zombies = {z['row']: z for z in state['zombies']}
            # Charm clears slow: that drummer reaches the final frame at stage 3
            # and is already removed when the slowed companions continue falling.
            expected_rows = set(range(len(types)))
            if group == 0 and stage >= 4:
                expected_rows.remove(2)
            assert set(zombies) == expected_rows, name
            current_ids = {row: z['id'] for row, z in zombies.items()}
            if ids is not None:
                assert all(ids[row] == entity_id for row, entity_id in current_ids.items()), name
            ids = current_ids
            for row, kind in enumerate(types):
                if row not in expected_rows: continue
                z = zombies[row]
                assert z['type'] == 'ZOMBIE_'+kind and z['isDying'], (name, kind)
                assert not z['hasArm'] and not z['armVisible'], (name, kind)
                if kind == 'CRYSTAL_DRUMMER':
                    assert z['drumRearArmVisible'] and z['drumRearMalletVisible'], name
                    assert not z['drumFrontArmVisible'] and not z['drumFrontMalletVisible'], name
            if group == 0 and stage == 3:
                assert zombies[2]['animFrame'] >= 215, name
            if stage == max(samples):
                assert all(z['animFrame'] >= 213 for z in zombies.values()), name
        assert not read(suite, f'group{group}_removed')['zombies']
    for z in read(suite, 'group0_fall3_reload')['zombies']:
        assert not z['hasArm'] and not z['armVisible']
        if z['type'] == 'ZOMBIE_CRYSTAL_DRUMMER':
            assert z['drumRearArmVisible'] and not z['drumFrontArmVisible']

    suite = 'smoke_door_arm_removal'
    assert read(suite, 'status')['status'] == 'passed'
    for kind in ('DOOR', 'REINFORCED_DOOR'):
        for stage in ('headless', 'unshielded', 'unshielded_reload', 'fall'):
            z, = read(suite, kind+'_'+stage)['zombies']
            assert not z['hasHead'] and not z['armVisible'], (kind, stage)
            if stage != 'headless': assert z['shieldHealth'] == 0
        for method in ('damage', 'magnet'):
            z, = read(suite, kind+'_healthy_'+method)['zombies']
            assert z['hasArm'] and z['hasHead'], (kind, method)
            if kind == 'REINFORCED_DOOR' and method == 'magnet':
                assert not z['hasMagneticItem'] and z['shieldHealth'] == 1030
            else:
                assert z['armVisible'] and z['shieldHealth'] == 0, (kind, method)
        assert not read(suite, kind+'_removed')['zombies']
    print('Death layers, severed limbs, charm, reload, 12 zombie types and both door removal paths passed')


if __name__ == '__main__':
    if '--generate' in sys.argv:
        generate()
    elif '--rigs' in sys.argv:
        verify_rigs()
    else:
        verify()
