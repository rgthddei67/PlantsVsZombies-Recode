"""Frozen-policy transfer test against previously unseen elite mushroom layouts."""
import argparse
import hashlib
import json
from pathlib import Path

from train_cold_storage import ROOT, run_batch, save


def evaluate(args):
    """Compare legacy/global-only/contextual policies without fitting to these results."""
    policy = json.loads(args.policy.read_text(encoding='utf-8'))
    assert policy.get('validated'), 'Use a previously validated, frozen candidate'
    output = args.output.resolve()
    identity = {'policySha256': hashlib.sha256(args.policy.read_bytes()).hexdigest(),
                'exeSha256': hashlib.sha256((ROOT / 'build/clang-release/PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'coreSha256': hashlib.sha256((ROOT / 'autotest/train_cold_storage.py').read_bytes()).hexdigest(),
                'seed': args.seed, 'seconds': args.seconds}
    existing = output / 'identity.json'
    if existing.exists() and json.loads(existing.read_text()) != identity:
        raise RuntimeError('Changed experiment; use a new output directory')
    save(existing, identity)
    cases = [(arena, opponent, args.seed + n * 7919 + offset, args.seconds)
             for n in range(3)
             for arena, opponent, offset in [('elite_cluster', 'bomb', 31), ('elite_spread_10_2', 'deny', 67)]]
    scores = run_batch(ROOT / 'build/clang-release', output, output.name,
                       [None, policy['weights'], policy], cases)
    save(output / 'checkpoint.json', {'history': []})
    save(output / 'report.json', {'policy': policy, 'heldoutCases': cases, 'scores': scores,
                                  'purpose': 'Unseen formation transfer only; does not select or republish policy.'})
    print('Elite-layout wins:', [sum(r['outcome'] == 'commander_win' for r in rows) for rows in scores])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=401000)
    parser.add_argument('--seconds', type=int, default=300)
    args = parser.parse_args()
    if not 60 <= args.seconds <= 1200:
        parser.error('seconds must be 60..1200')
    evaluate(args)
