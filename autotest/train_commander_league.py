"""Mixed full, masked and campaign-roster real-engine training with frozen holdouts.

Candidates are evidence artifacts only. This runner never replaces the shipped
policy: full-roster success alone cannot authorize a campaign-wide release.
"""
import argparse
import copy
import hashlib
import json
import math
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


def evaluation_gate(before, after, cases, curriculum):
    """Keep the chapter gate unchanged; a focused normal-pool course reports only its tested scope."""
    report = gate(before, after, cases)
    if curriculum != 'mixed':
        return report
    # 局部门槛沿用至少两场新增真实胜利、不能丢旧胜局；缺席的全/删减卡池仍无全章证据。
    mixed = [(a,b,c) for a,b,c in zip(before,after,cases)
             if c[0] == 'normal:opening_11_6' and c[1] == 'ice_bunker_mixed']
    transfer = [(a,b,c) for a,b,c in zip(before,after,cases)
                if c[0] == 'normal:opening_10_6' and c[1] == 'cob']
    def group(rows, minimum):
        old = sum(a['outcome'] == 'commander_win' for a,b,c in rows)
        new = sum(b['outcome'] == 'commander_win' for a,b,c in rows)
        lost = sum(a['outcome'] == 'commander_win' and b['outcome'] != 'commander_win' for a,b,c in rows)
        seeds = len({c[2] for a,b,c in rows})
        return {'games':len(rows),'independentSeeds':seeds,'oldWins':old,'newWins':new,'lostWins':lost,
                'passed':seeds >= minimum and new >= old and lost == 0}
    focus, migration = group(mixed,3), group(transfer,1)
    gain = sum((b['outcome'] == 'commander_win') - (a['outcome'] == 'commander_win') for a,b,c in mixed+transfer)
    chapter_passed = report['passed']
    report.update(scope='focused_mixed_11_6',chapterGatePassed=chapter_passed,
                  focusedMixed=focus,transfer=migration,newWinGain=gain,
                  passed=focus['passed'] and migration['passed'] and gain >= 2
                  and focus['lostWins'] + migration['lostWins'] == 0)
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
    elif name == 'abilities':
        # 能力适配后的局部训练：正常后段开局与全/半兵池共存，留出种子和对手不参与选优。
        selection = [('normal:opening_10_5','planner'),('normal:opening_10_6','ash'),
                     ('normal:opening_10_7','fortifier'),('opening','planner'),('masked:opening','hunter')]
        # 正式10-6卡池里的单一/混合火力片段，检验适应、鼓舞与补种压制的边际价值。
        # 只改变陪练场景，不规定僵尸必须购买什么；胜负仍由实际游戏结算。
        selection += [('normal:elite_mono_10_6','pine_elite'),('normal:elite_cluster_10_6','planner')]
        selection[1] = ('normal:opening_10_6','pine_elite')
        selection[3] = ('opening_10_6','pine_elite')
        collection = selection
        holdout = [('normal:opening','planner'),('normal:opening_10_6','fortifier'),('normal:opening_10_7','ash')]
        holdout += [(prefix+'opening',opponent) for prefix in ('','masked:') for opponent in ('planner','fortifier','ash')]
        holdout += [('normal:elite_mono_10_6','pine_elite'),('normal:elite_spread_10_6','hunter')]
        holdout[1] = ('normal:opening_10_6','pine_elite')
        holdout += [('opening_10_6','pine_elite'),('masked:opening_10_6','pine_elite')]
    elif name == 'sustain':
        # 经营压力与正常开局并列；完整/删减/正式卡池留出分别统计，胜负不规定必须出工人。
        pairs = [('sustain_repair_10_6','ice_fortifier'),('sustain_growth_10_6','ice_pine'),
                 ('sustain_rebuild_10_6','ice_bunker'),('sustain_mature_10_6','ice_pine')]
        selection = [('normal:'+a,o) for a,o in pairs]
        selection += [('sustain_repair_10_6','ice_fortifier'),('masked:sustain_growth_10_6','ice_pine'),
                      ('normal:opening_10_6','ice_bunker')]
        collection = selection
        holdout = [(prefix+'opening_10_'+str(level),opponent)
                   for prefix in ('normal:','','masked:')
                   for level,opponent in ((5,'ice_fortifier'),(6,'ice_pine'),(7,'ice_bunker'))]
        holdout += [('normal:'+a,o) for a,o in pairs]
    elif name == 'mixed':
        # 重复场景模板仍生成不同种子；每个案例都由真实正常开局、正式卡组和支援开始。
        selection = [('normal:opening_11_6','ice_bunker_mixed')] * 2
        holdout = [('normal:opening_11_6','ice_bunker_mixed')] * 3
        holdout += [('normal:opening_10_6','pine_elite')]
        # 拟合时保留9个完整拟合对局和3个独立误差验证对局；keep模式不跑这批或动旧校准。
        collection = [('normal:opening_11_6','ice_bunker_mixed')] * 12
    elif name != 'balanced':
        raise ValueError('Unknown curriculum: '+name)
    # 当前10-6正常开局的正式陪练是无增益炮阵；案例身份与实际选卡/控制器一致。
    def current_opponents(cases):
        return [(arena, 'cob' if arena.endswith('opening_10_6') else opponent) for arena, opponent in cases]
    return tuple(current_opponents(cases) for cases in (collection, selection, holdout))


def draw_cases(templates, rng, seconds, long_seconds, curriculum, phase='selection', used_seeds=None):
    """Long matches expose delayed attacks; neither idle time nor wave count is a training reward."""
    seen = used_seeds if used_seeds is not None else set()
    cases = []
    for arena,opponent in templates:
        seed = rng.randrange(2**30)
        while seed in seen:
            seed = rng.randrange(2**30)
        seen.add(seed)
        duration = long_seconds if (curriculum == 'mixed' and phase == 'holdout') or curriculum in ('openings','coached','abilities','sustain') or (
            curriculum in ('endurance','reserves') and (arena.startswith('normal:') or 'banked' in arena)) else seconds
        cases.append((arena,opponent,seed,duration))
    return cases


def execution_mode(args, holdout=False):
    """Map requested stage to actual rendering cadence; background search never hides the game window."""
    background = args.background_commander or (holdout and args.holdout_background_commander)
    return {'backgroundCommander':background,'batchStepsPerFrame':0 if background else 32,
            'timeScale':args.time_scale if background else 1}


def batch_options(args, cases, candidates, holdout=False):
    """Budget the visible process for the complete batch, without confusing wall time with match seconds."""
    mode = execution_mode(args,holdout)
    games = len(cases) * candidates
    # 同步沿用32逻辑步一帧；后台按真实倍率给整批留裕量，避免900秒默认超时杀掉合法长留出。
    cadence = mode['timeScale'] if mode['backgroundCommander'] else 32
    timeout = max(900,math.ceil(sum(case[3] for case in cases) * candidates / cadence * 1.5 + games*30 + 60))
    options = {'all_zombies':True,'steps':mode['batchStepsPerFrame'],
               'background_commander':mode['backgroundCommander'],'time_scale':mode['timeScale'],
               'wall_timeout_seconds':timeout}
    if args.curriculum == 'mixed':
        options.update(air_defense=True,shovel_counters=True)
    return options


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
    tracked.extend(path.resolve() for path in args.include_candidate)
    if args.reuse_paid_probes:
        tracked.append(args.reuse_paid_probes.resolve())
    identity = {'schema':1,'seed':seed,'seconds':args.seconds,'generations':args.generations,'curriculum':args.curriculum,
                'longSeconds':args.long_seconds,'population':args.population,'restarts':args.restarts,
                'calibrationMode':args.calibration,'opponentWeightStart':args.opponent_weight,
                'stateModel':args.state_model,
                'stateOnly':args.state_only,
                'netEconomy':args.net_economy,
                'includedCandidates':[path.resolve().as_posix() for path in args.include_candidate],
                'reusedPaidProbes':args.reuse_paid_probes.resolve().as_posix() if args.reuse_paid_probes else None,
                'execution':{'selection':execution_mode(args),'holdout':execution_mode(args,True)},
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
    included = []
    for path in args.include_candidate:
        saved = read(path)
        policy = {k:copy.deepcopy(saved[k]) for k in ('weights','preferences','productionCalibration','stateModel','netEconomy','anticipateBuilding','searchVersion','opponentWeight','anticipateEconomy') if k in saved}
        policy['trainingUnits'] = names
        for name in names:
            value = policy['preferences'].get(name,[0]*len(CONTEXT))
            policy['preferences'][name] = ([value]+[0]*7) if isinstance(value,(int,float)) else value
        included.append(policy)
    rng = random.Random(seed)
    used_seeds = set()
    duration = args.seconds
    # 新注册单位先获得付费出场证据；探针只揭示能力，不伪装成对单位价值的因果证明。
    probe_cases = [('opening','hunter',rng.randrange(2**30),duration)]
    used_seeds.add(probe_cases[0][2])
    probes = {}
    prior_probes = read(args.reuse_paid_probes)['units'] if args.reuse_paid_probes else {}
    if args.reuse_paid_probes:
        old_identity = read(args.reuse_paid_probes.parent/'identity.json')
        data_path = (game/'resources/gamedata.json').as_posix()
        if old_identity['hashes'].get(data_path)!=identity['hashes'][data_path]:
            raise ValueError('Unit configuration changed; paid spawn probes must be run again')
    for index,name in enumerate(unfamiliar):
        if name in prior_probes:
            # 这里只复用单位行为未修改时的付费出生事实，旧AI的输赢/分数不进入新引擎选优。
            evidence = read(prior_probes[name]['result'])
            cash = evidence['final']['coldStorage']
            if (cash['deploymentTypes'].get(name,0)<1 or cash['zombieCosts'].get(name,0)<=0
                    or cash['enemyIce']!=cash['initialEnemyIce']+cash['supplied']+cash['workerIncome']+cash['killIncome']-cash['spent']):
                raise ValueError('Invalid paid spawn evidence for '+name)
            probes[name] = {'result':prior_probes[name]['result'],'reusedPaidSpawnOnly':True}
            continue
        policy = dict(copy.deepcopy(source),probe=name)
        row = run_batch(game,output,output.name+f'_new_unit_{index}',[policy],probe_cases,
                        **batch_options(args,probe_cases,1))[0][0]
        result = read(row['result'])
        if result['final']['coldStorage']['deploymentTypes'].get(name,0) < 1:
            raise RuntimeError('New unit never actually deployed: '+name)
        probes[name] = row
    save(output/'new_unit_probes.json',{'units':probes,'inheritedUnits':len(names)-len(unfamiliar)})
    # 完整对局为单位划分，不把相邻决策分散到拟合/验证两边。
    collection, selection, holdout_templates = curriculum_templates(args.curriculum)
    collection = draw_cases(collection,rng,duration,args.long_seconds,args.curriculum,'collection',used_seeds)
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
    collected = run_batch(game,output,output.name+'_collect',[source,economic_explorer],collection,
                          **batch_options(args,collection,2)) if args.calibration=='fit' else []
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
    save(output/'production_calibration.json',{'mode':args.calibration,'model':model,'usable':usable,'groups':groups,
         'uncalibrated':original_error,'calibrated':corrected_error,'fitSamples':len(fit_rows),
         'note':'Starting policy calibration is preserved unchanged; no refit or removal.' if args.calibration=='keep'
                else 'Observational cohort returns; future escorts and plant actions are not held fixed.'})
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
        cases = draw_cases(selection,rng,duration,args.long_seconds,args.curriculum,'selection',used_seeds)
        population = state_population(champion,rng,args.population,generation) if args.state_only else ([champion,accounting_reference] if accounting_reference is not None else [source,champion] + ([reference] if reference else []))
        population.extend(copy.deepcopy(included))
        if args.curriculum in ('abilities','mixed'):
            # 从同一正式策略起步时不花实战预算重复测完全相同的参数，腾出位置给独立变异。
            population = [p for i,p in enumerate(population) if p not in population[:i]]
        if len(population)>args.population:
            raise ValueError('Population must fit all distinct supplied candidates and baselines')
        restart_end = len(population) + args.restarts
        while len(population) < args.population:
            parent = copy.deepcopy(champion)
            if len(population) < restart_end:
                population.append(exploratory_restart(parent,rng,neutral=len(population)%2==0))
                continue
            if args.state_model:
                parent.setdefault('stateModel',new_state_model())
            population.append(mutate(parent,rng,.6 if len(population)%2 else 1.2))
        scores = run_batch(game,output,output.name+f'_generation_{generation}',population,cases,
                           **batch_options(args,cases,len(population)))
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
    holdout = draw_cases(holdout_templates,rng,duration,args.long_seconds,args.curriculum,'holdout',used_seeds)
    policies = [source,champion]+([reference] if reference else [])+([neutral] if args.state_only else [])
    scores = run_batch(game,output,output.name+'_holdout',policies,holdout,
                       **batch_options(args,holdout,len(policies),True))
    report = evaluation_gate(scores[0],scores[1],holdout,args.curriculum)
    if reference:
        report['reference'] = evaluation_gate(scores[2],scores[1],holdout,args.curriculum)
        report['passed'] = report['passed'] and report['reference']['passed']
    if args.state_only:
        report['stateAblation'] = evaluation_gate(scores[-1],scores[1],holdout,args.curriculum)
        report['passed'] = report['passed'] and report['stateAblation']['passed']
    evaluation = {'identity':identity,'cases':holdout,'policies':policies,'scores':scores,'gate':report}
    save(output/'evaluation.json',evaluation)
    save(output/'opening_review.json',review(evaluation))
    artifact = {k:v for k,v in champion.items() if k != 'trainingUnits'}
    artifact.update(schema=1,validated=False,leagueGatePassed=report.get('chapterGatePassed',report['passed']),identity=identity,
                    note='Focused 11-6 mixed-defense evidence only; no chapter-wide publication gate. Shipped policy is unchanged.'
                         if args.curriculum=='mixed' else 'Review per-stage evidence before publishing; shipped policy is unchanged.')
    if args.curriculum=='mixed':
        artifact['focusedGatePassed']=report['passed']
    save(output/'candidate_policy.json',artifact)
    print(json.dumps(report),flush=True)
    if args.curriculum in ('openings','coached'):
        # 冻结后另跑压力诊断，既不参加本轮选优，也不把片段胜率混进正常开局发布门槛。
        fixtures = draw_cases([('normal:fortress','fortifier'),('normal:economy','lotus'),
                               ('normal:banked','lotus'),('fortress','hunter'),
                               ('masked:economy','fortifier')],rng,duration,args.long_seconds,'endurance','diagnostic',used_seeds)
        diagnostic_scores = run_batch(game,output,output.name+'_fixtures',policies,fixtures,
                                     **batch_options(args,fixtures,len(policies),True))
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
    parser.add_argument('--calibration',choices=('fit','keep','off'),default='fit',help='Fit production calibration; keep skips collection and preserves the starting calibration; off removes candidate calibration')
    parser.add_argument('--background-commander',action='store_true',help='Run every phase with visible realtime rendering and the normal background search budget')
    parser.add_argument('--holdout-background-commander',action='store_true',help='Use the normal background search only for frozen holdouts, retaining synchronous selection')
    parser.add_argument('--time-scale',type=int,choices=(1,2,5),default=1,help='DeltaTime game speed for requested background phases; synchronous phases remain at 1')
    parser.add_argument('--opponent-weight',type=float,help='Candidate-only initial value of reducing opponent terminal assets; subsequently evolved')
    parser.add_argument('--state-model',action='store_true',help='Explore conditional scoring and expanded voluntary formations; baselines stay unchanged')
    parser.add_argument('--state-only',action='store_true',help='Freeze base weights, preferences and calibration; vary only conditional coefficients and include a neutral-layer holdout')
    parser.add_argument('--search-version',type=int,choices=(1,2),help='Candidate-only search space version; incumbent/reference comparisons keep their original versions')
    parser.add_argument('--anticipate-building',action='store_true',help='Evaluate paid future player construction; keep baseline policy semantics unchanged')
    parser.add_argument('--net-economy',action='store_true',help='Train net-ice accounting candidates; keep original scoring in incumbent/reference comparisons')
    parser.add_argument('--long-seconds',type=int,default=900)
    parser.add_argument('--reference-policy',type=Path,help='Keep an additional baseline in selection and independent release checks')
    parser.add_argument('--curriculum',choices=('balanced','siege','endurance','reserves','openings','coached','abilities','sustain','mixed'),default='balanced',
                        help='Openings selects and gates full games; prebuilt positions are separate frozen diagnostics')
    parser.add_argument('--from-candidate',type=Path,help='Inherit prior policy parameters; all scores are measured again')
    parser.add_argument('--include-candidate',type=Path,action='append',default=[],help='Mixed course only: compare this frozen candidate alongside the incumbent; repeat for multiple candidates')
    parser.add_argument('--reuse-paid-probes',type=Path,help='Reuse paid spawn evidence only when unit spawn behavior is unchanged; never reuse scores')
    parser.add_argument('--seed',type=int,default=None,help='Optional experiment seed; omitted uses recorded entropy')
    args = parser.parse_args()
    if (not 120 <= args.seconds <= 1200 or args.generations < 0 or not 120 <= args.long_seconds <= 1800
            or (args.curriculum in ('endurance','reserves','openings','coached','abilities','sustain','mixed') and args.long_seconds < args.seconds)
            or (args.time_scale != 1 and not (args.background_commander or args.holdout_background_commander))
            or (args.state_only and (not args.state_model or args.restarts))
            or (args.include_candidate and (args.curriculum!='mixed' or args.state_only))
            or not 0 <= args.restarts <= args.population-2
            or (args.opponent_weight is not None and not 0 <= args.opponent_weight <= 100)
            or args.population < (4 if args.reference_policy else 3)):
        parser.error('Require seconds 120..1200, long-seconds 120..1800 (>=seconds for endurance), nonnegative generations, population >=3 (>=4 with reference), and an explicit background phase for accelerated speed.')
    train(args)
