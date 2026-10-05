"""Verify the real charge transaction and last-ash reserve/release on visible workers."""
import argparse
import json
from pathlib import Path


def verify(folder):
    """Use actual card cooldown, shared wallet and entity state rather than forecast scores."""
    read = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status'] == 'passed'
    log = (folder / 'run.log').read_text(encoding='utf-8-sig')
    assert 'script finished OK' in log and 'player purchased charge off' in log
    enabled, off, held, released = [read(name) for name in ('enabled', 'off', 'held', 'released')]
    assert enabled['weatherStation']['controls'][2]['value'] == 1
    control = off['weatherStation']['controls'][2]
    assert control['value'] == 0 and control['pending'] < 0 and control['protection'] > 0
    assert enabled['coldStorage']['playerIce'] - off['coldStorage']['playerIce'] == 40
    assert not read('hold')['playerPlantings'].get('PLANT_JALAPENO', 0)
    card = next(c for c in held['cards'] if c['gameplayType'] == 'PLANT_JALAPENO')
    assert card['ready'] and held['sun'] >= card['sunCost']
    assert held['coldStorage']['playerIce'] == off['coldStorage']['playerIce']
    assert read('release')['playerPlantings']['PLANT_JALAPENO'] == 1
    after_card = next(c for c in released['cards'] if c['gameplayType'] == 'PLANT_JALAPENO')
    assert not after_card['ready'] and after_card['cooldownRemainingMs'] > 0
    assert held['coldStorage']['playerIce'] - released['coldStorage']['playerIce'] == held['coldStorage']['plantCosts']['PLANT_JALAPENO']
    assert not any(z['type'] == 'ZOMBIE_ICE_WORKER' and z['bodyHealth'] > 0 for z in released['zombies'])
    assert any(z['type'] == 'ZOMBIE_NORMAL' and z['bodyHealth'] > 0 for z in released['zombies'])
    assert all(not z.get('hijackerLocked', False) for z in released['zombies'])
    assert 'player used reserved ash' in log
    for state in (enabled, off, held, released):
        c = state['coldStorage']
        assert c['enemyIce'] == c['initialEnemyIce'] + c['supplied'] + c['workerIncome'] + c['killIncome'] - c['spent']
        assert c['planningApplied'] == 0
    shared_before, shared_after = read('shared_before'), read('shared_after')
    other_ash = read('shared_wallet')
    assert shared_before['coldStorage']['playerIce'] == 65
    assert other_ash['playerPlantings'] == {'PLANT_DOOMSHROOM': 1}
    assert shared_before['coldStorage']['playerIce'] - shared_after['coldStorage']['playerIce'] == shared_before['coldStorage']['plantCosts']['PLANT_DOOMSHROOM']
    assert shared_after['coldStorage']['interferenceCooldownRemaining'] == 0
    held_card = next(c for c in shared_after['cards'] if c['gameplayType'] == 'PLANT_JALAPENO')
    assert held_card['ready'] and shared_after['coldStorage']['playerIce'] >= shared_after['coldStorage']['plantCosts']['PLANT_JALAPENO']
    shop_before, shop_after = read('shop_before'), read('shop_after')
    assert shop_before['sun'] - shop_after['sun'] == 100
    assert shop_after['sun'] >= next(c['sunCost'] for c in shop_after['cards'] if c['gameplayType'] == 'PLANT_JALAPENO')
    assert shop_after['coldStorage']['orderIce'] == 40 or shop_after['coldStorage']['playerIce'] == 45
    air_before, air_after = read('air_before'), read('air_after')
    assert read('air_episode')['playerPlantings'] == {'PLANT_BLOVER': 1}
    blover = next(c for c in air_before['cards'] if c['gameplayType'] == 'PLANT_BLOVER')
    assert air_before['sun'] - air_after['sun'] == blover['sunCost']
    assert air_before['coldStorage']['playerIce'] - air_after['coldStorage']['playerIce'] == air_before['coldStorage']['plantCosts']['PLANT_BLOVER']
    assert next(c for c in air_after['cards'] if c['gameplayType'] == 'PLANT_JALAPENO')['ready']
    assert all(z.get('balloonBloverBlowing', False) for z in air_after['zombies'] if z['type'] == 'ZOMBIE_BALLOON' and z['bodyHealth'] > 0)
    report = {'passed': True, 'chargeOffFee': 40, 'sharedWallet': True,
              'interferencePreservesReservedFee': True, 'reservedForWorkers': True,
              'iceOrderPreservesReservedSun': True, 'urgentAirDefensePreservesAshCooldown': True,
              'noFreeCooldownReset': True, 'battleStrengthVerified': False}
    (folder / 'reserved_ash_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    verify(parser.parse_args().folder)
