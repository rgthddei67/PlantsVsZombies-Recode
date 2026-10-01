"""Compare stored-doom forecasts with the real coffee transaction and stable-ID blast results."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_commander_stored_doom')


def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))


assert read('status')['status'] == 'passed'
assert 'script finished OK' in (root / 'run.log').read_text(encoding='utf-8')
before, actual = read('coffee-waiting-before'), read('actual-blast')
doom = next(p for p in before['plants'] if p['type'] == 'PLANT_DOOMSHROOM')
cx, cy = doom['logicalX'], doom['logicalY']
hits = set()
for zombie in before['zombies']:
    nx = max(zombie['x'] - 25, min(cx, zombie['x'] + 25))
    ny = max(zombie['y'] - 65, min(cy, zombie['y'] + 35))
    if (cx - nx) ** 2 + (cy - ny) ** 2 <= 250 ** 2:
        hits.add(zombie['id'])
assert len(hits) == 3
after_by_id = {z['id']: z for z in actual['zombies']}
for zombie in before['zombies']:
    survivor = after_by_id.get(zombie['id'])
    if zombie['id'] in hits:
        assert survivor is None or survivor['bodyHealth'] == 0
    else:
        assert survivor is not None and survivor['bodyHealth'] == zombie['bodyHealth']
expected_loss = len(hits) * before['coldStorage']['zombieCosts']['ZOMBIE_NORMAL']
for name in ('coffee-waiting-forecast', 'waking-forecast', 'charging-forecast'):
    state = read(name)
    assert state['coldStorage']['searchBaselineFeatures'][6] == expected_loss
    assert state['coldStorage']['playerIce'] == before['coldStorage']['playerIce']
    assert state['sun'] == before['sun'], 'committed coffee must not be charged twice'
    assert state['cards'][0]['cooldownRemainingMs'] > 0
assert not any(p['type'] == 'PLANT_DOOMSHROOM' for p in actual['plants'])
stored_before, forecast = read('stored-before'), read('stored-forecast')
assert forecast['coldStorage']['searchCounterHoldSeconds'] == 32
assert forecast['coldStorage']['searchBaselineFeatures'][6] == 72
assert forecast['coldStorage']['enemyIce'] == stored_before['coldStorage']['enemyIce']
assert forecast['coldStorage']['playerIce'] == stored_before['coldStorage']['playerIce']
print(f'PASS: real blast hit {len(hits)} stable IDs, all three committed phases predicted {expected_loss} ice loss; '
      'stored late-wave forecast includes 72 ice loss with a 32-second hold.')
