"""Verify actual card ticking, paid upgrades, forecast sampling, and cannon reload independently."""
import json
from pathlib import Path
import sys


def verify(folder):
    def read(name):
        return json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))

    def cards(state):
        return {card['type']: card for card in state['cards']}

    def cannon(state):
        return state['normalPlantsByCell']['2_0']

    def elapsed(before, after):
        return after['coldStorage']['elapsed'] - before['coldStorage']['elapsed']

    def close(actual, expected, tolerance=65):
        assert abs(actual - expected) <= tolerance, (actual, expected, tolerance)

    assert read('status')['status'] == 'passed'
    assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8')
    for case, ordinary_rate in (('off', 1.0), ('on', 2.2)):
        initial, before, planted, one, window, reload_start, reload_one = (
            read(case + '_' + name) for name in ('initial', 'before_purchase', 'planted',
                'after_one_second', 'after_kernel_window', 'reload_start', 'reload_after_one_second'))
        initial_cards = cards(initial)
        assert len(initial_cards) == 4 and all(c['ready'] and c['cooldownRemainingMs'] == 0 for c in initial_cards.values())
        assert initial['coldStorage']['openingBonusMask'] == (6 if case == 'on' else 0)
        assert initial_cards['PLANT_IMITATER']['gameplayType'] == 'PLANT_KERNELPULT'
        for kind, card in initial_cards.items():
            rate = 1.0 if kind == 'PLANT_COBCANNON' else ordinary_rate
            close(card['rechargeMultiplier'], rate, 1e-6)
            close(card['cooldownEffectiveTimeMs'], 50000 if kind == 'PLANT_COBCANNON' else 7500 / rate, 1)

        # These differences exclude the two fixture bases and cover four actual successful card transactions.
        assert before['sun'] - planted['sun'] == 800
        assert before['coldStorage']['playerIce'] - planted['coldStorage']['playerIce'] == 70
        assert cannon(planted)['id'] == planted['normalPlantsByCell']['2_1']['id']
        assert cannon(planted)['type'] == 'PLANT_COBCANNON'
        assert cannon(planted)['cobArmingTimeMs'] == 5000
        assert cards(planted)['PLANT_COBCANNON']['cooldownRemainingMs'] == 50000
        assert all(not card['ready'] for card in cards(planted).values())

        delta = elapsed(planted, one)
        assert 0.9 <= delta <= 1.2, delta
        for kind, card in cards(one).items():
            rate = 1.0 if kind == 'PLANT_COBCANNON' else ordinary_rate
            base = 50000 if kind == 'PLANT_COBCANNON' else 7500
            close(card['cooldownRemainingMs'], base - delta * rate * 1000)
            close(card['cooldownEffectiveRemainingMs'], base / rate - delta * 1000)
        close(cannon(one)['cobArmingTimeMs'], 5000 - delta * 1000)
        assert not cards(window)['PLANT_COBCANNON']['ready']
        assert all(card['ready'] == (case == 'on') for kind, card in cards(window).items() if kind != 'PLANT_COBCANNON')

        # Read the actual Board-produced search snapshot, not a hand-built formula-only surrogate.
        forecast = read(case + '_construction')
        assert forecast['boardUnchanged']
        choices = forecast['cobConstructionOptions']
        cobs = [c for c in choices if c['footprintColumns'] == 2]
        kernels = [c for c in choices if c['footprintColumns'] == 1]
        assert cobs and kernels
        for choice in cobs:
            close(choice['ready'], 50, 0.001)
            close(choice['recharge'], 50, 0.001)
        for choice in kernels:
            close(choice['ready'], 7.5 / ordinary_rate, 0.001)
            close(choice['recharge'], 7.5 / ordinary_rate, 0.001)

        assert cannon(reload_start)['id'] == cannon(planted)['id'] == cannon(reload_one)['id']
        assert cannon(reload_start)['cobPhase'] == cannon(reload_one)['cobPhase'] == 'ARMING'
        assert 29900 <= cannon(reload_start)['cobArmingTimeMs'] <= 30000
        reload_delta = elapsed(reload_start, reload_one)
        assert 0.9 <= reload_delta <= 1.2, reload_delta
        close(cannon(reload_start)['cobArmingTimeMs'] - cannon(reload_one)['cobArmingTimeMs'], reload_delta * 1000)
        assert reload_one['cobLaunchSoundRequestCount'] > planted['cobLaunchSoundRequestCount']

    print('PASS: initially ready, paid 50-second cannon card, accelerated ordinary/kernel/imitater cards, matching forecast, unchanged 30-second entity reload')


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_cob_card_opening_support'))
