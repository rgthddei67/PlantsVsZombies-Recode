"""Check live wave projections, the irreversible theft ledger and legal priest rifts."""
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'


def read(suite, name):
    return json.loads((OUT / suite / name).read_text(encoding='utf-8'))


def main():
    gates = {'NORMAL': 1, 'HEALER': 3, 'EXCAVATOR': 3, 'CRYSTAL_HORN_MINER': 3,
             'BUCKET': 5, 'SUN_THIEF': 5, 'REINFORCED_DOOR': 6, 'PINK_FOOTBALL': 8,
             'POLAR_CLOCKMAKER': 9, 'ELITE_JACK_IN_THE_BOX': 10, 'AURORA_PRIEST': 15}
    wave_caps = {'SUN_THIEF': 2, 'CRYSTAL_HORN_MINER': 1, 'POLAR_CLOCKMAKER': 3,
                 'AURORA_PRIEST': 3, 'REINFORCED_DOOR': 2, 'ELITE_JACK_IN_THE_BOX': 2}
    active_caps = {'SUN_THIEF': 3, 'CRYSTAL_HORN_MINER': 2,
                   'POLAR_CLOCKMAKER': 3, 'AURORA_PRIEST': 4}
    for level, waves in ((77, 35), (78, 40)):
        prior_ids = set()
        for wave in range(1, waves+1):
            state = read('smoke_mine_third_waves', f'level{level}_wave{wave}.json')
            assert state['maxWave'] == waves and state['wave'] == wave
            current = state['zombies']
            new = [z for z in current if z['id'] not in prior_ids]
            prior_ids = {z['id'] for z in current}
            spawned = Counter(z['type'].removeprefix('ZOMBIE_') for z in new)
            active = Counter(z['type'].removeprefix('ZOMBIE_') for z in current)
            for zombie in new:
                kind = zombie['type'].removeprefix('ZOMBIE_')
                assert kind in gates and wave >= gates[kind], (level, wave, kind)
                assert zombie['row'] in (1, 3), (level, wave, zombie['row'])
            for kind, cap in wave_caps.items():
                assert spawned[kind] <= cap, (level, wave, kind, spawned)
            for kind, cap in active_caps.items():
                assert active[kind] <= cap, (level, wave, kind, active)
            if level == 77:
                assert set(active) <= {'NORMAL','BUCKET','HEALER','PINK_FOOTBALL','REINFORCED_DOOR','SUN_THIEF'}
                if wave == 5:
                    assert spawned == {'SUN_THIEF': 1, 'NORMAL': 2}
                    assert all(z['row'] == 1 for z in new)

    clock = 'smoke_mine_third_clock'
    refunded = read(clock, 'refunded.json')
    revived = read(clock, 'revived.json')
    assert refunded['sun'] == revived['sun'] == 300
    assert refunded['sunTheftLedger'] == revived['sunTheftLedger']
    escaped = read('smoke_mine_third_retreat', 'escaped.json')
    assert escaped['sun'] == 150 and escaped['zombieCount'] == 0
    assert list(escaped['sunTheftLedger'].values()) == [
        {'carried': 0, 'disabled': False, 'escaped': True, 'stolen': 150}]

    priest = read('smoke_mine_third_combos', 'priest_rifts.json')
    assert priest['weather']['pendingAuroraRiftCount'] == 3
    summons = read('smoke_mine_third_combos', 'priest_summons.json')
    summoned = [z for z in summons['zombies'] if z['type'] != 'ZOMBIE_AURORA_PRIEST']
    assert len(summoned) == 3
    for z in summoned:
        assert z['type'] in {'ZOMBIE_BUCKET','ZOMBIE_DOOR','ZOMBIE_LADDER','ZOMBIE_POGO','ZOMBIE_FOOTBALL'}
        # Spawned walkers expose their committed next cell; it must remain a legal mine path.
        cell = z['mineTargetCell']
        assert cell >= 0 and not summons['mine']['rocks'][cell]
    print('Verified 75 waves, spawn gates/caps, theft conservation and legal priest summons.')


if __name__ == '__main__':
    main()
