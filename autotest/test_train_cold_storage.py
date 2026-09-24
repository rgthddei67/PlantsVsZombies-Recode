"""Small trainer contract checks; real game checks are produced by the trainer."""
import copy
import unittest
from train_cold_storage import episode_commands, score
from train_cold_storage_all import diverse_archive, outcome_gate, selection_key


class TrainerTests(unittest.TestCase):
    def test_winning_policy_is_not_discarded_for_stalling(self):
        stalled = [{'outcome':'timeout','score':150} for _ in range(4)]
        winning = [{'outcome':'commander_win','score':10000}] + [{'outcome':'player_win','score':-10000} for _ in range(3)]
        self.assertGreater(selection_key(winning),selection_key(stalled))

    def test_counter_and_ash_opponents_have_real_squash_cards(self):
        for opponent in ('counter','ash'):
            commands = episode_commands(None,9251,'opening',opponent,420,'case')
            cards = next(c for c in commands if c['op']=='choose_cards')['cards']
            self.assertTrue({'PLANT_SQUASH','PLANT_CHERRYBOMB','PLANT_JALAPENO','PLANT_WALLNUT'} <= set(cards))
            self.assertEqual(commands[-1]['opponent'],opponent)
            if opponent == 'ash':
                self.assertFalse({'PLANT_MELONPULT','PLANT_WINTERMELON'} & set(cards))
            self.assertFalse(any(c['op'] in ('plant','set_sun','set_cold_storage') for c in commands))

    def test_normal_curriculum_does_not_unlock_full_roster(self):
        commands = episode_commands([1] * 8, 601, 'normal:economy', 'bomb', 300, 'case', True)
        experiment = next(c for c in commands if c['op'] == 'commander_experiment')
        self.assertFalse(experiment['allZombies'])
        cards = next(c for c in commands if c['op'] == 'choose_cards')
        self.assertEqual(len(cards['cards']), 9)

    def test_elite_layouts_respect_lifetime_quota_and_rear_protection(self):
        for arena in ('elite_cluster', 'elite_spread_10_2'):
            commands = episode_commands(None, 401031, arena, 'bomb', 300, 'case')
            plants = [c for c in commands if c['op'] == 'plant']
            elite = [c for c in plants if c['type'] == 'PLANT_ELITE_SCAREDYSHROOM']
            self.assertEqual(len(elite), 4)
            shells = {(c['row'], c['col']) for c in plants if c['type'] == 'PLANT_PUMPKINSHELL'}
            self.assertTrue(all((c['row'], c['col']) in shells for c in elite))
            self.assertEqual(commands[-2], {'op': 'wait_frames', 'value': 2})

    def test_archive_retains_specialist_even_when_average_is_lower(self):
        policies = [{'name': 'rush'}, {'name': 'economy'}, {'name': 'dominated'}]
        scores = [[{'score': n} for n in row] for row in ([100, 100, 0], [0, 0, 150], [0, 0, 0])]
        self.assertEqual([p['name'] for p in diverse_archive(policies, scores)], ['rush', 'economy'])

    def test_outcome_gate_rejects_losing_an_existing_win(self):
        old = [{'outcome': 'timeout'} for _ in range(12)]
        new = copy.deepcopy(old)
        new[1]['outcome'] = new[2]['outcome'] = 'commander_win'
        self.assertTrue(outcome_gate(old, new))
        old[0]['outcome'] = 'commander_win'
        new[3]['outcome'] = 'commander_win'
        self.assertFalse(outcome_gate(old, new))

    def test_no_midgame_fixture_or_cooldown_cheats(self):
        for arena in ('opening', 'fortress'):
            commands = episode_commands(None, 101, arena, 'bomb', 180, 'case')
            self.assertEqual(commands[-1]['op'], 'commander_episode')
            self.assertFalse(any(c['op'] in ('spawn_zombie', 'set_no_cooldown', 'set_free_plant') for c in commands))
            if arena == 'opening':
                self.assertFalse(any(c['op'] in ('plant', 'set_sun', 'set_cold_storage') for c in commands))
            cards = next(c for c in commands if c['op'] == 'choose_cards')
            self.assertEqual(cards['imitaterTarget'], 'PLANT_MARIGOLD')

    def test_true_outcome_dominates_economic_shaping(self):
        state = {'plants': [], 'mowerCount': 5, 'coldStorage': {
            'killIncome': 0, 'playerKillIncome': 0, 'enemyIce': 600, 'workerIncome': 0}}
        result = {'initial': state, 'final': copy.deepcopy(state), 'outcome': 'timeout'}
        quiet = score(result)
        result['final']['coldStorage']['workerIncome'] = 1000000
        result['final']['coldStorage']['enemyIce'] = 1000000
        # 存冰的终局塑形封顶，不能靠无限拖延压过真实获胜。
        self.assertLessEqual(score(result) - quiet, 200)
        result['outcome'] = 'commander_win'
        self.assertGreater(score(result), 8000)
        result['outcome'] = 'player_win'
        self.assertLess(score(result), -8000)

    def test_supply_and_gross_production_are_not_free_fitness(self):
        initial = {'plants': [], 'coldStorage': {'killIncome': 0, 'playerKillIncome': 0,
                   'enemyIce': 96, 'workerIncome': 0, 'supplied': 0}}
        final = copy.deepcopy(initial)
        final['coldStorage'].update(enemyIce=276, supplied=180, workerIncome=9000)
        self.assertEqual(score({'initial': initial, 'final': final, 'outcome': 'timeout'}), 0)
        final['coldStorage']['enemyIce'] += 100
        self.assertGreater(score({'initial': initial, 'final': final, 'outcome': 'timeout'}), 0)


if __name__ == '__main__':
    unittest.main()
