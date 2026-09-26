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
from commander_opening_review import review
from train_cold_storage import ROOT, run_batch, save
from train_cold_storage_all import CONTEXT, catalog, mutate, new_state_model, state_population, net_economy_policy, restart_policy


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def exploratory_restart(parent, rng, neutral=False):
    """Explore a fresh objective without prescribing units, lanes or a spending threshold."""
    candidate = restart_policy(parent,rng)
    if neutral:
        candidate['preferences'] = {name:[0]*len(values) for name,values in candidate['preferences'].items()}
        if candidate.get('stateModel'):
            candidate['stateModel'] = new_state_model()
    candidate = mutate(candidate,rng,1)
    return net_economy_policy(candidate) if candidate.get('netEconomy') else candidate


def grouped_key(rows, cases):
    """Wins first; equal-win policies compete on actual progress, without rewarding timeout survival."""
    groups = [[row for row, case in zip(rows,cases) if family(case[0]) == mode]
              for mode in ('full','masked','normal')]
    present = [g for g in groups if g]
    return (min(sum(r['outcome'] == 'commander_win' for r in g)/len(g) for g in present),
            sum(r['outcome'] == 'commander_win' for r in rows),
            sum(r['progress'] for r in rows)/max(1,len(rows)))


def family(arena):
    return 'normal' if arena.startswith('normal:') else 'masked' if arena.startswith('masked:') else 'full'


def gate(before, after, cases):
    """Require win gains and actual progress in banked stress cases, not merely spending reserves."""
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
    # 高库存片段专门复现长期空转；烧完钱提前败北同样不能冒充修复。不约束兵种、路线或进攻时间。
    banked = [(i,row) for i,(row,case) in enumerate(zip(after,cases)) if 'banked' in case[0]]
    failures = [i for i,row in banked if row['outcome'] != 'commander_win' and row.get('progress',0) <= 0]
    report['bankedProgress'] = {'games':len(banked),'failedCases':failures,'passed':not failures}
    report['passed'] = report['passed'] and not failures
    return report


def curriculum_templates(name):
    """Select scenario families; draw fresh paired seeds later and never select on holdout scores."""
    collection = [('opening','builder'),('fortress','adaptive'),('masked:opening','counter'),
                  ('masked:developing','hunter'),('normal:opening','lotus'),('normal:opening_10_2','builder'),
                  ('economy','hunter'),('masked:elite_spread','adaptive'),('normal:opening_10_5','counter'),
                  ('opening','builder'),('masked:fortress','hunter'),('normal:opening_10_2','lotus')]
    selection = [('opening','builder'),('elite_cluster','adaptive'),('masked:developing','builder'),
                 ('masked:economy','hunter'),('normal:opening_10_2','lotus'),('normal:opening_10_6','ash')]
    holdout = [(f'normal:opening_10_{n}',('builder','lotus','ash')[n%3]) for n in range(1,10)]
    holdout += [('opening','ash'),('fortress','hunter'),('elite_spread','adaptive'),
                ('masked:opening','builder'),('masked:developing','adaptive'),('masked:elite_cluster','counter')]
    if name in ('siege','endurance','reserves'):
        # 训练更常遇到真人暴露的守线反制，仍保留三类卡池及其他阵型，不能只练一张截图。
        # 最后三场仅验证生产校准误差；同一局的两个行为策略必须始终属于同一划分。
        collection = [('normal:opening','lotus'),('normal:developing','lotus'),('normal:fortress','lotus'),
                      ('normal:opening_10_2','lotus'),('normal:developing','ash'),('masked:opening','builder'),
                      ('masked:economy','hunter'),('opening','builder'),('elite_cluster','adaptive'),
                      ('normal:opening','lotus'),('masked:fortress','lotus'),('opening','ash')]
        selection = [('normal:opening','lotus'),('normal:developing','lotus'),('normal:fortress','lotus'),
                     ('normal:opening_10_2','counter'),('normal:opening_10_6','ash'),
                     ('opening','builder'),('elite_spread','adaptive'),
                     ('masked:developing','hunter'),('masked:economy','lotus')]
        # 加测同类防守的新种子；预建战术片段与正常开局分别记录，不能合称完整对局胜率。
        holdout += [('normal:opening','lotus'),('normal:developing','lotus'),
                    ('normal:fortress','lotus'),('normal:opening','ash')]
        if name in ('endurance','reserves'):
            collection[2] = ('normal:banked','lotus')
            selection[2] = ('normal:banked','lotus')
            holdout += [('normal:banked','lotus'),('banked','lotus')]
        if name == 'reserves':
            selection = [('normal:banked_varied','lotus'),('normal:banked_varied_cooling','lotus'),
                         ('normal:banked_varied','ash'),('normal:opening','lotus'),
                         ('normal:opening_10_2','counter'),('banked_varied','builder'),
                         ('banked_varied_cooling','counter'),('masked:banked_varied','lotus'),
                         ('masked:banked_varied_cooling','hunter')]
            holdout += [('normal:banked_varied','lotus'),('normal:banked_varied_cooling','ash'),
                        ('banked_varied_cooling','builder'),('masked:banked_varied','hunter')]
    elif name in ('openings','coached'):
        # 先评估正常开局如何走向胜负，不要求候选主要靠翻盘预摆的劣势残局获胜。
        selection = [('normal:opening','fortifier'),('normal:opening_10_2','lotus'),
                     ('normal:opening_10_6','ash'),('opening','fortifier'),('opening','lotus'),
                     ('opening','ash'),('masked:opening','fortifier'),
                     ('masked:opening','builder'),('masked:opening','hunter')]
        collection = selection + [('normal:opening_10_5','builder'),
                                  ('opening','hunter'),('masked:opening','lotus')]
        holdout = [(f'normal:opening_10_{n}',opponent) for n in range(1,10)
                   for opponent in ('fortifier',('lotus','builder','ash')[n%3])]
        holdout += [(prefix+'opening',opponent) for prefix in ('','masked:')
                    for opponent in ('fortifier','lotus','ash')]
        if name == 'coached':
            # 增加预算型对手与前两关的完整开局，仍保留旧对手；这不是保证更强的单一陪练替换。
            selection[0] = ('normal:opening','planner')
            selection[2] = ('normal:opening','fortifier')
            selection[3] = ('opening','planner')
            selection[6] = ('masked:opening','planner')
            collection = selection + [('normal:opening_10_5','builder'),
                                      ('opening','hunter'),('masked:opening','lotus')]
            holdout = [(a,'planner' if o=='fortifier' else o) for a,o in holdout]
    elif name != 'balanced':
        raise ValueError('Unknown curriculum: '+name)
    return collection, selection, holdout


def draw_cases(templates, rng, seconds, long_seconds, curriculum):
    """Long matches expose delayed attacks; neither idle time nor wave count is a training reward."""
    return [(a,o,rng.randrange(2**30),
             long_seconds if curriculum in ('openings','coached') or (curriculum in ('endurance','reserves')
                             and (a.startswith('normal:') or 'banked' in a)) else seconds)
            for a,o in templates]


def train(args):
    """Collect, fit, select, then freeze before unseen paired games; keep every phase resumable."""
    game = ROOT / 'build/clang-release'
    output = args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    identity_path = output / 'identity.json'
    previous = read(identity_path) if identity_path.exists() else {}
    seed = args.seed if args.seed is not None else previous.get('seed',secrets.randbelow(2**30))
    tracked = [Path(__file__), ROOT/'autotest/train_cold_storage.py', ROOT/'autotest/train_cold_storage_all.py',
               ROOT/'autotest/commander_calibration.py', ROOT/'autotest/commander_opening_review.py', game/'PlantsVsZombies.exe',
               game/'resources/gamedata.json',game/'resources/ai/cold_storage_policy.json']
    if args.from_candidate:
        tracked.append(args.from_candidate.resolve())
    if args.reference_policy:
        tracked.append(args.reference_policy.resolve())
    identity = {'schema':1,'seed':seed,'seconds':args.seconds,'generations':args.generations,'curriculum':args.curriculum,
                'longSeconds':args.long_seconds,'population':args.population,'restarts':args.restarts,
                'calibrationMode':args.calibration,'opponentWeightStart':args.opponent_weight,
                'stateModel':args.state_model,
                'stateOnly':args.state_only,
                'netEconomy':args.net_economy,
                'anticipateBuilding':args.anticipate_building,'searchVersion':args.search_version,
                'hashes':{p.as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}}
    if previous and previous != identity:
        raise RuntimeError('Engine, policy or trainer changed; use a new output directory')
    save(identity_path,identity)
    names = read(output/'catalog.json')['units'] if (output/'catalog.json').exists() else catalog(game,output)
    incumbent = read(game/'resources/ai/cold_storage_policy.json')
    source = {k:copy.deepcopy(incumbent[k]) for k in ('weights','preferences','productionCalibration','stateModel','netEconomy','anticipateBuilding','searchVersion','opponentWeight','anticipateEconomy') if k in incumbent}
    source['trainingUnits'] = names
    unfamiliar = [name for name in names if name not in source['preferences']]
    for name in names:
        value = source['preferences'].get(name, [0]*len(CONTEXT))
        source['preferences'][name] = ([value]+[0]*7) if isinstance(value,(int,float)) else value
    reference = None
    if args.reference_policy:
        old = read(args.reference_policy)
        reference = {k:copy.deepcopy(old[k]) for k in ('weights','preferences','productionCalibration','stateModel','netEconomy','anticipateBuilding','searchVersion','opponentWeight','anticipateEconomy') if k in old}
        reference['trainingUnits'] = names
        for name in names:
            value = reference['preferences'].get(name,[0]*len(CONTEXT))
            reference['preferences'][name] = ([value]+[0]*7) if isinstance(value,(int,float)) else value
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
    collection, selection, holdout_templates = curriculum_templates(args.curriculum)
    collection = draw_cases(collection,rng,duration,args.long_seconds,args.curriculum)
    # 额外行为策略主动探索经营；只收集经验，不直接替换主策略，也不免费送工人。
    economic_explorer = copy.deepcopy(source)
    economic_explorer['weights'][4] = max(2.0,economic_explorer['weights'][4])
    economic_explorer['preferences']['ZOMBIE_ICE_WORKER'] = [8,0,0,0,0,0,0,0]
    if args.state_model:
        economic_explorer.setdefault('stateModel',new_state_model())
    if args.net_economy:
        economic_explorer = net_economy_policy(economic_explorer)
    if args.search_version:
        economic_explorer['searchVersion'] = args.search_version
    if args.anticipate_building:
        economic_explorer['anticipateBuilding'] = True
    collected = run_batch(game,output,output.name+'_collect',[source,economic_explorer],collection,all_zombies=True) if args.calibration=='fit' else []
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
        if 'stateModel' in prior:
            champion['stateModel'] = copy.deepcopy(prior['stateModel'])
        if 'productionCalibration' in prior:
            champion['productionCalibration'] = copy.deepcopy(prior['productionCalibration'])
        champion['netEconomy'] = prior.get('netEconomy',False)
        champion['anticipateBuilding'] = prior.get('anticipateBuilding',False)
        champion['anticipateEconomy'] = prior.get('anticipateEconomy',False)
        champion['searchVersion'] = prior.get('searchVersion',1)
        if 'opponentWeight' in prior:
            champion['opponentWeight']=prior['opponentWeight']
        for name,value in prior.get('preferences',{}).items():
            if name in champion['preferences']:
                champion['preferences'][name] = value
    if usable and not args.state_only:
        champion['productionCalibration'] = model
    if args.state_model:
        champion.setdefault('stateModel',new_state_model())
    if args.net_economy:
        champion = net_economy_policy(champion)
    if args.search_version:
        champion['searchVersion'] = args.search_version
    if args.anticipate_building:
        champion['anticipateBuilding'] = True
    if args.calibration=='off':
        champion.pop('productionCalibration',None)
    if args.opponent_weight is not None:
        champion['opponentWeight']=args.opponent_weight
    neutral = copy.deepcopy(champion)
    neutral['stateModel'] = new_state_model()
    accounting_reference = copy.deepcopy(reference or source) if args.net_economy or args.anticipate_building or args.search_version or args.opponent_weight is not None else None
    if accounting_reference is not None and args.net_economy:
        accounting_reference = net_economy_policy(accounting_reference)
    if accounting_reference is not None and args.anticipate_building:
        accounting_reference['anticipateBuilding'] = True
    if accounting_reference is not None:
        if args.search_version:
            accounting_reference['searchVersion'] = args.search_version
        if args.state_model:
            accounting_reference.setdefault('stateModel',new_state_model())
        if 'productionCalibration' in champion:
            accounting_reference['productionCalibration'] = copy.deepcopy(champion['productionCalibration'])
        if args.calibration=='off':
            accounting_reference.pop('productionCalibration',None)
        if args.opponent_weight is not None:
            accounting_reference['opponentWeight']=args.opponent_weight
    history = []
    # 仅换引擎复测也保存冻结前的检查点；空历史明确表示未重新搜索权重。
    save(output/'checkpoint.json',{'identity':identity,'history':history,'champion':champion})
    for generation in range(args.generations):
        cases = draw_cases(selection,rng,duration,args.long_seconds,args.curriculum)
        population = state_population(champion,rng,args.population,generation) if args.state_only else ([champion,accounting_reference] if accounting_reference is not None else [source,champion] + ([reference] if reference else []))
        restart_end = len(population) + args.restarts
        while len(population) < args.population:
            parent = copy.deepcopy(champion)
            if len(population) < restart_end:
                population.append(exploratory_restart(parent,rng,neutral=len(population)%2==0))
                continue
            if args.state_model:
                parent.setdefault('stateModel',new_state_model())
            population.append(mutate(parent,rng,.6 if len(population)%2 else 1.2))
        scores = run_batch(game,output,output.name+f'_generation_{generation}',population,cases,all_zombies=True)
        winner = max(range(len(population)),key=lambda i:grouped_key(scores[i],cases))
        champion = copy.deepcopy(population[winner])
        history.append({'generation':generation,'cases':cases,'population':population,'scores':scores,'winner':winner})
        save(output/'checkpoint.json',{'identity':identity,'history':history,'champion':champion})
    # 在读取留出成绩前冻结候选。覆盖九个正式关卡，不能拿 10-1 代表整个第十章。
    save(output/'frozen_policy.json',champion)
    if champion == source:
        # 没有新策略就不重复跑两份相同参数，也不能将完全相同的胜率称为通过升级门槛。
        report = {'passed':False,'reason':'unchanged_policy'}
        evaluation = {'identity':identity,'cases':[],'policies':[source,champion],
                      'scores':[[],[]],'gate':report}
        save(output/'evaluation.json',evaluation)
        save(output/'opening_review.json',review(evaluation))
        artifact = {k:v for k,v in champion.items() if k != 'trainingUnits'}
        artifact.update(schema=1,validated=False,leagueGatePassed=False,identity=identity,
                        note='Unchanged incumbent; duplicate holdout and fixtures skipped. Shipped policy is unchanged.')
        save(output/'candidate_policy.json',artifact)
        print(json.dumps(report),flush=True)
        return
    holdout = draw_cases(holdout_templates,rng,duration,args.long_seconds,args.curriculum)
    policies = [source,champion]+([reference] if reference else [])+([neutral] if args.state_only else [])
    scores = run_batch(game,output,output.name+'_holdout',policies,holdout,all_zombies=True)
    report = gate(scores[0],scores[1],holdout)
    if reference:
        report['reference'] = gate(scores[2],scores[1],holdout)
        report['passed'] = report['passed'] and report['reference']['passed']
    if args.state_only:
        report['stateAblation'] = gate(scores[-1],scores[1],holdout)
        report['passed'] = report['passed'] and report['stateAblation']['passed']
    evaluation = {'identity':identity,'cases':holdout,'policies':policies,'scores':scores,'gate':report}
    save(output/'evaluation.json',evaluation)
    save(output/'opening_review.json',review(evaluation))
    artifact = {k:v for k,v in champion.items() if k != 'trainingUnits'}
    artifact.update(schema=1,validated=False,leagueGatePassed=report['passed'],identity=identity,
                    note='Review per-stage evidence before publishing; shipped policy is unchanged.')
    save(output/'candidate_policy.json',artifact)
    print(json.dumps(report),flush=True)
    if args.curriculum in ('openings','coached'):
        # 冻结后另跑压力诊断，既不参加本轮选优，也不把片段胜率混进正常开局发布门槛。
        fixtures = draw_cases([('normal:fortress','fortifier'),('normal:economy','lotus'),
                               ('normal:banked','lotus'),('fortress','hunter'),
                               ('masked:economy','fortifier')],rng,duration,args.long_seconds,'endurance')
        diagnostic_scores = run_batch(game,output,output.name+'_fixtures',policies,fixtures,all_zombies=True)
        diagnostics = {'cases':fixtures,'policies':policies,'scores':diagnostic_scores,'informationalOnly':True}
        save(output/'diagnostics.json',diagnostics)
        save(output/'fixture_review.json',review(diagnostics))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=300)
    parser.add_argument('--generations',type=int,default=1)
    parser.add_argument('--population',type=int,default=3)
    parser.add_argument('--restarts',type=int,default=0,help='Explore this many fresh objectives per generation, alternating inherited and reset context biases')
    parser.add_argument('--calibration',choices=('fit','off'),default='fit',help='Fit production calibration, or omit collection and all candidate calibration models')
    parser.add_argument('--opponent-weight',type=float,help='Candidate-only initial value of reducing opponent terminal assets; subsequently evolved')
    parser.add_argument('--state-model',action='store_true',help='Explore conditional scoring and expanded voluntary formations; baselines stay unchanged')
    parser.add_argument('--state-only',action='store_true',help='Freeze base weights, preferences and calibration; vary only conditional coefficients and include a neutral-layer holdout')
    parser.add_argument('--search-version',type=int,choices=(1,2),help='Candidate-only search space version; incumbent/reference comparisons keep their original versions')
    parser.add_argument('--anticipate-building',action='store_true',help='Evaluate paid future player construction; keep baseline policy semantics unchanged')
    parser.add_argument('--net-economy',action='store_true',help='Train net-ice accounting candidates; keep original scoring in incumbent/reference comparisons')
    parser.add_argument('--long-seconds',type=int,default=900)
    parser.add_argument('--reference-policy',type=Path,help='Keep an additional baseline in selection and independent release checks')
    parser.add_argument('--curriculum',choices=('balanced','siege','endurance','reserves','openings','coached'),default='balanced',
                        help='Openings selects and gates full games; prebuilt positions are separate frozen diagnostics')
    parser.add_argument('--from-candidate',type=Path,help='Inherit prior policy parameters; all scores are measured again')
    parser.add_argument('--seed',type=int,default=None,help='Optional experiment seed; omitted uses recorded entropy')
    args = parser.parse_args()
    if (not 120 <= args.seconds <= 1200 or args.generations < 0 or not 120 <= args.long_seconds <= 1800
            or (args.curriculum in ('endurance','reserves','openings','coached') and args.long_seconds < args.seconds)
            or (args.state_only and (not args.state_model or args.restarts))
            or not 0 <= args.restarts <= args.population-2
            or (args.opponent_weight is not None and not 0 <= args.opponent_weight <= 100)
            or args.population < (4 if args.reference_policy else 3)):
        parser.error('Require seconds 120..1200, long-seconds 120..1800 (>=seconds for endurance), nonnegative generations, and population >=3 (>=4 with reference).')
    train(args)
