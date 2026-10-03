"""Validate cross-snapshot Chapter 11 contracts after both visible smoke scripts."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/clang-release/autotest/out'


def read(case, name):
    return json.loads((OUT / case / name).read_text(encoding='utf-8'))


def main():
    geometry = read('smoke_weather_station', '02_cell_geometry.json')
    assert (geometry['rows'], geometry['columns']) == (5, 9)
    # Verify every cell, then compare the independent plant anchors at the corners/centre.
    for r, row in enumerate(geometry['cells']):
        for c, cell in enumerate(row):
            assert (cell['centerXInt'], cell['centerYInt']) == (282 + 80*c, 138 + 100*r)
    for plant in geometry['plants']:
        cell = geometry['cells'][plant['row']][plant['col']]
        assert (plant['logicalX'], plant['logicalY']) == (cell['centerXInt'], cell['centerYInt'])
        assert not plant['sleeping']
    closed = read('smoke_weather_station', '05_charge_closed.json')
    retained = read('smoke_weather_station', '06_charge_retained.json')
    assert closed['weatherStation']['controls'][2]['value'] == 0
    assert closed['weather']['nightRoofCharge']['chargePct'] == retained['weather']['nightRoofCharge']['chargePct']
    before = read('smoke_weather_station_interactions', '04_before_discharge.json')
    after = read('smoke_weather_station_interactions', '05_discharge.json')
    def health(state):
        z = state['zombiesByType']['ZOMBIE_BUCKET']
        return z['bodyHealth'] + z['helmHealth'] + z['shieldHealth']
    assert health(before)-health(after) == 200
    ai = read('smoke_weather_station_interactions', '06_ai_commit.json')['coldStorage']
    assert ai['commanderStrategy'] == 'learned_search' and ai['searchSerial'] > 0
    assert ai['planningApplied'] > 0
    station = read('smoke_weather_station_interactions', '06_ai_commit.json')['weatherStation']
    assert any(c['pending'] >= 0 and not c['player'] for c in station['controls'])
    assert ai['pending'], 'same decision must combine a device and actual troop orders'
    device_prices = ((30,20,40,60),(40,20,30,40,60),(40,40))
    device_spent = sum(device_prices[i][c['pending']] for i,c in enumerate(station['controls'])
                       if c['pending'] >= 0 and not c['player'])
    assert ai['commanderSpent'] == sum(p['cost'] for p in ai['pending']) + device_spent
    levels = json.loads((ROOT/'build/clang-release/resources/spawnlists.json').read_text(encoding='utf-8'))
    chapter = sorted((v for v in levels if 91 <= v['level'] <= 99), key=lambda v:v['level'])
    assert len(chapter) == 9
    for a,b in zip(chapter,chapter[1:]):
        assert set(a['zombies']) <= set(b['zombies'])
    print('PASS: cell/plant geometry, night state, retained charge, dry discharge, real AI commit and cumulative roster')


if __name__ == '__main__':
    main()
