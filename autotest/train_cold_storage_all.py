"""Explore all implemented independent land zombies, then validate on normal rosters.

Special abilities run in the real engine. Per-type context weights learn the
residual value that the small positional predictor does not model explicitly.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import random
import statistics
import subprocess

from train_cold_storage import ROOT, INITIAL, SCALES, episode_commands, mean, run_batch, save, score

CONTEXT = ['bias', 'allied_wounds', 'allied_density', 'front_wall', 'slow', 'fire', 'workers', 'back_protection']
STATE_CONTEXT = ['stockpile','production','plant_fire','plant_economy','counter_readiness','no_progress']


def new_state_model():
    """Neutral trainable state layer; no manually supplied stockpile/attack threshold."""
    return {'schema':1,'featureCount':len(STATE_CONTEXT),'features':STATE_CONTEXT[:],
            'coefficients':[[0.0]*len(INITIAL) for _ in STATE_CONTEXT]}


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def catalog(game, output):
    """Ask the actual game registry for supported independent land units."""
    name = output.name + '_catalog'
    script = output / (name + '.json')
    commands = episode_commands(INITIAL, 11, 'opening', 'bomb', 1, 'catalog_episode', True)
    commands += [{'op': 'dump_state', 'name': 'catalog.json'}, {'op': 'quit'}]
    save(script, {'muteAudio': True, 'batchStepsPerFrame': 32, 'commands': commands})
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(ROOT / 'autotest/run_commander_batch.ps1'), '-GameDirectory', str(game),
                    '-Script', str(script)], check=True)
    folder = game / 'autotest/out' / name
    assert read(folder / 'status.json')['status'] == 'passed'
    state = read(folder / 'catalog.json')['coldStorage']
    assert state['trainingAllUnits']
    names = state['availableUnits']
    assert len(names) > 30 and not any('POOL_' in n or 'DOLPHIN' in n for n in names)
    save(output / 'catalog.json', {'units': names, 'contextFeatures': CONTEXT,
                                  'note': 'Aquatic, summoned-only and visual prototype units excluded.'})
    return names


def mutate(policy, rng, scale):
    """Mutate context coefficients sparsely so rare-unit experience is not erased."""
    result = copy.deepcopy(policy)
    result['weights'] = [max(-500, min(500, rng.gauss(w, s * scale))) for w, s in zip(policy['weights'], SCALES)]
    if result.get('stateModel'):
        for row in result['stateModel']['coefficients']:
            for j in range(len(row)):
                if rng.random() < .35:
                    row[j] = max(-500,min(500,rng.gauss(row[j],SCALES[j]*scale*.5)))
    for name, values in result['preferences'].items():
        if rng.random() < .25:
            for i in range(len(values)):
                if rng.random() < .4:
                    values[i] = round(max(-100, min(100, rng.gauss(values[i], 8 * scale))), 6)
    return result


def restart_policy(policy, rng):
    """Wide restarts explore different objectives instead of only perturbing a rush incumbent."""
    result = copy.deepcopy(policy)
    result['weights'] = [rng.uniform(.5, 5), rng.uniform(.1, 2), rng.uniform(60, 240),
                         rng.uniform(.1, 1), 10 ** rng.uniform(-1, .65), rng.uniform(-2, 1),
                         -10 ** rng.uniform(-.6, .6), rng.uniform(.1, 2)]
    return result


def selection_key(rows):
    """Counter training seeks actual wins first; repeated timeouts must not beat a winning policy."""
    return (sum(r['outcome'] == 'commander_win' for r in rows),
            -sum(r['outcome'] == 'player_win' for r in rows), mean(rows))


def diverse_archive(population, scores, prefer_wins=False):
    """Keep overall and per-scenario elites so economic survival experience isn't discarded."""
    order = [max(range(len(population)), key=lambda i: selection_key(scores[i]) if prefer_wins else mean(scores[i]))]
    order += [max(range(len(population)), key=lambda i: scores[i][case]['score'])
              for case in range(len(scores[0]))]
    return [copy.deepcopy(population[i]) for i in dict.fromkeys(order)]


def outcome_gate(baseline, learned):
    """Prefer real wins; don't reject a faster win for earning fewer farming rewards."""
    old_wins = sum(x['outcome'] == 'commander_win' for x in baseline)
    new_wins = sum(x['outcome'] == 'commander_win' for x in learned)
    lost_old_win = any(a['outcome'] == 'commander_win' and b['outcome'] != 'commander_win'
                       for a, b in zip(baseline, learned))
    return len(learned) >= 12 and new_wins >= old_wins + 2 and not lost_old_win


def train(args):
    game = ROOT / 'build/clang-release'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    identity = {'schema': 2, 'exeSha256': hashlib.sha256((game / 'PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'scriptSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'coreSha256': hashlib.sha256((ROOT / 'autotest/train_cold_storage.py').read_bytes()).hexdigest(),
                'gameDataSha256': hashlib.sha256((game / 'resources/gamedata.json').read_bytes()).hexdigest(),
                'population': args.population, 'generations': args.generations, 'seconds': args.seconds,
                'probeSeconds': args.probe_seconds, 'skipProbes': args.skip_probes, 'seed': args.seed, 'heldoutSeed': args.heldout_seed,
                'curriculum': args.curriculum}
    if args.from_checkpoint:
        identity['sourceSha256'] = hashlib.sha256(args.from_checkpoint.read_bytes()).hexdigest()
    identity_path = output / 'identity.json'
    if identity_path.exists() and read(identity_path) != identity:
        raise RuntimeError('Settings or engine changed; use a new output folder and --from-checkpoint')
    save(identity_path, identity)
    names = catalog(game, output)
    weights = INITIAL[:]
    saved = {}
    if args.from_checkpoint:
        saved = read(args.from_checkpoint)['history'][-1]['champion']
        weights = saved['weights'][:] if isinstance(saved, dict) else saved[:]
    champion = {'weights': weights, 'preferences': {name: [0] * len(CONTEXT) for name in names}}
    if isinstance(saved, dict):
        for name, values in saved.get('preferences', {}).items():
            if name in champion['preferences']:
                champion['preferences'][name] = (values + [0] * len(CONTEXT))[:len(CONTEXT)]
    if args.skip_probes:
        if not args.from_checkpoint:
            raise RuntimeError('--skip-probes requires a source checkpoint with previous unit exploration')
        save(output / 'probes.json', {'baseline': [], 'results': {}, 'inheritedFrom': str(args.from_checkpoint)})
    else:
        # 每个兵种都获得一次合法付费的实际出场机会，避免预测模型把陌生技能永久筛掉。
        probe_cases = [('opening', 'growth', 401, args.probe_seconds),
                       ('fortress', 'bomb', 402, args.probe_seconds)]
        baseline = run_batch(game, output, output.name + '_probe_base', [champion], probe_cases, all_zombies=True)[0]
        probe_results = {}
        for begin in range(0, len(names), 8):
            chunk = names[begin:begin+8]
            candidates = [dict(copy.deepcopy(champion), probe=name) for name in chunk]
            scores = run_batch(game, output, output.name + f'_probe_{begin}', candidates, probe_cases, all_zombies=True)
            for name, rows in zip(chunk, scores):
                for row in rows:
                    result = read(row['result'])
                    assert result['final']['coldStorage']['deploymentTypes'].get(name, 0) >= 1, name
                probe_results[name] = rows
            # 所有探针使用相同冻结策略；探针成绩不能改变后续探针的基线。
            save(output / 'probes.json', {'baseline': baseline, 'results': probe_results})
        for name, rows in probe_results.items():
            champion['preferences'][name][0] = round(max(-10, min(10, (mean(rows) - mean(baseline)) * .05)), 6)
    source_policy = copy.deepcopy(champion)
    incumbent = read(game / 'resources/ai/cold_storage_policy.json') if args.curriculum == 'counter' else source_policy
    incumbent = {key: copy.deepcopy(incumbent[key]) for key in ('weights','preferences')}
    rng = random.Random(args.seed)
    cases = [('normal:opening', 'growth', 601, args.seconds), ('normal:fortress', 'bomb', 602, args.seconds),
             ('fortress_10_2', 'deny', 603, args.seconds),
             ('normal:economy', 'bomb', 604, max(300, args.seconds))]
    if args.curriculum == 'counter':
        cases = [('normal:opening', 'counter', 2601, args.seconds),
                 ('normal:developing', 'counter', 2602, args.seconds),
                 ('normal:fortress_10_2', 'counter', 2603, max(300,args.seconds)),
                 ('normal:economy', 'bomb', 2604, max(300,args.seconds))]
    history = []
    archive = [copy.deepcopy(champion)]
    if args.from_checkpoint:
        prior = read(args.from_checkpoint)
        for generation_data in prior['history']:
            if isinstance(generation_data['champion'], dict):
                # 旧实战只用于选择多样的起点；新版本/新陪练下仍须重新比赛，旧分不进本轮排名。
                prior_scores = [[dict(row, score=score(read(row['result']))) if Path(row['result']).exists() else row
                                 for row in rows] for rows in generation_data['scores']]
                for policy in diverse_archive(generation_data['population'], prior_scores, args.curriculum == 'counter'):
                    normalized = copy.deepcopy(champion)
                    normalized['weights'] = policy['weights'][:]
                    for name, values in policy.get('preferences', {}).items():
                        if name in normalized['preferences']:
                            normalized['preferences'][name] = (values + [0] * len(CONTEXT))[:len(CONTEXT)]
                    archive.append(normalized)
    for generation in range(args.generations):
        population = [copy.deepcopy(champion)]
        population += [mutate(archive[(generation * (args.population-2) + i) % len(archive)], rng, .85 ** generation)
                       for i in range(args.population-2)]
        population.append(restart_policy(archive[-1], rng))
        if args.curriculum == 'counter' and generation == 0 and len(population) >= 3:
            population[1] = copy.deepcopy(incumbent)
            population[2] = {'weights':INITIAL[:], 'preferences':{name:[0]*len(CONTEXT) for name in names}}
            if len(population) >= 5:
                population[4] = dict(copy.deepcopy(population[2]),weights=incumbent['weights'][:])
        scores = run_batch(game, output, output.name + f'_generation_{generation}', population, cases, all_zombies=True)
        best = max(range(len(population)), key=lambda i: selection_key(scores[i]) if args.curriculum == 'counter' else mean(scores[i]))
        champion = population[best]
        archive = diverse_archive(population, scores, args.curriculum == 'counter')
        history.append({'generation': generation, 'population': population, 'scores': scores,
                        'champion': champion, 'trainingScore': mean(scores[best])})
        save(output / 'checkpoint.json', {'identity': identity, 'history': history, 'archive': archive,
                                         'sourceCheckpoint': str(args.from_checkpoint)})
    # 在单独的正式卡池训练场景选择迁移方案，之后冻结参数；留出集不用于挑冠军。
    transfer_cases = [('opening', 'growth', 1201, args.seconds),
                      ('fortress', 'bomb', 1202, args.seconds),
                      ('fortress_10_2', 'deny', 1203, args.seconds),
                      ('economy', 'bomb', 1204, max(300, args.seconds))]
    if args.curriculum == 'counter':
        transfer_cases = [('opening_10_2','counter',3601,args.seconds),
                          ('developing','counter',3602,args.seconds),
                          ('fortress','counter',3603,max(300,args.seconds)),
                          ('opening','growth',3604,args.seconds)]
    transfer = [incumbent, source_policy, champion] if args.curriculum == 'counter' else [source_policy, champion]
    transfer_scores = run_batch(game, output, output.name + '_transfer', transfer, transfer_cases)
    winner = max(range(len(transfer)), key=lambda i: selection_key(transfer_scores[i]) if args.curriculum == 'counter' else mean(transfer_scores[i]))
    champion = transfer[winner]
    history.append({'generation': 'normal_transfer', 'population': transfer, 'scores': transfer_scores,
                    'champion': champion, 'trainingScore': mean(transfer_scores[winner])})
    save(output / 'checkpoint.json', {'identity': identity, 'history': history,
                                     'sourceCheckpoint': str(args.from_checkpoint)})
    # 全兵种实验不能直接充当正式关卡胜率。恢复原卡池与解锁波数，用全新的留出种子检验迁移。
    heldout = []
    for n in range(3):
        seed = args.heldout_seed + n * 7919
        heldout += [('opening', 'bomb', seed + 11, max(240, args.seconds)),
                    ('opening_10_2', 'deny', seed + 23, max(240, args.seconds)),
                    ('fortress', 'growth', seed + 37, args.seconds),
                    ('fortress_10_2', 'deny', seed + 41, args.seconds),
                    ('economy_10_2', 'bomb', seed + 53, max(300, args.seconds))]
    if args.curriculum == 'counter':
        heldout = [(arena,opponent,args.heldout_seed + n*7919 + offset,max(420,args.seconds))
                   for n in range(3) for arena,opponent,offset in
                   [('opening','counter',11),('opening_10_2','counter',23),
                    ('developing','counter',37),('fortress_10_2','counter',41),('opening','growth',53)]]
    # 旧的训练冠军也一起比较，检查全兵种经验是否真能迁移，而非只会利用提前解锁。
    scores = run_batch(game, output, output.name + '_normal_holdout',
                       [None, incumbent if args.curriculum == 'counter' else weights, champion], heldout)
    # 独立经营压力对照只作解释，不用它继续调整已冻结的参数。
    economy_control = run_batch(game, output, output.name + '_economy_control',
                                [champion, dict(copy.deepcopy(champion), noWorkers=True)],
                                [('economy', 'bomb', args.heldout_seed + 999, max(360, args.seconds))])
    qualified = outcome_gate(scores[0], scores[2])
    artifact = {'schema': 1, 'validated': qualified, **champion, 'identity': identity,
                'validationRoster': 'normal', 'contextFeatures': CONTEXT,
                'wins': [sum(x['outcome'] == 'commander_win' for x in rows) for rows in scores],
                'means': [mean(rows) for rows in scores], 'pairedCases': len(heldout)}
    save(output / 'candidate_policy.json', artifact)
    save(output / 'report.json', {'policy': artifact, 'heldoutCases': heldout, 'scores': scores,
                                  'checkpoint': str(output / 'checkpoint.json'), 'probes': str(output / 'probes.json'),
                                  'economyControl': economy_control})
    if args.publish and qualified:
        save(game / 'resources/ai/cold_storage_policy.json', artifact)
        print('Published validated normal-roster policy.', flush=True)
    print(json.dumps({k: artifact[k] for k in ('validated', 'wins', 'means', 'pairedCases')}, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/clang-release/autotest/training/all_land_v1')
    parser.add_argument('--from-checkpoint', type=Path)
    parser.add_argument('--population', type=int, default=4)
    parser.add_argument('--generations', type=int, default=2)
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--probe-seconds', type=int, default=60)
    parser.add_argument('--seed', type=int, default=20260920)
    parser.add_argument('--heldout-seed', type=int, default=51000)
    parser.add_argument('--skip-probes', action='store_true', help='Retain prior per-unit experience and train against a revised opponent')
    parser.add_argument('--curriculum', choices=('mixed','counter'), default='mixed', help='Counter curriculum keeps normal unlocks and adds nuts plus three instant counters')
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    if not (2 <= args.population <= 32 and 0 <= args.generations <= 100
            and 60 <= args.seconds <= 1200 and 30 <= args.probe_seconds <= 300):
        parser.error('population 2..32, generations 0..100, seconds 60..1200, probe-seconds 30..300')
    train(args)
