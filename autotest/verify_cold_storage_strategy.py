"""Check the visible strategy fixture's actual paid formations and resource ledger."""
import json
import sys
from collections import Counter
from pathlib import Path


def verify(output):
    formations = []
    for name in ("bomb_ready", "bomb_unaffordable"):
        state = json.loads((output / f"{name}.json").read_text(encoding="utf-8"))
        pending = state["coldStorage"]["pending"]
        assert 1 <= len(pending) <= 11, (name, pending)
        assert sum(z["cost"] for z in pending) + state["coldStorage"]["enemyIce"] == 600
        assert sum(z['cost'] for z in pending) <= state['coldStorage']['commanderBudget']
        fronts, doctors = set(), set()
        for zombie in pending:
            # Stable ZombieType IDs of existing front-line units and healer in this fixture.
            if zombie["type"] in (3, 8, 11, 34, 36, 48):
                fronts.add(zombie["row"])
            if zombie["type"] == 40:
                assert zombie["row"] in fronts, "Healer committed without an earlier front line"
                assert zombie["row"] not in doctors, "Repeated healer in one lane"
                doctors.add(zombie["row"])
        formations.append(Counter(z["row"] for z in pending))
    assert max(formations[0].values()) == 1
    ready = json.loads((output / 'bomb_ready.json').read_text(encoding='utf-8'))['coldStorage']
    unavailable = json.loads((output / 'bomb_unaffordable.json').read_text(encoding='utf-8'))['coldStorage']
    assert ready['commanderMode'] == 'probe' and unavailable['commanderMode'] == 'pressure'
    assert ready['commanderBudget'] < unavailable['commanderBudget']
    print("Strategy verified: affordable bomb changes budget and spread; paid formations remain legal.")


if __name__ == "__main__":
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                "build/clang-release/autotest/out/smoke_cold_storage_strategy"))
