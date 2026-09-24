"""Verify real-game evidence, accounting and held-out separation for a completed run."""
import argparse
import json
from pathlib import Path


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def verify(directory):
    report = load(directory / 'report.json')
    checkpoint = load(directory / 'checkpoint.json')
    checked = set()
    jobs = [rows for generation in checkpoint['history'] for rows in generation['scores']]
    jobs += report['scores']
    jobs += report.get('economyControl', [])
    if report.get('preflight'):
        jobs.append(report['preflight'])
    if report.get('probes'):
        probes = load(report['probes'])
        jobs += [probes['baseline']] + list(probes['results'].values())
    for rows in jobs:
        for row in rows:
            path = Path(row['result'])
            if path in checked:
                continue
            checked.add(path)
            result = load(path)
            assert result['outcome'] in ('commander_win', 'player_win', 'timeout')
            for state in (result['initial'],result['final']):
                if 'testAudio' in state:
                    assert state['testAudio'] == {'muted':True,'masterPct':0,'soundPct':0,'musicPct':0}, path
            card_count = {'counter': 10, 'adaptive':10, 'ash': 8}.get(result['opponent'], 9)
            assert len(result['initial']['cards']) == card_count + (2 if result['initial']['coldStorage'].get('trainingAllUnits') else 0)
            assert sum(c['gameplayType'] == 'PLANT_MARIGOLD' for c in result['initial']['cards']) == 2
            for sample in [result['initial'], result['final']] + [{'coldStorage': t['ice']} for t in result['trace']]:
                ice = sample['coldStorage']
                assert ice['enemyIce'] == (ice['initialEnemyIce'] + ice['supplied']
                                           + ice['workerIncome'] + ice['killIncome'] - ice['spent']), path
                # 技能召唤可超过付费名额，不能把合法伴舞/小鬼当成超额购买。
                assert len(ice['refundableCosts']) + ice['pendingCount'] <= 64, path
                assert ice['pendingCount'] == len(ice['pending'])
                assert all(0 <= item['row'] < 5 and item['cost'] > 0 for item in ice['pending'])
            log = (path.parent / 'run.log').read_text(encoding='utf-8')
            assert load(path.parent / 'status.json')['status'] == 'passed'
            assert 'script finished OK' in log and 'FAIL' not in log
            for line in log.splitlines():
                assert 'rejected:' not in line or line.endswith('collect_sun rejected: sun_unavailable') or line.endswith('player_shovel rejected: no_shovel_target'), line
    # 留出胜负必须来自真实 Board 状态，不能由预测分数或高收入宣布胜利。
    for rows in report['scores']:
        for row in rows:
            result = load(row['result'])
            if result['outcome'] == 'commander_win':
                assert result['final']['boardState'] == 'LOSE_GAME'
            if result['outcome'] == 'player_win':
                assert result['final']['coldStorage']['trophySpawned']
    training_seeds = set()
    for script in directory.glob('*.json'):
        if not any(tag in script.stem for tag in ('generation_', '_transfer', '_confirmation', '_probe_', '_preflight')):
            continue
        for command in load(script).get('commands', []):
            if command['op'] == 'commander_experiment' and 'seed' in command:
                training_seeds.add(command['seed'])
    assert not training_seeds.intersection(case[2] for case in report['heldoutCases'])
    assert len(report['scores'][0]) == len(report['scores'][1]) == len(report['scores'][2])
    print(f'Verified {len(checked)} recorded episodes: legal accounting, opponents and held-out comparison.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    verify(args.directory)
