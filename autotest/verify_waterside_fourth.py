"""Cross-check real gameplay snapshots by stable entity identity and logical cell."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'
SUITES = ('smoke_float_sunflower', 'smoke_lotus_parasol', 'smoke_lotus_parasol_controls',
          'smoke_waterside_fourth_actions', 'smoke_waterside_fourth_progression', 'smoke_waterside_fourth_preview')
PARASOL = 'ZOMBIE_LOTUS_PARASOL'
FLOWER = 'PLANT_FLOATSUNFLOWER'


def read(suite, name):
    return json.loads((OUT / suite / (name + '.json')).read_text(encoding='utf-8'))


def plants(state):
    return {(p['row'], p['col']): p for p in state['plants'] if p['type'] == FLOWER}


def zombies(state):
    return {z['id']: z for z in state['zombies']}


def verify():
    for suite in SUITES:
        assert read(suite, 'status')['status'] == 'passed', suite
        log = (OUT / suite / 'run.log').read_text(encoding='utf-8')
        assert 'script finished OK' in log and 'FAIL' not in log, suite
    suite = SUITES[0]
    glow, first, restored = [read(suite, n) for n in ('land_glow', 'land_first', 'land_restored')]
    assert glow['normalSunCount'] == 0 and first['normalSunCount'] == restored['normalSunCount'] == 1
    assert plants(first)[2, 3]['id'] == plants(restored)[2, 3]['id']
    assert abs(plants(first)[2, 3]['produceTimerMs'] - plants(restored)[2, 3]['produceTimerMs']) <= 40
    assert read(suite, 'land_second')['normalSunCount'] == 1
    assert read(suite, 'water_first')['normalSunCount'] == 2
    start, progressed = plants(read(suite, 'forms_start')), plants(read(suite, 'fertile_progress'))
    dry_delta = progressed[0, 1]['produceTimerMs'] - start[0, 1]['produceTimerMs']
    fertile_delta = progressed[1, 5]['produceTimerMs'] - start[1, 5]['produceTimerMs']
    assert dry_delta >= 900 and abs(fertile_delta - 4*dry_delta) <= 5, (dry_delta, fertile_delta)
    flooded, reloaded, drained = [plants(read(suite, n)) for n in ('flooded', 'flood_restored', 'drained')]
    assert set(start) == set(flooded) == set(reloaded) == set(drained)
    for cell in start:
        assert start[cell]['id'] == flooded[cell]['id'] == reloaded[cell]['id'] == drained[cell]['id']
        assert flooded[cell]['produceTimerMs'] == reloaded[cell]['produceTimerMs'] == drained[cell]['produceTimerMs']
        assert flooded[cell]['health'] == drained[cell]['health'] == 300
        assert flooded[cell]['produceSunCount'] == 2 and flooded[cell]['watersideHazard'] == 0
        assert drained[cell]['watersideHazard'] == 0

    suite = SUITES[1]
    start = read(suite, 'start')
    keys = {(z['row'], z['gridColumnOn1000']//1000): z['id'] for z in start['zombies']}
    covered = zombies(read(suite, 'covered'))
    for cell in ((1, 5), (3, 7)):
        assert covered[keys[cell]]['bodyHealth'] == 270
    for cell in ((0, 6), (1, 4)):
        assert covered[keys[cell]]['bodyHealth'] == 230
    assert covered[keys[2, 6]]['bodyHealth'] == 1000
    assert covered[keys[2, 6]]['butterTimerMs'] == 0
    before, after = zombies(read(suite, 'overlap')), zombies(read(suite, 'restored'))
    for identity in before:
        for field in ('bodyHealth', 'parasolHealthOn2', 'parasolReflections'):
            assert before[identity].get(field) == after[identity].get(field)
    broken = zombies(read(suite, 'both_broken'))
    assert broken[keys[1, 5]]['bodyHealth'] == 270  # Both breaking blows were fully consumed.
    assert zombies(read(suite, 'unprotected'))[keys[1, 5]]['bodyHealth'] == 190
    assert all(z['bodyHealth'] == 1000 and not z['parasolVisible']
               for z in broken.values() if z['type'] == PARASOL)

    suite = SUITES[2]
    frozen, thawed, buttered, direct = [read(suite, n) for n in ('frozen_hit', 'thawed', 'buttered_hit', 'direct_hit')]
    assert frozen['lotusParasolReflectedCount'] == 0 and thawed['lotusParasolReflectedCount'] == 1
    assert buttered['lotusParasolReflectedCount'] == direct['lotusParasolReflectedCount'] == 1
    for a, b in zip((frozen, thawed), (thawed, buttered)):
        guard_a = next(z for z in a['zombies'] if z['type'] == PARASOL)
        guard_b = next(z for z in b['zombies'] if z['type'] == PARASOL)
        assert guard_a['id'] == guard_b['id'] and guard_a['bodyHealth'] == guard_b['bodyHealth']
    victim = next(z for z in buttered['zombies'] if z['type'] == 'ZOMBIE_BUCKET')
    assert victim['helmHealth'] == 980 and victim['butterTimerMs'] > 0
    guard = next(z for z in direct['zombies'] if z['type'] == PARASOL)
    assert guard['bodyHealth'] == next(z for z in buttered['zombies'] if z['id'] == guard['id'])['bodyHealth'] - 80
    headless = next(z for z in read(suite, 'headless')['zombies'] if z['type'] == PARASOL)
    assert not headless['hasHead'] and not headless['parasolActive'] and not headless['parasolVisible']

    suite = SUITES[3]
    blocked, restored = [read(suite, n) for n in ('lotus_blocked', 'lotus_restored')]
    assert blocked['bulletCount'] == restored['bulletCount'] == 0  # No children escaped a reflected mother seed.
    assert zombies(blocked).keys() == zombies(restored).keys()
    assert all(z['bodyHealth'] == (1000 if z['type'] == PARASOL else 270) for z in blocked['zombies'])
    assert read(suite, 'rooted')['lotusParasolReflectedCount'] == 1
    charmed = next(z for z in read(suite, 'charmed')['zombies'] if z['type'] == PARASOL)
    assert charmed['mindControlled'] and not charmed['parasolActive']
    assert not read(suite, 'cannon_bypasses')['zombies']
    dying = next(z for z in read(suite, 'death_started')['zombies'] if z['type'] == PARASOL)
    assert dying['isDying'] and not dying['parasolActive'] and not dying['parasolVisible']
    assert not read(suite, 'death_finished')['zombies']

    suite = SUITES[4]
    assert read(suite, 'reward')['adventureLevel'] == 106
    assert FLOWER in read(suite, 'reward')['haveCards']
    configs = {entry['level']: entry for entry in json.loads((ROOT / 'build/clang-release/resources/spawnlists.json').read_text())}
    enum = (ROOT / 'PlantVsZombies/Game/Zombie/ZombieType.h').read_text(encoding='utf-8').split('enum class ZombieType')[1]
    names = re.findall(r'^\s*(ZOMBIE_[A-Z0-9_]+)\s*,', enum, re.MULTILINE)
    for level in range(100, 108):
        state = read(suite, f'level_{level}')
        assert state['level'] == level and state['background'] == 'WATERSIDE_GARDEN'
        assert state['maxWave'] == configs[level]['waves']
        assert state['sun'] == configs[level]['sun']
        assert state['spawnTypeCount'] == len(configs[level]['zombies'])
        assert state['spawnList'] == [names[identity] for identity in configs[level]['zombies']]
    for level in (106, 107):
        quota, replacement, charmed = [read(suite, f'{name}_{level}') for name in ('quota', 'replacement', 'charmed_cap')]
        assert all(s['lotusParasolCount'] == 3 for s in (quota, replacement, charmed))
        assert quota['weather']['lotusParasolsSpawnedThisWave'] == 1
        assert replacement['weather']['lotusParasolsSpawnedThisWave'] == 2
        assert charmed['weather']['lotusParasolsSpawnedThisWave'] == 0
        assert any(z['type'] == PARASOL and z['mindControlled'] for z in charmed['zombies'])
    print('PASS: production, flood/save continuity, 3x3 reflection, breaking hit, controls, cannon, lifecycle, rewards and independent spawn caps')


if __name__ == '__main__':
    verify()
