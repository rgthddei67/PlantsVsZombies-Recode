"""Generate visible fixtures and verify stable identities, split damage and salvage transactions."""
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'autotest/scripts'
OUT = ROOT / 'build/clang-release/autotest/out'


def op(operation, **args):
    return dict(op=operation, **args)


def setup(level=103, cards=(), **options):
    return [op('goto_level', level=level, resetTestState=True, **options),
            op('choose_cards', cards=list(cards)), op('wait_state', state='GAME', timeout=30),
            op('set_spawn_paused', value=True), op('set_weather', intensity='CLEAR', duration=600),
            op('wait_seconds', value=2.2), op('set_sun', value=20000)]


def plant(kind, row, col):
    return op('plant', type='PLANT_' + kind, row=row, col=col)


def zombie(kind, row, x=900, **options):
    return op('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=x, **options)


def capture(name, image=False):
    return ([op('screenshot', name=name+'.png')] if image else []) + [op('dump_state', name=name+'.json')]


def check(path, value):
    return op('assert_state', path=path, equals=value)


def save_script(name, commands):
    (SCRIPTS / (name+'.json')).write_text(json.dumps({'commands': commands + [op('quit')]},
        ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def generate():
    commands = setup(cards=['PLANT_LOTUSPULT'])
    commands += [check('watersidePairTwoResourcesReady', True),
        check('plantDefinitions.PLANT_LOTUSPULT.sunCost', 225),
        check('plantDefinitions.PLANT_LOTUSPULT.cooldownMs', 15000),
        check('plantDefinitions.PLANT_LOTUSPULT.simulationBaseHealth', 300),
        op('assert_can_plant', type='PLANT_LOTUSPULT', row=0, col=6, expected=False),
        plant('LOTUSPULT', 0, 2), plant('LOTUSPULT', 2, 3), plant('LOTUSPULT', 1, 5),
        op('wait_seconds', value=.2)] + capture('forms', True)
    for i, seconds in enumerate((.37, .51, .62, .71, .95)):
        commands += [op('wait_seconds', value=seconds)] + capture(f'idle_{i}', True)
    commands += [check('topPlantsByCell.2_3.lotusWaterForm', True),
        check('topPlantsByCell.1_5.attackMultiplierMilli', 4000),
        op('set_waterside_water', value=100), op('wait_seconds', value=.1)] + capture('flooded', True)
    commands += [check('topPlantsByCell.0_2.watersideHazard', 0),
        check('topPlantsByCell.0_2.lotusWaterForm', True),
        check('topPlantsByCell.1_5.attackMultiplierMilli', 1000),
        op('set_waterside_water', value=0), op('wait_seconds', value=.1)] + capture('retreated')

    # One real ground attack: there are no synthetic direct-hit commands.
    commands += setup() + [plant('LOTUSPULT', 0, 2), zombie('NORMAL', 0, 820, stationary=True),
        op('set_cabbagepult_shoot_cycle', row=0, col=2, elapsed=2.99),
        op('wait_value', path='bulletCount', equals=1, timeout=8),
        op('set_timescale', value=0)] + capture('ground_launch', True)
    commands += [check('bullets.0.lotus.split', False), op('set_timescale', value=1),
        op('wait_seconds', value=1.5), check('zombies.0.bodyHealth', 190)] + capture('ground_hit')

    for variant in ('split', 'lost_target', 'kill_primary'):
        commands += setup() + [plant('LOTUSPULT', 2, 3), zombie('NORMAL', 2, 840, stationary=True)]
        # Eight eligible secondary targets; selection must cap at six distinct IDs.
        for row in (1, 1, 1, 3, 3, 3, 2, 2):
            commands.append(zombie('NORMAL', row, 840, stationary=True))
        if variant == 'kill_primary':
            commands.append(op('damage_zombie', row=2, index=0, damage=160))
        commands += [op('set_cabbagepult_shoot_cycle', row=2, col=3, elapsed=2.99),
            op('wait_value', path='bulletCount', equals=6, timeout=8), op('set_timescale', value=0)]
        commands += capture(variant+'_flying', True)
        commands += [op('save_level_snapshot', name=variant),
            op('reload_level_snapshot', name=variant), op('wait_state', state='GAME', timeout=30),
            op('set_timescale', value=0), op('set_spawn_paused', value=True)] + capture(variant+'_restored')
        if variant == 'lost_target': commands.append(op('kill_zombie', row=1, index=0))
        commands += [op('set_timescale', value=1), op('wait_seconds', value=.65)] + capture(variant+'_hit', True)
        commands.append(check('bulletCount', 0))

    # Launch-time water form survives leaving temporary flood while the parent is airborne.
    commands += setup() + [op('set_waterside_water', value=100), plant('LOTUSPULT', 0, 2),
        zombie('NORMAL', 0, 840, stationary=True), zombie('NORMAL', 1, 840, stationary=True),
        op('set_cabbagepult_shoot_cycle', row=0, col=2, elapsed=2.99),
        op('wait_value', path='bulletCount', equals=1, timeout=8), op('set_timescale', value=0),
        check('bullets.0.lotus.split', True), op('set_waterside_water', value=0),
        op('save_level_snapshot', name='parent'), op('reload_level_snapshot', name='parent'),
        op('wait_state', state='GAME', timeout=30), op('set_spawn_paused', value=True),
        op('set_timescale', value=1), op('wait_seconds', value=1.9)] + capture('parent_form_preserved')
    commands += [op('wait_value', path='bulletCount', equals=1, timeout=5), op('set_timescale', value=0),
        check('bullets.0.lotus.split', False),
        op('assert_state', path='bulletPoolHitCount', atLeast=1)] + capture('reused_dry_parent')
    commands += [op('set_timescale', value=1), op('wait_seconds', value=1.5)] + capture('reused_dry_hit')
    save_script('smoke_lotus_pult', commands)

    # Double foundation and pumpkin stay intact except the targeted lily.
    commands = setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6),
        op('set_waterside_water', value=100), plant('LILYPAD', 0, 6), plant('PUMPKINSHELL', 0, 6),
        zombie('SALVAGE', 0, 930), op('wait_seconds', value=.7), op('set_timescale', value=0)]
    commands += capture('hook_windup', True) + [op('save_level_snapshot', name='hook'),
        op('reload_level_snapshot', name='hook'), op('wait_state', state='GAME', timeout=30),
        op('set_timescale', value=0), op('set_spawn_paused', value=True)] + capture('hook_restored')
    commands += [op('set_timescale', value=1), op('wait_seconds', value=2.0),
        check('cells.0.6.under', 'NONE'), check('cells.0.6.foundation', 'PLANT_FLOWERPOT'),
        check('cells.0.6.normal', 'PLANT_WALLNUT'), check('cells.0.6.pumpkin', 'PLANT_PUMPKINSHELL'),
        check('zombies.0.salvageCount', 1), check('topPlantsByCell.0_6.watersideHazard', 1)]
    commands += capture('base_removed', True)
    commands += [plant('LILYPAD', 0, 6), op('wait_seconds', value=.1),
        check('topPlantsByCell.0_6.watersideHazard', 0)] + capture('rescued', True)
    commands += [op('wait_seconds', value=12.6), check('zombies.0.salvageCount', 2)] + capture('repeat')

    commands += setup() + [plant('FLOWERPOT', 0, 6), plant('LIGHTNINGRODPOT', 0, 6),
        plant('LOTUSPULT', 0, 6), zombie('SALVAGE', 0, 930), op('wait_seconds', value=2.8),
        check('cells.0.6.foundation', 'NONE'), check('topPlantsByCell.0_6.watersideHazard', 2)]
    commands += capture('upgraded_pot_removed', True)

    # Natural-ground front line blocks target acquisition behind it.
    commands += setup() + [plant('FLOWERPOT', 0, 3), plant('SUNFLOWER', 0, 3),
        plant('WALLNUT', 0, 5), zombie('SALVAGE', 0, 850), op('wait_seconds', value=3.2),
        check('zombies.0.salvageCount', 0), check('cells.0.3.foundation', 'PLANT_FLOWERPOT')]
    commands += capture('front_blocks')
    for effect in ('BUTTER', 'FROZEN'):
        commands += setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6),
            zombie('SALVAGE', 0, 930), op('wait_seconds', value=.4),
            op('apply_zombie_control', row=0, effect=effect), op('wait_seconds', value=.1),
            check('zombies.0.salvageWindupMs', 0), check('zombies.0.salvageCount', 0),
            op('wait_seconds', value=2.7), check('cells.0.6.foundation', 'PLANT_FLOWERPOT')]
        commands += capture('interrupt_'+effect, True)
    commands += setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6),
        zombie('SALVAGE', 0, 930), op('wait_seconds', value=.4), plant('ALARMBELLFLOWER', 0, 5),
        op('wait_seconds', value=.3), check('zombies.0.salvageWindupMs', 0)] + capture('alarm_interrupt')
    commands += setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6),
        zombie('SALVAGE', 0, 930), op('wait_seconds', value=.4),
        op('damage_zombie', row=0, damage=700), op('wait_seconds', value=.15),
        check('zombies.0.salvageWindupMs', 0)] + capture('headless', True)
    commands += [op('wait_seconds', value=4), check('cells.0.6.foundation', 'PLANT_FLOWERPOT')]
    commands += setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6),
        zombie('SALVAGE', 0, 930), op('wait_seconds', value=.4), op('charm_zombie', row=0),
        op('wait_seconds', value=.1), check('zombies.0.salvageWindupMs', 0)] + capture('charmed')
    commands += setup(10) + [plant('MAGNETSHROOM', 2, 5), zombie('SALVAGE', 2, 850),
        op('wait_value', path='zombies.0.salvageHasHook', equals=False, timeout=12),
        op('save_level_snapshot', name='disarmed'), op('reload_level_snapshot', name='disarmed'),
        op('wait_state', state='GAME', timeout=30), check('zombies.0.salvageHasHook', False)]
    commands += capture('magnet_disarmed', True)
    save_script('smoke_salvage_zombie', commands)

    commands = setup(101)
    commands += [op('set_adventure_level', level=101), op('force_trophy'), op('wait_seconds', value=.6),
        op('click', target='trophy', hold_frames=6), op('wait_seconds', value=4),
        check('scene', 'PlantRewardScene'), check('plantAlmanacSelected', 'PLANT_LOTUSPULT'),
        check('plantAlmanacPreviewReady', True)] + capture('reward', True)
    commands += [op('click', x=920, y=550), op('wait_frames', value=3), check('level', 102)]
    commands += capture('unlocked')
    for level, cap, waves in [(102, 1, 30), (103, 2, 40)]:
        commands += setup(level) + [check('maxWave', waves),
            op('set_wave_spawn_list', types=['ZOMBIE_NORMAL']), op('set_timescale', value=0),
            op('summon_next_wave', count=6, clearPrevious=True)] + capture(f'level_{level}_wave6')
        if level == 103:
            commands += [op('spawn_wave_zombie', type='ZOMBIE_SALVAGE', row=0, x=930, mutationRoll=1)
                for _ in range(3)]
        else:
            commands.append(op('spawn_wave_zombie', type='ZOMBIE_SALVAGE', row=0, x=930, mutationRoll=1))
        commands += [check('weather.salvageZombiesSpawnedThisWave', cap)] + capture(f'level_{level}_capped', True)
        commands += [op('save_level_snapshot', name=f'quota_{level}'),
            op('reload_level_snapshot', name=f'quota_{level}'), op('wait_state', state='GAME', timeout=30),
            op('set_timescale', value=0), op('set_spawn_paused', value=True),
            check('weather.salvageZombiesSpawnedThisWave', cap),
            op('summon_next_wave'),
            op('spawn_wave_zombie', type='ZOMBIE_SALVAGE', row=0, x=930, mutationRoll=1),
            check('weather.salvageZombiesSpawnedThisWave', 0)] + capture(f'level_{level}_simultaneous')
    save_script('smoke_waterside_second_progression', commands)
    commands = setup() + [check('lotusHitResourcesReady', True), plant('LOTUSPULT', 2, 3),
        zombie('NORMAL', 2, 840, stationary=True), zombie('NORMAL', 1, 840, stationary=True),
        zombie('NORMAL', 3, 840, stationary=True),
        op('set_cabbagepult_shoot_cycle', row=2, col=3, elapsed=2.99),
        op('wait_value', path='bulletCount', equals=1, timeout=8),
        check('particleEffectNameCounts.LotusSeedHit', 0), check('particleEffectNameCounts.LotusFragmentHit', 0),
        op('wait_value', path='particleEffectNameCounts.LotusSeedHit', equals=1, timeout=5),
        op('set_timescale', value=0), op('wait_frames', value=2),
        check('particleEffectNameCounts.LotusFragmentHit', 0),
        op('assert_state', path='particleEffectsByName.LotusSeedHit.0.renderQuadCount', atLeast=7)]
    commands += capture('main_impact', True)
    commands += [op('set_timescale', value=1), op('wait_seconds', value=.2), op('set_timescale', value=0)]
    commands += capture('fragments_in_flight', True)
    commands += [op('set_timescale', value=1),
        op('wait_value', path='particleEffectNameCounts.LotusFragmentHit', equals=2, timeout=3),
        op('set_timescale', value=0), op('wait_frames', value=2),
        check('particleEffectsByName.LotusFragmentHit.0.renderQuadCount', 3),
        check('particleEffectsByName.LotusFragmentHit.1.renderQuadCount', 3)]
    commands += capture('fragment_impacts', True)
    commands += [op('save_level_snapshot', name='after_hits'), op('reload_level_snapshot', name='after_hits'),
        op('wait_state', state='GAME', timeout=30), op('set_timescale', value=0),
        check('particleEffectNameCounts.LotusSeedHit', 0), check('particleEffectNameCounts.LotusFragmentHit', 0)]
    commands += capture('no_replayed_impacts')
    commands += setup() + [plant('LOTUSPULT', 1, 6), zombie('HEAVY_DIVER', 1, 950, stationary=True),
        op('set_cabbagepult_shoot_cycle', row=1, col=6, elapsed=2.99), op('wait_seconds', value=1.8),
        check('zombies.0.diverSubmerged', True), check('zombies.0.bodyHealth', 2920)] + capture('submerged_hit', True)
    commands += setup() + [plant('ANCHORLOTUS', 1, 6), plant('LILYPAD', 1, 7), plant('WALLNUT', 1, 7),
        zombie('SALVAGE', 1, 940), op('wait_seconds', value=2.7),
        check('zombies.0.salvageCount', 1), op('assert_state', path='zombies.0.anchorRootMs', atLeast=1)]
    commands += capture('root_does_not_interrupt', True)
    commands += setup() + [plant('FLOWERPOT', 0, 6), plant('WALLNUT', 0, 6), zombie('SALVAGE', 0, 930),
        op('wait_seconds', value=.4), op('damage_zombie', row=0, damage=2000), op('wait_seconds', value=3),
        check('cells.0.6.foundation', 'PLANT_FLOWERPOT'), check('zombieCount', 0)] + capture('death_cancels')
    save_script('smoke_waterside_second_edges', commands)
    print('Generated 4 visible suites.')


def read(suite, name):
    return json.loads((OUT / suite / (name+'.json')).read_text(encoding='utf-8'))


def verify():
    for suite in ('smoke_lotus_pult', 'smoke_salvage_zombie',
                  'smoke_waterside_second_progression', 'smoke_waterside_second_edges'):
        status = read(suite, 'status')
        assert status['status'] == 'passed' and status['exitCode'] == 0, (suite, status)
    suite = 'smoke_lotus_pult'
    before, wet, dry = [read(suite, n) for n in ('forms', 'flooded', 'retreated')]
    for other in (wet, dry):
        assert {(p['id'], p['health']) for p in before['plants']} == {(p['id'], p['health']) for p in other['plants']}
    for variant in ('split', 'lost_target', 'kill_primary'):
        flying, restored, hit = [read(suite, variant+n) for n in ('_flying', '_restored', '_hit')]
        fragments = [b for b in flying['bullets'] if b['typeName'] == 'BULLET_LOTUS_FRAGMENT']
        assert len(fragments) == 6
        ids = [b['lotus']['targetID'] for b in fragments]
        assert len(set(ids)) == 6
        original = {b['id']: b['lotus'] for b in fragments}
        assert original == {b['id']: b['lotus'] for b in restored['bullets']}
        prior = {z['id']: z for z in flying['zombies']}
        after = {z['id']: z for z in hit['zombies']}
        for identity, z in after.items():
            if z.get('isDying', False) or not prior[identity].get('hasHead', True): continue
            expected = prior[identity]['bodyHealth'] - (20 if identity in ids else 0)
            assert z['bodyHealth'] == expected, (variant, identity, expected, z['bodyHealth'])
        assert hit['bulletCount'] == 0
    parent = read(suite, 'parent_form_preserved')
    assert sorted(z['bodyHealth'] for z in parent['zombies']) == [190, 250]
    suite = 'smoke_salvage_zombie'
    windup, restored = read(suite, 'hook_windup'), read(suite, 'hook_restored')
    for key in ('id', 'salvageTargetID', 'salvageWindupMs', 'salvageCount'):
        assert windup['zombies'][0][key] == restored['zombies'][0][key]
    removed, rescued = read(suite, 'base_removed'), read(suite, 'rescued')
    for field in ('normal', 'pumpkin'):
        # Query by stable semantic layer rather than array ordering.
        kind = 'PLANT_WALLNUT' if field == 'normal' else 'PLANT_PUMPKINSHELL'
        a = next(p for p in windup['plants'] if p['type'] == kind)
        b = next(p for p in removed['plants'] if p['type'] == kind)
        c = next(p for p in rescued['plants'] if p['type'] == kind)
        assert a['id'] == b['id'] == c['id'] and a['health'] == b['health'] == c['health']
    for level, cap in [(102, 1), (103, 2)]:
        for suffix in ('capped', 'simultaneous'):
            state = read('smoke_waterside_second_progression', f'level_{level}_{suffix}')
            assert sum(z['type'] == 'ZOMBIE_SALVAGE' for z in state['zombies']) == cap
    print('PASS: split uniqueness/damage/save, water transitions, salvage identity and dual wave limits.')


if __name__ == '__main__':
    generate() if '--generate' in sys.argv else verify()
