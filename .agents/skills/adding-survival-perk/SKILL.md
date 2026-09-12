---
name: adding-survival-perk
description: Use when adding or tuning PvZ survival perks, their effect aggregation, selection UI or persistence.
---

# 新增或调整生存词条


`Board` 以值成员持有 `SurvivalPerkManager`，它是唯一词条数据层。当前目录、倍率与上限直接查 `SurvivalPerkManager.cpp` 的 `kPerks[]`、聚合 getter 与 `PerkType.h`，不在技能里维护第二份数值表。

## 核心契约

- 0 层 getter 返回中性值，非生存模式自然 no-op，不在每个结算点散写模式判断。
- 无状态效果只保存 manager 层数；每实体计数归实体；全场同频脉冲和共享计数归 Board。只在正式提交边沿消费次数。
- 伤害来源显式传 `DamageSource`，在唯一结算点聚合；攻速同时匹配射击间隔与动画速度，避免发射帧前反复重启。
- 新词条接入稳定枚举、定义、聚合入口与 AutoTest 名称；地图条件和稀有度独立。只调现有倍率时不重做注册/选择 UI 流程。
- 持久状态遵循实体/Board 正式存档，读档不重发出生奖励或已消费机会。
- 只问缺少依据的关键决策，常规细节按 [AGENTS.md](../../../AGENTS.md) 自主处理；若需要新增动画帧事件，仍必须先询问主人。

## 按任务读取

- 词条状态归属与结算：[references/effects.md](references/effects.md)。
- 选择界面与存档：[references/selection-save.md](references/selection-save.md)。
- 词条验证夹具：[references/verification.md](references/verification.md)。

## 验证与交付

纯倍率改动选对应效果专项，验证 0 层中性和预期差异；共享伤害改动追加相关来源/聚合回归，选择 UI 改动才追加选择、刷新、跳过与模态输入测试。改持久状态才验证快照往返，视觉改动检查同步截图。
构建与可见运行统一遵循 [项目指南](../../../docs/agent-guide/PROJECT_GUIDE.md)。vcpkg 外部写入受沙箱阻止时按 AGENTS.md 的已有构建授权申请提升权限，不把临时 Ninja 绕行写成正常流程。
交付只复核实际受影响的功能、测试及文档；小调参不附带技能/记忆全量审计。提交与 push 遵循 AGENTS.md。
