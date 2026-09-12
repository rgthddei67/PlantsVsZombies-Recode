"""Verify drummer clock snapshots restore progress without replaying committed buffs."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'build/clang-release/autotest/out/smoke_crystal_drummer_clock'
DRUMMER = 'ZOMBIE_CRYSTAL_DRUMMER'


def read(name):
    return json.loads((OUT / name).read_text(encoding='utf-8'))


def main():
    """Compare restored entity state with the actual serialized temporal target."""
    assert read('status.json')['status'] == 'passed'
    for case in ('waiting', 'windup', 'revived'):
        before = read(case + '_anchor.json')['zombiesByType'][DRUMMER]
        snapshot = read('snapshots/' + case + '_anchor.json')
        target = next(t for anchor in snapshot['temporalAnchors']
                      for t in anchor['targets'] if t['zombieID'] == before['id'])
        assert target['abilityStateValid']
        assert target['abilityPhase'] == (0 if case == 'waiting' else 1)
        restored = read(case + '_restored.json')['zombiesByType'][DRUMMER]
        assert restored['id'] == before['id']
        assert abs(restored['drumRemainingMs'] - round(target['abilityRemaining'] * 1000)) <= 100
        if case == 'waiting':
            assert not restored['drumWindingUp']
            assert restored['drumBeatCount'] > before['drumBeatCount']
            continue
        assert restored['drumWindingUp'] and restored['track'] == 'anim_idle'
        pending = read(case + '_before_commit.json')['zombiesByType'][DRUMMER]
        committed = read(case + '_committed.json')['zombiesByType'][DRUMMER]
        assert pending['drumBeatCount'] == restored['drumBeatCount']
        assert committed['drumBeatCount'] == restored['drumBeatCount'] + 1
        if case == 'revived':
            beneficiary = read(case + '_restored.json')['zombiesByType']['ZOMBIE_NORMAL']
            assert beneficiary['drumStacks'] == 1
            # The original pulse was emitted before death; revival must not refresh it to six seconds.
            assert 0 < beneficiary['drumInspiration'][0]['remaining'] < 2.5
        print(case, 'restored remaining ms:', restored['drumRemainingMs'])
    print('Waiting, windup, same-ID revival and pending snapshot reload passed; no replayed inspiration.')


if __name__ == '__main__':
    main()
