"""Generate and verify the visible, isolated workshop practice scenario."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def generate():
    commands = []
    def add(op, **kw): commands.append(dict(op=op, **kw))
    def wait(frames=4): add('wait_frames', value=frames)
    def click(control):
        add('click', target='workshop_control', control=control, hold_frames=4, trace=True)
        wait()
    def p(control): click('practice_' + control)
    def act(action, index=-1, value=0, type=''): add('workshop', action=action, index=index, value=value, type=type)
    def check(path, value): add('assert_state', path='workshop.' + path, equals=value)
    def step(value): check('practice.step', value)
    def esc(): add('key', name='escape'); wait()
    def snap(name): add('workshop_snapshot', action='save', name=name)
    def picture(name):
        add('move_mouse', x=1080, y=590, hold_frames=2); wait()
        add('screenshot', name=name + '.png'); add('dump_state', name=name + '.json')
    def ticks(count):
        for _ in range(count): p('tick')
    def wire(port, source, target):
        p('wire_' + port); p('cell_' + str(source)); p('cell_' + str(target))
    wait(90); add('click', target='main_menu_workshop', hold_frames=6, trace=True); wait(20)
    act('pause'); snap('starter'); add('dump_state', name='initial.json')
    click('help'); click('practice'); check('practiceOpen', True)
    # Starter has only the first two lessons. Disabled control must not start a locked lesson.
    add('click', x=672, y=104, hold_frames=4, trace=True); wait(); check('practice.lesson', 0); picture('01_starter_locked')
    esc(); check('guideOpen', True); esc(); check('modalOpen', False)
    add('workshop_snapshot', action='load', name='starter', patch=[
        {'op':'add','path':'/state/completedWaves','value':2},
        {'op':'add','path':'/guideSeenWave','value':2},
        {'op':'add','path':'/state/energy','value':10000}])
    click('help'); click('practice'); snap('practice_before')
    p('belt'); p('cell_24'); step(0); check('practice.world.cells.24.type', 'empty')
    p('cell_22'); step(1); p('feeder'); p('cell_23'); step(2); p('route'); step(3)
    picture('02_supply_built')
    # Exercise real wall-time playback while the official world remains paused.
    p('run'); add('wait_value', path='workshop.practice.complete', equals=True, timeout=15)
    picture('03_supply_delivered'); check('practice.running', False); p('next')
    p('tick'); step(1); picture('04_overloaded'); p('cell_3'); p('power'); step(2); p('tick'); check('practice.complete', True)
    picture('05_power_released'); p('next'); check('practice.lesson', 2)
    p('cell_5'); p('route'); p('route'); step(1); p('cell_23'); p('route2'); step(2)
    wire('a', 5, 23); p('tick'); step(2); picture('06_wrong_a')
    p('disconnect'); wire('b', 5, 23); step(3); p('enemy_plus'); ticks(4); step(4); picture('07_b_high')
    p('enemy_minus'); ticks(4); check('practice.complete', True); picture('08_b_low'); p('next')
    p('cell_5'); p('route'); p('route'); step(1); p('filter'); step(2)
    for _ in range(5): p('setting_minus')
    step(3); wire('a', 5, 7); step(4); wire('a', 7, 21); step(5)
    p('stock_full'); step(5); p('tick'); step(6); picture('09_stock_stop')
    p('stock_empty'); ticks(4); check('practice.complete', True); picture('10_stock_restart')
    snap('practice_after'); p('reset'); step(0); picture('11_replay'); esc(); check('guideOpen', True); esc()
    check('modalOpen', False); add('dump_state', name='lessons_closed.json')
    # Concrete two-route rule and per-ammo blockage, mirroring the user's design, in this isolated save.
    for index, machine in [(5,'enemy'), (6,'enemy'), (7,'not'), (8,'and'), (23,'feeder')]: act('build', index, type=machine)
    act('route',5,2); act('route',6,1); act('wire_a',7,6); act('wire_a',8,5); act('wire_b',8,7); act('wire_b',23,8)
    act('route0',23,1); act('route1',23,2); act('filter0',23,0); act('filter1',23,0)
    snap('diagnostic_fixture')
    add('workshop_snapshot', action='load', name='diagnostic_fixture', patch=[
        {'op':'add','path':'/state/lanes/1/ammo','value':10},
        {'op':'add','path':'/state/lanes/1/plainQuota','value':10},
        {'op':'add','path':'/state/lanes/1/iceQuota','value':70}])
    act('select_machine',23); picture('12_ports_on_base'); click('configure_tool'); click('cfg_reason')
    snap('reason_before'); picture('13_actual_reason'); check('reasonOpen', True)
    wait(60); snap('reason_after'); esc(); check('configOpen', True); esc(); check('modalOpen', False)
    add('click', x=946, y=473, hold_frames=4, trace=True); wait(); picture('14_quota_consumers'); esc()
    # Verify modal pausing against a real running Board, not only preparation.
    act('start'); add('wait_seconds',value=2); act('pause'); click('help'); click('practice')
    snap('assault_before'); p('run'); wait(90); p('run'); snap('assault_after')
    esc(); check('guideOpen', True); esc(); check('effectivePaused', True); check('modalOpen', False)
    add('dump_state', name='finished.json'); add('quit')
    (ROOT/'autotest/scripts/smoke_workshop_practice.json').write_text(json.dumps({'name':'smoke_workshop_practice','commands':commands},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def canonical(value):
    if isinstance(value,dict): return {k:canonical(v) for k,v in value.items()}
    if isinstance(value,list):
        if value and all(isinstance(v,dict) and 'id' in v for v in value): value=sorted(value,key=lambda v:v['id'])
        return [canonical(v) for v in value]
    return value


def verify():
    out=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/clang-release/autotest/out/smoke_workshop_practice'
    def read(name): return canonical(json.loads((out/(name+'.json')).read_text(encoding='utf-8')))
    assert read('status')['status']=='passed'
    for prefix in ('practice','reason','assault'):
        assert read(prefix+'_before')==read(prefix+'_after'), f'{prefix} altered the real base or native combat'
    def lesson(name): return read(name)['workshop']['practice']
    assert lesson('03_supply_delivered')['world']['lanes'][2]['ammo']>0
    assert lesson('04_overloaded')['world']['power']['speed']<1
    assert lesson('05_power_released')['world']['power']['speed']==1
    high,low=lesson('07_b_high'),lesson('08_b_low')
    assert high['world']['cells'][5]['signal'] and not low['world']['cells'][5]['signal']
    assert high['world']['cells'][23]['activePlan']==1 and low['world']['cells'][23]['activePlan']==0
    stopped,restarted=lesson('09_stock_stop'),lesson('10_stock_restart')
    assert stopped['world']['cells'][21]['power']==0 and restarted['world']['produced']>0
    explanation=' '.join(read('13_actual_reason')['workshop']['selectedReasons'])
    assert '第3路有僵尸' in explanation and '第2路没有僵尸' in explanation and '配额已满：10/10' in explanation
    for name in ('lessons_closed','finished'):
        w=read(name)['workshop']
        assert not w['modalOpen'] and not w['practiceOpen'] and not w['reasonOpen']
        assert w['uiButtonCount']==read('initial')['workshop']['uiButtonCount'], 'modal button leak'
    assert len(list(out.glob('*.png')))==14
    print('Workshop practice passed: real input in four lessons, incorrect port feedback, isolation during native combat, diagnostics and UI cleanup')


if __name__=='__main__':
    generate() if '--generate' in sys.argv else verify()
