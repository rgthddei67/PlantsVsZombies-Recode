"""Cross-check actual births with all legal commander portraits, including health scaling."""
import json
from pathlib import Path
import sys


def verify(output):
    """Keep independent summons separate and compare each formal health layer, not a pooled total."""
    folder = Path(output)
    load = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
    base, scaled = load('actual_birth'), load('scaled_birth')
    old_ids = {z['id'] for z in base['zombies']}
    expected_types = set(base['zombieBirthVitals'])
    assert len(expected_types) == 60
    for state, old in [(base, set()), (scaled, old_ids)]:
        seen = set()
        for zombie in state['zombies']:
            if zombie['id'] in old or zombie.get('bobsledRole') == 'FOLLOWER':
                continue
            kind = zombie['type']
            vitals = state['zombieBirthVitals'][kind]
            assert vitals['known']
            actual = [zombie[k] for k in ('bodyMaxHealth', 'helmMaxHealth', 'shieldMaxHealth', 'attackDamage')]
            expected = [vitals[k] for k in ('body', 'helm', 'shield', 'bite')]
            assert actual == expected, (kind, actual, expected)
            seen.add(kind)
        assert seen == expected_types, ('missing birth coverage', expected_types - seen)
    covered = set()
    for report_name, state in [('portraits', base), ('scaled_portraits', scaled),
                               ('brawl_portraits', load('brawl_context'))]:
        report = load(report_name)
        assert report['boardUnchanged'] and report['legalOptions']
        for option in report['legalOptions']:
            kind = option['type']
            vitals = state['zombieBirthVitals'][kind]
            actual = [option[k] for k in ('birthBodyHealth', 'birthHelmHealth', 'birthShieldHealth', 'birthBiteDps')]
            expected = [vitals[k] for k in ('body', 'helm', 'shield', 'bite')]
            assert actual == expected, (report_name, kind, actual, expected)
            covered.add(kind)
            if kind in ('ZOMBIE_ZAMBONI', 'ZOMBIE_GILDED_ZAMBONI'):
                assert not option['birthCanChill'] and not option['birthCanParalyze']
            if kind in ('ZOMBIE_CATAPULT', 'ZOMBIE_ELITE_CATAPULT'):
                assert option['birthCanChill'] and not option['birthCanParalyze']
            slow = {'ZOMBIE_FASTBUCKET': .4, 'ZOMBIE_ELITE_DANCER': .4,
                    'ZOMBIE_PINK_FOOTBALL': .375, 'ZOMBIE_POGO': .5, 'ZOMBIE_ELITE_POGO': .5}
            if kind in slow:
                assert abs(option['birthSlowFactor'] - slow[kind]) < .00001
    assert {'ZOMBIE_FASTBUCKET', 'ZOMBIE_NEWSPAPER',
            'ZOMBIE_ELITE_CATAPULT', 'ZOMBIE_ICE_WALL_ENGINEER'} <= covered
    assert len(covered) >= 51
    print(f'All 60 formal births matched in two health modes; {len(covered)} legal commander types matched across 11-6 and Brawl.')


if __name__ == '__main__':
    verify(sys.argv[1])
