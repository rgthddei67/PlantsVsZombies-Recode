"""Describe how complete openings develop; tactical fixtures are reported separately.

These observations are diagnostic, not extra rewards or a test of whether a
position is theoretically lost. A small bank can still support a winning army.
"""
import argparse
import json
from pathlib import Path

from train_cold_storage import save


def is_opening(arena):
    """Recognize full games by their explicit scenario name, never by their result."""
    return arena.removeprefix('normal:').removeprefix('masked:').startswith('opening')


def trajectory(result):
    """Report actual minute-by-minute cash flow and defense, without guessing causality."""
    initial = result['initial']
    previous = dict(initial['coldStorage'])
    previous_time = 0
    snapshots = []
    trace = result['trace']
    targets = list(range(60, int(result['seconds']) + 1, 60))
    if not targets or targets[-1] != result['seconds']:
        targets.append(result['seconds'])
    for target in targets:
        eligible = [t for t in trace if previous_time < t['seconds'] <= target]
        if not eligible:
            continue
        point = eligible[-1]
        ice = point['ice']
        delta = {k: ice.get(k, 0) - previous.get(k, 0)
                 for k in ('spent', 'supplied', 'workerIncome', 'killIncome')}
        snapshots.append({'from': previous_time, 'to': point['seconds'],
                          'enemyIce': ice['enemyIce'], 'cashFlow': delta,
                          'pendingCount': ice['pendingCount'], 'zombies': point['zombies'],
                          'playerSun': point['sun'], 'playerIce': ice['playerIce'],
                          'plants': point['plants'], 'plantTypes': point.get('plantTypes'),
                          'mowers': point.get('mowers')})
        previous, previous_time = ice, point['seconds']
    return snapshots


def review(evaluation):
    """Keep opening outcomes and prebuilt-position results in distinct groups for each policy."""
    policies = []
    for rows in evaluation['scores']:
        groups = {'openings': [], 'fixtures': []}
        for case, row in zip(evaluation['cases'], rows):
            result = json.loads(Path(row['result']).read_text(encoding='utf-8'))
            group = 'openings' if is_opening(case[0]) else 'fixtures'
            groups[group].append({'arena': case[0], 'opponent': case[1], 'seed': case[2],
                                  'outcome': row['outcome'], 'seconds': result['seconds'],
                                  'trajectory': trajectory(result), 'result': row['result']})
        policies.append(groups)
    return {'schema': 1, 'note': 'Observed trajectories only; low cash does not prove a lost position.',
            'policies': policies}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    evaluation = json.loads((args.directory / 'evaluation.json').read_text(encoding='utf-8'))
    save(args.directory / 'opening_review.json', review(evaluation))
