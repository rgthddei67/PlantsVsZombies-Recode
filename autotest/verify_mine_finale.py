"""Check 9-9 wave gates and legal entrances against recorded formal wave plans."""
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'
GATES = {0: 1, 52: 3, 6: 6, 48: 8, 53: 10, 31: 12, 54: 12, 55: 15, 40: 20, 36: 30}


def read(suite, name):
    return json.loads((OUT / suite / name).read_text(encoding='utf-8'))


def main():
    """Validate all 60 preannounced waves and publish measured first appearances."""
    suite = 'smoke_mine_finale_waves'
    first, total = {}, Counter()
    for wave in range(1, 61):
        state = read(suite, f'plan_{wave:02}.json')
        assert state['mine']['forecastWave'] == wave
        plan = state['mine']['wavePlan']
        assert plan, wave
        counts = Counter(kind for kind, row in plan)
        for kind, row in plan:
            assert kind in GATES and wave >= GATES[kind], (wave, kind)
            assert row in (0, 2, 4), (wave, row)
            first.setdefault(kind, wave)
        for kind, cap in ((52, 1), (53, 1), (54, 2), (55, 1)):
            assert counts[kind] <= cap, (wave, kind, counts)
        total.update(counts)
    assert set(first) == set(GATES), first
    last = read(suite, 'wave60.json')
    assert last['mine']['wavePlan'] == []
    assert last['mine']['pathValid'] and last['maxWave'] == 60
    levels = json.loads((ROOT / 'build/clang-release/resources/spawnlists.json').read_text())
    # Type IDs follow the stable enum order; compare the entire untouched neighbor pools.
    import re
    source = (ROOT / 'PlantVsZombies/Game/Zombie/ZombieType.h').read_text(encoding='utf-8')
    names = re.findall(r'\bZOMBIE_[A-Z_]+\b', source.split('enum class ZombieType {')[1].split('};')[0])
    for level in levels:
        if not 73 <= level['level'] <= 80:
            continue
        state = read('smoke_mine_finale_neighbors', f"level{level['level']}.json")
        assert state['spawnList'] == [names[kind] for kind in level['zombies']]
    completed = read('smoke_mine_finale', 'completed.json')
    assert completed['scene'] == 'GameSelectScene' and completed['adventureLevel'] == 82
    assert 81 in completed['gameSelectVisibleLevels']
    index = completed['gameSelectVisibleLevels'].index(81)
    assert completed['gameSelectVisibleCompleted'][index]
    for name in ('smoke_mine_finale', suite, 'smoke_mine_finale_neighbors'):
        assert read(name, 'status.json')['status'] == 'passed', name
    print('All 60 wave plans respect gates, legal entrances and mine unit caps.')
    print('Measured first appearances:', first)
    print('Planned totals:', dict(total))


if __name__ == '__main__':
    main()
