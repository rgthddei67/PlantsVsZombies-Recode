"""Generate and verify the real-game waterside pair regression fixtures."""
from pathlib import Path
import argparse
import json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out/smoke_waterside_pair'


def generate():
    commands = [{'op':'assert_state','path':'watersidePairResourcesReady','equals':True}]

    def op(operation, **args): commands.append(dict(op=operation, **args))
    def wait(seconds): op('wait_seconds', value=seconds)
    def dump(name): op('dump_state', name=name + '.json')
    def plant(kind, row, col): op('plant', type='PLANT_' + kind, row=row, col=col)
    def zombie(row, x, kind='HEAVY_DIVER', **args): op('spawn_zombie', type='ZOMBIE_' + kind, row=row, x=x, **args)
    def setup(pool=False):
        op('goto_level', level=101, resetTestState=True, **({'background':'WATER_POOL'} if pool else {}))
        op('choose_cards', cards=['PLANT_ANCHORLOTUS','PLANT_LILYPAD','PLANT_WALLNUT','PLANT_PUMPKINSHELL','PLANT_TANGLEKELP'])
        op('wait_state', state='GAME', timeout=30)
        op('set_spawn_paused', value=True)
        op('set_weather', intensity='CLEAR', duration=600)
        wait(2.2)
        op('set_timescale', value=1)
        op('set_sun', value=20000)
    def bullet(kind, row=2, x=650, damage=100):
        op('spawn_bullet', type='BULLET_' + kind, row=row, x=x, y=280, velocityX=0, damage=damage)
        wait(.15)

    # A >1.5-cell target must be acquired, while a >3-cell target is rejected.
    setup(True)
    plant('ANCHORLOTUS',2,2)
    zombie(2,685,stationary=True)
    wait(.1); dump('range_outside')
    zombie(2,675,stationary=True)
    zombie(3,600,stationary=True)
    zombie(2,380,stationary=True)
    wait(.1); dump('range_inside')
    op('assert_state',path='plants.0.anchorResourcesReady',equals=True)
    op('assert_state',path='plantDefinitions.PLANT_ANCHORLOTUS.simulationBaseHealth',equals=4000)
    op('assert_can_plant',type='PLANT_ANCHORLOTUS',row=0,col=2,expected=False)
    op('assert_can_plant',type='PLANT_ANCHORLOTUS',row=2,col=3,expected=True)
    plant('LILYPAD',2,3)
    op('assert_can_plant',type='PLANT_ANCHORLOTUS',row=2,col=3,expected=False)

    # Two sources share target immunity; pause/save/reload may not refresh it.
    setup(True)
    plant('ANCHORLOTUS',2,2); plant('ANCHORLOTUS',2,3)
    zombie(2,675,stationary=True)
    wait(.2); dump('bound')
    op('set_timescale',value=0); op('save_level_snapshot',name='rooted'); dump('save_before')
    op('reload_level_snapshot',name='rooted'); op('wait_state',state='GAME',timeout=30)
    op('set_spawn_paused',value=True); op('set_timescale',value=0); dump('save_after')
    op('set_timescale',value=1); wait(3.1); dump('immune')
    wait(2.8); dump('cooldown_ready_but_immune')
    wait(1.2); dump('rebound')
    op('shovel_plant_at',row=2,col=2); wait(.1); dump('source_removed')

    # Actual projectile collisions, ordinary direct damage, ash and air cycle.
    setup()
    zombie(2,650,stationary=True); wait(.1); dump('damage_before')
    bullet('PEA'); dump('pea')
    bullet('CABBAGE'); dump('cabbage')
    bullet('FIREBALL'); dump('fireball')
    op('damage_zombie',row=2,damage=100,source='PLANT'); dump('ordinary_damage')
    op('ash_damage_zombie',row=2,damage=100); dump('ash')
    plant('ANCHORLOTUS',2,3)
    wait(7.6); dump('breathing')
    bullet('PEA'); dump('breathing_hit')
    wait(.6); op('screenshot',name='breathing_window.png')
    wait(4); dump('submerged_again')
    op('set_timescale',value=0); op('save_level_snapshot',name='air'); dump('air_before')
    op('reload_level_snapshot',name='air'); op('wait_state',state='GAME',timeout=30)
    op('set_spawn_paused',value=True); op('set_timescale',value=0); dump('air_after')

    # Melee still executes while rooted and only hits the pumpkin top layer.
    setup()
    plant('ANCHORLOTUS',2,3); plant('LILYPAD',2,4); plant('WALLNUT',2,4); plant('PUMPKINSHELL',2,4)
    zombie(2,615)
    wait(.5); dump('windup'); op('screenshot',name='anchor_windup.png')
    op('set_timescale',value=0); op('save_level_snapshot',name='windup')
    op('reload_level_snapshot',name='windup'); op('wait_state',state='GAME',timeout=30)
    op('set_spawn_paused',value=True); op('set_timescale',value=0); dump('windup_restored')
    op('set_timescale',value=1); wait(1); dump('smash_once'); op('screenshot',name='anchor_impact.png')
    wait(3); dump('smash_twice')
    op('butter_zombie',row=2); wait(1); dump('buttered')
    op('charm_zombie',row=2); wait(.1); dump('charmed')

    # Existing kelp consumes itself after five seconds without killing the diver.
    setup()
    plant('TANGLEKELP',2,4); zombie(2,603)
    wait(.3); dump('kelp_grab')
    wait(4); dump('kelp_holding')
    wait(1.2); dump('kelp_released')
    op('damage_zombie',row=2,damage=2500); wait(.1); dump('headless')
    wait(6); dump('death_completed')

    # Natural walking through the mixed shore uses current tile medium.
    setup()
    zombie(2,755)
    dump('crossing_start')
    op('set_timescale',value=5)
    for i in range(10): wait(2); dump('crossing_' + str(i))
    op('set_timescale',value=1)
    op('summon_next_wave',count=12,clearPrevious=True)
    op('wait_frames',value=5); dump('natural_spawn')
    op('quit')
    path=ROOT/'autotest/scripts/smoke_waterside_pair.json'
    path.write_text(json.dumps({'description':'Aquatic placement, 6s/3-cell root, immunity, damage classes, air, smash, kelp, lifecycle and saves.', 'commands':commands},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Real trophy/reward and next-level selection flow uses its own fresh process.
    reward=[
        {'op':'set_adventure_level','level':100}, {'op':'goto_level','level':100},
        {'op':'choose_cards','cards':[]}, {'op':'wait_state','state':'GAME','timeout':30},
        {'op':'set_spawn_paused','value':True},{'op':'force_trophy'}, {'op':'wait_seconds','value':.7},
        {'op':'click','target':'trophy','hold_frames':6},{'op':'wait_seconds','value':4},
        {'op':'assert_state','path':'scene','equals':'PlantRewardScene'},
        {'op':'assert_state','path':'plantAlmanacSelected','equals':'PLANT_ANCHORLOTUS'},
        {'op':'assert_state','path':'plantAlmanacPreviewReady','equals':True},
        {'op':'screenshot','name':'lotus_reward.png'},{'op':'click','x':920,'y':550},
        {'op':'wait_state','state':'CHOOSE_CARD','timeout':30},{'op':'wait_seconds','value':5},
        {'op':'assert_state','path':'level','equals':101},
        {'op':'assert_state','path':'background','equals':'WATERSIDE_GARDEN'},
        {'op':'assert_state','path':'maxWave','equals':20},
        {'op':'assert_state','path':'spawnTypeCount','equals':5},
        {'op':'dump_state','name':'next_level.json'}, {'op':'screenshot','name':'lotus_selection.png'},
        {'op':'choose_cards','cards':['PLANT_ANCHORLOTUS']},{'op':'wait_state','state':'GAME','timeout':30},
        {'op':'set_spawn_paused','value':True},{'op':'wait_seconds','value':2.2},{'op':'set_sun','value':400},
        {'op':'click','x':220,'y':35,'hold_frames':3},{'op':'wait_frames','value':2},{'op':'assert_state','path':'cards.0.selected','equals':True},
        {'op':'move_mouse','x':762,'y':212},{'op':'wait_frames','value':2},{'op':'click','x':762,'y':212,'hold_frames':3},
        {'op':'wait_frames','value':3},{'op':'assert_state','path':'plantCount','equals':1},
        {'op':'assert_state','path':'sun','equals':275},{'op':'assert_state','path':'cards.0.cooldown','equals':True},
        {'op':'dump_state','name':'card_placed.json'},
        {'op':'screenshot','name':'lotus_card.png'},{'op':'quit'}]
    (ROOT/'autotest/scripts/smoke_waterside_pair_reward.json').write_text(json.dumps({'commands':reward},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Generated',len(commands),'gameplay commands and',len(reward),'reward commands')


def verify():
    def state(name): return json.loads((OUT/(name+'.json')).read_text(encoding='utf-8'))
    def z(name): return state(name)['zombies'][0]
    def health(name): return z(name)['bodyHealth']
    outside=state('range_outside'); inside=state('range_inside')
    assert outside['plants'][0]['anchorCount']==0
    assert inside['plants'][0]['anchorCount']==1
    target=next(e for e in inside['zombies'] if e['anchorRooted'])
    source=inside['plants'][0]
    assert 120 < target['x']-source['logicalX'] <= 240 and target['row']==source['row']
    assert sum(e['anchorRooted'] for e in inside['zombies'])==1
    assert 5700 <= source['anchorCooldownMs'] <= 6000
    assert z('bound')['anchorRooted'] and not z('immune')['anchorRooted']
    assert 3400 < z('immune')['anchorRootImmunityMs'] <= 4000
    assert not z('cooldown_ready_but_immune')['anchorRooted']
    assert z('rebound')['anchorRooted']
    assert not z('source_removed')['anchorRooted'] and z('source_removed')['anchorRootImmunityMs']>0
    for before,after,fields in [
        ('save_before','save_after',['anchorRootPlantID','anchorRootMs','diverAirMs']),
        ('air_before','air_after',['diverAirMs','diverBreathing','diverSubmerged']),
        ('windup','windup_restored',['diverSmashCount'])]:
        a,b=z(before),z(after)
        for key in fields: assert a[key]==b[key],(before,key,a[key],b[key])
    assert health('damage_before')-health('pea')==40
    assert health('pea')-health('cabbage')==100
    assert health('cabbage')-health('fireball')==40
    assert health('fireball')-health('ordinary_damage')==100
    assert health('ordinary_damage')-health('ash')==100
    assert z('breathing')['diverBreathing'] and z('breathing')['anchorRooted']
    assert health('breathing')-health('breathing_hit')==100
    assert z('submerged_again')['diverSubmerged']
    assert z('windup')['diverSmashCount']==0 and z('windup')['anchorRooted']
    assert z('smash_once')['diverSmashCount']==1 and z('smash_once')['anchorRooted']
    assert z('smash_twice')['diverSmashCount']==2
    for name,hits in [('smash_once',1),('smash_twice',2)]:
        plants=state(name)['plants']
        lotus=next(p for p in plants if p['type']=='PLANT_ANCHORLOTUS')
        pumpkin=next(p for p in plants if p['type']=='PLANT_PUMPKINSHELL')
        assert lotus['health']==4000
        assert pumpkin['maxHealth']-pumpkin['health']==600*hits
    assert z('buttered')['diverSmashCount']==2
    assert z('charmed')['mindControlled'] and not z('charmed')['anchorRooted']
    assert state('kelp_holding')['plantCount']==1 and health('kelp_holding')==3000
    assert state('kelp_released')['plantCount']==0 and health('kelp_released')==3000
    assert not z('headless')['hasHead'] and state('death_completed')['zombieCount']==0
    crossing=[z('crossing_'+str(i)) for i in range(10)]
    assert not z('crossing_start')['inPool']
    assert any(e['inPool'] for e in crossing) and not crossing[-1]['inPool']
    natural=state('natural_spawn')
    assert any(e['type']=='ZOMBIE_HEAVY_DIVER' for e in natural['zombies'])
    for e in natural['zombies']:
        if e['type']=='ZOMBIE_HEAVY_DIVER': assert e['row'] in [1,2,3,4]
    print('Waterside pair: placement, range, cooldown, immunity, projectile damage, air, melee, kelp, death, save and shore checks passed.')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--generate',action='store_true');args=parser.parse_args()
    generate() if args.generate else verify()
