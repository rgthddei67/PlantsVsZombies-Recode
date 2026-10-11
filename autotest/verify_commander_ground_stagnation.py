"""R5 controlled Board lifecycle checks. Synthetic trial attribution is NOT strength evidence."""
import json
import sys
from pathlib import Path


def verify(folder):
    def load(name):
        return json.loads((folder / name).read_text(encoding="utf-8-sig"))

    def state(name):
        return load(name + ".json")

    def ice(name):
        return state(name)["coldStorage"]

    def record(cold):
        history = cold["boundedTrials"]
        assert history["disabled"] is False, history
        assert len(history["records"]) == 1, history
        result = history["records"][0]
        assert result["signature"] == 510001 and result["latestPaidWave"] == 10
        assert result["cost"] == 24 and result["startedAt"] == 100
        assert result["paidWaves"] == [10]
        return result

    def workers(dump):
        result = {z["spawnWave"]: z for z in dump["zombies"]
                  if z["type"] == "ZOMBIE_ICE_WORKER"}
        assert set(result) == {9, 10}, result
        return result

    def conserved(before, after):
        """Artificial wallet controls start new phases; compare only untouched-wallet intervals."""
        assert after["enemyIce"] - before["enemyIce"] == (
            after["supplied"] - before["supplied"]
            + after["workerIncome"] - before["workerIncome"]
            + after["killIncome"] - before["killIncome"]
            - after["spent"] + before["spent"])

    assert load("status.json")["status"] == "passed"
    assert "script finished OK" in (folder / "run.log").read_text(encoding="utf-8-sig")

    before = ice("purchase_before")
    paid = ice("paid_before_synthetic_record")
    assert before["trainingAllUnits"] is False and paid["trainingAllUnits"] is False
    assert before["boundedTrials"]["records"] == paid["boundedTrials"]["records"] == []
    assert before["enemyIce"] == 600 and before["spent"] == 0 and before["decisions"] == 9
    assert paid["enemyIce"] == 552 and paid["spent"] == 48 and paid["decisions"] == 10
    assert sorted((p["wave"], p["row"], p["cost"], p["typeName"]) for p in paid["pending"]) == [
        (9, 0, 24, "ZOMBIE_ICE_WORKER"), (10, 4, 24, "ZOMBIE_ICE_WORKER")]
    conserved(before, paid)

    queued = ice("queue_before_reload")
    restored = ice("queue_after_reload")
    assert record(queued)["unresolved"] is True and record(queued)["production"] == 0
    assert queued["boundedTrials"] == restored["boundedTrials"]
    assert queued["pending"] == restored["pending"]
    for key in ("enemyIce", "spent", "workerIncome", "killIncome", "supplied", "decisions"):
        assert queued[key] == restored[key], ("queue reload", key)
    assert queued["troopActivity"] and restored["troopActivity"]

    for first, second in (("queue_paused_before", "queue_paused_after"), ("paused_before", "paused_after")):
        one, two = state(first), state(second)
        assert one["paused"] and two["paused"]
        for key in ("enemyIce", "spent", "workerIncome", "killIncome", "supplied", "elapsed",
                    "emptyWaitSeconds", "supplyRemaining", "decisionRemaining", "strikeAimRemaining",
                    "strikeCooldownRemaining", "pending", "boundedTrials"):
            assert one["coldStorage"][key] == two["coldStorage"][key], ("real pause", key)

    old = ice("old_wave_only")
    both_dump = state("both_waves_produced")
    both = both_dump["coldStorage"]
    assert old["workerIncome"] > 0 and record(old)["production"] == 0
    assert len(old["pending"]) == 1 and old["pending"][0]["wave"] == 10
    assert old["productionEvents"] and {event["wave"] for event in old["productionEvents"]} == {9}
    assert old["emptyWaitSeconds"] == both["emptyWaitSeconds"] == 0
    assert record(old)["unresolved"] and record(both)["unresolved"]
    events = both["productionEvents"]
    assert {event["wave"] for event in events} == {9, 10}
    by_wave = {wave: sum(event["amount"] for event in events if event["wave"] == wave) for wave in (9, 10)}
    assert record(both)["production"] == by_wave[10]
    assert both["workerIncome"] == by_wave[9] + by_wave[10]
    assert 0 < record(both)["production"] < record(both)["cost"]
    assert both["spent"] == 48 and not both["pending"]
    assert all(z["iceBatches"] > 0 for z in workers(both_dump).values())
    conserved(restored, both)

    full_before_dump, full_after_dump = state("full_wallet_before"), state("full_wallet_after")
    full_before, full_after = full_before_dump["coldStorage"], full_after_dump["coldStorage"]
    assert full_before["enemyIce"] == full_after["enemyIce"] == 1_000_000
    for key in ("workerIncome", "boundedTrials", "spent", "productionEvents"):
        assert full_before[key] == full_after[key], ("full wallet", key)
    first_workers, last_workers = workers(full_before_dump), workers(full_after_dump)
    for wave in (9, 10):
        assert first_workers[wave]["id"] == last_workers[wave]["id"]
        assert last_workers[wave]["iceBatches"] > first_workers[wave]["iceBatches"], wave

    surviving = ice("old_wave_still_alive")
    assert surviving["hostileCount"] == 1 and surviving["troopActivity"]
    assert record(surviving)["unresolved"] is True
    failed = ice("failed_settled")
    assert not failed["troopActivity"] and not failed["pending"]
    assert record(failed)["unresolved"] is False
    assert 0 < record(failed)["production"] < record(failed)["cost"]
    assert record(failed)["production"] == record(both)["production"]
    failed_history = failed["boundedTrials"]

    pre_skill = ice("precision_before_payment")
    before_reload, after_reload = ice("before_reload"), ice("after_reload")
    ended, cycle = ice("skill_finished"), ice("precision_cycle_finished")
    assert before_reload["spent"] - pre_skill["spent"] == 60
    assert pre_skill["enemyIce"] - before_reload["enemyIce"] == 60
    assert before_reload["enemyIce"] == 910 and before_reload["spent"] == 108
    for key in ("enemyIce", "spent", "workerIncome", "supplied", "killIncome", "strikeTargetID", "boundedTrials"):
        assert before_reload[key] == after_reload[key], ("aim reload", key)
    assert before_reload["strikeAimRemaining"] >= 5 and after_reload["strikeAimRemaining"] > 0
    assert ended["strikeTargetID"] == -1 and 40 <= ended["strikeCooldownRemaining"] <= 45
    expected_reward = after_reload["plantCosts"]["PLANT_SUNFLOWER"] * (after_reload["difficulty"] + 2) // 4
    assert ended["killIncome"] - after_reload["killIncome"] == expected_reward
    assert ended["troopPlantKills"] == 0
    assert cycle["elapsed"] - pre_skill["elapsed"] >= 53
    assert cycle["strikeTargetID"] == -1 and cycle["strikeCooldownRemaining"] == 0
    assert ended["spent"] == cycle["spent"] == after_reload["spent"]
    conserved(after_reload, ended)
    conserved(ended, cycle)
    for name in ("precision_before_payment", "before_reload", "after_reload", "skill_finished",
                 "precision_cycle_finished", "failed_after_reload", "paused_before", "paused_after", "final"):
        assert ice(name)["boundedTrials"] == failed_history, ("failed history retained", name)
    reloaded_failure = ice("failed_after_reload")
    assert reloaded_failure["spent"] == cycle["spent"]
    conserved(cycle, reloaded_failure)

    for name in ("during_aim.json", "during_cooldown.json"):
        diagnosis = load(name)
        assert diagnosis["boardUnchanged"]
        assert diagnosis["groundFallback"]["probeBudget"] == 0
        assert diagnosis["groundFallback"]["allIn"] is False
        assert diagnosis["groundFallback"]["troopActivity"] is False
    for name in ("paid_queue", "aiming_trial", "failed_trial"):
        snapshot = load(f"snapshots/{name}.json")
        assert snapshot["schemaVersion"] == 31
        assert snapshot["coldStorage"]["boundedTrials"]["records"][0]["cost"] == 24
    final = ice("final")
    assert final["troopPlantKills"] == final["stallStartTroopPlantKills"] == 2
    assert final["enemyIce"] == 1_000_000 and final["emptyWaitSeconds"] == 17

    report = {
        "passed": True,
        "scope": "controlled Board transaction/lifecycle fixture, NOT strength evidence",
        "trialRecordOrigin": "synthetic only after two real legal queue payments; wave 10 owns 24 ice",
        "autonomousTrialCommitCovered": False,
        "purchaseWaveCosts": {"9": 24, "10": 24},
        "actualAcceptedProductionByWave": by_wave,
        "exactWaveAttribution": True,
        "fullWalletBatchesWithoutFalseCredit": True,
        "unresolvedHistoryAndPaidQueueResumeNoDoubleCharge": True,
        "oldLiveWaveDelaysTrialSettlement": True,
        "realPauseFreezesCashAndClocks": True,
        "failedHistorySurvivesPrecision53SecondsAndReload": True,
        "precisionKillCannotRepayTrial": True,
        "fullWalletTroopKillsDoNotEraseFailure": True,
        "legacyAllInAndProbeBudgetDisabled": True,
        "schemaVersion": 31,
    }
    (folder / "ground_stagnation_verification.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
