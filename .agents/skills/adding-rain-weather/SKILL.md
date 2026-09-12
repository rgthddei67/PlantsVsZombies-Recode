---
name: adding-rain-weather
description: Use when adding or modifying Board-owned weather dimensions or weather-dependent abilities, mutations, forecasts and interactions.
---

# 天气与条件能力


天气真相由 Board 统一拥有。先定位本次影响的维度与结算入口，再决定需要读取哪些专项；当前数值直接查源码/配置。

## 核心契约

- 即时条件在结算点查询，平滑倍率用现有插值 getter，只缩放对应能力的时钟；不要改全局 DeltaTime 或复制雨势状态。
- 并行雾势、低温、积雪等维度保持独立状态、预报和存档，通过明确交互点读取其他维度。
- 正式波次变异在选型/预算的统一 resolver 只 roll 一次；直接生成单只的夹具不能证明自然波次正确。
- 实体只保存自己的冷却、次数或形态；实际切档、地图门禁、同档续期与台风策略归 Board。
- 一次性随机、锁定结果、剩余时间与次数入档；读档不重 roll、不返还额度、不重播已提交效果。
- 关键玩法选择按 [AGENTS.md](../../../AGENTS.md#任务路由) 集中提问；统一规则能推导的普通分支自主处理。新增动画帧事件前，按仓库规则先询问主人。

## 按任务读取

- 天气扩展原型与实施：[references/implementation.md](references/implementation.md)。
- 天气机制专项约束：[references/mechanisms.md](references/mechanisms.md)。
- 天气专项验证：[references/verification.md](references/verification.md)。
- 查接口或某个天气维度：[contracts.md](references/contracts.md)，先看目录，只读命中章节。

涉及植物、僵尸、粒子或词条实现时，叠加对应技能的相关部分。

## 验证与交付

按实际改动选择天气边沿、正式波次、控制/时间或存档专项。构建、可见运行、截图条件与跨后端矩阵统一见 [AutoTest 指南](../../../docs/agent-guide/AUTOTEST.md)；不因修改一个概率重跑所有天气机制。已有相关验证足够时不扩展或重复。
只汇报本次变化与证据；文档和 Git 流程遵循 AGENTS.md。
