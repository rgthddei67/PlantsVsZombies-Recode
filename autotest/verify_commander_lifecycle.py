"""Generate focused lifecycle fixtures (--generate), then verify visible runs and normal AI recovery.

Reuse the dense background fixture's setup; generated scripts live in the build directory.
Run quit_computing with seeds 42, 7 and 111, preserving status/run/closing under seed suffixes.
"""
import argparse
import json
import statistics
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
build = repo / 'build/clang-release'
out = build / 'autotest/out'
cases = ('quit_computing', 'world_change', 'budget_change', 'escort_loss')


def assertion(path, **expected):
    """Use actual state assertions without changing gameplay state."""
    return {'op': 'assert_state', 'path': 'coldStorage.' + path, **expected}


def generate():
    """Reuse the dense setup and stop before its synchronous timing comparison."""
    source = json.loads((repo / 'autotest/scripts/stress_commander_background.json').read_text(encoding='utf-8'))
    commands = source['commands']
    end = next(i for i, cmd in enumerate(commands) if cmd['op'] == 'save_level_snapshot')
    setup = commands[:end]
    setup[0] = {**setup[0], 'level': 96}
    for case in cases:
        start = [
            {'op': 'plan_ice_attack', 'background': True},
            {'op': 'wait_frames', 'value': 3},
            assertion('planningComputing', equals=True),
            {'op': 'dump_state', 'name': 'before.json'},
        ]
        if case == 'quit_computing':
            finish = [{'op': 'quit', 'requireCommanderComputing': True}]
        else:
            change = {
                'world_change': [{'op': 'plant', 'type': 'PLANT_WALLNUT', 'row': 2, 'col': 8}],
                'budget_change': [{'op': 'queue_ice_zombie', 'type': 'ZOMBIE_ICE_WORKER', 'row': 2, 'delay': 40}],
                # Each row initially has three giants before its workers; remove two by stable ID order.
                'escort_loss': [{'op': 'kill_zombie', 'row': row} for row in range(5) for _ in range(2)],
            }[case]
            reason = {'world_change': 'worldChanged', 'budget_change': 'budget', 'escort_loss': 'escortLoss'}[case]
            finish = change + [
                assertion('planningComputing', equals=True),
                {'op': 'await_ice_attack', 'timeout': 15},
                assertion('planningDiscarded', equals=1),
                assertion('planningDiscardReasons.' + reason, atLeast=1),
                assertion('planningApplied', equals=0),
                {'op': 'dump_state', 'name': 'discarded.json'},
                # No explicit plan command: the normal Board update must restart the commander itself.
                {'op': 'set_spawn_paused', 'value': False},
                {'op': 'wait_value', 'path': 'coldStorage.planningApplied', 'equals': 1, 'timeout': 15},
                assertion('planningStarted', atLeast=2),
                assertion('planningWorkerThreads', equals=2),
                assertion('planningParallelPlans', atLeast=1),
                {'op': 'dump_state', 'name': 'recovered.json'},
                {'op': 'wait_seconds', 'value': 1},
                {'op': 'dump_state', 'name': 'running.json'},
                {'op': 'quit'},
            ]
        script = {'description': f'Commander lifecycle: {case}; require unfinished computation before intervention.',
                  'backgroundCommander': True, 'commands': setup + start + finish}
        (build / f'smoke_commander_{case}.json').write_text(json.dumps(script, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    benchmark = {'description': 'Same snapshot, paired seeds and 600 ms budget; compare one and two computation threads.',
                 'commands': setup + [{'op': 'compare_commander_search', 'name': 'throughput',
                                       'workerComparison': True, 'repeats': 5}, {'op': 'quit'}]}
    (build / 'smoke_commander_worker_throughput.json').write_text(json.dumps(benchmark, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Generated four lifecycle fixtures and one throughput fixture in build/clang-release.')


def read(folder, name):
    """Check the real resource ledger, including explicitly paid queue additions."""
    state = json.loads((folder / f'{name}.json').read_text(encoding='utf-8'))
    ice = state['coldStorage']
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']
    return state, ice


def verify():
    """Verify active exit, invalidation without double payment, automatic restart and continued updates."""
    close = out / 'smoke_commander_quit_computing'
    for seed in (42, 7, 111):
        assert json.loads((close / f'status-seed-{seed}.json').read_text())['status'] == 'passed'
        log = (close / f'run-seed-{seed}.log').read_text(encoding='utf-8')
        assert 'quit with unfinished commander computation' in log and 'script finished OK' in log
        _, before = read(close, f'before-seed-{seed}')
        assert before['planningComputing'] and before['planning'] and before['spent'] == 0
    for case in cases[1:]:
        folder = out / f'smoke_commander_{case}'
        assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
        assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')
        _, before = read(folder, 'before')
        _, discarded = read(folder, 'discarded')
        _, recovered = read(folder, 'recovered')
        _, running = read(folder, 'running')
        assert before['planningComputing'] and before['planningApplied'] == 0
        assert discarded['planningDiscarded'] == 1 and discarded['planningApplied'] == 0
        assert discarded['spent'] == (24 if case == 'budget_change' else 0)
        assert recovered['planningStarted'] >= 2 and recovered['planningApplied'] == 1
        assert recovered['planningWorkerThreads'] == 2 and recovered['planningParallelPlans'] > 0
        assert recovered['spent'] > discarded['spent']  # 正常更新不只是恢复轮询，还实际提交了新购买。
        assert running['elapsed'] >= recovered['elapsed'] + .9
        assert running['planningDiscardReasons']['failed'] == 0
        print(case, 'discard reasons:', discarded['planningDiscardReasons'],
              'recovered starts/applied:', recovered['planningStarted'], recovered['planningApplied'],
              'spent:', recovered['spent'], 'deployments:', running['deployments'])
    print('Three active exits and world/wallet/escort invalidation followed by automatic AI recovery passed.')
    folder = out / 'smoke_commander_worker_throughput'
    assert json.loads((folder / 'status.json').read_text())['status'] == 'passed'
    report = json.loads((folder / 'throughput.json').read_text(encoding='utf-8'))
    assert report['boardUnchanged'] and len(report['branches']) == 10
    single, dual = [], []
    for repeat in range(5):
        pair = [x for x in report['branches'] if x['repeat'] == repeat]
        assert len(pair) == 2 and pair[0]['seed'] == pair[1]['seed']
        assert all(x['budgetMs'] == 600 and not x['relaxCumulativeRisk'] for x in pair)
        one, two = sorted(pair, key=lambda x: x['workers'])
        assert one['workers'] == 1 and one['parallelPlans'] == 0
        assert two['workers'] == 2 and two['parallelPlans'] > 0
        single.append(one['evaluated']); dual.append(two['evaluated'])
        print('pair', repeat, 'one/two evaluated:', one['evaluated'], two['evaluated'],
              'wall ms:', round(one['elapsedMs'], 2), round(two['elapsedMs'], 2))
    print('Mean complete candidates one/two:', statistics.mean(single), statistics.mean(dual),
          'ratio:', round(statistics.mean(dual) / statistics.mean(single), 3))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate', action='store_true')
    args = parser.parse_args()
    generate() if args.generate else verify()
