"""Verify explicitly scripted episode purchases without treating them as autonomous wins."""
import argparse
import json
import math
from pathlib import Path


def member_receipts(purchase):
    """Match ordered member evidence, retaining compatibility with legacy flat R3 receipts."""
    request = purchase["requested"]
    mixed = "members" in request
    if mixed:
        assert not any(key in request for key in ("type", "row", "count", "delay")), "ambiguous purchase format"
        requested = request["members"]
        assert isinstance(requested, list) and requested
    else:
        requested = [request]
    if "members" in purchase:
        receipts = purchase["members"]
        assert len(receipts) == len(requested)
        assert [member["requested"] for member in receipts] == requested, "member identity/order changed"
    else:
        assert not mixed, "mixed purchases need per-member evidence"
        receipts = [{"requested": request, **{key: purchase[key] for key in
                    ("typeID", "unlockWave", "unitCost", "requestedCost")}}]
    return receipts


def verify_response_contrast(purchase):
    """Require four complete full-evaluator worlds with one horizon, weights and narrow deletion."""
    enabled = purchase["requested"].get("responseContrast", False)
    assert type(enabled) is bool
    if not enabled:
        assert "responseContrast" not in purchase, "disabled request unexpectedly ran contrast worlds"
        return
    contrast = purchase["responseContrast"]
    assert contrast["hypothetical"] and not contrast["probabilityModel"] and contrast["unlimitedDiagnostic"]
    assert contrast["basis"] == "shared_full_plan_cart_wait"
    assert contrast["evaluator"] == "informational_trial_four_arm"
    assert contrast["responsePolicy"] == "unchanged_full_plan", "other paid/manual response choices must stay available"
    assert contrast["complete"] and contrast["matchesRequested"]
    assert contrast["bill"] == purchase["requestedCost"]
    assert contrast["hypothesis"] == "future_direct_paid_area_counters_withheld"
    keys = contrast["removedSourceKeys"]
    assert keys == sorted(set(keys)) and all(type(key) is int and 1 <= key <= 64 for key in keys)
    assert type(contrast["removedCounterCount"]) is int and contrast["removedCounterCount"] >= len(keys)
    assert (contrast["removedCounterCount"] == 0) == (len(keys) == 0)
    assert contrast["evaluationMs"] >= 0 and math.isfinite(contrast["evaluationMs"])
    assert contrast["requestedSearchVersion"] in (1, 2) and contrast["searchVersion"] in (1, 2)
    assert type(contrast["expandedForecast"]) is bool
    assert not contrast["expandedForecast"] or contrast["searchVersion"] == 2
    assert contrast["traceHorizonSeconds"] in (60, 90, 120)
    assert contrast["searchVersion"] != 2 or contrast["traceHorizonSeconds"] == 120
    assert contrast["productionWindowSeconds"] == 60
    delay = max(member["requested"].get("delay", 0) for member in member_receipts(purchase))
    assert math.isclose(contrast["responseEnvelopeSeconds"], delay, rel_tol=1e-6, abs_tol=1e-4)
    assert math.isfinite(contrast["storedResponseEnvelopeSeconds"])
    minimum_hold = max(32, delay)
    assert (contrast["storedResponseEnvelopeSeconds"] >= minimum_hold
            or math.isclose(contrast["storedResponseEnvelopeSeconds"], minimum_hold, rel_tol=1e-6, abs_tol=1e-4))
    assert contrast["patientResponseHoldSeconds"] == 8
    before, after = (contrast["counterInventory"][name] for name in ("robust", "potential"))
    for inventory in (before, after):
        assert all(type(inventory[key]) is int and inventory[key] >= 0
                   for key in ("total", "freshPaidArea", "free", "committed", "stored", "reusable", "other"))
        assert inventory["total"] == inventory["freshPaidArea"] + inventory["other"]
    assert before["freshPaidArea"] == contrast["removedCounterCount"] and after["freshPaidArea"] == 0
    assert before["total"] - after["total"] == contrast["removedCounterCount"]
    for key in ("free", "committed", "stored", "reusable", "other"):
        assert before[key] == after[key], "narrow deletion removed another counter capability"
    same = lambda a, b: math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-4)
    shared_weights = contrast["robust"]["purchase"]["effectiveWeights"]
    assert len(shared_weights) == 8 and all(math.isfinite(f) for f in shared_weights)
    for name in ("robust", "potential"):
        pair = contrast[name]
        assert pair["complete"]
        for action in ("purchase", "wait"):
            world = pair[action]
            assert world["complete"]
            assert type(world["paidDefensesRetained"]) is bool  # A selected full response may legitimately retain paid tools.
            assert len(world["features"]) == len(world["baseline"]) == 8
            assert all(math.isfinite(f) for f in world["features"] + world["baseline"])
            assert world["effectiveWeights"] == shared_weights
            for key in ("requestedSearchVersion", "searchVersion", "expandedForecast", "traceHorizonSeconds", "productionWindowSeconds"):
                assert world[key] == contrast[key], "arm used a different prediction horizon"
            assert math.isfinite(world["score"])
            for key in ("rawProduction", "productionAfterWindow", "totalRawProduction", "counterHoldSeconds",
                        "paidCounterCasts", "cobShots", "constructedCobShots", "cobUpgrades", "planted", "exchanges", "orders",
                        "sunSpent", "iceSpent", "plantAssets", "plantAttackAssets"):
                assert math.isfinite(world[key]) and world[key] >= 0
            assert same(world["totalRawProduction"], world["rawProduction"] + world["productionAfterWindow"])
            assert isinstance(world["workerTrace"], list) and isinstance(world["counterTrace"], list)
            if contrast["traceEconomy"]:
                assert same(sum(point["income"] for point in world["workerTrace"]), world["totalRawProduction"])
                assert all(0 <= point["time"] <= contrast["traceHorizonSeconds"] for point in world["workerTrace"])
                window = contrast["productionWindowSeconds"]
                assert same(sum(point["income"] for point in world["workerTrace"] if point["time"] < window),
                            world["rawProduction"]), "worker trace disagrees with calibration-window income"
                assert same(sum(point["income"] for point in world["workerTrace"] if point["time"] >= window),
                            world["productionAfterWindow"]), "worker trace disagrees with after-window income"
        cart, wait, delta = pair["purchase"], pair["wait"], pair["delta"]
        assert cart["baseline"] == wait["features"], "cart must subtract its own same-army wait"
        for key in ("rawProduction", "productionAfterWindow", "totalRawProduction"):
            assert same(delta[key], cart[key] - wait[key])
        expected_cash = (cart["features"][0] - wait["features"][0] + cart["totalRawProduction"] - wait["totalRawProduction"]
                         - max(0, cart["features"][5] - wait["features"][5] - contrast["bill"]))
        assert same(delta["cash"], expected_cash), "old army or calibration gain was credited to the new cart"
        assert same(delta["plantAttackAssets"], cart["plantAttackAssets"] - wait["plantAttackAssets"])
        assert delta["breach"] == (cart["features"][2] > wait["features"][2])


def verify_episode(episode, require_accepted=False, require_mixed=False, require_all_rejected=False):
    """Check real full-batch accounting and queue identity; predictions cannot prove payment."""
    assert not (require_all_rejected and (require_accepted or require_mixed)), "conflicting verification modes"
    purchases = episode["diagnosticPurchases"]
    assert purchases and len(purchases) <= 64
    assert episode["controller"] == "autonomous_with_scripted_purchase"
    accepted_serials = set()
    accepted = mixed_accepted = 0
    statuses = {}
    for purchase in purchases:
        status = purchase["status"]
        statuses[status] = statuses.get(status, 0) + 1
        assert status != "scheduled", "unfinished requests must be marked with the episode stopping reason"
        if not purchase["attempted"]:
            assert not purchase["accepted"] and status in {"episode_ended", "episode_timeout"}
            continue
        assert purchase.get("controller", "scripted_purchase") == "scripted_purchase"
        request = purchase["requested"]
        if status != "invalid_request":
            assert purchase["seconds"] >= request["at"], "never execute before the requested episode game time"
        assert purchase["actualCost"] == purchase["spentAfter"] - purchase["spentBefore"]
        assert purchase["actualCost"] == purchase["walletBefore"] - purchase["walletAfter"]
        if not purchase["accepted"]:
            assert status != "accepted"
            assert purchase["actualCost"] == purchase["acceptedCount"] == 0, "rejected batch paid or queued a prefix"
            assert purchase["decisionsAfter"] == purchase["decisionsBefore"]
            assert purchase["serialAfter"] == purchase["serialBefore"]
            if "pendingBefore" in purchase:
                assert purchase["pendingBefore"] == purchase["pendingAfter"], "rejected batch changed existing tickets"
                assert purchase.get("oldQueuePreserved", True)
            if status == "locked":
                assert any(member["unlockWave"] > purchase["decisionsBefore"] + 1
                           for member in member_receipts(purchase))
            if status == "insufficient_ice":
                assert purchase["requestedCost"] > purchase["walletBefore"]
            if status == "insufficient_capacity" and "availableCapacity" in purchase:
                count = sum(member["requested"].get("count", 1) for member in member_receipts(purchase))
                assert count > purchase["availableCapacity"]
            continue
        accepted += 1
        mixed_accepted += "members" in request
        assert status == "accepted"
        members = member_receipts(purchase)
        count = 0
        bill = 0
        expected_queue = []
        for member in members:
            requested = member["requested"]
            assert isinstance(requested["type"], str)
            assert type(requested["row"]) is int and requested["row"] >= 0
            if "rows" in episode["initial"]:
                assert requested["row"] < episode["initial"]["rows"]
            n = requested.get("count", 1)
            delay = requested.get("delay", 0)
            assert type(n) is int and 1 <= n <= 192
            assert type(delay) in (int, float) and math.isfinite(delay) and 0 <= delay <= 60
            assert type(member["typeID"]) is int
            assert type(member["unitCost"]) is int and member["unitCost"] > 0
            assert member["unlockWave"] <= purchase["decisionsBefore"] + 1, "same batch cannot unlock its later members"
            assert member["requestedCost"] == member["unitCost"] * n
            count += n
            bill += member["requestedCost"]
            expected_queue.extend((member["typeID"], requested["row"], member["unitCost"], delay) for _ in range(n))
        assert 1 <= count <= 192
        assert purchase.get("requestedCount", count) == purchase["acceptedCount"] == count
        assert purchase["actualCost"] == purchase["requestedCost"] == bill
        assert bill <= purchase["walletBefore"]
        if "availableCapacity" in purchase:
            assert count <= purchase["availableCapacity"]
        if "members" not in request:
            for key in ("typeID", "unlockWave", "unitCost"):
                assert purchase[key] == members[0][key], "legacy summary disagrees with member evidence"
        assert purchase["decisionsAfter"] == purchase["decisionsBefore"] + 1
        assert purchase["serialAfter"] == purchase["serialBefore"] + 1
        assert purchase["serialAfter"] not in accepted_serials
        accepted_serials.add(purchase["serialAfter"])
        assert purchase["oldQueuePreserved"]
        assert purchase.get("newQueueMatchesRequested", True)
        before, after = purchase["pendingBefore"], purchase["pendingAfter"]
        assert after[:len(before)] == before, "existing paid ticket, route, delay, cost and wave must all survive"
        added = after[len(before):]
        assert len(added) == count
        assert len({paid["ticket"] for paid in after}) == len(after)
        for paid, (type_id, row, unit_cost, delay) in zip(added, expected_queue):
            assert paid["type"] == type_id and paid["row"] == row, "paid members reordered, substituted or rerouted"
            assert paid["cost"] == unit_cost and paid["wave"] == purchase["decisionsAfter"]
            assert abs(paid["remaining"] - delay) < 0.0001, "member delay must stay relative to the one payment"
        prediction = purchase["prediction"]
        assert prediction["matchesRequested"] and prediction["unlimitedDiagnostic"]
        assert prediction["evaluationMs"] >= 0 and math.isfinite(prediction["score"])
        assert len(prediction["features"]) == len(prediction["baseline"]) == 8
        verify_response_contrast(purchase)

    if require_accepted:
        assert accepted > 0, "no diagnostic purchase occurred; a busy/locked rejection does not verify successful payment"
    if require_mixed:
        assert mixed_accepted > 0, "no mixed batch completed; request presence is not payment evidence"
    if require_all_rejected:
        assert accepted == 0 and any(p["attempted"] for p in purchases), "expected attempted, wholly rejected requests"
    for decision in episode["decisions"]:
        expected = "scripted_purchase" if decision["serial"] in accepted_serials else "autonomous"
        assert decision["controller"] == expected
    for state in [episode["initial"]["coldStorage"], episode["final"]["coldStorage"],
                  *(point["ice"] for point in episode["trace"])]:
        assert state["enemyIce"] == (state["initialEnemyIce"] + state["supplied"]
                                     + state["workerIncome"] + state["killIncome"] - state["spent"])
    return {"passed": True, "acceptedPurchases": accepted, "acceptedMixedPurchases": mixed_accepted,
            "purchaseLifecycleObserved": accepted > 0, "statuses": statuses,
            "controller": episode["controller"], "autonomousStrengthVerified": False}


def verify(folder: Path, episode_name="episode", require_accepted=False, require_mixed=False, require_all_rejected=False):
    """Load an AutoTest result and publish only the transaction evidence it actually contains."""
    read = lambda name: json.loads((folder / (name + ".json")).read_text(encoding="utf-8-sig"))
    assert read("status")["status"] == "passed"
    report = verify_episode(read(episode_name), require_accepted, require_mixed, require_all_rejected)
    (folder / "diagnostic_purchase_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--episode", default="episode")
    parser.add_argument("--require-accepted", action="store_true")
    parser.add_argument("--require-mixed", action="store_true")
    parser.add_argument("--require-all-rejected", action="store_true")
    args = parser.parse_args()
    if args.require_all_rejected and (args.require_accepted or args.require_mixed):
        parser.error("--require-all-rejected cannot be combined with successful-purchase requirements")
    verify(args.folder, args.episode, args.require_accepted, args.require_mixed, args.require_all_rejected)
