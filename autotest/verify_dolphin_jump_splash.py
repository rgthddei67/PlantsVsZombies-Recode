"""Check landing feedback against submitted body geometry, not the delayed logical root."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / "build/clang-release/autotest/out/smoke_dolphin_jump_splash"


def read(name):
    return json.loads((ROOT / (name + ".json")).read_text(encoding="utf-8"))


def verify():
    assert read("status")["status"] == "passed"
    for name in ("normal_1", "elite_1", "elite_2"):
        state = read(name)
        zombie = state["zombies"][0]
        body = state["animatedObjectsByTag"]["Zombie"][0]
        splash = state["animatedObjectsByTag"]["PoolSplash"][0]
        drops = state["particleEffectsByName"]["PlantingPool"][0]
        assert zombie["dolphinPhase"] == "JUMPING"
        assert 490 <= zombie["dolphinJumpProgressOn1000"] <= 600
        assert all(probe["renderProbeReady"] for probe in (body, splash, drops))
        body_center = body["worldBounds"]["centerXInt"]
        for effect in (splash, drops):
            bounds = effect["worldBounds"]
            assert abs(bounds["centerXInt"] - body_center) <= 30, (name, bounds, body["worldBounds"])
            assert bounds["leftInt"] < body["worldBounds"]["rightInt"]
            assert bounds["rightInt"] > body["worldBounds"]["leftInt"]
        # 水滴落在当前行水面；不得继承骑手还在空中的高度，也不能等逻辑根提交才定位。
        assert abs(drops["originYInt"] - zombie["yInt"]) <= 1
        assert drops["originXInt"] < zombie["xInt"] - 60
        landed = read(name + "_landed")
        assert landed["particleEffectNameCounts"]["PlantingPool"] == 0
        assert landed["animatedObjectTagCounts"]["PoolSplash"] == 0
    print("Dolphin splash passed: both visual layers align with the rendered body for all three jumps; no replay after loading.")


if __name__ == "__main__":
    verify()
