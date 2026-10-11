"""R7 real Board repayment/save checks; synthetic ownership is NOT AI strength evidence.

Usage: python autotest/verify_commander_repaid_trial.py <AutoTest output directory>
Only the verification report is written. Original dumps, snapshots, status and log
are kept intact, including when a check fails. The caller owns run/output isolation.
"""

import json
import sys
from pathlib import Path


def verify(folder):
    """Reconcile real paid workers and accepted production across one paused reload."""
    folder = Path(folder)

    def load(name):
        return json.loads((folder / name).read_text(encoding="utf-8-sig"))

    def state(name):
        return load(name + ".json")

    def record(cold):
        history = cold["boundedTrials"]
        assert history["disabled"] is False, history
        assert len(history["records"]) == 1, history
        result = history["records"][0]
        assert result["signature"] == 710001, result
        assert result["latestPaidWave"] == 10 and result["paidWaves"] == [9, 10], result
        assert result["cost"] == 48 and result["startedAt"] == 100, result
        return result

    def workers(dump, waves=(9, 10, 11)):
        """Use stable wave/identity pairs, and reject duplicates or dead replacements."""
        entities = dump["zombies"]
        assert len(entities) == len(waves), (dump["coldStorage"]["elapsed"], entities)
        assert all(z["type"] == "ZOMBIE_ICE_WORKER" for z in entities), entities
        result = {z["spawnWave"]: z for z in entities}
        assert set(result) == set(waves), result
        assert len({z["id"] for z in entities}) == len(waves), entities
        for wave, worker in result.items():
            assert worker["row"] == {9: 0, 10: 4, 11: 2}[wave], worker
            assert worker["bodyHealth"] > 0 and worker["hasHead"], worker
            assert not worker["isDying"] and not worker["mindControlled"], worker
        return result

    def same_fields(one, two, keys, label):
        for key in keys:
            assert one[key] == two[key], (label, key, one[key], two[key])

    def conserved(before, after, label):
        """Wallet controls start new intervals; never pretend those edits are income."""
        actual = after["enemyIce"] - before["enemyIce"]
        accounted = (after["supplied"] - before["supplied"]
                     + after["workerIncome"] - before["workerIncome"]
                     + after["killIncome"] - before["killIncome"]
                     - after["spent"] + before["spent"])
        assert actual == accounted, (label, actual, accounted)

    def event_totals(cold):
        """Sum every accepted event by source; the diagnostic event list resets on reload."""
        events = cold["productionEvents"]
        totals = {9: 0, 10: 0, 11: 0}
        previous_at = 100
        seen = set()
        for event in events:
            wave, amount, at = event["wave"], event["amount"], event["at"]
            assert wave in totals and type(amount) is int and amount > 0, event
            assert previous_at <= at <= cold["elapsed"], event
            assert (wave, at) not in seen, ("duplicate event", event)
            seen.add((wave, at))
            previous_at = at
            totals[wave] += amount
        # This short fixture must retain the complete current event segment.
        assert cold["elapsed"] < 220, cold["elapsed"]
        return totals

    assert load("status.json")["status"] == "passed", load("status.json")
    assert "script finished OK" in (folder / "run.log").read_text(encoding="utf-8-sig")
    names = (
        "purchase_before", "paid_before_synthetic_record", "synthetic_record", "all_paid",
        "old_wave_only", "both_waves_before_repayment", "full_wallet_before",
        "full_wallet_after", "repayment_resume_before", "repaid_alive",
        "paused_before", "paused_after", "repaid_after_reload", "old_waves_resumed",
    )
    dumps = {name: state(name) for name in names}
    cold = {name: dump["coldStorage"] for name, dump in dumps.items()}
    for name, dump in dumps.items():
        assert dump["level"] == 91 and dump["boardState"] == "GAME", name
        assert dump["plantCount"] == 0 and dump["plants"] == [], name
        assert dump["selectedTimeScaleOn1000"] == 5000, name
        assert cold[name]["trainingAllUnits"] is False, name
        assert cold[name]["planning"] is False and cold[name]["decisionRemaining"] > 0, name

    before, paid = cold["purchase_before"], cold["paid_before_synthetic_record"]
    assert before["enemyIce"] == 600 and before["spent"] == 0 and before["decisions"] == 9
    assert paid["enemyIce"] == 552 and paid["spent"] == 48 and paid["decisions"] == 10
    for value in (before, paid):
        assert value["boundedTrials"] == {"disabled": False, "records": []}, value["boundedTrials"]
        assert value["workerIncome"] == 0 and value["productionEvents"] == []
        assert value["supplied"] == value["killIncome"] == 0
    assert sorted((p["wave"], p["row"], p["cost"], p["typeName"]) for p in paid["pending"]) == [
        (9, 0, 24, "ZOMBIE_ICE_WORKER"), (10, 4, 24, "ZOMBIE_ICE_WORKER")]
    conserved(before, paid, "two actual queue payments")

    money_keys = ("enemyIce", "spent", "workerIncome", "killIncome", "supplied")
    synthetic = cold["synthetic_record"]
    assert record(synthetic)["production"] == 0 and record(synthetic)["unresolved"] is True
    same_fields(paid, synthetic, money_keys + ("pending", "elapsed", "decisions"),
                "synthetic ownership does not pay or earn")
    all_paid = cold["all_paid"]
    assert all_paid["enemyIce"] == 528 and all_paid["spent"] == 72 and all_paid["decisions"] == 11
    assert all_paid["boundedTrials"] == synthetic["boundedTrials"]
    assert sorted((p["wave"], p["row"], p["cost"], p["typeName"]) for p in all_paid["pending"]) == [
        (9, 0, 24, "ZOMBIE_ICE_WORKER"), (10, 4, 24, "ZOMBIE_ICE_WORKER"),
        (11, 2, 24, "ZOMBIE_ICE_WORKER")]
    conserved(synthetic, all_paid, "unowned wave 11 payment")

    totals_by_phase = {}
    previous_events = []
    # Partial set_cold_storage edits keep diagnostic events on the existing Board.
    for name in names[2:12]:
        current = cold[name]
        totals = event_totals(current)
        totals_by_phase[name] = totals
        events = current["productionEvents"]
        assert events[:len(previous_events)] == previous_events, (name, "lost/replayed event prefix")
        previous_events = events
        assert record(current)["production"] == totals[9] + totals[10], (name, totals, record(current))
        assert current["workerIncome"] == sum(totals.values()), (name, totals)
        if name != "synthetic_record":
            assert current["spent"] == 72 and current["decisions"] == 11, name

    old = cold["old_wave_only"]
    workers(dumps["old_wave_only"], (9, 11))
    assert len(old["pending"]) == 1 and old["pending"][0]["wave"] == 10
    assert totals_by_phase["old_wave_only"][9] > 0 and totals_by_phase["old_wave_only"][10] == 0
    assert totals_by_phase["old_wave_only"][11] > 0
    both = cold["both_waves_before_repayment"]
    original_workers = workers(dumps["both_waves_before_repayment"])
    assert all(value > 0 for value in totals_by_phase["both_waves_before_repayment"].values())
    for current in (old, both):
        assert 0 < record(current)["production"] < 48 and record(current)["unresolved"] is True
        assert current["troopActivity"] is True
    assert both["pending"] == [] and both["deployments"] == 3
    conserved(all_paid, both, "production before wallet control")

    full_before, full_after = cold["full_wallet_before"], cold["full_wallet_after"]
    same_fields(both, full_before, ("boundedTrials", "workerIncome", "productionEvents", "spent"),
                "full-wallet test control")
    assert full_before["enemyIce"] == full_after["enemyIce"] == 1_000_000
    same_fields(full_before, full_after, money_keys + ("boundedTrials", "productionEvents"),
                "overflow batches cannot credit the trial or wallet")
    first, last = workers(dumps["full_wallet_before"]), workers(dumps["full_wallet_after"])
    for wave in first:
        assert first[wave]["id"] == last[wave]["id"] == original_workers[wave]["id"]
        assert last[wave]["iceBatches"] > first[wave]["iceBatches"], ("real overflow production", wave)
    assert record(full_after)["unresolved"] is True and record(full_after)["production"] < 48
    resume_before = cold["repayment_resume_before"]
    assert resume_before["enemyIce"] == 600
    same_fields(full_after, resume_before, ("boundedTrials", "workerIncome", "productionEvents", "spent"),
                "restoring test wallet cannot repay the trial")

    repaid = cold["repaid_alive"]
    assert record(repaid)["production"] >= 48 and record(repaid)["unresolved"] is False
    assert repaid["troopActivity"] and repaid["hostileCount"] == 3 and repaid["pending"] == []
    repaid_workers = workers(dumps["repaid_alive"])
    assert {wave: z["id"] for wave, z in repaid_workers.items()} == {
        wave: z["id"] for wave, z in original_workers.items()}
    earned = 0
    crossed_at = None
    for event in repaid["productionEvents"]:
        if event["wave"] in (9, 10):
            earned += event["amount"]
            if earned >= 48:
                crossed_at = event["at"]
                break
    assert crossed_at is not None
    # wait_value must observe release on the earning update, at most one fixed
    # 5x logic step later if Board and worker update order changes.
    assert 0 <= repaid["elapsed"] - crossed_at <= 5 / 60 + 0.001, (repaid["elapsed"], crossed_at)
    conserved(resume_before, repaid, "actual repayment after wallet control")

    paused_before, paused_after = cold["paused_before"], cold["paused_after"]
    assert dumps["paused_before"]["paused"] and dumps["paused_after"]["paused"]
    persisted_keys = money_keys + (
        "initialEnemyIce", "elapsed", "decisionRemaining", "supplyRemaining", "decisions",
        "deployments", "pending", "boundedTrials", "incomeWindow", "incomeWindowProduction",
        "incomeWindowSpent", "emptyWaitSeconds", "strikeAimRemaining", "strikeCooldownRemaining",
    )
    same_fields(paused_before, paused_after, persisted_keys + ("productionEvents",), "real pause")
    conserved(repaid, paused_before, "pause input edge")
    worker_keys = ("id", "row", "spawnWave", "bodyHealth", "hasHead", "isDying",
                   "iceBatches", "iceRemainingMs", "nextIceYieldOn1000")
    paused_workers = workers(dumps["paused_after"])
    for wave, worker in workers(dumps["paused_before"]).items():
        same_fields(worker, paused_workers[wave], worker_keys, ("paused worker", wave))

    snapshot = load("snapshots/repaid_trial.json")
    assert snapshot["schemaVersion"] == 31, snapshot["schemaVersion"]
    saved = snapshot["coldStorage"]
    same_fields(paused_after, saved, persisted_keys, "actual paused snapshot")
    reloaded = cold["repaid_after_reload"]
    same_fields(saved, reloaded, persisted_keys, "exact paid state after reload")
    assert record(reloaded)["unresolved"] is False
    assert reloaded["productionEvents"] == [], "diagnostic events are not persistent income"
    expected_refunds = sorted((worker["id"], 24) for worker in paused_workers.values())
    for current in (paused_after, saved, reloaded):
        assert sorted((item["id"], item["cost"]) for item in current["refundableCosts"]) == expected_refunds
    reloaded_workers = workers(dumps["repaid_after_reload"])
    for wave in paused_workers:
        same_fields(paused_workers[wave], reloaded_workers[wave], worker_keys, ("worker reload", wave))

    resumed = cold["old_waves_resumed"]
    resumed_workers = workers(dumps["old_waves_resumed"])
    after_reload_totals = event_totals(resumed)
    assert all(value > 0 for value in after_reload_totals.values()), after_reload_totals
    assert record(resumed)["production"] - record(reloaded)["production"] == (
        after_reload_totals[9] + after_reload_totals[10]), after_reload_totals
    assert resumed["workerIncome"] - reloaded["workerIncome"] == sum(after_reload_totals.values())
    assert record(resumed)["unresolved"] is False and resumed["troopActivity"] is True
    assert resumed["spent"] == 72 and resumed["decisions"] == 11
    assert resumed["pending"] == [] and resumed["deployments"] == 3
    for wave in reloaded_workers:
        assert resumed_workers[wave]["id"] == reloaded_workers[wave]["id"]
        assert resumed_workers[wave]["iceBatches"] > reloaded_workers[wave]["iceBatches"], wave
    conserved(reloaded, resumed, "old waves continue after reload without double payment")

    report = {
        "passed": True,
        "scope": "controlled real Board transaction/repayment/lifecycle fixture, NOT strength evidence",
        "trialRecordOrigin": "synthetic after two legal 24-ice queue payments; owns waves 9 and 10 only",
        "autonomousTrialCommitCovered": False,
        "purchaseWaveCosts": {"9": 24, "10": 24, "11": 24},
        "trialPaidWaves": [9, 10],
        "trialCost": 48,
        "acceptedProductionByWaveBeforeReload": totals_by_phase,
        "acceptedProductionByWaveAfterReload": after_reload_totals,
        "repaidAt": crossed_at,
        "repaidProduction": record(repaid)["production"],
        "productionAfterReloadResume": record(resumed)["production"],
        "repaymentReleasesUnresolvedWhileOwnedWorkersLive": True,
        "unownedWaveExcluded": True,
        "fullWalletBatchesWithoutFalseCredit": True,
        "realPauseFreezesCashLedgerAndWorkerProduction": True,
        "exactCashLedgerPaidWavesAndWorkerReload": True,
        "oldAndLatestOwnedWavesContinueCreditingAfterReload": True,
        "noDoublePaymentOrDuplicateDeployments": True,
        "schemaVersion": 31,
    }
    (folder / "repaid_trial_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: verify_commander_repaid_trial.py <AutoTest output directory>")
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
