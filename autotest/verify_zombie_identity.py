"""Generate and verify six costumes, articulated motion, mirrored limbs and reloads."""
from pathlib import Path
import argparse
import json
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'
STYLES = [
    ('ICE_WORKER', 'worker', 'IceWorkerZombie', 'NormalZombie', 500),
    ('BOILER', 'boiler', 'BoilerZombie', 'NormalZombie', 1000),
    ('WEATHER_JAMMER', 'jammer', 'WeatherJammerZombie', 'BucketZombie', 270),
    ('HEALER', 'healer', 'HealerZombie', 'NormalZombie', 800),
    ('SLUDGE', 'sludge', 'SludgeZombie', 'ConeZombie', 800),
    ('PRESSURE_GUARD', 'guard', 'PressureGuardZombie', 'NormalZombie', 600),
]


def generate():
    for mirrored in (False, True):
        commands = []
        def add(op, **args):
            commands.append(dict(op=op, **args))
        def capture(name):
            add('screenshot', name=name + '.png')
            add('dump_state', name=name + '.json')
        def advance(seconds):
            add('set_timescale', value=0.5)
            add('wait_seconds', value=seconds)
            add('set_timescale', value=0)
        add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
        add('choose_cards', cards=['PLANT_WALLNUT'])
        add('wait_state', state='GAME', timeout=30)
        add('set_difficulty', value=1)
        add('set_fullscreen', value=False)
        add('set_spawn_paused', value=True)
        add('set_monte_carlo_ai', value=False)
        add('set_timescale', value=0)
        for i, (kind, stem, _, _, _) in enumerate(STYLES):
            add('spawn_zombie', type='ZOMBIE_' + kind, row=1 if i < 3 else 3,
                x=(350 if mirrored else 470) + (i % 3) * 215, bodyTrack='anim_walk2' if mirrored else 'anim_walk')
            if mirrored:
                add('charm_zombie', type='ZOMBIE_' + kind)
        add('wait_frames', value=3)
        capture('birth')
        # 不规则间隔覆盖一个完整步态循环，避免恰好总采到同一帧。
        for i, seconds in enumerate((0.35, 0.55, 0.8, 1.1, 1.45, 1.8, 2.15)):
            advance(seconds)
            capture(f'walk_{i}')
        add('save_level_snapshot', name='walking')
        add('reload_level_snapshot', name='walking')
        add('wait_state', state='GAME', timeout=30)
        add('set_spawn_paused', value=True)
        add('set_timescale', value=0)
        add('wait_frames', value=3)
        capture('loaded')
        # 先正式移除头盔，随后本体夹具逐层触发断肢，不混入承伤护卫的限伤预算。
        for kind, damage in [('WEATHER_JAMMER', 1100), ('SLUDGE', 1000)]:
            add('damage_zombie', type='ZOMBIE_' + kind, damage=damage)
        for kind, _, _, _, health in STYLES:
            add('damage_zombie_body', type='ZOMBIE_' + kind, damage=round(health * .55))
        capture('arm_drop')
        advance(.23)
        capture('arm_falling')
        advance(1.1)
        add('save_level_snapshot', name='armless')
        add('reload_level_snapshot', name='armless')
        add('wait_state', state='GAME', timeout=30)
        add('set_spawn_paused', value=True)
        add('set_timescale', value=0)
        add('wait_frames', value=3)
        capture('armless_loaded')
        for kind, _, _, _, health in STYLES:
            add('damage_zombie_body', type='ZOMBIE_' + kind, damage=round(health * .25))
        capture('head_drop')
        advance(.19)
        capture('head_falling')
        add('save_level_snapshot', name='headless')
        add('reload_level_snapshot', name='headless')
        add('wait_state', state='GAME', timeout=30)
        add('set_spawn_paused', value=True)
        add('set_timescale', value=0)
        add('wait_frames', value=3)
        capture('headless_loaded')
        for kind, _, _, _, _ in STYLES:
            add('damage_zombie_body', type='ZOMBIE_' + kind, damage=100000)
        advance(1.0)
        capture('dying')
        advance(5.5)
        capture('dead')
        add('quit')
        name = 'smoke_zombie_identity' + ('_mirrored' if mirrored else '')
        (ROOT / f'autotest/scripts/{name}.json').write_text(
            json.dumps({'description': 'Costume and detached-part presentation regression.', 'commands': commands}, indent=2) + '\n', encoding='utf-8')
    generate_contexts()
    generate_armor()
    generate_tints()


def generate_tints():
    commands = []
    def add(op, **args):
        commands.append(dict(op=op, **args))
    add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
    add('choose_cards', cards=[])
    add('wait_state', state='GAME', timeout=30)
    add('set_difficulty', value=1)
    add('set_spawn_paused', value=True)
    add('set_timescale', value=0)
    for row, kind in enumerate(['ICE_WORKER', 'BOILER', 'HEALER'], 1):
        add('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=650, stationary=True)
    add('apply_zombie_control', row=1, effect='FROZEN')
    add('apply_zombie_control', row=2, effect='PARALYSIS', duration=5)
    add('apply_toxin_stacks', row=3, count=1)
    add('screenshot', name='tinted_alive.png')
    for kind in ['ICE_WORKER', 'BOILER', 'HEALER']:
        add('damage_zombie_body', type='ZOMBIE_' + kind, damage=100000)
    add('screenshot', name='tinted_drop.png')
    add('dump_state', name='tinted_drop.json')
    add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
    add('choose_cards', cards=[])
    add('wait_state', state='GAME', timeout=30)
    add('set_spawn_paused', value=True)
    add('set_timescale', value=0)
    for row, kind in enumerate(['NORMAL', 'FOOTBALL', 'NEWSPAPER'], 1):
        add('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=650, stationary=True)
        add('charm_zombie', type='ZOMBIE_' + kind)
        add('damage_zombie', type='ZOMBIE_' + kind, damage=100000)
    add('screenshot', name='classic_tinted_drop.png')
    add('dump_state', name='classic_tinted_drop.json')
    add('quit')
    (ROOT / 'autotest/scripts/smoke_zombie_identity_tints.json').write_text(
        json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')


def generate_contexts():
    commands = []
    def add(op, **args):
        commands.append(dict(op=op, **args))
    for group, kinds in enumerate([['ICE_WORKER', 'BOILER', 'HEALER'], ['WEATHER_JAMMER', 'SLUDGE', 'PRESSURE_GUARD']]):
        add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
        add('choose_cards', cards=['PLANT_WALLNUT'])
        add('wait_state', state='GAME', timeout=30)
        add('set_difficulty', value=1)
        add('set_spawn_paused', value=True)
        add('set_monte_carlo_ai', value=False)
        add('set_sun', value=9990)
        for row, kind in enumerate(kinds, 1):
            add('plant', type='PLANT_WALLNUT', row=row, col=5)
            add('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=700)
        add('set_timescale', value=.5)
        for i, seconds in enumerate([.2, .37, .61, .89, 1.13, 1.47, 1.8]):
            add('wait_seconds', value=seconds)
            add('screenshot', name=f'eat_{group}_{i}.png')
            add('dump_state', name=f'eat_{group}_{i}.json')
        add('set_timescale', value=0)
        for kind in kinds:
            add('damage_zombie_body', type='ZOMBIE_' + kind, damage=100000)
        add('screenshot', name=f'eating_drop_{group}.png')
        add('dump_state', name=f'eating_drop_{group}.json')
    add('set_adventure_level', level=110)
    add('goto_zombie_almanac')
    add('wait_frames', value=30)
    add('screenshot', name='almanac_0.png')
    add('dump_state', name='almanac_0.json')
    add('click', target='zombie_almanac_next_page')
    add('wait_frames', value=8)
    add('screenshot', name='almanac_1.png')
    add('dump_state', name='almanac_1.json')
    add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
    add('choose_cards', cards=[])
    add('wait_state', state='GAME', timeout=30)
    add('set_spawn_paused', value=True)
    add('set_timescale', value=0)
    for i in range(12):
        add('spawn_zombie', type='ZOMBIE_' + STYLES[i % 6][0], row=1+i//4, x=520+(i%4)*65)
    add('set_timescale', value=.5)
    add('wait_seconds', value=.7)
    add('set_timescale', value=0)
    add('screenshot', name='mixed_crowd.png')
    add('dump_state', name='mixed_crowd.json')
    add('quit')
    (ROOT / 'autotest/scripts/smoke_zombie_identity_contexts.json').write_text(
        json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')


def generate_armor():
    commands = []
    def add(op, **args):
        commands.append(dict(op=op, **args))
    kinds = ['BUCKET', 'TRAFFIC_CONE', 'FASTBUCKET', 'POOL_BUCKET', 'POOL_CONE', 'GROUNDING', 'ICE_WALL_ENGINEER']
    armor = [1100, 370, 600, 1100, 370, 1200, 370]
    for mirrored in (False, True):
        for group in range(2):
            add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
            add('choose_cards', cards=[])
            add('wait_state', state='GAME', timeout=30)
            add('set_difficulty', value=1)
            add('set_spawn_paused', value=True)
            add('set_timescale', value=0)
            batch = kinds[group*4:group*4+4]
            for row, kind in enumerate(batch):
                add('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=650, stationary=True)
                if mirrored:
                    add('charm_zombie', type='ZOMBIE_' + kind)
            add('wait_frames', value=3)
            add('screenshot', name=f'armor_{int(mirrored)}_{group}_before.png')
            for kind in batch:
                add('damage_zombie', type='ZOMBIE_' + kind, damage=armor[kinds.index(kind)])
            add('screenshot', name=f'armor_{int(mirrored)}_{group}_drop.png')
            add('dump_state', name=f'armor_{int(mirrored)}_{group}_drop.json')
            add('set_timescale', value=.25)
            add('wait_seconds', value=.08)
            add('set_timescale', value=0)
            add('screenshot', name=f'armor_{int(mirrored)}_{group}_next.png')
            add('dump_state', name=f'armor_{int(mirrored)}_{group}_next.json')
    add('quit')
    (ROOT / 'autotest/scripts/smoke_zombie_armor_drop.json').write_text(
        json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')


def verify_rigs():
    """All clips, swaps and affine transforms must match the source, apart from texture keys."""
    for _, _, name, original, _ in STYLES:
        base = ROOT / 'build/clang-release/resources/reanim'
        a = ET.fromstring('<root>' + (base / f'{original}.reanim').read_text() + '</root>')
        b = ET.fromstring('<root>' + (base / f'{name}Identity.reanim').read_text() + '</root>')
        for root in (a, b):
            for frame in root.findall('track/t'):
                for image in frame.findall('i'):
                    image.text = 'texture'
        assert ET.tostring(a) == ET.tostring(b), name


def verify_run(name, mirrored):
    path = OUT / name
    read = lambda n: json.loads((path / (n + '.json')).read_text(encoding='utf-8-sig'))
    status = read('status')
    assert status['status'] == 'passed', status
    expected = {'ZOMBIE_' + s[0] for s in STYLES}
    for phase in ['birth', 'loaded', 'arm_drop', 'armless_loaded', 'head_drop', 'headless_loaded']:
        state = read(phase)
        zombies = {z['type']: z for z in state['zombies']}
        assert set(zombies) == expected, (name, phase, set(zombies))
        for kind, stem, _, _, _ in STYLES:
            z = zombies['ZOMBIE_' + kind]
            assert z['identity'] == stem and z['identityResourcesReady'], (phase, stem, 'resources')
            armless = phase not in ('birth', 'loaded')
            headless = phase in ('head_drop', 'headless_loaded')
            assert z['identityStumpVisible'] == armless, (phase, stem, 'stump')
            assert z['identityHeadVisible'] != headless, (phase, stem, 'head')
            assert z['identityHeadgearVisible'] == (stem in ('worker', 'boiler', 'healer') and not headless)
            if headless:
                assert not z['identityTongueVisible'], (phase, stem, 'tongue')
        if phase in ('arm_drop', 'head_drop'):
            part = 'arm' if phase == 'arm_drop' else 'head'
            for _, stem, _, _, _ in STYLES:
                key = f'Identity_{stem}_{part}' + ('_right' if mirrored else '')
                effects = state['particleEffectsByName'][key]
                assert len(effects) == 1 and effects[0]['activeParticleCount'] == 1, (phase, stem, 'particle')
                assert effects[0]['renderProbeReady'], (phase, stem, 'render')
                effect = effects[0]
                assert effect['firstColorRGB'] == ([255, 135, 135] if mirrored else [255, 255, 255]), (phase, stem, effect['firstColorRGB'])
                assert abs(effect['originToRenderCenterDxInt']) <= 1 and abs(effect['originToRenderCenterDyInt']) <= 1, (phase, stem, 'first-frame offset')
                relative_y = effect['nearestZombie']['centerDyInt']
                assert (-110 < relative_y < -15) if part == 'head' else (-45 < relative_y < 45), (phase, stem, relative_y)
        if phase.endswith('loaded'):
            assert not any(p['name'].startswith('Identity_') for p in state['particleEffects']), (phase, 'replayed drop')
    assert not read('dead')['zombies'], (name, 'death cleanup')


def verify_contexts():
    path = OUT / 'smoke_zombie_identity_contexts'
    read = lambda n: json.loads((path / (n + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    eating = set()
    for group in range(2):
        for i in range(7):
            eating.update(z['type'] for z in read(f'eat_{group}_{i}')['zombies'] if z['isEating'])
    assert eating == {'ZOMBIE_' + s[0] for s in STYLES}, eating
    shown = set()
    for page in range(2):
        for z in read(f'almanac_{page}')['zombieAlmanacGridPreviews']:
            if 'identity' in z:
                shown.add(z['identity'])
                assert z['identityResourcesReady']
                assert z['identityHeadgearVisible'] == (z['identity'] in ('worker', 'boiler', 'healer'))
    assert shown == {s[1] for s in STYLES}, shown
    crowd = read('mixed_crowd')['zombies']
    assert len(crowd) == 12 and all(z['identityResourcesReady'] for z in crowd)


def verify_armor():
    path = OUT / 'smoke_zombie_armor_drop'
    read = lambda n: json.loads((path / (n + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    names = {'ZombieBucketOff', 'ZombieConeOff', 'ZombieFastBucketOff', 'GroundingZombieConeOff', 'IceWallEngineerHardhatOff'}
    for mirrored in range(2):
        for group in range(2):
            initial = read(f'armor_{mirrored}_{group}_drop')
            later = read(f'armor_{mirrored}_{group}_next')
            first = [p for p in initial['particleEffects'] if p['name'] in names]
            next_parts = [p for p in later['particleEffects'] if p['name'] in names]
            assert len(first) == (4 if group == 0 else 3) == len(next_parts)
            owners = sorted(initial['zombies'], key=lambda z: z['id'])
            for a, b, owner in zip(first, next_parts, owners):
                assert a['renderProbeReady'] and b['renderProbeReady']
                assert abs(a['originToRenderCenterDxInt']) <= 1 and abs(a['originToRenderCenterDyInt']) <= 1
                assert -150 < a['worldBounds']['centerYInt'] - owner['yInt'] < -35, (a, owner)
                assert a['firstColorRGB'] == ([255, 135, 135] if mirrored else [255, 255, 255]), a
                # 只比较同一部件两个相邻阶段的中心位移，捕获旧 Position 场导致的首帧跳位。
                for axis in ('X', 'Y'):
                    assert abs(a['worldBounds'][f'center{axis}Int'] - b['worldBounds'][f'center{axis}Int']) < 20, (a, b)


def verify_tints():
    path = OUT / 'smoke_zombie_identity_tints'
    read = lambda n: json.loads((path / (n + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    state = read('tinted_drop')
    for stem, rgb in [('worker', [90, 90, 255]), ('boiler', [185, 119, 255]), ('healer', [199, 124, 227])]:
        for part in ('head', 'arm'):
            effect = state['particleEffectsByName'][f'Identity_{stem}_{part}'][0]
            assert effect['firstColorRGB'] == rgb, effect
    state = read('classic_tinted_drop')
    for name in ['ZombieHeadOff', 'ZombieArmOff', 'FootballZombieHeadOff', 'FootballZombieArmOff',
                 'ZombieFootballOff', 'ZombiePaperOff', 'PaperZombieArmOff']:
        assert state['particleEffectsByName'][name], name
        for effect in state['particleEffectsByName'][name]:
            assert effect['firstColorRGB'] == [255, 135, 135], effect


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--rigs-only', action='store_true')
    args = parser.parse_args()
    verify_rigs()
    if args.generate:
        generate()
    elif not args.rigs_only:
        verify_run('smoke_zombie_identity', False)
        verify_run('smoke_zombie_identity_mirrored', True)
        verify_contexts()
        verify_armor()
        verify_tints()
    print('Zombie identity checks passed.')
