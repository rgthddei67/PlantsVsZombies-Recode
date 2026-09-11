"""Check recorded mine-pair runtime gates, roster limits, and route snapshots."""
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'


def read(case, name):
    return json.loads((OUT / case / name).read_text(encoding='utf-8'))


def verify():
    """Validate observable contracts from formal wave and lifecycle runs."""
    gates = dict(ZOMBIE_NORMAL=1, ZOMBIE_BUCKET=5, ZOMBIE_PINK_FOOTBALL=8,
                 ZOMBIE_GARGANTUAR=15, ZOMBIE_POLAR_CLOCKMAKER=9,
                 ZOMBIE_CRYSTAL_HORN_MINER=3, ZOMBIE_EXCAVATOR=3,
                 ZOMBIE_HEALER=3, ZOMBIE_ZAMBONI=8)
    base = set(gates) - {'ZOMBIE_EXCAVATOR', 'ZOMBIE_HEALER', 'ZOMBIE_ZAMBONI'}
    for level, maximum in ((75, 30), (76, 40)):
        state = read('smoke_mine_pair_waves', f'level{level}_last.json')
        assert state['wave'] == state['maxWave'] == maximum
        zombies = state['zombies']
        expected = base if level == 75 else set(gates)
        assert {z['type'] for z in zombies} == expected
        for z in zombies:
            assert z['spawnWave'] >= gates[z['type']], (level, z['type'], z['spawnWave'])
            if z['type'] == 'ZOMBIE_ZAMBONI':
                assert z['row'] == 0
        counts = Counter(z['spawnWave'] for z in zombies if z['type'] == 'ZOMBIE_CRYSTAL_HORN_MINER')
        assert max(counts.values(), default=0) <= 1 and sum(counts.values()) <= 2
        first = read('smoke_mine_pair_waves', f'level{level}_wave10.json')
        again = read('smoke_mine_pair_waves', f'level{level}_wave14.json')
        assert first['mine']['fogTutorialSeen'] and first['mine']['fogElapsedMs'] >= 0
        assert again['mine']['fogNextWave'] == 14 and again['mine']['fogElapsedMs'] >= 0
        print(f'{level}: full roster, {maximum} waves, gates, caps, ice-car lane, fog recurrence OK')
    teaching = read('smoke_mine_pair_waves', 'level75_wave3.json')
    third = [z for z in teaching['zombies'] if z['spawnWave'] == 3]
    assert Counter(z['type'] for z in third) == {'ZOMBIE_CRYSTAL_HORN_MINER': 1, 'ZOMBIE_NORMAL': 2}
    assert all(z['row'] == 0 for z in third)
    impact = read('smoke_mine_pair_edges', 'exact_impact.json')
    layers = [p for p in impact['plants'] if p['row'] == 0 and p['col'] == 3]
    assert {p['type']: p['health'] for p in layers} == {'PLANT_WALLNUT': 4000, 'PLANT_PUMPKINSHELL': 3500}
    orphan = read('smoke_mine_pair_echo_routes', 'orphan_wave.json')
    assert not orphan['plants'] and not orphan['echoWaves'] and orphan['zombies'][0]['helmHealth'] == 1000
    for name in ('imp_landed.json', 'imp_rock_fallback.json'):
        state = read('smoke_mine_pair_vehicles', name)
        imp = state['zombiesByType']['ZOMBIE_IMP']
        column = round((imp['x'] - 282) / 80)
        assert imp['impPhase'] == 'WALKING' and 0 <= column < 9
        cell = imp['row'] * 9 + column
        assert not state['mine']['rocks'][cell] and state['mine']['connected'][cell]
    print('Teaching, exact top-layer impact, orphan wave, and legal Imp landing OK')


if __name__ == '__main__':
    verify()
