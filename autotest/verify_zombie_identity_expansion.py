"""Generate visible costume checks; verify resources, damage stages and detached parts."""
from pathlib import Path
import argparse
import json
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'
GENERATED = ROOT / 'build/clang-release/autotest/generated'
# Birth values are fixture inputs; runtime checks also verify the actual spawned health.
STYLES = [
    ('COLD_CHAIN_GUARD', 'coldguard', 'ColdChainGuardZombie', 'NormalZombie', 500, 1300),
    ('DISASTER_ENGINEER', 'disaster', 'DisasterEngineerZombie', 'NormalZombie', 1000, 0),
    ('INSULATOR', 'insulator', 'InsulatorZombie', 'NormalZombie', 300, 1200),
    ('GROUNDING', 'grounding', 'GroundingZombie', 'GroundingZombie', 270, 1200),
    ('ICE_WALL_ENGINEER', 'iceengineer', 'IceWallEngineerZombie', 'ConeZombie', 800, 370),
    ('POLAR_CLOCKMAKER', 'clockmaker', 'PolarClockmakerZombie', 'NormalZombie', 1000, 1200),
    ('AURORA_PRIEST', 'priest', 'AuroraPriestZombie', 'NormalZombie', 1200, 800),
    ('CRYSTAL_DRUMMER', 'drummer', 'CrystalDrummerZombie', 'CrystalDrummerZombie', 1600, 0),
]


def rig_contracts():
    base = ROOT / 'build/clang-release/resources/reanim'
    for _, _, name, rig, _, _ in STYLES:
        trees = [ET.fromstring('<root>' + (base / f'{n}.reanim').read_text() + '</root>')
                 for n in (rig, name + 'Identity')]
        for tree in trees:
            for image in tree.findall('track/t/i'):
                image.text = 'texture'
        assert ET.tostring(trees[0]) == ET.tostring(trees[1]), name


def generate():
    GENERATED.mkdir(parents=True, exist_ok=True)
    for group in range(2):
        styles = STYLES[group * 4:group * 4 + 4]
        for mirrored in (False, True):
            commands = []
            def add(op, **kwargs):
                commands.append(dict(op=op, **kwargs))
            def capture(name):
                add('screenshot', name=name + '.png')
                add('dump_state', name=name + '.json')
            def advance(seconds):
                add('set_timescale', value=.5)
                add('wait_seconds', value=seconds)
                add('set_timescale', value=0)
            def reload(name):
                add('save_level_snapshot', name=name)
                add('reload_level_snapshot', name=name)
                add('wait_state', state='GAME', timeout=30)
                add('set_spawn_paused', value=True)
                add('set_timescale', value=0)
                add('wait_frames', value=3)
                capture(name + '_loaded')
            add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
            add('choose_cards', cards=[])
            add('wait_state', state='GAME', timeout=30)
            add('set_difficulty', value=1)
            add('set_spawn_paused', value=True)
            add('set_fullscreen', value=False)
            add('set_monte_carlo_ai', value=False)
            add('set_timescale', value=0)
            for i, (kind, _, _, _, _, _) in enumerate(styles):
                add('spawn_zombie', type='ZOMBIE_' + kind, row=1 + (i // 2) * 2,
                    x=(430 if mirrored else 530) + (i % 2) * 290,
                    bodyTrack='anim_walk2' if mirrored else 'anim_walk')
                if mirrored:
                    add('charm_zombie', type='ZOMBIE_' + kind)
            add('wait_frames', value=3)
            capture('birth')
            for i, seconds in enumerate((.19, .37, .61, .89, 1.13, 1.47)):
                advance(seconds)
                capture(f'walk_{i}')
            reload('walking')
            # Stop time across damage transitions so repair/AI cannot alter the fixtures.
            for stage, fraction in (('light', .42), ('heavy', .30), ('armor_drop', .28)):
                for kind, _, _, _, _, armor in styles:
                    if armor:
                        damage = int(armor * fraction) if stage != 'armor_drop' else armor - int(armor * .42) - int(armor * .30)
                        add('damage_zombie', type='ZOMBIE_' + kind, damage=damage)
                capture(stage)
            advance(.2)
            capture('armor_falling')
            advance(1.0)
            for kind, _, _, _, health, _ in styles:
                add('damage_zombie_body', type='ZOMBIE_' + kind, damage=round(health * .55))
            capture('arm_drop')
            advance(.23)
            capture('arm_falling')
            reload('armless')
            for kind, _, _, _, health, _ in styles:
                add('damage_zombie_body', type='ZOMBIE_' + kind, damage=round(health * .25))
            capture('head_drop')
            advance(.19)
            capture('head_falling')
            reload('headless')
            for kind, _, _, _, _, _ in styles:
                add('damage_zombie_body', type='ZOMBIE_' + kind, damage=100000)
            advance(.5)
            capture('dying')
            advance(5.5)
            capture('dead')
            add('quit')
            name = f'smoke_identity_expansion_{group}' + ('_mirrored' if mirrored else '')
            (GENERATED / (name + '.json')).write_text(json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')
    generate_contexts()
    generate_feedback()
    generate_hat_fit()


def generate_hat_fit():
    """Focused regression for the owner's request to seat the safety helmet lower."""
    commands = []
    def add(op, **kwargs):
        commands.append(dict(op=op, **kwargs))
    def capture(name):
        add('screenshot', name=name + '.png')
        add('dump_state', name=name + '.json')
    for mirrored in (False, True):
        add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
        add('choose_cards', cards=['PLANT_WALLNUT'])
        add('wait_state', state='GAME', timeout=30)
        add('set_difficulty', value=1)
        add('set_spawn_paused', value=True)
        add('set_monte_carlo_ai', value=False)
        add('set_timescale', value=0)
        add('spawn_zombie', type='ZOMBIE_DISASTER_ENGINEER', row=2, x=700)
        if mirrored:
            add('charm_zombie', type='ZOMBIE_DISASTER_ENGINEER')
        else:
            add('set_sun', value=9990)
            add('plant', type='PLANT_WALLNUT', row=2, col=5)
        for i, seconds in enumerate((.1, .31, .63, .87, 1.29, 1.61)):
            add('set_timescale', value=.5)
            add('wait_seconds', value=seconds)
            add('set_timescale', value=0)
            capture(f'fit_{int(mirrored)}_{i}')
        add('damage_zombie_body', type='ZOMBIE_DISASTER_ENGINEER', damage=800)
        capture(f'drop_{int(mirrored)}')
        effect = 'Identity_disaster_head' + ('_right' if mirrored else '')
        add('assert_state', path=f'particleEffectsByName.{effect}.0.activeParticleCount', equals=1)
        add('assert_state', path=f'particleEffectsByName.{effect}.0.renderProbeReady', equals=True)
    add('quit')
    (GENERATED / 'smoke_identity_hat_fit.json').write_text(json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')


def generate_contexts():
    commands = []
    def add(op, **kwargs):
        commands.append(dict(op=op, **kwargs))
    for group in range(2):
        add('goto_level', level=1, resetTestState=True, background='GROUND_DAY')
        add('choose_cards', cards=['PLANT_WALLNUT'])
        add('wait_state', state='GAME', timeout=30)
        add('set_difficulty', value=1)
        add('set_spawn_paused', value=True)
        add('set_monte_carlo_ai', value=False)
        add('set_sun', value=9990)
        for i, style in enumerate(STYLES[group * 4:group * 4 + 4]):
            add('plant', type='PLANT_WALLNUT', row=i, col=5)
            add('spawn_zombie', type='ZOMBIE_' + style[0], row=i, x=700)
        add('set_timescale', value=.5)
        for i, seconds in enumerate((.2, .37, .61, .89, 1.13, 1.47, 1.8)):
            add('wait_seconds', value=seconds)
            add('screenshot', name=f'eat_{group}_{i}.png')
            add('dump_state', name=f'eat_{group}_{i}.json')
    add('set_adventure_level', level=110)
    add('goto_zombie_almanac')
    for i in range(2):
        if i:
            add('click', target='zombie_almanac_next_page')
        add('wait_frames', value=20)
        add('screenshot', name=f'almanac_{i}.png')
        add('dump_state', name=f'almanac_{i}.json')
    add('quit')
    (GENERATED / 'smoke_identity_expansion_contexts.json').write_text(json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')


def generate_feedback():
    commands = []
    def add(op, **kwargs):
        commands.append(dict(op=op, **kwargs))
    def scene(level=1):
        add('goto_level', level=level, resetTestState=True)
        add('choose_cards', cards=[])
        add('wait_state', state='GAME', timeout=30)
        add('set_difficulty', value=1)
        add('set_spawn_paused', value=True)
        add('set_timescale', value=0)
    def advance(seconds):
        add('set_timescale', value=1)
        add('wait_seconds', value=seconds)
        add('set_timescale', value=0)
    def capture(name, effect):
        add('screenshot', name=name + '.png')
        add('dump_state', name=name + '.json')
        add('assert_state', path='particleEffectNameCounts.' + effect, atLeast=1)
        add('assert_state', path=f'particleEffectsByName.{effect}.0.activeParticleCount', atLeast=1)
        add('assert_state', path=f'particleEffectsByName.{effect}.0.renderProbeReady', equals=True)
    scene()
    add('spawn_zombie', type='ZOMBIE_COLD_CHAIN_GUARD', row=1, x=650, stationary=True)
    add('spawn_zombie', type='ZOMBIE_CRYSTAL_DRUMMER', row=3, x=650, stationary=True)
    add('damage_zombie', type='ZOMBIE_COLD_CHAIN_GUARD', damage=500)
    advance(2.08)
    capture('drum_beat', 'IdentityDrumBeat')
    advance(.5)
    capture('shield_repair', 'IdentityColdShieldRepair')
    add('assert_state', path='zombiesByType.ZOMBIE_COLD_CHAIN_GUARD.helmHealth', equals=1200)
    scene(46)
    add('set_weather', intensity='CLEAR', duration=90)
    add('spawn_zombie', type='ZOMBIE_INSULATOR', row=2, x=650, stationary=True)
    add('set_night_roof_charge', phase='WARNING', charge=100, row=2, remaining=.05)
    advance(.12)
    capture('ceramic_overload', 'IdentityCeramicCharge')
    # Reuse the existing guided-lightning fixture through its real discharge boundary.
    grounding = json.loads((ROOT / 'autotest/scripts/smoke_grounding_zombie.json').read_text(encoding='utf-8-sig'))['commands']
    for command in grounding:
        commands.append(command)
        if command['op'] == 'wait_state':
            add('set_timescale', value=1)
            add('set_monte_carlo_ai', value=True)
        if command['op'] == 'wait_seconds' and command.get('value') == .6:
            break
    add('set_timescale', value=0)
    capture('grounding_discharge', 'IdentityGroundingSpark')
    # The area-11 fixture's second scene protects workers while the engineer survives.
    engineer = json.loads((ROOT / 'autotest/scripts/smoke_area11_units.json').read_text(encoding='utf-8-sig'))['commands']
    starts = [i for i, command in enumerate(engineer) if command['op'] == 'goto_level']
    for command in engineer[starts[1]:]:
        if command['op'] == 'apply_zombie_control':
            break
        command = dict(command)
        commands.append(command)
        if command['op'] == 'wait_state':
            add('set_timescale', value=1)
    add('set_timescale', value=0)
    capture('canister_vent', 'IdentityDisasterVent')
    advance(6.1)
    capture('canister_ready', 'IdentityDisasterReady')
    add('assert_state', path='zombiesByType.ZOMBIE_DISASTER_ENGINEER.engineerFull', equals=True)
    add('quit')
    (GENERATED / 'smoke_identity_expansion_feedback.json').write_text(json.dumps({'commands': commands}, indent=2) + '\n', encoding='utf-8')
    last_scene = max(i for i, command in enumerate(commands) if command['op'] == 'goto_level')
    (GENERATED / 'smoke_identity_expansion_canister.json').write_text(json.dumps({'commands': commands[last_scene:]}, indent=2) + '\n', encoding='utf-8')


def verify_contexts():
    folder = OUT / 'smoke_identity_expansion_contexts'
    read = lambda phase: json.loads((folder / (phase + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    eating = set()
    for group in range(2):
        for i in range(7):
            eating.update(z['type'] for z in read(f'eat_{group}_{i}')['zombies'] if z['isEating'])
    assert {'ZOMBIE_' + s[0] for s in STYLES} <= eating, ('eating', eating)
    shown = set()
    for page in range(2):
        for z in read(f'almanac_{page}')['zombieAlmanacGridPreviews']:
            if z.get('identity') in {s[1] for s in STYLES}:
                shown.add(z['identity'])
                assert z['identityResourcesReady']
    assert shown == {s[1] for s in STYLES}, ('almanac', shown)
    assert json.loads((OUT / 'smoke_identity_expansion_feedback/status.json').read_text(encoding='utf-8-sig'))['status'] == 'passed'
    assert json.loads((OUT / 'smoke_identity_hat_fit/status.json').read_text(encoding='utf-8-sig'))['status'] == 'passed'
    assert json.loads((OUT / 'smoke_identity_expansion_canister/status.json').read_text(encoding='utf-8-sig'))['status'] == 'passed'
    print('Eating, almanac and native ability feedback: passed')


def verify_run(group, mirrored):
    name = f'smoke_identity_expansion_{group}' + ('_mirrored' if mirrored else '')
    styles = STYLES[group * 4:group * 4 + 4]
    read = lambda phase: json.loads((OUT / name / (phase + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed', name
    expected = {'ZOMBIE_' + s[0] for s in styles}
    for phase in ('birth', 'walking_loaded', 'arm_drop', 'armless_loaded', 'head_drop', 'headless_loaded'):
        state = read(phase)
        zombies = {z['type']: z for z in state['zombies'] if z['type'] in expected}
        assert set(zombies) == expected, (name, phase, zombies.keys())
        for kind, stem, _, _, health, armor in styles:
            z = zombies['ZOMBIE_' + kind]
            assert z['identity'] == stem and z['identityResourcesReady'], (phase, stem, 'resources')
            armless = phase not in ('birth', 'walking_loaded')
            headless = phase in ('head_drop', 'headless_loaded')
            assert z['identityStumpVisible'] == armless, (phase, stem, 'stump')
            assert z['identityHeadVisible'] != headless, (phase, stem, 'head')
            assert z['identityHeadgearVisible'] == (stem not in ('grounding', 'iceengineer') and not headless), (phase, stem, 'hat')
            if phase == 'birth':
                assert z['bodyHealth'] == health and z['helmHealth'] == armor, (stem, 'fixture')
            if phase in ('arm_drop', 'head_drop'):
                part = 'arm' if phase == 'arm_drop' else 'head'
                key = f'Identity_{stem}_{part}' + ('_right' if mirrored else '')
                particles = state['particleEffectsByName'][key]
                assert len(particles) == 1 and particles[0]['activeParticleCount'] == 1, key
                p = particles[0]
                assert p['renderProbeReady'], key
                assert p['firstColorRGB'] == ([255, 135, 135] if mirrored else [255, 255, 255]), key
                assert abs(p['originToRenderCenterDxInt']) <= 1 and abs(p['originToRenderCenterDyInt']) <= 1, key
        if phase.endswith('_loaded'):
            assert not any(p['name'].startswith('Identity') for p in state['particleEffects']), (phase, 'replayed feedback')
    drop = read('armor_drop')['particleEffectsByName']
    effects = ['IdentityColdShieldBreak', 'IdentityCeramicBreak'] if group == 0 else ['IdentityClockShards', 'IdentityAuroraShards']
    for effect in effects:
        assert drop[effect][0]['activeParticleCount'] > 0 and drop[effect][0]['renderProbeReady'], effect
    assert not any(z['type'] in expected for z in read('dead')['zombies']), (name, 'death cleanup')
    print(name + ': passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--rigs-only', action='store_true')
    args = parser.parse_args()
    rig_contracts()
    if args.generate:
        generate()
    elif not args.rigs_only:
        for group in range(2):
            for mirrored in (False, True):
                verify_run(group, mirrored)
        verify_contexts()
    print('Identity expansion rig contracts: passed')
