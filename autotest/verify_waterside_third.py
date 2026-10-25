"""Check real damage, ownership, saves and finite wave budgets from visible AutoTest evidence."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'build/clang-release/autotest/out'


def read(suite, name):
    return json.loads((OUT / suite / (name + '.json')).read_text(encoding='utf-8'))


def plant(state, kind, row=None, col=None):
    items = [p for p in state['plants'] if p['type'] == 'PLANT_' + kind
             and (row is None or p['row'] == row) and (col is None or p['col'] == col)]
    assert len(items) == 1, (kind, row, col, items)
    return items[0]


def verify():
    for suite in ('smoke_thorn_lily', 'smoke_sludge_zombie', 'smoke_sludge_edges', 'smoke_waterside_third_progression', 'smoke_waterside_third_actions'):
        assert read(suite, 'status')['status'] == 'passed', suite
    suite = 'smoke_thorn_lily'
    before, after = read(suite, 'before_upgrade'), read(suite, 'upgraded')
    assert plant(before, 'WALLNUT')['id'] == plant(after, 'WALLNUT')['id']
    assert plant(after, 'THORNLILY')['health'] == 1500
    first, restored = read(suite, 'first_strike'), read(suite, 'thorn_restored')
    assert plant(first, 'THORNLILY')['thornStrikes'] == plant(restored, 'THORNLILY')['thornStrikes'] == 1
    assert all(z['bodyHealth'] == 210 for z in first['zombies'] if z['type'] == 'ZOMBIE_NORMAL')
    assert next(z for z in first['zombies'] if z['type'] == 'ZOMBIE_HEAVY_DIVER')['bodyHealth'] == 2940
    assert next(z for z in first['zombies'] if z['type'] == 'ZOMBIE_BALLOON')['bodyHealth'] == 270
    anchored, drained = read(suite, 'anchored'), read(suite, 'drained')
    for kind in ('WALLNUT', 'FLOWERPOT', 'PUMPKINSHELL'):
        assert plant(anchored, kind)['id'] == plant(drained, kind)['id']
    assert not any(p['type'] == 'PLANT_THORNLILY' for p in drained['plants'])
    assert plant(read(suite, 'salvaged'), 'WALLNUT')['watersideHazard'] == 1
    suite = 'smoke_sludge_zombie'
    repaired = read(suite, 'repair_600')
    assert repaired['particleEffectNameCounts']['SludgeArmorRepair'] == 1
    restored = read(suite, 'spray_restored')
    assert repaired['zombies'][0]['id'] == restored['zombies'][0]['id']
    assert restored['zombies'][0]['sludgeShots'] == 1
    hit = read(suite, 'pumpkin_hit')
    shell = plant(hit, 'PUMPKINSHELL')
    assert shell['maxHealth'] - shell['health'] == 360
    assert shell['sludgeRemainingMs'] > 0
    assert plant(hit, 'WALLNUT')['health'] == 4000
    assert plant(hit, 'WALLNUT')['sludgeRemainingMs'] == 0
    for name, hp, stage in [('repair_600', 600, 2), ('repair_900', 900, 1), ('repair_capped', 1000, 1), ('armor_gone', 0, 0)]:
        z = read(suite, name)['zombies'][0]
        assert z['bodyHealth'] == 800 and z['helmHealth'] == hp and z['coneStage'] == stage, (name, z)
        assert z['coneVisible'] == (hp > 0), name
    assert read(suite, 'armor_gone')['zombies'][0]['sludgeRepaired'] == 700
    suite = 'smoke_sludge_edges'
    blocked = read(suite, 'mirror_blocked')
    assert plant(blocked, 'ICEMIRRORGRASS')['health'] == 300
    assert plant(blocked, 'ICEMIRRORGRASS')['sludgeRemainingMs'] == 0
    assert blocked['zombies'][0]['sludgeRepaired'] == 300
    muddy = read(suite, 'fertile_muddy')
    assert plant(muddy, 'SUNFLOWER')['attackRateMilli'] == 2000
    assert plant(muddy, 'SUNFLOWER')['sludgeRemainingMs'] > 0
    assert plant(read(suite, 'muddy_restored'), 'SUNFLOWER')['sludgeRemainingMs'] == plant(muddy, 'SUNFLOWER')['sludgeRemainingMs']
    assert plant(read(suite, 'mud_expired'), 'SUNFLOWER')['attackRateMilli'] == 4000
    suite = 'smoke_waterside_third_progression'
    for level in (104, 105):
        state = read(suite, f'no_simultaneous_cap_{level}')
        assert len([z for z in state['zombies'] if z['type'] == 'ZOMBIE_SLUDGE']) == 6
        assert state['weather']['sludgeZombiesSpawnedThisWave'] == 3
    suite = 'smoke_waterside_third_actions'
    start, end = read(suite, 'production_start'), read(suite, 'production_end')
    slow = plant(end, 'SUNFLOWER', 0)['produceTimerMs'] - plant(start, 'SUNFLOWER', 0)['produceTimerMs']
    normal = plant(end, 'SUNFLOWER', 5)['produceTimerMs'] - plant(start, 'SUNFLOWER', 5)['produceTimerMs']
    assert abs(slow * 2 - normal) <= 3 and normal >= 1900, (slow, normal)
    assert plant(read(suite, 'combined_slow'), 'SUNFLOWER', 0)['attackRateMilli'] == 400
    heal = read(suite, 'healing_end')
    assert plant(heal, 'MENDINGCOTTON', 0)['cottonHeals'] == 0
    assert plant(heal, 'MENDINGCOTTON', 5)['cottonHeals'] == 1
    assert plant(heal, 'WALLNUT', 0)['health'] == 2000 and plant(heal, 'WALLNUT', 5)['health'] == 2060
    destroyed = read(suite, 'pumpkin_destroyed')
    assert plant(destroyed, 'WALLNUT')['health'] == 4000
    assert plant(destroyed, 'THORNLILY')['sludgeRemainingMs'] == 0
    under = read(suite, 'under_attack')
    assert plant(under, 'WALLNUT')['sludgeRemainingMs'] > 0 and plant(under, 'THORNLILY')['thornStrikes'] == 1
    assert plant(under, 'THORNLILY')['attackRateMilli'] == 1000
    assert plant(read(suite, 'source_dead_hit'), 'WALLNUT')['health'] == 3940
    assert plant(read(suite, 'finite_range'), 'WALLNUT')['health'] == 4000
    print('PASS: damage, layered support, repair/cap/loss, interception, actual production/healing, saves and wave limits')


if __name__ == '__main__':
    verify()
