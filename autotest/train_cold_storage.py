"""Offline evolutionary search measured in the real game, with a held-out gate.

Run from repository root. Every game is visible and uses legal player actions.
Only the named fortress setup is a tactical fixture; opening games start normally.
Resume by repeating exactly the same command/output directory (EXE hash checked).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
INITIAL = [3, 1, 120, .4, .25, -1, -2, .5]
SCALES = [1, .6, 40, .4, .2, .7, .8, .5]
CARDS = ['SUNFLOWER', 'MARIGOLD', 'IMITATER', 'MELONPULT', 'WINTERMELON',
         'CHERRYBOMB', 'JALAPENO', 'WALLNUT', 'PUMPKINSHELL']


def save(path, value):
    """Atomic checkpoint publication, so interrupted training can be resumed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    temporary.replace(path)


def episode_commands(weights, seed, arena, opponent, seconds, name, all_zombies=False, air_defense=False):
    """Build legal cards/rosters and explicit tactical fixtures before a real, resource-limited episode."""
    episode_opponent = opponent
    if opponent in ('ice_pine_hold', 'ice_bunker_hold'):
        opponent = opponent.removesuffix('_hold')  # 同卡组/规则，只让独立陪练保留手动菠萝。
    # 正式各关保留实际卡池及解锁；mask 仅影响显式全兵种实验。
    if arena.startswith('normal:'):
        arena = arena[len('normal:'):]
        all_zombies = False
    policy = weights if isinstance(weights, dict) else {"weights": weights}
    varied = arena.startswith(('banked_varied', 'masked:banked_varied'))
    fixture_rng = random.Random(seed ^ 0xB41CE)
    roster = None
    if arena.startswith('masked:'):
        arena = arena[len('masked:'):]
        if not all_zombies or not policy.get('trainingUnits'):
            raise ValueError('masked arena requires an explicit training registry')
        pool = sorted(policy['trainingUnits'])
        roster = sorted(random.Random(seed ^ 0xC01D).sample(pool, max(1, len(pool) // 2)))
    match = re.search(r'_(10|11)_([1-9])$', arena)
    level = (81 if match.group(1) == '10' else 90) + int(match.group(2)) if match else 82
    station = 91 <= level <= 99
    mixed = opponent == 'ice_bunker_mixed'
    storage_defense = opponent in ('ice_fortifier', 'ice_pine', 'ice_bunker')
    pine_elite = opponent in ('pine_elite', 'ice_pine', 'ice_bunker')
    cards = CARDS + (["BLOVER", "CACTUS"] if all_zombies else [])
    if opponent in ('counter', 'ash', 'adaptive', 'hunter', 'builder', 'lotus', 'fortifier', 'planner', 'pine_elite', 'ice_fortifier', 'ice_pine', 'ice_bunker'):
        cards += ['SQUASH']
        # 正式卡槽最多 11 张；已有三叶草对空时，将重复对空位置留给倭瓜。
        if all_zombies:
            cards.remove('CACTUS')
    if opponent in ('lotus', 'fortifier', 'planner', 'ice_fortifier'):
        cards += ['DAWNLOTUS']
        if all_zombies:
            cards.remove('SQUASH')  # 全兵种保留对空；曙光莲占用这张即时反制卡的正式卡槽。
    if opponent == 'ash':
        cards = [c for c in cards if c not in ('MELONPULT', 'WINTERMELON')]
    elite = arena.startswith('elite_') or pine_elite
    mono = arena.startswith('elite_mono') or pine_elite
    if elite:
        cards = [({'MELONPULT': 'ELITE_SCAREDYSHROOM' if mono else 'REPEATER',
                   'WINTERMELON': 'ELITE_SCAREDYSHROOM'}).get(c, c) for c in cards]
        cards = list(dict.fromkeys(cards))
    if pine_elite:
        cards.append('COLDPINEAPPLE')
    if storage_defense:
        cards = ['ICESTORAGENUT' if c == 'WALLNUT' else c for c in cards]
    if opponent == 'ice_bunker':
        cards = [c for c in cards if c != 'SQUASH'] + ['DOOMSHROOM', 'INSTANT_COFFEE']
    if pine_elite and arena.startswith('sustain_'):
        cards.append('REPEATER')
    if station and 'INSTANT_COFFEE' in cards:
        cards.remove('INSTANT_COFFEE')  # 夜晚灰烬直接使用，保留正常经济卡位。
    while len(cards) > 11:
        # 全池蓄爆局仍保留三叶草对空，精简重复直接灰烬，不突破正式卡槽容量。
        cards.remove(next(c for c in ('SQUASH','CHERRYBOMB','IMITATER') if c in cards))
    if station:
        cards = ['SUNSHROOM' if c == 'SUNFLOWER' else c for c in cards]
        if len(cards) >= 11:
            cards.remove(next(c for c in ('SQUASH','CHERRYBOMB','IMITATER') if c in cards))
        cards.append('PLANTERN')
    if air_defense and 'BLOVER' not in cards:
        # 正式气象站卡池也有气球。可显式保留真实对空，避免把卡组无解误计成 AI 经营能力。
        if len(cards) >= 11:
            cards.remove(next(c for c in ('IMITATER', 'SQUASH', 'CHERRYBOMB') if c in cards))
        cards.append('BLOVER')
    if mixed:
        if not station:
            raise ValueError('ice_bunker_mixed requires a normal Area 11 weather-station arena')
        # 与已验证混合实战相同的11个正式卡槽：真实补精英、配额耗尽后补雷鸣花，
        # 对空、灰烬和照明各自保留，不借旧西瓜卡组冒充当前强阵型。
        cards = ['SUNSHROOM', 'ELITE_SCAREDYSHROOM', 'PUMPKINSHELL', 'MARIGOLD',
                 'DOOMSHROOM', 'JALAPENO', 'ICESTORAGENUT', 'DAWNLOTUS',
                 'THUNDERFLOWER', 'PLANTERN', 'BLOVER']
    opening_bonus = (pine_elite or mixed) and level >= 87
    commands = [
        {'op': 'reset_test_state'},
        {'op': 'commander_experiment', 'weights': policy['weights'], 'seed': seed,
         'allZombies': all_zombies, 'preferences': policy.get('preferences', {}),
         'productionCalibration': policy.get('productionCalibration'), 'stateModel': policy.get('stateModel'),
         'netEconomy': policy.get('netEconomy',False), 'anticipateBuilding': policy.get('anticipateBuilding',False),
         'searchVersion': policy.get('searchVersion',1),'opponentWeight':policy.get('opponentWeight',0),
         'anticipateEconomy':policy.get('anticipateEconomy',False)},
        {'op': 'goto_level', 'level': level, 'coldStorageBonusSelection': opening_bonus},
        {'op': 'choose_cards', 'cards': ['PLANT_' + c for c in cards],
         'imitaterTarget': 'PLANT_MARIGOLD'},
        {'op': 'wait_state', 'state': 'GAME', 'timeout': 25},
    ]
    if opening_bonus:
        # 复用正式三选二：名额搭配卡速或准备时间，随配对种子变化，不向AI泄露玩家未来操作。
        commands[3:3] = [{'op':'wait_frames','value':3},
                         {'op':'cold_storage_bonus_pick','choice':1},
                         {'op':'cold_storage_bonus_pick','choice':3 if mixed or seed % 2 else 2}]
    if all_zombies or policy.get('noWorkers'):
        commands.append({'op': 'commander_roster', 'workers': not policy.get('noWorkers', False)})
        if roster is not None:
            commands[-1]['units'] = [n for n in roster if not (policy.get('noWorkers') and n == 'ZOMBIE_ICE_WORKER')]
    if arena.startswith('sustain_'):
        # 明确标记的经营战术夹具：只在开局布置资源和阵地，比赛中费用/冷却/修复全部走正式入口。
        # 不在胜利条件里指定工人；同种子移除工人的实战消融检验场景是否确有经营区分度。
        if not storage_defense:
            raise ValueError('sustain fixtures require an ice-defense opponent')
        stock = fixture_rng.choice((240, 320, 400))
        commands += [
            {'op':'set_spawn_paused','value':True},
            {'op':'set_cold_storage','state':{'elapsed':120,'decisions':20,'enemyIce':stock,
             'initialEnemyIce':stock,'playerIce':2000,'decisionRemaining':1,'dispatchQuietSeconds':25}},
            {'op':'set_sun','value':800},
        ]
        for row in range(5):
            relative = (row-seed%5)%5
            lineup = [('SUNFLOWER',3),('SUNFLOWER',5),('ICESTORAGENUT',6)]
            if 'rebuild' in arena:
                # 蓄爆阵也保护经济后排，防止廉价狙击收割裸向日葵就足以绕过经营考验。
                lineup += [('PUMPKINSHELL',3),('PUMPKINSHELL',5)]
            if pine_elite:
                # 两路集中与四路分散轮换，剩余路线用普通输出补齐，避免只因空路直接获胜。
                columns = ([0,2] if relative in (0,2) else []) if 'rebuild' in arena else ([0] if relative<4 else [])
                lineup += [('COLDPINEAPPLE',1)]
                for col in columns:
                    lineup += [('ELITE_SCAREDYSHROOM',col),('PUMPKINSHELL',col)]
                if not columns:
                    lineup += [('REPEATER',0),('PUMPKINSHELL',0)]
            else:
                lineup += [('MELONPULT',0),('PUMPKINSHELL',0)]
                if relative in (1,3):
                    lineup += [('WINTERMELON',0)]
            for kind,col in lineup:
                commands.append({'op':'plant','type':'PLANT_'+kind,'row':row,'col':col})
        if opponent == 'ice_bunker':
            commands.append({'op':'plant','type':'PLANT_DOOMSHROOM','row':seed%5,'col':7})
        if 'mature' in arena:
            # 先让精英菇真实射击成长；单一火力的适应头盔静止靶可持续承受射击，不伪造成长值。
            # 靶与准备期间的资源只属于夹具，正式计分前清除并恢复一次性初始账本。
            for row in range(5):
                commands.append({'op':'spawn_zombie','type':'ZOMBIE_ADAPTIVE_HELMET','row':row,'x':1000,'stationary':True})
            commands.append({'op':'wait_seconds','value':60,'timeout':90})
            for row in range(5):
                commands.append({'op':'kill_zombie','row':row})
            commands.append({'op':'wait_seconds','value':6,'timeout':15})
            for row in range(5):
                if (row-seed%5)%5 < 4:
                    commands += [{'op':'plant','type':'PLANT_REPEATER','row':row,'col':2},
                                 {'op':'plant','type':'PLANT_PUMPKINSHELL','row':row,'col':2}]
            commands += [{'op':'set_sun','value':800},
                         {'op':'set_cold_storage','state':{'enemyIce':stock,'initialEnemyIce':stock,
                          'supplied':0,'spent':0,'workerIncome':0,'killIncome':0,'elapsed':120,'decisionRemaining':1,
                          'supplyRemaining':30,'incomeIdleSeconds':0,'plantKillIdleSeconds':0}}]
        commands += [{'op':'set_cold_storage','state':{'playerIce':180}},
                     {'op':'set_spawn_paused','value':False}]
    if arena.startswith(('fortress', 'economy', 'elite_', 'developing', 'banked')):
        developing = arena.startswith('developing')
        banked = arena.startswith('banked')
        opening_ice = 1800 if banked else 250 if developing else 96 if arena.startswith('economy') else 600
        if varied:
            opening_ice = fixture_rng.choice((96, 300, 800, 1800))
        if banked:
            commands.append({'op':'set_spawn_paused','value':True})
        commands += [
            {'op': 'set_cold_storage', 'state': {'elapsed': 100 if developing else 300, 'decisions': 4 if developing else 20,
             'enemyIce': opening_ice, 'initialEnemyIce': opening_ice, 'playerIce': 3000,
             'decisionRemaining': 1, 'dispatchQuietSeconds': 25}},
            {'op': 'set_sun', 'value': 1000},
        ]
        for row in range(5):
            lineup = [('MELONPULT', 0), ('WINTERMELON', 0), ('MELONPULT', 1),
                              ('SUNFLOWER', 3), ('SUNFLOWER', 5),
                              ('PUMPKINSHELL', 0), ('PUMPKINSHELL', 1)]
            if elite:
                lineup = [('REPEATER', 2), ('PUMPKINSHELL', 2), ('SUNFLOWER', 3), ('SUNFLOWER', 5)]
                if mono:
                    # 单一持续火力与现有混合火力形成对照；灰烬仍按真实资源/冷却由陪练决定。
                    lineup = [('SUNFLOWER', 3), ('SUNFLOWER', 5)]
                # 正式限制是一局累计四株。集中两路与分散四路各用同样的四株，轮换位置。
                relative = (row - seed % 5) % 5
                elite_columns = ([0, 1] if relative in (0, 2) else []) if 'cluster' in arena else ([0] if relative < 4 else [])
                for column in elite_columns:
                    lineup += [('ELITE_SCAREDYSHROOM', column), ('PUMPKINSHELL', column)]
                if pine_elite:
                    lineup.append(('COLDPINEAPPLE',1))
            if developing:
                lineup = [('MELONPULT', 0), ('SUNFLOWER', 3), ('SUNFLOWER', 5)]
            if banked:
                # 高库存成型阵地是战术夹具，不冒充真人存档重放；钱仅在开局布置一次。
                lineup = [('MELONPULT',0),('WINTERMELON',0),('SUNFLOWER',4),('SUNFLOWER',5),('SUNFLOWER',7)]
                for column in (1,2,3):
                    kind = 'DAWNLOTUS' if opponent in ('lotus', 'fortifier', 'planner') and row == seed % 5 and column == 2 else 'MELONPULT'
                    lineup.append((kind,column))
                lineup += [('PUMPKINSHELL',c) for c in range(4)]
                if varied:
                    # 每路独立抽取火力厚度，后排仍有南瓜；不把某一路写成固定正确答案。
                    columns = fixture_rng.randint(1, 4)
                    lineup = [(kind,col) for kind,col in lineup if col >= 4 or col < columns]
            if opponent == 'ash':
                lineup = [(kind,col) for kind,col in lineup if kind not in ('MELONPULT','WINTERMELON')]
            for kind, col in lineup:
                commands.append({'op': 'plant', 'type': 'PLANT_' + kind, 'row': row, 'col': col})
            if arena.startswith('economy'):
                commands += [{'op': 'plant', 'type': 'PLANT_MELONPULT', 'row': row, 'col': 2},
                             {'op': 'plant', 'type': 'PLANT_PUMPKINSHELL', 'row': row, 'col': 2}]
            if banked or developing or arena.startswith('economy') or row != seed % 5:
                commands.append({'op': 'plant', 'type': 'PLANT_WALLNUT', 'row': row, 'col': 6})
        commands.append({'op': 'set_cold_storage', 'state': {'playerIce': 2600 if banked else 180 if developing else 300}})
        if banked:
            # 正式计时充能，保留完整冷却与费用规则；该过程发生在片段初始快照之前。
            commands.append({'op':'wait_seconds','value':21,'timeout':30})
            if varied and 'cooling' in arena:
                # 在片段开始前真实付费使用反制，使卡槽进入正式冷却；不直接改冷却或战斗中补钱。
                for kind,col in (('CHERRYBOMB',8),('JALAPENO',8),('SQUASH',8)):
                    if kind in cards:
                        commands.append({'op':'player_plant','slot':cards.index(kind),'row':seed%5,'col':col})
                        commands.append({'op':'wait_seconds','value':3,'timeout':10})
            commands.append({'op':'set_spawn_paused','value':False})
    if policy.get('probe'):
        commands.append({'op': 'queue_ice_zombie', 'type': policy['probe'], 'row': seed % 5, 'delay': 0})
    # 升级株替换的旧实体在下一逻辑步清理，基线不能把同一格的新旧输出重复计数。
    commands.append({'op': 'wait_frames', 'value': 2})
    commands.append({'op': 'commander_episode', 'opponent': episode_opponent, 'seconds': seconds,
                     'name': name, 'timeout': seconds + 30})
    if mixed:
        commands[-1].update(shovelCounters=True, traceUnits=True)
    return commands


def score(result):
    """Real outcome first; bounded shaping cannot outweigh a victory/defeat."""
    initial, final = result['initial'], result['final']
    ice, before = final['coldStorage'], initial['coldStorage']
    outcome = result['outcome']
    terminal = 10000 if outcome == 'commander_win' else -10000 if outcome == 'player_win' else 0
    def fire(s):
        weights = {'PLANT_MELONPULT': 1, 'PLANT_WINTERMELON': 1.5,
                   'PLANT_REPEATER': .6, 'PLANT_ELITE_SCAREDYSHROOM': 1.5}
        return sum(weights.get(p['type'], 0) for p in s['plants'] if p['health'] > 0)
    def assets(s):
        c = s['coldStorage']
        # 已付费的存活/待出生单位仍是可用投资；免费召唤不虚增购买资产。
        return (c['enemyIce'] + sum(x['cost'] for x in c.get('refundableCosts', []))
                + sum(x['cost'] for x in c.get('pending', [])))
    # 评价净资产增长而非毛产冰；基础补给和初始库存不算经营成果。
    # 不能重罚每次伤亡，压过破阵/扩产的长期收益而把永远不出兵排在前面。
    growth = assets(final) - assets(initial) - (ice.get('supplied', 0) - before.get('supplied', 0))
    shaping = (2 * (ice['killIncome'] - before['killIncome'])
               + .2 * max(-1000, min(1000, growth))
               + 100 * (initial.get('mowerCount', 5) - final.get('mowerCount', 5))
               - 25 * (fire(final) - fire(initial)))
    return terminal + max(-2000, min(2000, shaping))


def battle_progress(result):
    """Tie-break equal win counts by real cleared plants/mowers, not stockpiling or timeout survival."""
    initial, final = result['initial'], result['final']
    kills = max(0,final['coldStorage']['killIncome']-initial['coldStorage']['killIncome'])
    mowers = max(0,initial.get('mowerCount',5)-final.get('mowerCount',5))
    return 2*kills + 100*mowers


def run_batch(game_dir, output, name, candidates, cases, steps=32, all_zombies=False,
              wall_timeout_seconds=900, background_commander=False, time_scale=1, shovel_counters=False,
              air_defense=False):
    """Pair scenarios/seeds; wall timeout bounds the visible process, not game time or AI work."""
    if wall_timeout_seconds < 1:
        raise ValueError('Wall timeout must be positive')
    if background_commander and steps != 0:
        raise ValueError('Realtime commander validation requires steps=0; keep accelerated training synchronous')
    if time_scale not in (1, 2, 5) or (time_scale != 1 and not background_commander):
        raise ValueError('Accelerated game speed requires explicit realtime background validation (1x/2x/5x)')
    script = output / (name + '.json')
    commands, jobs = [], []
    for index, weights in enumerate(candidates):
        for case_index, (arena, opponent, seed, seconds) in enumerate(cases):
            result_name = f'candidate_{index}_case_{case_index}'
            for command in episode_commands(weights, seed, arena, opponent, seconds, result_name, all_zombies, air_defense):
                if command['op'] == 'commander_episode' and shovel_counters:
                    command = dict(command, shovelCounters=True)
                if command['op'] == 'commander_episode' and time_scale != 1:
                    commands.append({'op': 'set_timescale', 'value': time_scale})
                    command = dict(command, timeScale=time_scale)
                commands.append(command)
            jobs.append((index, case_index, result_name))
    commands += [{'op': 'screenshot', 'name': 'final.png'}, {'op': 'quit'}]
    payload = {'muteAudio': True, 'batchStepsPerFrame': steps, 'commands': commands}
    if background_commander:
        payload['backgroundCommander'] = True
    artifact_dir = game_dir / 'autotest' / 'out' / name
    fingerprint = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
    cache = output / (name + '_scores.json')
    if cache.exists():
        cached = json.loads(cache.read_text(encoding='utf-8'))
        if cached['fingerprint'] != fingerprint:
            raise RuntimeError('Existing batch has different inputs; use a new output directory')
        return cached['scores']
    save(script, payload)
    started = time.monotonic()
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(ROOT / 'autotest/run_commander_batch.ps1'), '-GameDirectory', str(game_dir),
                    '-Script', str(script), '-TimeoutSeconds', str(wall_timeout_seconds)], check=True)
    status = json.loads((artifact_dir / 'status.json').read_text(encoding='utf-8'))
    log = (artifact_dir / 'run.log').read_text(encoding='utf-8')
    if status['status'] != 'passed' or 'script finished OK' not in log or 'FAIL' in log:
        raise RuntimeError(f'Batch failed: {artifact_dir}')
    unexpected = [line for line in log.splitlines() if 'rejected:' in line
                  and not (line.endswith('collect_sun rejected: sun_unavailable')
                          or line.endswith('player_shovel rejected: no_shovel_target'))]
    if unexpected:
        raise RuntimeError('Player controller rejected actions: ' + unexpected[0])
    scores = [[] for _ in candidates]
    for index, case_index, result_name in jobs:
        result = json.loads((artifact_dir / (result_name + '.json')).read_text(encoding='utf-8'))
        if candidates[index] is not None and result['seconds'] >= 45 and not any(
                t['ice']['commanderStrategy'] == 'learned_search' for t in result['trace']):
            raise RuntimeError('Candidate never used search policy')
        scores[index].append({'score': score(result), 'outcome': result['outcome'],
                              'progress': battle_progress(result),
                              'seconds': result['seconds'], 'result': str(artifact_dir / (result_name + '.json'))})
    save(cache, {'fingerprint': fingerprint, 'scores': scores, 'wallSeconds': time.monotonic() - started})
    print(name, [[round(r['score'], 2) for r in rows] for rows in scores], flush=True)
    return scores


def mean(rows):
    return statistics.mean(r['score'] for r in rows)


def train(args):
    game_dir = (ROOT / 'build/clang-release').resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    identity = {'exeSha256': hashlib.sha256((game_dir / 'PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'population': args.population, 'generations': args.generations,
                'seconds': args.seconds, 'seed': args.seed, 'validationSeeds': args.validation_seeds, 'schema': 1,
                'trainerSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'gameDataSha256': hashlib.sha256((game_dir / 'resources/gamedata.json').read_bytes()).hexdigest()}
    if args.from_checkpoint:
        identity['fromCheckpointSha256'] = hashlib.sha256(args.from_checkpoint.read_bytes()).hexdigest()
    identity_file = output / 'identity.json'
    if identity_file.exists() and json.loads(identity_file.read_text()) != identity:
        raise RuntimeError('Training settings or EXE changed; use a new output directory')
    save(identity_file, identity)
    rng = random.Random(args.seed)
    cases = [('opening', 'growth', 101, args.seconds), ('fortress', 'bomb', 202, args.seconds)]
    centre, champion, best = INITIAL[:], INITIAL[:], -math.inf
    history = []
    if args.from_checkpoint:
        prior = json.loads(args.from_checkpoint.read_text(encoding='utf-8'))
        champion = prior['history'][-1]['champion'][:]
        if len(champion) != len(INITIAL) or not all(math.isfinite(v) and abs(v) <= 500 for v in champion):
            raise RuntimeError('Invalid checkpoint policy')
        centre = champion[:]
    if args.generations == 0:
        # 换 EXE 后只继承参数，不冒用旧分数；冻结冠军在当前引擎重测，留出集不参与选优。
        scores = run_batch(game_dir, output, f'{output.name}_confirmation', [INITIAL, champion], cases)
        best = mean(scores[-1])
        history.append({'generation': 'confirmation', 'population': [INITIAL, champion],
                        'scores': scores, 'champion': champion, 'trainingScore': best})
        save(output / 'checkpoint.json', {'identity': identity, 'history': history,
                                         'sourceCheckpoint': str(args.from_checkpoint)})
    for generation in range(args.generations):
        population = [champion[:]]
        sigma = .85 ** generation
        for _ in range(args.population - 1):
            population.append([round(max(-500, min(500, rng.gauss(v, scale * sigma))), 6)
                               for v, scale in zip(centre, SCALES)])
        # 支出与风险的符号也允许被探索；真实对局会惩罚盲目倾家荡产，不能在训练器写死打法。
        scores = run_batch(game_dir, output, f'{output.name}_generation_{generation}', population, cases)
        ranked = sorted(range(len(population)), key=lambda i: mean(scores[i]), reverse=True)
        winner = ranked[0]
        if mean(scores[winner]) > best:
            best, champion = mean(scores[winner]), population[winner][:]
        elite = [population[i] for i in ranked[:max(2, args.population // 2)]]
        centre = [statistics.mean(v[i] for v in elite) for i in range(len(INITIAL))]
        history.append({'generation': generation, 'population': population, 'scores': scores,
                        'champion': champion, 'trainingScore': best})
        save(output / 'checkpoint.json', {'identity': identity, 'history': history})
    # 只评一次留出集；失败后不能用其结果调参数再宣称还是独立验证。
    heldout = [('opening', 'bomb', 1307, max(240, args.seconds)),
               ('opening_10_2', 'deny', 1907, max(240, args.seconds)),
               ('fortress', 'growth', 2909, args.seconds),
               ('fortress_10_2', 'deny', 3911, args.seconds)]
    heldout = [(arena, opponent, seed + n * 10000, seconds)
               for n in range(args.validation_seeds) for arena, opponent, seed, seconds in heldout]
    scores = run_batch(game_dir, output, f'{output.name}_holdout', [None, INITIAL, champion], heldout)
    baseline, initial, learned = scores
    gains = [c['score'] - b['score'] for b, c in zip(baseline, learned)]
    # 单局不能批准上线；必须跨独立种子且没有明显退步，并至少获得一次真实胜利。
    gate = (mean(learned) > mean(baseline) + 25 and min(gains) >= -50
            and sum(r['outcome'] == 'commander_win' for r in learned)
            >= sum(r['outcome'] == 'commander_win' for r in baseline))
    qualified = gate and len(heldout) >= 12 and any(r["outcome"] == "commander_win" for r in learned)
    artifact = {'schema': 1, 'validated': qualified, 'weights': champion,
                'experimentalGatePassed': gate, 'identity': identity,
                'trainingScore': best, 'baselineMean': mean(baseline), 'initialMean': mean(initial),
                'championMean': mean(learned), 'pairedGains': gains,
                'note': 'Passed paired holdout gate.' if qualified else 'Not qualified for publication; keep the legacy policy.'}
    save(output / 'candidate_policy.json', artifact)
    save(output / 'report.json', {'policy': artifact, 'heldoutCases': heldout, 'scores': scores,
                                   'checkpoint': str(output / 'checkpoint.json')})
    if args.publish:
        if qualified:
            save(game_dir / 'resources/ai/cold_storage_policy.json', artifact)
            print('Published qualified policy for the next game process.', flush=True)
        else:
            print('Publication skipped: held-out gate did not qualify.', flush=True)
    print(json.dumps(artifact, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/clang-release/autotest/training/search_v1')
    parser.add_argument('--from-checkpoint', type=Path, help='Keep a saved champion across engine fixes; all new scores are measured again')
    parser.add_argument('--population', type=int, default=4)
    parser.add_argument('--generations', type=int, default=2)
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--validation-seeds', type=int, default=1)
    parser.add_argument('--publish', action='store_true', help='Publish only if >=12 held-out cases pass the gate')
    parser.add_argument('--seed', type=int, default=20260919)
    args = parser.parse_args()
    if not (1 <= args.validation_seeds <= 10 and 2 <= args.population <= 32 and 0 <= args.generations <= 100 and 60 <= args.seconds <= 1200):
        parser.error('validation-seeds 1..10, population 2..32, generations 0..100, seconds 60..1200')
    if args.generations == 0 and not args.from_checkpoint:
        parser.error('generations=0 requires --from-checkpoint')
    train(args)
