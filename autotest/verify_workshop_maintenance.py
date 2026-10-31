"""Generate and verify visible cargo cleanup, quotas, preferences and configuration checks."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out/smoke_workshop_maintenance'


def generate():
    commands = []
    def add(op, **kw): commands.append(dict(op=op, **kw))
    def wait(n=5): add('wait_frames', value=n)
    def act(action, index=-1, type='', value=0): add('workshop', action=action, index=index, type=type, value=value)
    def click(control):
        add('click', target='workshop_control', control=control, hold_frames=6, trace=True); wait()
    def button(index):
        add('click', target='message_box_button', index=index, hold_frames=6, trace=True); wait()
    def esc(): add('key', name='escape'); wait()
    def snap(name): add('workshop_snapshot', action='save', name=name)
    def check(path, value): add('assert_state', path='workshop.'+path, equals=value)
    def picture(name):
        add('move_mouse', x=1080, y=590, hold_frames=2); wait()
        add('screenshot', name=name+'.png'); add('dump_state', name=name+'.json')
    def load(name, values):
        add('workshop_snapshot', action='load', name=name,
            patch=[{'op':'add','path':p,'value':v} for p,v in values.items()]); wait()
    wait(90); add('click', target='main_menu_workshop', hold_frames=6, trace=True); wait(20)
    act('pause'); snap('starter'); picture('01_starter_controls')
    load('starter', {'/state/completedWaves':9,'/guideSeenWave':9,'/powerGuideSeen':True,'/state/energy':9000})
    for index, kind in [(2,'melon_grower'),(3,'cooler'),(4,'belt'),(5,'feeder'),(6,'switch')]: act('build', index, kind)
    act('wire_b', 5, value=6)
    snap('layout')
    patch = {'/guideSeenWave':9, '/powerGuideSeen':True,
             '/state/cells/4/extraStock':[0,1,0,0], '/state/cells/5/extraStock':[0,2,1,0],
             '/state/cells/5/filter':3}
    for row in range(5):
        patch[f'/state/lanes/{row}/plainQuota'] = 40-row*5
        patch[f'/state/lanes/{row}/iceQuota'] = 20
        patch[f'/state/lanes/{row}/extraQuota'] = [0,20,row*5]
    load('layout', patch)
    act('select_machine',5); click('sidebar_config'); click('cfg_filter0')
    check('cells.5.activeFilter',4); picture('02_feeder_config')
    click('cfg_cleanup'); picture('03_cleanup_confirm'); esc()
    check('cells.5.extraStock',[0,2,1,0]); click('cfg_cleanup'); click('cleanup_confirm')
    check('cells.4.extraStock',[0,0,0,0]); check('cells.5.extraStock',[0,0,1,0]); picture('04_cleaned_config')
    esc(); act('select_machine',2); click('configure_tool'); click('cfg_upgrade'); check('cells.2.level',2)
    picture('05_upgrade_in_config'); esc()
    # Recreate mismatch and capture the warning above B wiring, using an isolated snapshot.
    snap('cleaned'); load('cleaned', {'/guideSeenWave':9,'/state/cells/4/extraStock':[0,1,0,0]})
    act('select_machine',5); wait(24); picture('06_warning_top')
    click('quota_all'); picture('07_mixed_quotas'); button(10); picture('08_batch_quotas'); esc()
    click('quota_2'); picture('09_single_quota'); button(11); picture('10_all_from_single'); esc()
    click('catalog'); add('click',target='workshop_machine',type=15,hold_frames=6,trace=True); wait(); esc()
    click('catalog'); add('click',target='workshop_machine',type=17,hold_frames=6,trace=True); wait(); esc()
    add('click', x=1070, y=105, hold_frames=6, trace=True); wait(); check('selectedTimeScale',3)
    picture('11_preferences_before'); click('back'); wait(30)
    add('click',target='main_menu_workshop',hold_frames=6,trace=True); wait(25)
    check('selectedTimeScale',3); picture('12_preferences_restored')
    act('view_front'); picture('13_front_shovel'); act('view_factory')
    # Old/malformed optional preferences are ignored; valid entries are deduplicated and bounded.
    load('starter', {'/selectedTimeScale':'invalid','/recentMachines':['unknown',False,'cooler','belt','belt','solar']})
    check('selectedTimeScale',1); check('recentMachines',['belt','solar']); picture('14_legacy_preferences')
    load('layout', {'/guideSeenWave':9,'/powerGuideSeen':True,'/selectedTimeScale':2.5,
                    '/recentMachines':['melon_grower','cooler','feeder','belt','solar']})
    act('select_machine',0); click('configure_tool'); picture('15_solar_config'); esc()
    act('save'); add('quit')
    (ROOT/'autotest/scripts/smoke_workshop_maintenance.json').write_text(
        json.dumps({'name':'smoke_workshop_maintenance','commands':commands},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def verify():
    def read(name): return json.loads((OUT/(name+'.json')).read_text(encoding='utf-8'))
    def state(name): return read(name)['workshop']
    assert read('status')['status'] == 'passed'
    initial = state('01_starter_controls')['visibleControls']
    assert not set(initial) & {'repair','expand','configure_tool','disconnect_tool','rotate_tool','front_ability','front_auto'}
    config = state('02_feeder_config')['configControls']
    assert 'cfg_disconnect_a' not in config and 'cfg_disconnect_b' in config and 'cfg_cleanup' in config
    assert 'cfg_cleanup' not in state('04_cleaned_config')['configControls']
    before, after = state('07_mixed_quotas'), state('08_batch_quotas')
    for row, (old, new) in enumerate(zip(before['lanes'],after['lanes'])):
        assert new['extraQuota'][2] == old['extraQuota'][2] + 5
        assert new['plainQuota'] <= old['plainQuota'] and new['iceQuota'] <= old['iceQuota']
        assert new['plainQuota'] + new['iceQuota'] + sum(new['extraQuota']) == 80
        assert [new[k] for k in ('ammo','iceAmmo','extraAmmo')] == [old[k] for k in ('ammo','iceAmmo','extraAmmo')]
    original, restored = state('11_preferences_before'), state('12_preferences_restored')
    assert original['recentMachines'] == restored['recentMachines']
    assert restored['recentMachines'][:2] == ['melon_grower','cooler']
    assert restored['selectedTimeScale'] == 3
    saved = read('workshop')
    assert saved['selectedTimeScale'] == 2.5 and saved['recentMachines'][:2] == ['melon_grower','cooler']
    assert not state('13_front_shovel')['visibleControls'] or 'front_ability' not in state('13_front_shovel')['visibleControls']
    assert len(list(OUT.glob('*.png'))) == 15
    print('Workshop maintenance passed: cleanup scope/confirmation, config upgrades, mixed batch quotas, real save re-entry and preference validation.')


if __name__ == '__main__':
    generate() if '--generate' in sys.argv else verify()
