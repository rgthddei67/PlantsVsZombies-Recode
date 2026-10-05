"""Verify finite-wallet x5 battles and report real income, attacks and worker protection."""
import json
import sys
from pathlib import Path


def verify(folder):
    """Keep timeout distinct from defeat; validate ledger and report observed stable entities."""
    def load(name):
        return json.loads((folder / name).read_text(encoding='utf-8-sig'))

    assert load('status.json')['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')
    initial = load('initial.json')
    traces, episodes, protection = [], [], []
    for phase in ('opening', 'middle', 'late', 'endgame'):
        if not (folder / (phase + '.json')).exists():
            continue
        episode = load(phase + '.json')
        assert episode['timeScale'] == 5 and episode['playerActions']
        assert not episode['externalSun']['enabled'] and not episode['externalSun']['events']
        episodes.append(episode)
        traces += episode['trace']
        protection += episode.get('engineerProtectionEvents', [])
    assert episodes and traces
    final = episodes[-1]['final']
    workers, accompanied, applications, selected = set(), set(), set(), set()
    experience, first_income = 0, None
    maximum_elites = 0
    for trace in traces:
        c = trace['ice']
        assert c['enemyIce'] == (c['initialEnemyIce'] + c['supplied']
                                + c['workerIncome'] + c['killIncome'] - c['spent'])
        application = c.get('planningApplied', 0)
        if application not in applications:
            applications.add(application)
            experience += c.get('searchExperiencedEvaluated', 0)
        if c.get('searchExperiencedSelected'):
            selected.add(application)
        if first_income is None and c['workerIncome'] > 0:
            first_income = c['elapsed']
        maximum_elites = max(maximum_elites, trace['plantTypes'].get('PLANT_ELITE_SCAREDYSHROOM', 0))
        units = trace.get('units', [])
        for unit in units:
            if unit['type'] != 'ZOMBIE_ICE_WORKER' or not unit.get('hasHead', True):
                continue
            workers.add(unit['id'])
            if any(ally['row'] == unit['row'] and ally['type'] not in ('ZOMBIE_ICE_WORKER', 'ZOMBIE_BALLOON')
                   and ally.get('countableExecutionHealth', 0) >= 1000
                   and ally['xInt'] < unit['xInt'] for ally in units):
                accompanied.add(unit['id'])
    c = final['coldStorage']
    outcome = episodes[-1]['outcome']
    assert outcome in ('commander_win', 'player_win', 'timeout')
    assert maximum_elites > 0, 'the selected strong-defense card must actually be planted'
    report = {'passed': True, 'level': initial['level'], 'gameTimeScale': 5,
              'outcome': outcome, 'gameSeconds': c['elapsed'], 'wave': c['decisions'],
              'actualSpent': c['spent'], 'workerIncome': c['workerIncome'],
              'plantKillIncome': c['killIncome'], 'remainingIce': c['enemyIce'],
              'firstIncomeSeconds': first_income, 'observedWorkerIDs': len(workers),
              'workersObservedBehindFront': len(accompanied),
              'engineerProtectionEvents': len(protection), 'maximumLiveEliteShrooms': maximum_elites,
              'experienceEvaluations': experience, 'directExperienceSelections': len(selected)}
    (folder / 'hybrid_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


folders = [Path(name) for name in sys.argv[1:]] or [
    Path('build/clang-release/autotest/out/battle_commander_hybrid_96'),
    Path('build/clang-release/autotest/out/battle_commander_hybrid_87')]
for folder in folders:
    print(json.dumps(verify(folder)))
