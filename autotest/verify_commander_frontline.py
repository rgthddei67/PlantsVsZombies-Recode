"""Inspect actual episode units; require paid-in production behind a damaged frontline when requested."""
import argparse
import json
from pathlib import Path


def inspect(folder):
    """Check resource accounting and report stable-ID observations, without requiring a zombie roster."""
    episodes = []
    examples = []
    engineer_uses = {}
    protection_events = []
    interference_uses = 0
    previous_cooldown = 0
    for name in ("opening", "middle", "late"):
        path = folder / (name + ".json")
        data = json.loads(path.read_text(encoding="utf-8"))
        assert not data["externalSun"]["enabled"], "normal battle must not inject player resources"
        final = data["final"]["coldStorage"]
        balance = final["initialEnemyIce"] + final["supplied"] + final["workerIncome"] + final["killIncome"] - final["spent"]
        assert balance == final["enemyIce"], (name, "commander resource ledger", balance, final["enemyIce"])
        episodes.append({"phase": name, "wave": data["final"]["wave"], "income": final["workerIncome"],
                         "ice": final["enemyIce"], "deployments": final["deploymentTypes"], "outcome": data["outcome"]})
        trace = data["trace"]
        protection_events.extend({"phase": name, **event} for event in data.get("engineerProtectionEvents", []))
        for sample in trace:
            assert "units" in sample, "episode must enable traceUnits"
            cooldown = sample["ice"]["interferenceCooldownRemainingMs"]
            if cooldown > previous_cooldown + 1000:
                interference_uses += 1
            previous_cooldown = cooldown
            for unit in sample["units"]:
                if "engineerProtectionUses" in unit:
                    engineer_uses[unit["id"]] = max(engineer_uses.get(unit["id"], 0), unit["engineerProtectionUses"])
        for before, after in zip(trace, trace[1:]):
            old = {unit["id"]: unit for unit in before["units"]}
            new = {unit["id"]: unit for unit in after["units"]}
            for identity, guard in old.items():
                later = new.get(identity)
                # 观察地面前排的承伤与后排生产；支援自身与飞行绕路不能作为挡线证据。
                if not later or guard["type"] in {"ZOMBIE_ICE_WORKER", "ZOMBIE_DISASTER_ENGINEER",
                        "ZOMBIE_POLAR_CLOCKMAKER", "ZOMBIE_BALLOON", "ZOMBIE_BUNGEE"} or later["bodyHealth"] <= 0:
                    continue
                damage = guard["countableExecutionHealth"] - later["countableExecutionHealth"]
                if damage <= 0:
                    continue
                workers = []
                for worker in old.values():
                    end = new.get(worker["id"])
                    if worker["type"] != "ZOMBIE_ICE_WORKER" or not end or worker["bodyHealth"] <= 0:
                        continue
                    if worker["row"] != guard["row"] or worker["bodyHealth"] != end["bodyHealth"]:
                        continue
                    if worker["xInt"] < guard["xInt"] + 20 or end["xInt"] < later["xInt"] + 20:
                        continue
                    if end.get("iceBatches", 0) > worker.get("iceBatches", 0):
                        workers.append(worker["id"])
                if workers:
                    examples.append({"phase": name, "elapsed": before["ice"]["elapsed"], "row": guard["row"] + 1,
                                     "frontId": identity, "frontType": guard["type"], "damage": damage,
                                     "producingWorkers": workers})
    return {"episodes": episodes, "frontlineIntervals": len(examples), "examples": examples,
            "engineerProtectionUsesObserved": sum(engineer_uses.values()), "timeInterferencesObserved": interference_uses,
            "engineerProtectionEvents": len(protection_events),
            "workersProtectedFromAsh": sum(len(event["workerIDs"]) for event in protection_events),
            "protectionExamples": protection_events[:5],
            "note": "These intervals show a damaged forward unit and unchanged-health workers completing production behind it; they are not a win-rate claim."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--require-frontline", action="store_true")
    args = parser.parse_args()
    result = inspect(args.folder)
    (args.folder / "frontline_evidence.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({**result, "examples": result["examples"][:5]}, ensure_ascii=False, indent=2))
    if args.require_frontline:
        assert result["frontlineIntervals"] > 0, "no actual guarded-production interval observed"


if __name__ == "__main__":
    main()
