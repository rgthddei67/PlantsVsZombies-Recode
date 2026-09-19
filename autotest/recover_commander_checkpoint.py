"""Recover policy experience when a game batch passed but Python postprocessing stopped."""
import argparse
import json
from pathlib import Path
import re
from train_cold_storage import ROOT, mean, save, score


def recover(script, destination):
    source = json.loads(script.read_text(encoding='utf-8'))
    folder = ROOT / 'build/clang-release/autotest/out' / script.stem
    status = json.loads((folder / 'status.json').read_text())
    if status['status'] != 'passed':
        raise RuntimeError('Only a completely finished engine batch can be recovered')
    log = (folder / 'run.log').read_text(encoding='utf-8')
    if 'FAIL' in log or 'script finished OK' not in log:
        raise RuntimeError('Incomplete or failed engine batch')
    for line in log.splitlines():
        if 'rejected:' in line and not (line.endswith('collect_sun rejected: sun_unavailable')
                                       or line.endswith('player_shovel rejected: no_shovel_target')):
            raise RuntimeError('Unexpected player action rejection: ' + line)
    policies, scores, current = {}, {}, None
    for command in source['commands']:
        if command['op'] == 'commander_experiment':
            current = {'weights': command['weights'], 'preferences': command.get('preferences', {})}
        if command['op'] != 'commander_episode':
            continue
        match = re.fullmatch(r'candidate_(\d+)_case_(\d+)', command['name'])
        if not match or current is None or current['weights'] is None:
            raise RuntimeError('Expected a candidate training batch')
        candidate = int(match[1])
        if candidate in policies and policies[candidate] != current:
            raise RuntimeError('Candidate policy changed between cases')
        policies[candidate] = current
        result_path = folder / (command['name'] + '.json')
        result = json.loads(result_path.read_text(encoding='utf-8'))
        scores.setdefault(candidate, []).append({'score': score(result), 'outcome': result['outcome'],
                                                 'seconds': result['seconds'], 'result': str(result_path)})
    if not policies or len({len(v) for v in scores.values()}) != 1:
        raise RuntimeError('Candidate case counts differ')
    ordered = sorted(policies)
    winner = max(ordered, key=lambda i: mean(scores[i]))
    identity = json.loads((script.parent / 'identity.json').read_text())
    save(destination, {'identity': identity, 'recoveredFrom': str(script.resolve()),
                       'history': [{'generation': 'recovered', 'population': [policies[i] for i in ordered],
                                    'scores': [scores[i] for i in ordered], 'champion': policies[winner],
                                    'trainingScore': mean(scores[winner])}]})
    print(f'Recovered {sum(map(len, scores.values()))} completed games; champion {winner}.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('script', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    recover(args.script, args.destination)
