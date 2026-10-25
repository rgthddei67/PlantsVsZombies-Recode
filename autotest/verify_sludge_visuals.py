"""Check nozzle placement and real equipment pixels through windup, load and cancellation."""
import json
from pathlib import Path

import numpy as np
from PIL import Image

OUT = Path(__file__).resolve().parents[1] / 'build/clang-release/autotest/out/smoke_sludge_visuals'


def verify():
    def state(name):
        return json.loads((OUT / (name + '.json')).read_text(encoding='utf-8'))

    assert state('status')['status'] == 'passed'
    names = ('early', 'charged', 'loaded', 'releasing', 'fired', 'cancelled')
    zombies = {}
    for name in names:
        actors = [z for z in state(name)['zombies'] if z['type'] == 'ZOMBIE_SLUDGE']
        assert len(actors) == 1
        zombies[name] = actors[0]
    assert len({z['id'] for z in zombies.values()}) == 1
    for name in ('early', 'charged', 'loaded', 'releasing'):
        assert zombies[name]['animFrame'] == 0 and zombies[name]['track'] == 'anim_idle'
    for name in ('charged', 'loaded'):
        # The fixed classic body pose scales its equipment by 0.8. These are nozzle
        # coordinates relative to the visual origin, not a moving actor's absolute X/Y.
        x, y = zombies[name]['sludgeMuzzleOffsetOn1000']
        assert 1090 <= x <= 1110 and 90960 <= y <= 91010, (name, x, y)
    for name in ('fired', 'cancelled'):
        assert zombies[name]['sludgeWindupMs'] == 0
        assert zombies[name]['sludgeMuzzleOffsetOn1000'] == [900, 88200]
    assert zombies['fired']['sludgeShots'] == 1 and zombies['cancelled']['sludgeShots'] == 0

    def frame(name):
        return np.asarray(Image.open(OUT / (name + '.png')).convert('RGB'), dtype=np.int16)

    # Stationary, single-actor fixture: sample the backpack, excluding the nozzle
    # pulse and emitted particles so those cannot masquerade as tank inflation.
    x, y = zombies['charged']['xInt'], zombies['charged']['yInt']
    crops = {name: frame(name)[y-45:y-5, x-5:x+30] for name in (*names, 'paused')}
    assert np.array_equal(crops['charged'], crops['loaded']), 'loading changed equipment size'
    assert np.array_equal(crops['loaded'], crops['paused']), 'paused equipment moved'
    for a, b in (('early', 'charged'), ('charged', 'releasing')):
        changed = np.max(np.abs(crops[a] - crops[b]), axis=2) > 8
        assert np.count_nonzero(changed) > 100, (a, b, 'equipment did not follow windup')
    assert np.array_equal(crops['fired'], crops['cancelled']), 'cancellation retained inflation'
    print('PASS: relative nozzle, live tank inflation, pause, save/load, firing and cancellation')


if __name__ == '__main__':
    verify()
