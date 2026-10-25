"""Compare simultaneous wet/dry rows, then confirm drift resumes after magnetic disarm."""
from pathlib import Path
import json
import sys


def verify(directory):
    def read(name):
        return json.loads((directory / name).read_text(encoding='utf-8'))

    assert read('status.json')['status'] == 'passed'
    before, after = read('before'), read('after')
    starts = {z['id']: z for z in before['zombies']}
    ends = {z['id']: z for z in after['zombies']}
    for kind in ('ZOMBIE_POGO', 'ZOMBIE_ELITE_POGO'):
        pair = [z for z in starts.values() if z['type'] == kind]
        assert len(pair) == 2
        moves = [ends[z['id']]['x'] - z['x'] for z in pair]
        assert all(ends[z['id']]['hasPogo'] for z in pair)
        assert max(moves) - min(moves) < .02, (kind, moves)
        assert all(move < -10 for move in moves), (kind, moves)
    disarmed = read('dismounted_before')
    wet = next(z for z in disarmed['zombies'] if z['row'] == 0)
    assert not wet['hasPogo'] and wet['roofRunoffDriftVelocity'] == -60, wet
    end = next(z for z in read('dismounted_after')['zombies'] if z['id'] == wet['id'])
    assert end['x'] < wet['x'] - 20, (wet['x'], end['x'])
    print('PASS: both pogo variants ignore runoff while bouncing; dismounted walking receives runoff.')


if __name__ == '__main__':
    verify(Path(sys.argv[1]))
