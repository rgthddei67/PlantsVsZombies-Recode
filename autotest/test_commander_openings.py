"""Guard complete-opening evaluation and observed trajectory accounting."""
import random
import unittest

from commander_opening_review import is_opening, trajectory
from train_cold_storage import episode_commands
from train_commander_league import curriculum_templates, draw_cases, family


class OpeningTests(unittest.TestCase):
    def test_primary_curriculum_contains_no_prebuilt_positions(self):
        collection, selection, holdout = curriculum_templates('openings')
        for templates in (collection, selection, holdout):
            self.assertTrue(all(is_opening(a) for a, _ in templates))
            self.assertEqual({family(a) for a, _ in templates}, {'normal', 'full', 'masked'})
        for n in range(1, 10):
            self.assertIn((f'normal:opening_10_{n}', 'fortifier'), holdout)
        cases = draw_cases(holdout, random.Random(9), 300, 900, 'openings')
        self.assertTrue(all(c[3] == 900 for c in cases))

    def test_fortifier_has_legal_cards_without_free_opening_assets(self):
        policy = {'weights': [1]*8, 'trainingUnits': ['ZOMBIE_NORMAL', 'ZOMBIE_CONE']}
        for arena in ('opening', 'masked:opening', 'normal:opening_10_2'):
            commands = episode_commands(policy, 91, arena, 'fortifier', 900, 'test', True)
            cards = next(c['cards'] for c in commands if c['op'] == 'choose_cards')
            self.assertLessEqual(len(cards), 11)
            self.assertTrue({'PLANT_DAWNLOTUS', 'PLANT_PUMPKINSHELL', 'PLANT_CHERRYBOMB'} <= set(cards))
            self.assertFalse(any(c['op'] in ('plant', 'set_sun', 'set_cold_storage', 'set_no_cooldown') for c in commands))

    def test_fixtures_are_not_openings_even_if_they_win(self):
        for arena in ('fortress', 'normal:economy', 'normal:banked', 'masked:developing'):
            self.assertFalse(is_opening(arena))

    def test_cash_flow_excludes_starting_assets_and_never_uses_future_samples(self):
        initial = {'enemyIce': 600, 'spent': 20, 'workerIncome': 0, 'killIncome': 5, 'supplied': 0}
        def point(at, spent, income):
            ice = dict(initial, spent=spent, workerIncome=income, enemyIce=620+income-spent,
                       playerIce=20, pendingCount=1)
            return {'seconds': at, 'ice': ice, 'zombies': 3, 'sun': 400, 'plants': 10}
        result = {'initial': {'coldStorage': initial}, 'seconds': 95,
                  'trace': [point(0,20,0), point(50,100,50), point(60,120,70),
                            point(90,150,100), point(95,160,100)]}
        points = trajectory(result)
        self.assertEqual([(p['from'],p['to']) for p in points], [(0,60),(60,95)])
        self.assertEqual([p['cashFlow']['spent'] for p in points], [100,40])
        self.assertEqual([p['cashFlow']['workerIncome'] for p in points], [70,30])
        self.assertEqual([p['cashFlow']['killIncome'] for p in points], [0,0])
        self.assertIsNone(points[0]['plantTypes'])  # Old traces do not invent defense composition.


if __name__ == '__main__':
    unittest.main()
