"""Verify legal mixed sparring development and replacement, not commander strength."""
import argparse
import collections
import json
from pathlib import Path


def verify(folder):
    """Check real entity counts, normal opening funds and a new Lotus identity after death."""
    read = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')
    initial, developed, rebuilt = [read(name) for name in ('initial', 'developed', 'rebuilt')]
    assert initial['sun'] == 6000 and initial['coldStorage']['playerIce'] == 375
    counts = collections.Counter(p['type'] for p in developed['plants'] if p['health'] > 0)
    assert counts['PLANT_SUNSHROOM'] == 20, counts
    assert counts['PLANT_THUNDERFLOWER'] == developed['rows']
    assert counts['PLANT_ICESTORAGENUT'] == developed['rows']
    assert counts['PLANT_DAWNLOTUS'] == 1
    old = developed['normalPlantsByCell']['2_3']
    new = rebuilt['normalPlantsByCell']['2_3']
    assert old['type'] == new['type'] == 'PLANT_DAWNLOTUS' and old['id'] != new['id']
    for name in ('stage1', 'stage2', 'stage3', 'rebuild'):
        episode = read(name)
        assert episode['timeScale'] == 5 and not episode['externalSun']['enabled']
        assert not episode['decisions'] and not episode['final']['coldStorage']['deployments']
        c = episode['final']['coldStorage']
        assert c['enemyIce'] == c['initialEnemyIce'] + c['supplied'] + c['workerIncome'] + c['killIncome'] - c['spent']
        assert c['playerIce'] >= 0 and episode['final']['sun'] >= 0
    report = {'passed': True, 'development': dict(counts), 'lotusReplantedAtReservedCell': True,
              'externalResources': False, 'battleStrengthVerified': False}
    (folder / 'sustain_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    verify(parser.parse_args().folder)
