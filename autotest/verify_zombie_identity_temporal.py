"""Verify costume restoration through real clock anchors, including save/reload."""
import json
import sys
from pathlib import Path

from verify_zombie_identity import STYLES

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/clang-release'
NAME = 'smoke_zombie_identity_temporal'


def generate():
    commands = []

    def add(op, **args):
        commands.append(dict(op=op, **args))

    def capture(name):
        add('screenshot', name=name + '.png')
        add('dump_state', name=name + '.json')

    for mode in ('survivor', 'revived'):
        add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
        add('choose_cards', cards=[])
        add('wait_state', state='GAME', timeout=20)
        add('set_spawn_paused', value=True)
        add('set_difficulty', value=1)
        add('set_timescale', value=0)
        for index, (kind, _, _, _, _) in enumerate(STYLES):
            add('spawn_zombie', type='ZOMBIE_' + kind, row=1 if index < 3 else 3,
                x=450 + (index % 3) * 190, stationary=True)
        add('spawn_zombie', type='ZOMBIE_NORMAL', row=2, x=450, stationary=True)
        add('spawn_zombie', type='ZOMBIE_POLAR_CLOCKMAKER', row=2, x=900, stationary=True)
        add('set_timescale', value=1)
        add('wait_value', path='weather.temporalAnchorCount', equals=1, timeout=15)
        add('set_timescale', value=0)
        add('assert_state', path='weather.temporalAnchorTargetCount', equals=8)
        capture(mode + '_anchor')
        for index, (kind, _, _, _, health) in enumerate(STYLES):
            if mode == 'survivor':
                add('damage_zombie_body', type='ZOMBIE_' + kind, damage=round(health * .72))
            else:
                add('kill_zombie', row=1 if index < 3 else 3, index=0)
        add('save_level_snapshot', name=mode + '_pending')
        # Use the real serialized anchor, shortening only the remaining delay so
        # headless survivors have not bled out before the restoration boundary.
        add('reload_level_snapshot', name=mode + '_pending', patch=[
            {'op': 'replace', 'path': '/temporalAnchors/0/timer', 'value': .2}])
        add('wait_state', state='GAME', timeout=20)
        add('set_timescale', value=0)
        capture(mode + '_pending')
        add('set_timescale', value=1)
        add('wait_value', path='weather.temporalAnchorCount', equals=0, timeout=5)
        add('set_timescale', value=0)
        capture(mode + '_restored')
        for kind, stem, _, _, _ in STYLES:
            prefix = 'zombiesByType.ZOMBIE_' + kind
            add('assert_state', path=prefix + '.hasHead', equals=True)
            add('assert_state', path=prefix + '.hasArm', equals=True)
            add('assert_state', path=prefix + '.identityHairVisible',
                equals=stem not in ('worker', 'boiler', 'healer'))
        add('save_level_snapshot', name=mode + '_restored')
        add('reload_level_snapshot', name=mode + '_restored')
        add('wait_state', state='GAME', timeout=20)
        add('set_timescale', value=0)
        capture(mode + '_reloaded')
    add('quit')
    path = BUILD / 'autotest/generated' / (NAME + '.json')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')
    print(path)


def verify():
    output = BUILD / 'autotest/out' / NAME

    def read(name):
        return json.loads((output / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    for mode in ('survivor', 'revived'):
        anchor = {z['id']: z for z in read(mode + '_anchor')['zombies'] if 'identity' in z}
        pending = {z['id']: z for z in read(mode + '_pending')['zombies'] if 'identity' in z}
        if mode == 'survivor':
            assert set(pending) == set(anchor)
            assert all(not z['hasHead'] and not z['hasArm'] for z in pending.values())
        else:
            assert not pending
        for stage in ('anchor', 'restored', 'reloaded'):
            actual = {z['id']: z for z in read(mode + '_' + stage)['zombies'] if 'identity' in z}
            assert set(actual) == set(anchor), (mode, stage, 'stable IDs')
            for identity, z in actual.items():
                hat = z['identity'] in ('worker', 'boiler', 'healer')
                assert z['identityResourcesReady'] and z['hasHead'] and z['hasArm'], (mode, stage, z)
                assert z['bodyHealth'] == anchor[identity]['bodyHealth'], (mode, stage, z)
                assert z['identityHeadgearVisible'] == hat, (mode, stage, z)
                assert z['identityHairVisible'] != hat, (mode, stage, z)
                assert not z['identityStumpVisible'], (mode, stage, z)
    print('Six costumes preserve hair, headgear and limbs through rewind, revival and reload.')


if __name__ == '__main__':
    generate() if '--generate' in sys.argv else verify()
