"""Bounded cold-storage play experiment using only legal player operations.

This small scripted player is a balancing probe, not proof of human difficulty.
Run after interactive_play_10_1.json; no free sun, planting, cooldowns or kills.
"""
import argparse
import json
from pathlib import Path
from live import LiveClient


def play(output, duration):
    client = LiveClient(output, timeout=120)
    state = client.send([{"op": "observe"}])["state"]
    samples = []
    actions = []
    next_sample = 0
    while state.get("boardState") == "GAME":
        ice = state["coldStorage"]
        if ice["trophySpawned"] or ice["elapsed"] >= duration:
            break
        commands = [{"op": "collect_sun", "id": s["id"]}
                    for s in state.get("suns", []) if not s.get("collected")][:40]
        cards = {c["gameplayType"]: c for c in state["cards"]}
        plants = {(p["row"], p["col"]): p["type"] for p in state["plants"]
                  if not p.get("squished") and p["type"] != "PLANT_PUMPKINSHELL"}
        zombies = [z for z in state["zombies"] if z.get("bodyHealth", 0) > 0]
        sun, stock = state["sun"], ice["playerIce"]
        chosen = None

        def attempt(name, cells):
            nonlocal chosen
            card = cards.get("PLANT_" + name)
            if chosen or not card or not card["ready"] or sun < card["sunCost"]:
                return
            if name in ("TORCHWOOD", "TWINSUNFLOWER", "GATLINGPEA", "MELONPULT", "WINTERMELON", "PUMPKINSHELL") and sun - card["sunCost"] < 150:
                return
            if stock < ice["plantCosts"][card["gameplayType"]]:
                return
            legal = {tuple(cell) for cell in card["legalCells"]}
            for cell in cells:
                if cell in legal:
                    chosen = {"op": "player_plant", "slot": card["slot"],
                              "row": cell[0], "col": cell[1]}
                    return

        urgent = sorted(zombies, key=lambda z: z["xInt"])
        if urgent and urgent[0]["xInt"] < 650:
            row = urgent[0]["row"]
            attempt("JALAPENO", [(row, col) for col in range(8, -1, -1)])
            col = max(0, min(8, (urgent[0]["xInt"] - 242) // 80))
            attempt("CHERRYBOMB", [(row, col), (row, max(0, col - 1))])

        # 先建立每路前排；坚果长冷却需提前使用，而不是等僵尸贴脸。
        defense_rows = list(dict.fromkeys([z["row"] for z in urgent] + [2, 0, 4, 1, 3]))
        attempt("WALLNUT", [(r, 6) for r in defense_rows if (r, 6) not in plants])
        producers = sum(t in ("PLANT_SUNFLOWER", "PLANT_TWINSUNFLOWER") for t in plants.values())
        if producers < 6:
            attempt("SUNFLOWER", [(r, c) for c in (0, 1) for r in (2, 0, 4, 1, 3)])
        fire_rows = {r for (r, _), t in plants.items()
                     if t in ("PLANT_REPEATER", "PLANT_GATLINGPEA", "PLANT_MELONPULT", "PLANT_WINTERMELON")}
        weak = [r for r in range(5) if r not in fire_rows]
        threatened = sorted(weak, key=lambda r: min([z["xInt"] for z in zombies if z["row"] == r] or [2000]))
        if zombies:
            attempt("POTATOMINE", [(r, 4) for r in threatened if any(z["row"] == r and z["xInt"] > 800 for z in zombies)])
        attempt("REPEATER", [(r, 2) for r in threatened])
        if len(fire_rows) >= 3 and producers < 8:
            attempt("SUNFLOWER", [(r, c) for c in (0, 1) for r in (2, 0, 4, 1, 3)])
        attempt("TORCHWOOD", [(r, 3) for r in range(5) if plants.get((r, 2)) in ("PLANT_REPEATER", "PLANT_GATLINGPEA")])
        if len(fire_rows) == 5:
            attempt("PUMPKINSHELL", [(r, 6) for r in defense_rows if plants.get((r, 6)) == "PLANT_WALLNUT"])
            attempt("TWINSUNFLOWER", [(r, 0) for r in range(5)])
            attempt("GATLINGPEA", [(r, 2) for r in (2, 0, 4, 1, 3)])
            attempt("MELONPULT", [(r, 5) for r in (1, 3, 2, 0, 4)])
            attempt("WINTERMELON", [(r, 5) for r in (1, 3, 2, 0, 4)])
        if chosen:
            commands.append(chosen)
            sun -= state["cards"][chosen["slot"]]["sunCost"]
        if stock < 35 and ice["orderIce"] == 0 and sun >= (100 if stock < 10 else 225):
            commands.append({"op": "buy_ice", "large": sun >= 500})
        commands.append({"op": "advance", "steps": 30})  # 4x 下两秒游戏时间，正常冷却与产光照常运行。
        reply = client.send(commands)
        actions.extend(reply["results"])
        state = reply["state"]
        if state["coldStorage"]["elapsed"] >= next_sample:
            sample = {k: state["coldStorage"][k] for k in
                      ("elapsed", "playerIce", "enemyIce", "spent", "supplied", "killIncome", "deployments")}
            sample.update(sun=state["sun"], plants=state["plantCount"], zombies=state["zombieCount"])
            samples.append(sample)
            print(json.dumps(sample), flush=True)
            Path(output, "legal_play_checkpoint.json").write_text(json.dumps({"samples": samples, "state": state}, ensure_ascii=False), encoding="utf-8")
            next_sample += 30
    result = {"boardState": state.get("boardState"), "economy": state.get("coldStorage"),
              "samples": samples, "actions": actions, "state": state}
    Path(output, "legal_play_experiment.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    client.send([{"op": "screenshot", "name": "legal_play_final.png"}, {"op": "quit"}])
    print("Experiment finished:", result["boardState"], "win=", result["economy"]["trophySpawned"], flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=float, default=720)
    arguments = parser.parse_args()
    play(arguments.output, arguments.seconds)
