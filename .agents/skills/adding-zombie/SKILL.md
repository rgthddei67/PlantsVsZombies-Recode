---
name: adding-zombie
description: Use when adding or modifying PvZ zombies, armor, abilities, animation, or adventure and survival spawn pools.
---

# 新增或修改 PvZ 僵尸

提问和任务范围遵循 [AGENTS.md](../../../AGENTS.md#任务路由)：只问缺少依据的关键决策，普通细节自主处理。按下面路由只读本次需要的参考段落；纯调参直接查源码/权威配置、单位与必要断言。

**新工作需要添加动画帧事件时，必须先询问主人。** 帧号仍由主人确认；`AddFrameEvent` 真实帧号 = 动画预览帧号 − 1，主人报的帧号默认已减 1，代码直接使用，不许再减。只有自己读取预览工具时才换算；已确认帧号仍须实测触发，不能用超时回收代替死亡事件验收。

## 核心契约

- 新经典僵尸查 C# 的行为、断肢、音效与当前 reanim；接入当前 `Zombie` 继承体系和窄虚接口。同时间轴变体复用最近父类 Setup，不重复注册事件。
- 死亡/啃食事件必须实际触发；无头、死亡、魅惑或读档不能留下禁止移动却无法完成的动作。只取消前摇/倒计时，已提交弹丸、干扰或世界对象不回滚。
- 逻辑 `row/x`、视觉偏移和坡面/矿道运动分开；位置与空间索引由当前 Board/EntityRegistry 入口维护，禁止照抄原版绝对坐标。
- 保持稳定枚举和实体 ID；权威资源注册、运行时键与 `HasReanimation` / `GetTexture(key,false)` 断言闭合。静态装备跟随父轨使用命名 follower，独立时间轴才用子 Animator。
- 品种拥有自身阶段/防具/受击语义，Board 拥有共享地形、正式波次预算和提交后的独立事务；读档恢复状态，不能重新应用出生倍率、补召或重播效果。

## 按任务读取

- 注册、预览与原版适配：[references/creation.md](references/creation.md)。
- 坐标、装备与断肢动画：[references/visual-animation.md](references/visual-animation.md)。
- 动作、啃食与控制状态：[references/actions-control.md](references/actions-control.md)。
- 召唤、编队与延迟攻击：[references/summons.md](references/summons.md)。
- 伤害、防具与剥离：[references/combat-armor.md](references/combat-armor.md)。
- 查询索引与战斗推演：[references/indexes-simulation.md](references/indexes-simulation.md)。
- 天气、出怪与掉落：[references/weather-spawns.md](references/weather-spawns.md)。
- 存档与时间回溯：[references/save-temporal.md](references/save-temporal.md)。
- 僵尸专项验证：[references/verification.md](references/verification.md)。
- 矿道、施工与搬运：[references/mine-relocation.md](references/mine-relocation.md)。
- 修改冒险关卡节奏：[出怪编排](references/adventure-spawnlist-pacing.md)。
- 新增脱离建造者仍存在、可承伤/拦截弹道的对象：[持久世界对象](references/persistent-built-world-object.md)。

涉及粒子、天气、植物、词条或经典分件适配时，再使用对应技能的相关部分。

## 验证与交付

构建、可见运行与渲染矩阵统一按 [项目指南](../../../docs/agent-guide/PROJECT_GUIDE.md) 和 [AutoTest](../../../docs/agent-guide/AUTOTEST.md)；默认 `clang-release`，只跑相关用例。改动画/死亡才增加事件与消失验证，改持久状态才验存读档，改外观才检查同步截图。新增完整角色覆盖其实际具有的能力、资源、生命周期和表现；未改渲染后端不加跑兼容路径。

外观验收检查本次受影响的本体、分件与反馈，确保实机尺度可读、锚点正确且无未完成占位符。原有简洁图形若已经是完整设计，不因图元种类而强制重画。汇报变更、相关验证和关键调参入口；小改动不列全量参数或逐项审计表。文档维护、提交与 push 统一遵循 AGENTS.md。
