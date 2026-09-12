---
name: adding-particle
description: Use when adding or tuning PvZ ParticleSystem XML effects, particle textures, colors, emission anchors or lifetimes.
---

# 新增或调整 PvZ 粒子


适用于 ParticleSystem XML；reanim 粒子和实体 Draw 状态贴图应按其真实绘制路径定位。只问缺少依据的关键表现/玩法决策；新增动画帧事件仍必须先询问主人。

## 实施

- 常规配置直接查 [XML 标签参考](references/xml-reference.md) 对应标签或配方；出现语义冲突、参考未覆盖的标签或修改消费端时，核对 `ParticleEmitter/ParticleXMLLoader` 源码，只更新失效部分。
- 资源只改 `build/clang-release/resources/`；发射前核对真实效果名和 `<Image>` 键的注册与运行时加载。manifest 存在不能替代 `GetTexture(key,false)` 断言。
- 世界发射锚点与 XML 局部偏移各算一次；动画分件优先取轨道世界位置。原版绝对坐标按当前 Board/视觉原点换算。
- 宿主 follower 缩放不自动传给离体粒子，比较游戏内最终尺寸；存档恢复终态不能重播爆发或一次性声音。
- 改 XML 数值无需编译，重启加载后验证。只调颜色/数量不重新勘察全部触发与生命周期。

## 按任务读取

- XML 标签、字段与配方：[references/xml-reference.md](references/xml-reference.md)。
- 坐标、资源与生命周期：[references/geometry-lifecycle.md](references/geometry-lifecycle.md)。
- 粒子专项取证：[references/verification.md](references/verification.md)。

## 验证与交付

按 [AutoTest 指南](../../../docs/agent-guide/AUTOTEST.md) 跑默认可见专项，检查受影响资源、状态与同步截图；新增反馈验证发生时机，改几何验证相对锚点，改生命周期验证结束/恢复。未改渲染后端不追加兼容路径。
范围、方向、颜色和实机尺度符合设计即可，不固定要求逐项审计报告。构建、文档维护与提交遵循 [AGENTS.md](../../../AGENTS.md)。
