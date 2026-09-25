"""Attribution, censoring, leakage boundaries and roster contracts for league training."""
import copy
import unittest
import random

from commander_calibration import fit, metrics, predict, samples
from train_cold_storage import episode_commands, battle_progress
from train_commander_league import gate, curriculum_templates, family, grouped_key, draw_cases
from train_cold_storage_all import new_state_model, mutate, state_population, net_economy_policy
from audit_commander_forecasts import windows
from evaluate_commander_roster_ablation import roster_commands


class CalibrationTests(unittest.TestCase):
    def test_calibration_prioritizes_ice_error_instead_of_tiny_forecast_ratios(self):
        rows=[{'x':[0]*10,'raw':10,'target':1,'actual':10} for _ in range(24)]
        rows += [{'x':[0]*10,'raw':1000,'target':0,'actual':0} for _ in range(12)]
        tree=fit(rows,depth=0)
        self.assertLess(predict(tree,[0]*10),.001)
        self.assertLess(metrics(rows,tree)['mae'],metrics(rows)['mae'])
    def test_net_accounting_is_explicit_and_cannot_mutate_a_spending_reward(self):
        original={'weights':[1]*8,'preferences':{'worker':[0]*8},'stateModel':new_state_model()}
        original['stateModel']['coefficients'][0][5]=10
        candidate=net_economy_policy(original)
        self.assertNotIn('netEconomy',original)
        self.assertEqual(original['stateModel']['coefficients'][0][5],10)
        for seed in range(10):
            changed=mutate(candidate,random.Random(seed),2)
            self.assertTrue(changed['netEconomy'])
            self.assertEqual(changed['weights'][5],0)
            self.assertTrue(all(row[5]==0 for row in changed['stateModel']['coefficients']))
        command=next(c for c in episode_commands(candidate,17,'normal:opening','lotus',120,'x') if c['op']=='commander_experiment')
        self.assertTrue(command['netEconomy'])
    def test_roster_ablation_changes_only_legal_purchase_pool(self):
        policy={'weights':[1]*8}
        units=['ZOMBIE_NORMAL','ZOMBIE_ICE_WORKER','ZOMBIE_ELITE_DANCER']
        a=roster_commands(policy,912,'opening','builder',300,'paired',units)
        b=roster_commands(policy,912,'opening','builder',300,'paired',units[:-1])
        self.assertEqual([c for c in a if c['op']!='commander_roster'],[c for c in b if c['op']!='commander_roster'])
        self.assertEqual(next(c['units'] for c in b if c['op']=='commander_roster'),units[:-1])

    def test_state_only_preserves_every_other_policy_field_and_explores_both_signs(self):
        policy={'weights':[1]*8,'preferences':{'normal':[7]*8},'productionCalibration':{'nodes':[1]}}
        before=copy.deepcopy(policy)
        population=state_population(policy,random.Random(21),6)
        self.assertEqual(policy,before)
        for candidate in population:
            self.assertEqual({k:v for k,v in candidate.items() if k!='stateModel'},before)
        for a,b in ((2,3),(4,5)):
            ca,cb=(population[n]['stateModel']['coefficients'] for n in (a,b))
            self.assertTrue(any(x!=0 for row in ca for x in row))
            self.assertEqual(ca,[[-v for v in row] for row in cb])
        for seed in range(10):
            rng=random.Random(seed)
            rows=[next(i for i,row in enumerate(p['stateModel']['coefficients']) if any(row))
                  for generation in range(2) for p in state_population(policy,rng,8,generation)[2::2]]
            self.assertEqual(sorted(rows),list(range(6)))

    def test_reserve_fixtures_vary_and_cooldowns_use_paid_actions(self):
        budgets=set(); layouts=set()
        for seed in range(12):
            commands=episode_commands([1]*8,seed,'normal:banked_varied_cooling','ash',300,'x',True)
            cards=next(c['cards'] for c in commands if c['op']=='choose_cards')
            plants=[c for c in commands if c['op']=='plant']
            self.assertTrue(all(c['type'] in cards for c in plants))
            budgets.update(c['state']['enemyIce'] for c in commands if c['op']=='set_cold_storage' and 'enemyIce' in c['state'])
            self.assertEqual(sum(c['op']=='player_plant' for c in commands),3)
            self.assertFalse(any(c['op'] in ('set_no_cooldown','spawn_zombie') for c in commands))
            commands=episode_commands([1]*8,seed,'normal:banked_varied','lotus',300,'x',True)
            layouts.add(tuple(sum(c['op']=='plant' and c.get('row')==r and c.get('type')=='PLANT_MELONPULT' for c in commands) for r in range(5)))
        self.assertGreater(len(budgets),2);self.assertGreater(len(layouts),2)
        _,selection,holdout=curriculum_templates('reserves')
        self.assertEqual({family(a) for a,o in selection},{'full','masked','normal'})
        self.assertTrue(all(any(a==f'normal:opening_10_{n}' for a,o in holdout) for n in range(1,10)))

    def test_forecast_audit_excludes_censoring_and_marks_later_purchase(self):
        episode=self.episode(end=80)
        episode['final']['coldStorage']['killIncome']=12
        episode['decisions'][0].update(features=[20,0,0,0,80,24,0,0],killIncome=0)
        rows=windows(episode)
        self.assertEqual(rows[0]['actualKills'],12)
        self.assertEqual(rows[0]['actualProduction'],20)
        self.assertFalse(rows[0]['laterPurchases'])
        episode['decisions'][0].update(features=[0,0,0,0,64,600,30,0],effectiveWeights=[0,0,0,0,1,.7,0,0])
        self.assertTrue(windows(episode)[0]['rewardedPredictedLoss'])
        episode['decisions'].append(dict(episode['decisions'][0],elapsed=30,wave=3))
        self.assertTrue(windows(episode)[0]['laterPurchases'])
        episode['final']['coldStorage']['elapsed']=50
        self.assertEqual(windows(episode),[])

    def test_state_layer_is_forwarded_and_mutated_without_changing_baselines(self):
        policy={'weights':[1]*8,'preferences':{'ZOMBIE_NORMAL':[0]*8},'stateModel':new_state_model()}
        before=copy.deepcopy(policy)
        changed=mutate(policy,random.Random(17),1)
        self.assertEqual(policy,before)
        self.assertNotEqual(changed['stateModel']['coefficients'],policy['stateModel']['coefficients'])
        self.assertEqual(len(changed['stateModel']['coefficients']),6)
        self.assertTrue(all(len(row)==8 and all(-500<=x<=500 for x in row) for row in changed['stateModel']['coefficients']))
        command=next(c for c in episode_commands(changed,17,'normal:opening','lotus',900,'x',True) if c['op']=='commander_experiment')
        self.assertEqual(command['stateModel'],changed['stateModel'])
        self.assertFalse(command['allZombies'])
        legacy=copy.deepcopy(policy);del legacy['stateModel']
        self.assertNotIn('stateModel',mutate(legacy,random.Random(17),1))

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
