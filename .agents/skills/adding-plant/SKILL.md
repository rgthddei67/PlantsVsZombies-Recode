---
name: adding-plant
description: Use when adding or modifying PvZ plants or skill cards sharing plant card slots, including plant combat, placement, upgrades, animation and projectile integration.
---

# 新增或修改 PvZ 植物

提问和任务范围遵循 [AGENTS.md](../../../AGENTS.md#任务路由)：只问缺少依据的关键决策，普通细节自主处理。按下面路由只读本次需要的参考段落；纯调参直接查源码/权威配置、单位与必要断言。

新增动画帧事件前仍须先询问主人；帧号与触发验收统一见[动画帧事件约定](../../../docs/agent-guide/ARCHITECTURE_AND_RESOURCES.md#动画帧事件约定)。

## 核心契约

- 新经典植物先查 C# 行为与实际 reanim，再接当前继承体系；复用最窄 `Plant` / `Shooter` / `Shroom` 扩展点，不复制一套状态或组件系统。玩法分类按原版/已批准设计确认，不从基类名称推定睡眠资格。
- 逻辑格位与 `mVisualOffset` 分离；800×600 原版坐标换算到当前 1100×600 场景、Board 网格或稳定视觉锚点。
- 保持枚举 ID 稳定；资源只改 `build/clang-release/resources/`。文件/manifest、loader 注册、真实资源键、运行时加载断言要闭合；被忽略的新资源审核后定向 `git add -f`。
- 植物声明自身能力，目标拥有受击/免疫/中断语义，Board 拥有格子、天气与共享事务；跨格操作按稳定实体 ID 去重。
- 自定义状态走 `SaveExtraData/LoadExtraData`；Animator 由现有恢复入口处理，恢复终态不得重播已提交效果。

## 按任务读取

- 新增注册与原版适配：[references/creation.md](references/creation.md)。
- 坐标、分件与动画：[references/visual-animation.md](references/visual-animation.md)。
- 占格、升级与生命周期：[references/plant-lifecycle.md](references/plant-lifecycle.md)。
- 战斗与天气接口：[references/combat-weather.md](references/combat-weather.md)。
- 视觉验证与卡牌输入：[references/verification-ui.md](references/verification-ui.md)。
- 跨系统效果入口：[references/effect-routing.md](references/effect-routing.md)。
- 僵尸状态与持续伤害：[references/zombie-status.md](references/zombie-status.md)。
- 弹丸与对象池：[references/projectiles.md](references/projectiles.md)。
- 搬运与矿场交互：[references/mine-relocation.md](references/mine-relocation.md)。

涉及粒子、天气、僵尸、词条或复用经典分件时，再使用对应技能的相关部分。

## 验证与交付

按[角色改动与外观验收](../../../docs/agent-guide/AUTOTEST.md#角色改动与外观验收)选择相关证据；专项夹具见上面的参考入口。文档维护与提交遵循 [AGENTS.md](../../../AGENTS.md)。
