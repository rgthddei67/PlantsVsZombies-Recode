"""Verify full-card cannon sparring pays for daytime Doom and Coffee before real damage."""
import json
from pathlib import Path
import sys


def verify(folder):
    """Use stable target identities and actual wallet deltas, without treating the fixture as a match."""
    read = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
    assert read('status')['status'] == 'passed'
    log = (folder / 'run.log').read_text(encoding='utf-8')
    assert 'script finished OK' in log
    assert not [line for line in log.splitlines() if 'rejected:' in line
                and not line.endswith('collect_sun rejected: sun_unavailable')]
    result = read('doom_coffee')
    initial, final = result['initial'], result['final']
    cards = {card['gameplayType'] for card in initial['cards']}
    assert len(initial['cards']) == 11
    assert {'PLANT_DOOMSHROOM', 'PLANT_INSTANT_COFFEE', 'PLANT_CHERRYBOMB',
            'PLANT_JALAPENO', 'PLANT_BLOVER', 'PLANT_COBCANNON', 'PLANT_TALLNUT'} <= cards
    assert initial['coldStorage']['openingBonusMask'] == 0 and not result['externalSun']['enabled']
    assert result['playerPlantings']['PLANT_DOOMSHROOM'] == result['playerPlantings']['PLANT_INSTANT_COFFEE'] == 1
    assert not {'PLANT_DOOMSHROOM', 'PLANT_INSTANT_COFFEE'} & {p['type'] for p in final['plants']}
    cost = sum(count * initial['coldStorage']['plantCosts'][kind] for kind, count in result['playerPlantings'].items())
    assert initial['coldStorage']['playerIce'] - final['coldStorage']['playerIce'] == cost
    surviving = {z['id']: z['bodyHealth'] for z in final['zombies']}
    targets = [z for z in initial['zombies'] if z['type'] == 'ZOMBIE_GARGANTUAR']
    assert len(targets) == 3
    assert all(z['bodyHealth'] - surviving.get(z['id'], 0) >= 1800 for z in targets)
    print('PASS: eleven legal cards, real Doom/Coffee fees and wake-up, stable-target explosion damage.')


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_cob_ash'))
