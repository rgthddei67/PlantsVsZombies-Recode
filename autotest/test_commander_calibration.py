"""Attribution, censoring, leakage boundaries and roster contracts for league training."""
import copy
import unittest
import random

from commander_calibration import fit, metrics, predict, samples
from train_cold_storage import episode_commands, battle_progress
from train_commander_league import gate, curriculum_templates, family, grouped_key, draw_cases


class CalibrationTests(unittest.TestCase):
    def test_delayed_victory_is_not_penalized_and_idle_wealth_is_not_progress(self):
        cases=[('normal:opening','lotus',1,900)]
        idle=[{'outcome':'timeout','progress':0,'score':2000,'seconds':900}]
        failed_attack=[{'outcome':'player_win','progress':80,'score':-10000,'seconds':600}]
        late_win=[{'outcome':'commander_win','progress':20,'score':10000,'seconds':890}]
        self.assertGreater(grouped_key(failed_attack,cases),grouped_key(idle,cases))
        self.assertGreater(grouped_key(late_win,cases),grouped_key(failed_attack,cases))
        early_win=copy.deepcopy(late_win);early_win[0]['seconds']=100
        self.assertEqual(grouped_key(early_win,cases),grouped_key(late_win,cases))
        result={'initial':{'coldStorage':{'killIncome':0},'mowerCount':5},
                'final':{'coldStorage':{'killIncome':0,'enemyIce':2000},'mowerCount':5}}
        self.assertEqual(battle_progress(result),0)
        result['final']['coldStorage']['killIncome']=40
        self.assertEqual(battle_progress(result),80)

    def test_endurance_has_long_banked_cases_and_legal_fixture(self):
        collection,selection,holdout=curriculum_templates('endurance')
        self.assertIn(('normal:banked','lotus'),selection)
        cases=draw_cases(holdout,random.Random(7),300,900,'endurance')
        self.assertTrue(all(s==900 for a,o,r,s in cases if a.startswith('normal:') or 'banked' in a))
        self.assertTrue(any(s==300 for a,o,r,s in cases))
        commands=episode_commands([1]*8,7,'normal:banked','lotus',900,'banked',True)
        cards=next(c['cards'] for c in commands if c['op']=='choose_cards')
        plants=[c for c in commands if c['op']=='plant']
        self.assertTrue(all(c['type'] in cards for c in plants))
        self.assertEqual(sum(c['type']=='PLANT_DAWNLOTUS' for c in plants),1)
        self.assertEqual(sum(c['type']=='PLANT_PUMPKINSHELL' for c in plants),20)
        self.assertEqual(sum(c['state'].get('enemyIce')==1800 for c in commands if c['op']=='set_cold_storage'),1)
        self.assertEqual(commands[-1]['op'],'commander_episode')

    def test_siege_curriculum_keeps_mixed_pools_and_campaign_holdouts(self):
        collection, selection, holdout = curriculum_templates('siege')
        self.assertEqual(len(collection),12)
        for cases in (collection[:9],collection[9:],selection,holdout):
            self.assertEqual({family(a) for a,_ in cases},{'full','masked','normal'})
        for arena in ('opening','developing','fortress'):
            self.assertIn(('normal:'+arena,'lotus'),selection)
        for n in range(1,10):
            self.assertTrue(any(a==f'normal:opening_10_{n}' for a,_ in holdout))
        policy = {'weights':[1]*8,'trainingUnits':[f'ZOMBIE_{n}' for n in range(12)]}
        for a,o in collection+selection+holdout:
            commands=episode_commands(policy,71,a,o,300,'fixture',True)
            cards=next(c['cards'] for c in commands if c['op']=='choose_cards')
            self.assertLessEqual(len(cards),11)
            if a.startswith('normal:'):
                self.assertFalse(any(c['op']=='commander_roster' for c in commands))
        self.assertEqual([len(c) for c in curriculum_templates('balanced')],[12,6,15])

    def test_lotus_opponent_uses_real_card_slots(self):
        for all_units in (False,True):
            commands = episode_commands([1]*8,17,'opening','lotus',120,'lotus',all_units)
            cards = next(c['cards'] for c in commands if c['op']=='choose_cards')
            self.assertLessEqual(len(cards),11)
            self.assertIn('PLANT_DAWNLOTUS',cards)
            if all_units:
                self.assertIn('PLANT_BLOVER',cards)
            else:
                self.assertIn('PLANT_SQUASH',cards)

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
