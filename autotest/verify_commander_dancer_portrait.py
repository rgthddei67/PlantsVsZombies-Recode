"""Verify real dancer bodies, stage-aware purchased/paid portraits and the actual damage chain."""
import json
from pathlib import Path
import sys


def verify(output):
    """Treat the forced cohort as a diagnostic fixture, never as an autonomous battle result."""
    folder = Path(output)
    load = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
    report, paid = load('before'), load('paid')
    assert report['boardUnchanged'] and paid['boardUnchanged']
    for kind, health in [('ZOMBIE_DANCER', 500), ('ZOMBIE_BACKUP_DANCER', 270)]:
        options = [o for o in report['legalOptions'] if o['type'] == kind]
        assert len(options) == 5 and all(o['birthHealth'] == health for o in options)
        entities = [z for z in load('live_birth')['zombies'] if z['type'] == kind]
        assert entities and all(z['bodyHealth'] == health for z in entities)
    cases = {p['name']: p for p in report['candidatePlans']}
    guarded = cases['light_guard']
    assert guarded['legal'] and guarded['dancerSummons'] >= 4
    assert guarded['score'] < cases['wait']['score'], 'adopted policy must reject this unprofitable light-guard cohort'
    # The legacy always-walking proxy yields thousands from a shield that never exists.
    # Keep a tolerance for half-second integration and deliberate opponent timing choices.
    assert guarded['rawProduction'] <= 2000
    assert guarded['features'][5] == 1512
    queued = paid['candidatePlans'][0]
    assert queued['rawProduction'] == guarded['rawProduction']
    assert queued['dancerSummons'] == guarded['dancerSummons']
    before, after = load('before_strike'), load('after_strike')
    workers = [z for z in before['zombies'] if z['type'] == 'ZOMBIE_ICE_WORKER']
    assert len(workers) == 62 and min(z['bodyHealth'] for z in workers) < 500
    assert not [z for z in after['zombies'] if z['type'] == 'ZOMBIE_ICE_WORKER']
    ice = after['coldStorage']
    assert ice['workerIncome'] < ice['spent'] == 1512
    assert ice['enemyIce'] == ice['initialEnemyIce'] + ice['supplied'] + ice['workerIncome'] + ice['killIncome'] - ice['spent']
    print('Dancer bodies, independent summons, purchased/paid forecast consistency and real cohort losses passed.')


if __name__ == '__main__':
    verify(sys.argv[1])
