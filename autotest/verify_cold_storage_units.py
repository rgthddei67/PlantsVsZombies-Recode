"""Verify economic outcomes and commander choices from visible game evidence."""
import json
from pathlib import Path
import sys


def verify(root):
    """Compare actual ledgers, saved production state and decisions under changed threats."""
    units = root / 'smoke_cold_storage_units'
    ai = root / 'smoke_cold_storage_economy_ai'

    def read(folder, name):
        return json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))

    for folder in (units, ai, root / 'smoke_cold_storage_reward'):
        assert read(folder, 'status')['status'] == 'passed', folder
    paused, restored = (read(units, n) for n in ('buttered', 'restored'))
    for key in ('iceRemainingMs', 'iceBatches', 'nextIceYieldOn1000'):
        assert paused['zombiesByType']['ZOMBIE_ICE_WORKER'][key] == restored['zombiesByType']['ZOMBIE_ICE_WORKER'][key], key
    before, after = (read(units, n) for n in ('charmed', 'friendly_production'))
    mint_batches = after['iceMintsByCell']['2_1']['productionBatches'] - before['iceMintsByCell']['2_1']['productionBatches']
    assert after['coldStorage']['playerProductionIncome'] - before['coldStorage']['playerProductionIncome'] == 15 + mint_batches * 3
    assert after['coldStorage']['workerIncome'] == before['coldStorage']['workerIncome']
    assert read(units, 'removed')['coldStorage']['playerProductionIncome'] == read(units, 'after_removed')['coldStorage']['playerProductionIncome']
    for name in ('units', 'units_loaded'):
        assert read(units, name)['zombiesByType']['ZOMBIE_ICE_WORKER']['iceMachineVisible']

    for key in ('iceRemainingMs', 'iceBatches', 'nextIceYieldOn1000'):
        assert read(units, 'frozen_start')['zombiesByType']['ZOMBIE_ICE_WORKER'][key] == read(units, 'frozen_wait')['zombiesByType']['ZOMBIE_ICE_WORKER'][key], key
    assert read(units, 'killed')['coldStorage']['workerIncome'] == read(units, 'after_killed')['coldStorage']['workerIncome']

    states = {name: read(ai, name)['coldStorage'] for name in (
        'protected_investment', 'bomb_ready', 'unprofitable_fire', 'guard_ahead',
        'guard_behind', 'mature_guarded', 'assault_window')}
    # Stable appended enum; changing an existing ID is a save-compatibility violation.
    worker_id = 56
    for name, state in states.items():
        pending = state['pending']
        assert sum(z['cost'] for z in pending) + state['enemyIce'] == 400, name
        assert sum(z['cost'] for z in pending) <= state['commanderBudget'], name
    protected = states['protected_investment']
    assert protected['commanderMode'] == 'economy'
    workers = [z for z in protected['pending'] if z['type'] == worker_id]
    assert workers, protected
    for worker in workers:
        assert any(z['type'] != worker_id and z['row'] == worker['row']
                   and z['remaining'] + 3 <= worker['remaining'] for z in protected['pending'])
    assert states['bomb_ready']['commanderMode'] == 'probe'
    assert states['bomb_ready']['commanderBudget'] < protected['commanderBudget']
    for name in ('bomb_ready', 'unprofitable_fire', 'assault_window'):
        assert all(z['type'] != worker_id for z in states[name]['pending']), name
    assert states['assault_window']['commanderMode'] == 'assault'
    assert states['guard_ahead']['predictedProductionOn100'] > states['guard_behind']['predictedProductionOn100']
    assert states['mature_guarded']['predictedProductionOn100'] > 0
    print('Verified: production ownership/save continuity; guards before investment; bomb/fire risk; positional cover; mature income; assault priority.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/clang-release/autotest/out'))
