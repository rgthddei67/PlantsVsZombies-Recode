"""Train against mixed defensive players; gate a frozen candidate against the shipped policy."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import random

from train_cold_storage import ROOT, INITIAL, mean, run_batch, save
from train_cold_storage_all import mutate, outcome_gate, restart_policy, selection_key


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def cases(seed, seconds):
    """Use opening play and tactical positions, with different legal defensive controllers."""
    return [(arena, opponent, seed + offset, seconds) for arena, opponent, offset in (
        ('opening', 'adaptive', 11), ('opening_10_2', 'adaptive', 23),
        ('developing', 'adaptive', 37), ('fortress_10_2', 'adaptive', 41),
        ('opening', 'counter', 53), ('opening_10_2', 'growth', 67))]


def release_gate(legacy, incumbent, candidate):
    """Improve the shipped policy on fresh paired cases; retain the legacy outcome floor."""
    return (outcome_gate(incumbent, candidate)
            and selection_key(candidate)[:2] >= selection_key(legacy)[:2])


def perturb(policy, active, rng, scale, costs=None):
    """Spend the mutation budget on units actually available in this curriculum."""
    small = {'weights': policy['weights'],
             'preferences': {name: policy.get('preferences', {}).get(name, [0]*8) for name in active}}
    changed = mutate(small, rng, scale)
    result = copy.deepcopy(policy)
    result['weights'] = changed['weights']
    result.setdefault('preferences', {}).update(changed['preferences'])
    if costs and rng.random() < .75:
        # Correlated steps let real games learn a shared situational budget response.
        # Independent mutations alone almost never move all available troop types together.
        feature = rng.choice((2,3,4,5,6))
        shift = rng.gauss(0,.25*scale)
        for name in active:
            values=result['preferences'][name]
            values[feature]=max(-100,min(100,values[feature]+shift*costs[name]))
    return result


def situational_seed(policy, active, costs, feature, amount):
    """Offer a context-sensitive spending hypothesis, without fixing troops, lanes or results."""
    result=copy.deepcopy(policy)
    for name in active:
        values=result.setdefault('preferences',{}).setdefault(name,[0]*8)
        values[feature]=max(-100,min(100,values[feature]+amount*costs[name]))
    return result


def train(args):
    """Keep selection cases and final holdout disjoint; never select a new winner from holdout."""
    game = ROOT/'build/clang-release'
    output = args.output.resolve()
    incumbent = read(game/'resources/ai/cold_storage_policy.json')
    incumbent = {key: incumbent[key] for key in ('weights', 'preferences')}
    source = read(args.from_checkpoint)['history'][-1]['champion']
    identity = {'exeSha256': hashlib.sha256((game/'PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'trainerSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'coreSha256': hashlib.sha256((ROOT/'autotest/train_cold_storage.py').read_bytes()).hexdigest(),
                'gameDataSha256': hashlib.sha256((game/'resources/gamedata.json').read_bytes()).hexdigest(),
                'incumbent': incumbent, 'source': source, 'seed': args.seed,
                'heldoutSeed': args.heldout_seed, 'generations': args.generations,
                'seconds': args.seconds, 'population': 8, 'curriculum': 'mixed_defense',
                'gate': 'At least 12 paired cases; 2 more wins than shipped and no lost shipped win; legacy outcome floor.'}
    if (output/'identity.json').exists() and read(output/'identity.json') != identity:
        raise RuntimeError('Experiment changed; use a new output directory')
    save(output/'identity.json', identity)
    baseline = run_batch(game, output, output.name+'_preflight', [incumbent], cases(args.seed, args.seconds))[0]
    active = sorted({name for row in baseline for name in read(row['result'])['initial']['coldStorage']['availableUnits']})
    costs = read(baseline[0]['result'])['initial']['coldStorage']['zombieCosts']
    champion = copy.deepcopy(source)
    rng = random.Random(args.seed)
    history = []
    for generation in range(args.generations):
        population = [copy.deepcopy(champion), copy.deepcopy(incumbent)]
        if generation == 0:
            # Separate exploratory seeds expose the existing contextual features to training;
            # heldout scores never choose the feature or mutation direction.
            plain = {'weights': INITIAL[:], 'preferences': {name:[0]*8 for name in active}}
            population += [plain,
                           situational_seed(incumbent,active,costs,5,-.5),
                           situational_seed(incumbent,active,costs,3,-.5)]
        while len(population) < 7:
            population.append(perturb(champion, active, rng, max(.4, 1-generation*.15),costs))
        population.append(restart_policy(champion, rng))
        scores = run_batch(game, output, output.name+f'_generation_{generation}', population,
                           cases(args.seed+1000+generation*100, args.seconds))
        best = max(range(len(population)), key=lambda i:selection_key(scores[i]))
        champion = copy.deepcopy(population[best])
        history.append({'generation':generation, 'population':population, 'scores':scores,
                        'champion':champion, 'trainingScore':mean(scores[best])})
        save(output/'checkpoint.json', {'identity':identity, 'history':history})
    # Choose among distinct generation winners on separate selection cases, then freeze.
    finalists = [incumbent]
    for entry in history:
        if entry['champion'] not in finalists:
            finalists.append(entry['champion'])
    transfer = run_batch(game, output, output.name+'_transfer', finalists, cases(args.seed+9000,args.seconds))
    best = max(range(len(finalists)), key=lambda i:selection_key(transfer[i]))
    champion = finalists[best]
    history.append({'generation':'transfer', 'population':finalists, 'scores':transfer,
                    'champion':champion, 'trainingScore':mean(transfer[best])})
    save(output/'checkpoint.json', {'identity':identity, 'history':history})
    heldout = [case for n in range(3) for case in cases(args.heldout_seed+n*7919,args.seconds)]
    scores = run_batch(game, output, output.name+'_holdout', [None,incumbent,champion], heldout)
    qualified = release_gate(*scores)
    artifact = {'schema':1, 'validated':qualified, **champion, 'identity':identity,
                'validationRoster':'normal', 'wins':[sum(r['outcome']=='commander_win' for r in rows) for rows in scores],
                'means':[mean(rows) for rows in scores], 'pairedCases':len(heldout)}
    save(output/'candidate_policy.json',artifact)
    save(output/'report.json',{'policy':artifact, 'scores':scores, 'heldoutCases':heldout,
                              'preflight':baseline, 'checkpoint':str(output/'checkpoint.json')})
    print(json.dumps({k:artifact[k] for k in ('validated','wins','pairedCases')},indent=2),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--from-checkpoint', type=Path, required=True)
    parser.add_argument('--generations', type=int, default=4)
    parser.add_argument('--seconds', type=int, default=480)
    parser.add_argument('--seed', type=int, default=2509000)
    parser.add_argument('--heldout-seed', type=int, default=3509000)
    args = parser.parse_args()
    if not 1 <= args.generations <= 20 or not 60 <= args.seconds <= 1200:
        parser.error('generations must be 1..20; seconds must be 60..1200')
    train(args)
