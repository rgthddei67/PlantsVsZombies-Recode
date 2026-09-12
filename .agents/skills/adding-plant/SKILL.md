---
name: adding-plant
description: Use when adding or modifying PvZ plants, including plant combat, placement, upgrades, animation and projectile integration.
---

# 新增或修改 PvZ 植物

提问和任务范围遵循 [AGENTS.md](../../../AGENTS.md#任务路由)：只问缺少依据的关键决策，普通细节自主处理。按下面路由只读本次需要的参考段落；纯调参直接查源码/权威配置、单位与必要断言。

**新工作需要添加动画帧事件时，必须先询问主人。** 帧号仍由主人确认；`AddFrameEvent` 真实帧号 = 动画预览帧号 − 1，主人报的帧号默认已减 1，代码直接使用，不许再减。只有自己读取预览工具时才换算；已确认帧号仍须实测触发，不能用超时回收代替死亡事件验收。

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

构建、可见运行与渲染矩阵统一按 [项目指南](../../../docs/agent-guide/PROJECT_GUIDE.md) 和 [AutoTest](../../../docs/agent-guide/AUTOTEST.md)；默认 `clang-release`，只跑相关用例。改动画/死亡才增加事件与消失验证，改持久状态才验存读档，改外观才检查同步截图。新增完整角色覆盖其实际具有的能力、资源、生命周期和表现；未改渲染后端不加跑兼容路径。

外观验收检查本次受影响的本体、分件与反馈，确保实机尺度可读、锚点正确且无未完成占位符。原有简洁图形若已经是完整设计，不因图元种类而强制重画。汇报变更、相关验证和关键调参入口；小改动不列全量参数或逐项审计表。文档维护、提交与 push 统一遵循 AGENTS.md。
