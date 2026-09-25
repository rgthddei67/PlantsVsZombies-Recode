"""Mixed full, masked and campaign-roster real-engine training with frozen holdouts.

Candidates are evidence artifacts only. This runner never replaces the shipped
policy: full-roster success alone cannot authorize a campaign-wide release.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import random
import secrets

from commander_calibration import fit, metrics, samples
from train_cold_storage import ROOT, run_batch, save
from train_cold_storage_all import CONTEXT, catalog, mutate, selection_key


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def grouped_key(rows, cases):
    """Protect the weakest roster family before using bounded score to break ties."""
    groups = [[row for row, case in zip(rows,cases) if family(case[0]) == mode]
              for mode in ('full','masked','normal')]
    present = [g for g in groups if g]
    return (min(sum(r['outcome'] == 'commander_win' for r in g)/len(g) for g in present),
            *selection_key(rows))


def family(arena):
    return 'normal' if arena.startswith('normal:') else 'masked' if arena.startswith('masked:') else 'full'


def gate(before, after, cases):
    """Require actual gains and no lost incumbent wins in each roster family."""
    report = {}
    for mode in ('full','masked','normal'):
        pairs = [(a,b) for a,b,c in zip(before,after,cases) if family(c[0]) == mode]
        old = sum(a['outcome'] == 'commander_win' for a,b in pairs)
        new = sum(b['outcome'] == 'commander_win' for a,b in pairs)
        lost = sum(a['outcome'] == 'commander_win' and b['outcome'] != 'commander_win' for a,b in pairs)
        report[mode] = {'games': len(pairs), 'oldWins': old, 'newWins': new, 'lostWins': lost,
                        'passed': len(pairs) >= 3 and new >= old and lost == 0}
    report['passed'] = all(report[m]['passed'] for m in ('full','masked','normal')) and sum(
        report[m]['newWins'] - report[m]['oldWins'] for m in ('full','masked','normal')) >= 2
    return report


def train(args):
    """Collect, fit, select, then freeze before unseen paired games; keep every phase resumable."""
    game = ROOT / 'build/clang-release'
    output = args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    identity_path = output / 'identity.json'
    previous = read(identity_path) if identity_path.exists() else {}
    seed = args.seed if args.seed is not None else previous.get('seed',secrets.randbelow(2**30))
    tracked = [Path(__file__), ROOT/'autotest/train_cold_storage.py', ROOT/'autotest/train_cold_storage_all.py',
               ROOT/'autotest/commander_calibration.py', game/'PlantsVsZombies.exe',
               game/'resources/gamedata.json',game/'resources/ai/cold_storage_policy.json']
    if args.from_candidate:
        tracked.append(args.from_candidate.resolve())
    identity = {'schema':1,'seed':seed,'seconds':args.seconds,'generations':args.generations,
                'hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}}
    if previous and previous != identity:
        raise RuntimeError('Engine, policy or trainer changed; use a new output directory')
    save(identity_path,identity)
    names = read(output/'catalog.json')['units'] if (output/'catalog.json').exists() else catalog(game,output)
    incumbent = read(game/'resources/ai/cold_storage_policy.json')
    source = {k:copy.deepcopy(incumbent[k]) for k in ('weights','preferences','productionCalibration') if k in incumbent}
    source['trainingUnits'] = names
    unfamiliar = [name for name in names if name not in source['preferences']]
    for name in names:
        value = source['preferences'].get(name, [0]*len(CONTEXT))
        source['preferences'][name] = ([value]+[0]*7) if isinstance(value,(int,float)) else value
    rng = random.Random(seed)
    duration = args.seconds
    # 新注册单位先获得付费出场证据；探针只揭示能力，不伪装成对单位价值的因果证明。
    probe_cases = [('opening','hunter',rng.randrange(2**30),duration)]
    probes = {}
    for index,name in enumerate(unfamiliar):
        policy = dict(copy.deepcopy(source),probe=name)
        row = run_batch(game,output,output.name+f'_new_unit_{index}',[policy],probe_cases,all_zombies=True)[0][0]
        result = read(row['result'])
        if result['final']['coldStorage']['deploymentTypes'].get(name,0) < 1:
            raise RuntimeError('New unit never actually deployed: '+name)
        probes[name] = row
    save(output/'new_unit_probes.json',{'units':probes,'inheritedUnits':len(names)-len(unfamiliar)})
    # 完整对局为单位划分，不把相邻决策分散到拟合/验证两边。
    collection = [('opening','builder'),('fortress','adaptive'),('masked:opening','counter'),
                  ('masked:developing','hunter'),('normal:opening','lotus'),('normal:opening_10_2','builder'),
                  ('economy','hunter'),('masked:elite_spread','adaptive'),('normal:opening_10_5','counter'),
                  ('opening','builder'),('masked:fortress','hunter'),('normal:opening_10_2','lotus')]
    collection = [(a,o,rng.randrange(2**30),duration) for a,o in collection]
    # 额外行为策略主动探索经营；只收集经验，不直接替换主策略，也不免费送工人。
    economic_explorer = copy.deepcopy(source)
    economic_explorer['weights'][4] = max(2.0,economic_explorer['weights'][4])
    economic_explorer['preferences']['ZOMBIE_ICE_WORKER'] = [8,0,0,0,0,0,0,0]
    collected = run_batch(game,output,output.name+'_collect',[source,economic_explorer],collection,all_zombies=True)
    fit_rows, validation_rows = [], []
    groups = []
    for policy_rows in collected:
        for i,row in enumerate(policy_rows):
            data = samples(read(row['result']))
            (validation_rows if i >= 9 else fit_rows).extend(data)
            groups.append({'result':row['result'],'split':'validation' if i>=9 else 'fit','samples':len(data)})
    model = fit(fit_rows) if len(fit_rows) >= 24 else None
    original_error, corrected_error = metrics(validation_rows), metrics(validation_rows,model)
    usable = (model is not None and len(validation_rows)>=12
              and corrected_error['mae'] < original_error['mae']*.95)
    save(output/'production_calibration.json',{'model':model,'usable':usable,'groups':groups,
         'uncalibrated':original_error,'calibrated':corrected_error,'fitSamples':len(fit_rows),
         'note':'Observational cohort returns; future escorts and plant actions are not held fixed.'})
    champion = copy.deepcopy(source)
    if args.from_candidate:
        prior = read(args.from_candidate)
        champion['weights'] = prior['weights']
        for name,value in prior.get('preferences',{}).items():
            if name in champion['preferences']:
                champion['preferences'][name] = value
    if usable:
        champion['productionCalibration'] = model
    history = []
    for generation in range(args.generations):
        cases = [('opening','builder'),('elite_cluster','adaptive'),('masked:developing','builder'),
                 ('masked:economy','hunter'),('normal:opening_10_2','lotus'),('normal:opening_10_6','ash')]
        cases = [(a,o,rng.randrange(2**30),duration) for a,o in cases]
        population = [source,champion,mutate(champion,rng,.6)]
        scores = run_batch(game,output,output.name+f'_generation_{generation}',population,cases,all_zombies=True)
        winner = max(range(len(population)),key=lambda i:grouped_key(scores[i],cases))
        champion = copy.deepcopy(population[winner])
        history.append({'generation':generation,'cases':cases,'population':population,'scores':scores,'winner':winner})
        save(output/'checkpoint.json',{'identity':identity,'history':history,'champion':champion})
    # 在读取留出成绩前冻结候选。覆盖九个正式关卡，不能拿 10-1 代表整个第十章。
    save(output/'frozen_policy.json',champion)
    holdout = [(f'normal:opening_10_{n}',('builder','lotus','ash')[n%3]) for n in range(1,10)]
    holdout += [('opening','ash'),('fortress','hunter'),('elite_spread','adaptive'),
                ('masked:opening','builder'),('masked:developing','adaptive'),('masked:elite_cluster','counter')]
    holdout = [(a,o,rng.randrange(2**30),duration) for a,o in holdout]
    scores = run_batch(game,output,output.name+'_holdout',[source,champion],holdout,all_zombies=True)
    report = gate(scores[0],scores[1],holdout)
    save(output/'evaluation.json',{'identity':identity,'cases':holdout,'scores':scores,'gate':report})
    artifact = {k:v for k,v in champion.items() if k != 'trainingUnits'}
    artifact.update(schema=1,validated=False,leagueGatePassed=report['passed'],identity=identity,
                    note='Review per-stage evidence before publishing; shipped policy is unchanged.')
    save(output/'candidate_policy.json',artifact)
    print(json.dumps(report),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=300)
    parser.add_argument('--generations',type=int,default=1)
    parser.add_argument('--from-candidate',type=Path,help='Reuse previous weights only; all scores are measured again')
    parser.add_argument('--seed',type=int,default=None,help='Optional experiment seed; omitted uses recorded entropy')
    args = parser.parse_args()
    if not 120 <= args.seconds <= 1200 or args.generations < 0:
        parser.error('seconds must be 120..1200 and generations nonnegative')
    train(args)
