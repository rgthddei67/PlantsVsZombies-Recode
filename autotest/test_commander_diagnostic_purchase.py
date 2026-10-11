"""Independent receipt counterexamples for atomic mixed diagnostic purchases (no game run)."""
import copy
import unittest

from verify_commander_diagnostic_purchase import verify_episode


def mixed_episode():
    """A four-unit cart pays once; its heterogeneous tickets retain member order and delays."""
    members = [{"type": "ZOMBIE_ICE_WORKER", "row": 1, "count": 2, "delay": 7},
               {"type": "ZOMBIE_NORMAL", "row": 3},
               {"type": "ZOMBIE_ICE_WORKER", "row": 4, "delay": 1}]
    old = {"ticket": 10, "type": 1, "row": 2, "cost": 10, "remaining": 3, "wave": 1}
    paid = [{"ticket": 11, "type": 100, "row": 1, "cost": 24, "remaining": 7, "wave": 3},
            {"ticket": 12, "type": 100, "row": 1, "cost": 24, "remaining": 7, "wave": 3},
            {"ticket": 13, "type": 1, "row": 3, "cost": 10, "remaining": 0, "wave": 3},
            {"ticket": 14, "type": 100, "row": 4, "cost": 24, "remaining": 1, "wave": 3}]
    purchase = {"requested": {"at": 63, "members": members}, "attempted": True, "accepted": True,
                "controller": "scripted_purchase", "status": "accepted", "seconds": 63.083,
                "walletBefore": 200, "walletAfter": 118, "spentBefore": 10, "spentAfter": 92,
                "decisionsBefore": 2, "decisionsAfter": 3, "serialBefore": 6, "serialAfter": 7,
                "actualCost": 82, "requestedCost": 82, "requestedCount": 4, "acceptedCount": 4,
                "availableCapacity": 4, "oldQueuePreserved": True, "newQueueMatchesRequested": True,
                "pendingBefore": [old], "pendingAfter": [copy.deepcopy(old), *paid],
                "members": [
                    {"requested": members[0], "typeID": 100, "unitCost": 24, "requestedCost": 48, "unlockWave": 3},
                    {"requested": members[1], "typeID": 1, "unitCost": 10, "requestedCost": 10, "unlockWave": 1},
                    {"requested": members[2], "typeID": 100, "unitCost": 24, "requestedCost": 24, "unlockWave": 3}],
                "prediction": {"matchesRequested": True, "unlimitedDiagnostic": True, "evaluationMs": 8,
                               "score": -3, "features": [0] * 8, "baseline": [0] * 8}}
    initial = {"initialEnemyIce": 210, "enemyIce": 200, "supplied": 0, "workerIncome": 0, "killIncome": 0, "spent": 10}
    final = {**initial, "enemyIce": 118, "spent": 92}
    return {"controller": "autonomous_with_scripted_purchase", "diagnosticPurchases": [purchase],
            "decisions": [{"serial": 6, "controller": "autonomous"}, {"serial": 7, "controller": "scripted_purchase"}],
            "initial": {"rows": 5, "coldStorage": initial}, "final": {"coldStorage": final}, "trace": []}


def reject(purchase, status):
    """Model a refusal that leaves the original wallet, wave, serial and paid queue intact."""
    purchase.update(accepted=False, status=status, actualCost=0, acceptedCount=0,
                    walletAfter=purchase["walletBefore"], spentAfter=purchase["spentBefore"],
                    decisionsAfter=purchase["decisionsBefore"], serialAfter=purchase["serialBefore"],
                    pendingAfter=copy.deepcopy(purchase["pendingBefore"]))


class DiagnosticPurchaseTests(unittest.TestCase):
    def test_contrast_requires_four_matched_worlds_and_physical_incremental_income(self):
        episode = mixed_episode()
        purchase = episode["diagnosticPurchases"][0]
        purchase["requested"]["responseContrast"] = True
        metadata = {"requestedSearchVersion": 1, "searchVersion": 2, "expandedForecast": True,
                    "traceHorizonSeconds": 120, "productionWindowSeconds": 60}
        world = {**metadata, "complete": True, "paidDefensesRetained": True,
                 "features": [0, 0, 0, 0, 40, 0, 0, 0], "baseline": [0, 0, 0, 0, 40, 0, 0, 0],
                 "effectiveWeights": [0, 0, 0, 0, 1, -1, 0, 0], "score": 40, "counterHoldSeconds": 32,
                 "rawProduction": 100, "productionAfterWindow": 20, "totalRawProduction": 120,
                 "paidCounterCasts": 1, "cobShots": 2, "constructedCobShots": 1, "cobUpgrades": 1, "planted": 2,
                 "exchanges": 1, "orders": 1, "sunSpent": 500, "iceSpent": 80, "plantAssets": 200, "plantAttackAssets": 0,
                 "workerTrace": [{"time": 20, "income": 100}, {"time": 80, "income": 20}], "counterTrace": []}
        pair = {"complete": True, "purchase": copy.deepcopy(world), "wait": copy.deepcopy(world),
                "delta": {"cash": 0, "plantAttackAssets": 0, "breach": False, "rawProduction": 0,
                          "productionAfterWindow": 0, "totalRawProduction": 0}}
        # A cart-specific calibration can increase feature[4] while the existing army's actual yield stays unchanged.
        pair["purchase"]["features"][4] = 120
        pair["purchase"]["features"][5] = purchase["requestedCost"]
        pair["purchase"]["score"] = 38
        inventory = {"total": 15, "freshPaidArea": 12, "free": 1, "committed": 1, "stored": 1, "reusable": 1, "other": 3}
        contrast = {**metadata, "hypothetical": True, "probabilityModel": False, "unlimitedDiagnostic": True,
                    "basis": "shared_full_plan_cart_wait", "evaluator": "informational_trial_four_arm",
                    "responsePolicy": "unchanged_full_plan", "complete": True, "matchesRequested": True,
                    "bill": purchase["requestedCost"], "traceEconomy": True,
                    "hypothesis": "future_direct_paid_area_counters_withheld", "responseEnvelopeSeconds": 7,
                    "storedResponseEnvelopeSeconds": 32, "patientResponseHoldSeconds": 8,
                    "removedCounterCount": 12, "removedSourceKeys": [2, 8], "evaluationMs": 9,
                    "counterInventory": {"robust": inventory, "potential": {**inventory, "total": 3, "freshPaidArea": 0}},
                    "robust": copy.deepcopy(pair), "potential": copy.deepcopy(pair)}
        purchase["responseContrast"] = contrast
        verify_episode(episode, require_mixed=True)
        self.assertEqual(purchase["prediction"]["score"], -3)
        for name, damage in {
            "missing robust passive": lambda c: c["robust"].pop("wait"),
            "missing potential passive": lambda c: c["potential"].pop("wait"),
            "partial passive": lambda c: c["potential"]["wait"].update(complete=False),
            "broad paid suppression": lambda c: c.update(responsePolicy="all_paid_disabled"),
            "deleted free cannon": lambda c: c["counterInventory"]["potential"].update(free=0),
            "deleted committed counter": lambda c: c["counterInventory"]["potential"].update(committed=0),
            "different horizon": lambda c: c["potential"]["purchase"].update(traceHorizonSeconds=60),
            "different version": lambda c: c["potential"]["wait"].update(searchVersion=1),
            "missing expansion": lambda c: c["robust"]["wait"].update(expandedForecast=False),
            "different weights": lambda c: c["potential"]["wait"].update(effectiveWeights=[0] * 8),
            "different wait baseline": lambda c: c["potential"]["purchase"].update(baseline=[0] * 8),
            "omitted after-window income": lambda c: c["potential"]["purchase"].update(totalRawProduction=100),
            "wrong trace total": lambda c: c["potential"]["purchase"].update(workerTrace=[{"time": 20, "income": 100}]),
            "same total in wrong windows": lambda c: c["potential"]["purchase"].update(
                workerTrace=[{"time": 80, "income": 100}, {"time": 20, "income": 20}]),
            "calibration credited as cash": lambda c: c["potential"]["delta"].update(cash=80),
            "old army credited as new": lambda c: c["potential"]["delta"].update(totalRawProduction=120),
            "lost delay envelope": lambda c: c.update(responseEnvelopeSeconds=0),
            "probability claim": lambda c: c.update(probabilityModel=True),
            "unidentified removed source": lambda c: c.update(removedSourceKeys=[0]),
        }.items():
            with self.subTest(name=name):
                bad = copy.deepcopy(episode)
                damage(bad["diagnosticPurchases"][0]["responseContrast"])
                with self.assertRaises((AssertionError, KeyError)):
                    verify_episode(bad)

        with self.subTest(name="float32 delay remains legal"):
            fractional = copy.deepcopy(episode)
            receipt = fractional["diagnosticPurchases"][0]
            receipt["requested"]["members"][0]["delay"] = 40.1
            for paid in receipt["pendingAfter"][1:3]:
                paid["remaining"] = 40.099998474121094
            receipt["responseContrast"].update(responseEnvelopeSeconds=40.099998474121094,
                                                storedResponseEnvelopeSeconds=40.099998474121094)
            verify_episode(fractional, require_mixed=True)
            for shortened in (40.09, 40, 32):
                with self.subTest(shortened=shortened):
                    bad = copy.deepcopy(fractional)
                    bad["diagnosticPurchases"][0]["responseContrast"]["storedResponseEnvelopeSeconds"] = shortened
                    with self.assertRaises(AssertionError):
                        verify_episode(bad)

        with self.subTest(name="exact production boundary belongs to after-window"):
            boundary = copy.deepcopy(episode)
            trace = boundary["diagnosticPurchases"][0]["responseContrast"]["potential"]["purchase"]["workerTrace"]
            trace[1]["time"] = 60
            verify_episode(boundary, require_mixed=True)
            trace[1]["time"] = 59.5
            with self.assertRaises(AssertionError):
                verify_episode(boundary)

    def test_mixed_batch_pays_once_and_preserves_ordered_heterogeneous_tickets(self):
        report = verify_episode(mixed_episode(), require_accepted=True, require_mixed=True)
        self.assertEqual(report["acceptedMixedPurchases"], 1)
        self.assertTrue(report["purchaseLifecycleObserved"])
        self.assertFalse(report["autonomousStrengthVerified"])

    def test_old_flat_receipt_remains_compatible(self):
        episode = mixed_episode()
        purchase = episode["diagnosticPurchases"][0]
        member = purchase.pop("members")[0]
        purchase["requested"] = {"at": 63, **member["requested"]}
        purchase.update(typeID=100, unitCost=24, unlockWave=3, requestedCost=48, actualCost=48,
                        walletAfter=152, spentAfter=58, acceptedCount=2)
        purchase.pop("requestedCount")
        purchase["pendingAfter"] = purchase["pendingAfter"][:3]
        episode["final"]["coldStorage"].update(enemyIce=152, spent=58)
        self.assertEqual(verify_episode(episode, require_accepted=True)["acceptedPurchases"], 1)
        with self.assertRaises(AssertionError):
            verify_episode(episode, require_mixed=True)

    def test_rejected_mixed_later_member_cannot_advance_unlock(self):
        episode = mixed_episode()
        purchase = episode["diagnosticPurchases"][0]
        purchase["members"][2]["unlockWave"] = 4
        with self.assertRaises(AssertionError):
            verify_episode(episode)
        reject(purchase, "locked")
        purchase["failedMember"] = 2
        episode["decisions"] = [{"serial": 6, "controller": "autonomous"}]
        episode["final"] = copy.deepcopy(episode["initial"])
        report = verify_episode(episode, require_all_rejected=True)
        self.assertFalse(report["purchaseLifecycleObserved"])
        with self.assertRaises(AssertionError):
            verify_episode(episode, require_accepted=True)

    def test_queue_and_payment_counterexamples_are_rejected(self):
        cases = {
            "rerouted member": lambda p: p["pendingAfter"][3].update(row=1),
            "delay normalized": lambda p: p["pendingAfter"][1].update(remaining=0),
            "late member omitted": lambda p: p["pendingAfter"].pop(),
            "old ticket retimed": lambda p: p["pendingAfter"][0].update(remaining=0),
            "one wave per member": lambda p: p.update(decisionsAfter=5),
            "one decision per member": lambda p: p.update(serialAfter=9),
            "summary uses first unit price": lambda p: p.update(requestedCost=96),
            "duplicate ticket": lambda p: p["pendingAfter"][4].update(ticket=11),
            "capacity only fits prefix": lambda p: p.update(availableCapacity=3),
            "ambiguous legacy override": lambda p: p["requested"].update(delay=0),
            "changed member receipt": lambda p: p["members"].reverse(),
            "early payment": lambda p: p.update(seconds=62),
            "wrong controller": lambda p: p.update(controller="autonomous"),
        }
        for name, damage in cases.items():
            with self.subTest(name=name):
                episode = mixed_episode()
                damage(episode["diagnosticPurchases"][0])
                with self.assertRaises(AssertionError):
                    verify_episode(episode)

    def test_all_rejected_cannot_hide_partial_purchase_or_claim_success(self):
        for status in ("invalid_request", "planner_busy", "insufficient_capacity"):
            with self.subTest(status=status):
                episode = mixed_episode()
                purchase = episode["diagnosticPurchases"][0]
                reject(purchase, status)
                purchase["availableCapacity"] = 3
                episode["decisions"] = []
                episode["final"] = copy.deepcopy(episode["initial"])
                self.assertEqual(verify_episode(episode, require_all_rejected=True)["acceptedPurchases"], 0)
                purchase["walletAfter"] -= 24
                purchase["spentAfter"] += 24
                purchase["actualCost"] = 24
                with self.assertRaises(AssertionError):
                    verify_episode(episode, require_all_rejected=True)

    def test_all_rejected_requires_an_actual_attempt(self):
        episode = mixed_episode()
        episode["diagnosticPurchases"] = [{"requested": {"at": 120, "members": []},
                                          "attempted": False, "accepted": False, "status": "episode_timeout"}]
        episode["decisions"] = []
        with self.assertRaises(AssertionError):
            verify_episode(episode, require_all_rejected=True)


if __name__ == "__main__":
    unittest.main()
