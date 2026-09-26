"""Fresh origins must not silently inherit learned terms or mutate their parent."""
import copy
import random
import unittest
from train_commander_fresh import initial_policy, population


class FreshTests(unittest.TestCase):
    def test_new_architecture_starts_its_new_weight_at_zero(self):
        policy=initial_policy(['normal'],anticipate_building=False,opponent=True)
        self.assertFalse(policy['anticipateBuilding'])
        self.assertEqual(policy['opponentWeight'],0)
        self.assertNotIn('productionCalibration',policy)
        variants=population(policy,random.Random(918),8,0,4)
        self.assertTrue(any(p['opponentWeight']>0 for p in variants))

    def test_initialization_contains_no_learned_terms(self):
        policy=initial_policy(['normal','worker'])
        self.assertNotIn('productionCalibration',policy)
        self.assertTrue(all(not any(row) for row in policy['preferences'].values()))
        self.assertTrue(all(not any(row) for row in policy['stateModel']['coefficients']))
        self.assertEqual(policy['weights'][5],0)

    def test_exploration_keeps_versions_and_currency_constraints(self):
        parent=initial_policy(['normal','worker'])
        before=copy.deepcopy(parent)
        for generation in (0,1):
            group=population(parent,random.Random(123),10,generation,4)
            self.assertEqual(len(group),10)
            self.assertEqual(group[0],parent)
            self.assertGreater(len({tuple(p['weights']) for p in group}),5)
            for candidate in group:
                self.assertEqual(candidate['searchVersion'],2)
                self.assertTrue(candidate['netEconomy'])
                self.assertTrue(candidate['anticipateBuilding'])
                self.assertNotIn('productionCalibration',candidate)
                self.assertEqual(candidate['weights'][5],0)
                self.assertTrue(all(row[5]==0 for row in candidate['stateModel']['coefficients']))
        self.assertEqual(parent,before)


if __name__=='__main__':
    unittest.main()
