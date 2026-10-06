"""Check captured cannon ownership, removal and real resource-limited sparring inputs."""
import json
from pathlib import Path
import sys


def verify(folder):
    """Keep controlled source tests separate from the normal-opening diagnostic outcome."""
    def read(name):
        return json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    log = (folder / 'run.log').read_text(encoding='utf-8')
    assert 'script finished OK' in log
    assert not [line for line in log.splitlines() if 'rejected:' in line
                and not line.endswith('collect_sun rejected: sun_unavailable')]
    ready, aiming, removed = (read(name) for name in ('ready', 'aiming', 'removed'))
    assert all(report['boardUnchanged'] for report in (ready, aiming, removed))
    initial = read('ready_state')
    ids = {p['id'] for p in initial['plants'] if p['type'] == 'PLANT_COBCANNON' and p['health'] > 0}
    assert len(ids) == len(ready['counterSources']) == 3
    assert ready['cannonAimRight'] == 1100, 'cannon aim must include the visible right margin beyond the final grid cell'
    assert {c['plantID'] for c in ready['counterSources']} == ids
    assert all(not c['consumesPlant'] and c['flightSeconds'] == 2 and c['ready'] == 0
               for c in ready['counterSources'])
    assert any(branch['precisionTargetID'] in ids for branch in ready['branches']), 'free search must consider real cannon removal'
    assert ready['candidatePlans'][0]['counterTrace'], 'ready cannons must threaten an actual army'
    assert not aiming['candidatePlans'][0]['counterTrace'], 'paid removal before launch must cancel future fire'
    assert not removed['counterSources'] and not removed['candidatePlans'][0]['counterTrace']
    after = read('removed_state')['coldStorage']
    before = initial['coldStorage']
    assert after['spent'] - before['spent'] == 180
    assert after['killIncome'] > before['killIncome']
    builder, firing = read('builder'), read('firing')
    assert builder['seconds'] == 75 and builder['initial']['coldStorage']['openingBonusMask'] == 0
    assert not builder['externalSun']['enabled'] and not firing['externalSun']['enabled']
    assert builder['playerPlantings']['PLANT_COBCANNON'] >= 2
    assert builder['playerPlantings']['PLANT_KERNELPULT'] >= 4
    assert builder['playerPlantings']['PLANT_TALLNUT'] >= 1
    assert all(p['col'] == 6 for p in builder['final']['plants'] if p['type'] == 'PLANT_TALLNUT')
    assert firing['final']['cobLaunchSoundRequestCount'] > firing['initial']['cobLaunchSoundRequestCount']
    allowed = {'PLANT_COBCANNON', 'PLANT_KERNELPULT', 'PLANT_TALLNUT', 'PLANT_SUNFLOWER', 'PLANT_MARIGOLD'}
    assert set(builder['playerPlantings']) <= allowed and set(firing['playerPlantings']) <= allowed
    print('PASS: stable cannon sources, paid removal, legal two-kernel upgrades, front tallnuts and actual cannon launch.')


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_cob_cannon'))
