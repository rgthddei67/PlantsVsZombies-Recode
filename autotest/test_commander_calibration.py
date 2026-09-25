"""Attribution, censoring, leakage boundaries and roster contracts for league training."""
import copy
import unittest

from commander_calibration import fit, metrics, predict, samples
from train_cold_storage import episode_commands
from train_commander_league import gate


class CalibrationTests(unittest.TestCase):
    def episode(self, outcome='timeout', end=80):
        ice = {'elapsed':end,'productionEvents':[
            {'at':10,'wave':1,'amount':100},  # Before decision.
            {'at':30,'wave':1,'amount':8},
            {'at':40,'wave':2,'amount':12},
            {'at':50,'wave':3,'amount':99},  # Later reinforcement is not this plan's return.
            {'at':80,'wave':2,'amount':40}]}
        return {'outcome':outcome,'decisions':[{'elapsed':10,'wave':2,'rawProduction':100,'productionInputs':[0]*10}],
                'trace':[{'ice':ice}],'final':{'coldStorage':ice}}

    def test_cohort_and_horizon(self):
        result = samples(self.episode())[0]
        self.assertEqual(result['actual'],20)
        self.assertEqual(result['target'],.2)

    def test_censored_windows_not_zero_labels(self):
        self.assertEqual(samples(self.episode(end=50)),[])
        self.assertEqual(samples(self.episode('commander_win',50)),[])
        self.assertEqual(samples(self.episode('player_win',50))[0]['actual'],20)

    def test_model_learns_conditional_returns(self):
        rows = [{'x':[float(i%2)]+[0]*9,'target':.2 if i%2==0 else .8,'raw':100,
                 'actual':20 if i%2==0 else 80} for i in range(60)]
        tree = fit(rows)
        self.assertAlmostEqual(predict(tree,[0]*10),.2)
        self.assertAlmostEqual(predict(tree,[1]+[0]*9),.8)
        self.assertLess(metrics(rows,tree)['mae'],.0001)
        self.assertLessEqual(len(tree['nodes']),15)

    def test_masks_identical_for_paired_policies_and_preserve_campaign(self):
        source = {'weights':[1]*8,'trainingUnits':[f'ZOMBIE_{n}' for n in range(12)]}
        candidate = copy.deepcopy(source); candidate['weights'][0]=5
        a = episode_commands(source,18,'masked:opening','hunter',120,'a',True)
        b = episode_commands(candidate,18,'masked:opening','hunter',120,'b',True)
        roster = lambda commands:next(c['units'] for c in commands if c['op']=='commander_roster')
        self.assertEqual(roster(a),roster(b)); self.assertEqual(len(roster(a)),6)
        cards = next(c['cards'] for c in a if c['op']=='choose_cards')
        self.assertLessEqual(len(cards),11)
        self.assertIn('PLANT_BLOVER',cards)
        self.assertIn('PLANT_SQUASH',cards)
        for n in range(1,10):
            c = episode_commands(source,18,f'normal:opening_10_{n}','hunter',120,'c',True)
            self.assertEqual(next(x['level'] for x in c if x['op']=='goto_level'),81+n)
            self.assertFalse(next(x['allZombies'] for x in c if x['op']=='commander_experiment'))
            self.assertFalse(any(x['op']=='commander_roster' for x in c))

    def test_full_roster_improvement_cannot_hide_campaign_regression(self):
        cases = [(a,'hunter',i,120) for i,a in enumerate(['opening']*3+['masked:opening']*3+['normal:opening']*3)]
        before = [{'outcome':'player_win'} for _ in cases]
        before[-1] = {'outcome':'commander_win'}
        after = [{'outcome':'commander_win'} for _ in cases]
        after[-1] = {'outcome':'player_win'}
        self.assertFalse(gate(before,after,cases)['passed'])


if __name__ == '__main__':
    unittest.main()
