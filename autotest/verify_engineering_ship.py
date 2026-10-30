"""Cross-check engineering-ship gameplay by stable identity and locked cells."""
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/clang-release/autotest/out'
SUITES = ('smoke_engineering_ship_progression', 'smoke_engineering_ship_combat',
          'smoke_engineering_ship_controls', 'smoke_engineering_ship_edges',
          'smoke_engineering_ship_targeting')
SHIP = 'ZOMBIE_ENGINEERING_SHIP'


def read(suite, name):
    return json.loads((OUT / suite / (name + '.json')).read_text(encoding='utf-8'))


def plants(state):
    return {p['id']: p for p in state['plants']}


def verify():
    for suite in SUITES:
        assert read(suite, 'status')['status'] == 'passed', suite
        log = (OUT / suite / 'run.log').read_text(encoding='utf-8')
        assert 'script finished OK' in log and 'FAIL' not in log, suite
    suite = SUITES[0]
    before, after = [read(suite, n)['zombieAlmanacEntries'] for n in ('almanac_before', 'almanac_after')]
    assert SHIP not in before and set(after) - set(before) == {SHIP}
    cfg = read(suite, 'config')
    assert cfg['maxWave'] == 40 and cfg['sun'] == 500 and cfg['spawnTypeCount'] == 10
    first = read(suite, 'final_wave')
    counts = Counter(z['type'] for z in first['zombies'])
    assert counts[SHIP] == 1 and counts['ZOMBIE_GARGANTUAR'] == 0
    assert counts['ZOMBIE_NORMAL'] + counts['ZOMBIE_POOL_NORMAL'] == 4
    assert counts['ZOMBIE_BUCKET'] + counts['ZOMBIE_POOL_BUCKET'] == 2
    restored = read(suite, 'final_wave_reload')
    assert {z['id'] for z in first['zombies']} == {z['id'] for z in restored['zombies']}
    half, restored = [read(suite, n) for n in ('half', 'half_reload')]
    assert len(half['zombies']) == len(restored['zombies']) == 13
    assert half['engineeringShip']['shipReinforcementCount'] == 6
    assert {z['id'] for z in half['zombies']} == {z['id'] for z in restored['zombies']}
    assert read(suite, 'boss_dead')['trophy'] is None and read(suite, 'cleared')['trophy'] is not None
    assert read(suite, 'reward')['adventureLevel'] == 109
    assert set(read(suite, 'reward')['haveCards']) == set(first['haveCards'])

    suite = SUITES[1]
    start, end = [plants(read(suite, n)) for n in ('charging', 'impact')]
    assert start.keys() == end.keys()
    for identity, initial in start.items():
        loss = 300 if initial['type'] in ('PLANT_WALLNUT', 'PLANT_PUMPKINSHELL') else 0
        assert initial['health'] - end[identity]['health'] == loss, initial
    restored = read(suite, 'impact_reload')
    assert {(i, p['health']) for i, p in plants(restored).items()} == {(i, p['health']) for i, p in end.items()}
    assert restored['engineeringShip']['shipShotCount'] == 1

    suite = SUITES[2]
    before, paused = [read(suite, n)['engineeringShip'] for n in ('before_butter', 'butter_paused')]
    assert abs(before['shipPhaseRemainingMs'] - paused['shipPhaseRemainingMs']) <= 40
    assert read(suite, 'butter_immunity')['engineeringShip']['shipButterImmunityMs'] > 4500
    assert read(suite, 'bell_interrupt')['engineeringShip']['shipShotCount'] == 0
    before, frozen = [read(suite, n)['engineeringShip'] for n in ('before_freeze', 'frozen_pause')]
    assert abs(before['shipPhaseRemainingMs'] - frozen['shipPhaseRemainingMs']) <= 40
    moved, hit = [plants(read(suite, n)) for n in ('moved_after_lock', 'locked_impact')]
    assert moved.keys() == hit.keys()
    for identity, initial in moved.items():
        loss = 300 if initial['type'] == 'PLANT_WALLNUT' and initial['col'] in (4, 5) else 0
        assert initial['health'] - hit[identity]['health'] == loss, initial
    assert read(suite, 'anchor_cannon')['engineeringShip']['shipShotCount'] == 1
    assert read(suite, 'anchor_cannon')['engineeringShip']['anchorRooted']

    suite = SUITES[3]
    half, restored, released = [read(suite, n) for n in ('quota_half', 'quota_reload', 'quota_released')]
    for state in (half, restored, released):
        assert state['engineeringShip']['shipReinforcementCount'] == 5
    for state in (half, restored):
        assert sum(z['type'] == 'ZOMBIE_SALVAGE' and z['bodyHealth'] > 0 for z in state['zombies']) == 2
    assert not any(z['type'] == 'ZOMBIE_SALVAGE' and z['bodyHealth'] > 0 for z in released['zombies'])
    assert read(suite, 'lethal_no_guards')['zombieCount'] == 0
    for name in ('kelp_survived', 'mower_survived'):
        assert read(suite, name)['engineeringShip']['bodyHealth'] == 24000
    for name in ('pea_fertile', 'cabbage_fertile'):
        assert 23000 <= read(SUITES[4], name)['engineeringShip']['bodyHealth'] <= 23990
    print('Engineering ship: progression, locked-cell damage, controls, guard quotas and saves passed.')


if __name__ == '__main__':
    verify()
