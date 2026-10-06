"""Verify real fallback payments, saved history, isolated capture and controlled Echo progress."""
import hashlib
import json
import sys
from pathlib import Path


def load(folder, name):
    return json.loads((folder / name).read_text(encoding='utf-8-sig'))


def passed(folder):
    assert load(folder, 'status.json')['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')


def ledger(state):
    c = state['coldStorage']
    assert c['enemyIce'] == (c['initialEnemyIce'] + c['supplied']
                            + c['workerIncome'] + c['killIncome'] - c['spent'])
    return c


def transactions(folder):
    """Check the actual queued fees, resume history and side-effect-free diagnostic files."""
    passed(folder)
    states = {name: load(folder, name + '.json') for name in ('initial', 'trial', 'reloaded', 'all_in')}
    b, t, r, a = [ledger(states[name]) for name in ('initial', 'trial', 'reloaded', 'all_in')]
    assert t['commanderMode'] == 'trial' and t['stallProbeRounds'] == 1
    trial_fee = sum(u['cost'] for u in t['pending'])
    assert 0 < trial_fee <= 64 and t['spent'] - b['spent'] == trial_fee
    assert t['stallProbeSpent'] == trial_fee and t['decisions'] == b['decisions'] + 1
    assert r['stallProbeRounds'] == t['stallProbeRounds'] and r['stallProbeSpent'] == t['stallProbeSpent']
    assert r['enemyIce'] == t['enemyIce'] and r['spent'] == t['spent']
    assert a['commanderMode'] == 'all_in' and a['stallProbeRounds'] == 3
    full_fee = sum(u['cost'] for u in a['pending'])
    assert full_fee >= 384 and full_fee <= r['enemyIce'] and a['spent'] - r['spent'] == full_fee
    assert 3 <= len(a['pending']) <= 64
    for c in (t, a):
        capture = Path(c['stallDiagnosticPath']).resolve()
        root = Path('build/clang-release/autotest/out/commander_stalls').resolve()
        assert capture.is_relative_to(root)
        record = load(capture, 'diagnostics.json')
        snapshot = load(capture, 'level_snapshot.json')
        assert snapshot['schemaVersion'] == 27
        assert record['capture']['levelSnapshotSaved'] and record['capture']['policyCopySaved']
        assert record['capture']['gameDataCopySaved']
        assert record['runtime']['devSpawnPaused'], 'paused capture must record the real switch, never unpause it'
        for file, source in (('policy.json', 'resources/ai/cold_storage_policy.json'),
                             ('gamedata.json', 'resources/gamedata.json')):
            assert hashlib.sha256((capture / file).read_bytes()).digest() == hashlib.sha256(
                (Path('build/clang-release') / source).read_bytes()).digest()
        assert all(key in record['snapshot'] for key in ('fallbackAllIn', 'fallbackProbeBudget'))
    report = {'passed': True, 'trialFee': trial_fee, 'allInFee': full_fee,
              'allInTroops': len(a['pending']), 'resumeNoDoublePayment': True,
              'isolatedSnapshots': True, 'schemaVersion': 27}
    (folder / 'stall_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def portrait(folder):
    """The portrait is a reproduction attempt, not the player's exact saved state or win-rate evidence."""
    passed(folder)
    initial = load(folder, 'initial.json')
    final = load(folder, 'after180.json')
    b, f = ledger(initial), ledger(final)
    assert b['decisions'] == 31 and b['enemyIce'] == 1013
    assert f['decisions'] > 31 and f['spent'] > b['spent']
    outcome = load(folder, 'episode120.json')['outcome']
    report = {'passed': True, 'scope': 'controlled portrait, not exact player replay',
              'outcome': outcome, 'endWave': f['decisions'], 'spent': f['spent'],
              'workerIncome': f['workerIncome'], 'plantKillIncome': f['killIncome']}
    (folder / 'stall_portrait_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


mode = sys.argv[1]
folder = Path(sys.argv[2])
print(json.dumps(transactions(folder) if mode == 'transactions' else portrait(folder)))
