# PlantVsZombies 项目指南

[返回文档导航](../README.md) · [核心系统阅读地图](../systems/README.md)

本页只负责按任务路由。始终生效的规则见根目录 [AGENTS.md](../../AGENTS.md)；详细说明在下面三个主题中维护，按任务读取相关章节。

| 任务 | 必读主题 | 常用定位 |
|---|---|---|
| 配置、编译、F5、运行、崩溃排查 | [构建与调试](BUILD_AND_DEBUG.md) | [构建与运行](BUILD_AND_DEBUG.md#构建与运行)、[TestDriver 编译例外](BUILD_AND_DEBUG.md#testdriver-的-release-编译例外) |
| 运行 AutoTest、选择验证路径、查输出与夹具 | [AutoTest 验证](AUTOTEST.md) | [测试套件](AUTOTEST.md#autotest-测试套件) |
| 修改架构、对象生命周期、坐标、存档或资源 | [架构与资源契约](ARCHITECTURE_AND_RESOURCES.md) | [所有权](ARCHITECTURE_AND_RESOURCES.md#所有权与场景边界)、[网格](ARCHITECTURE_AND_RESOURCES.md#board-网格)、[存档](ARCHITECTURE_AND_RESOURCES.md#存档系统)、[资源](ARCHITECTURE_AND_RESOURCES.md#资源与资产) |
| 新增功能、查原版参考或编码约定 | [架构与资源契约](ARCHITECTURE_AND_RESOURCES.md#参考与实现指引)及 AGENTS.md 路由的对应技能 | [编码约定](ARCHITECTURE_AND_RESOURCES.md#编码约定) |

一次任务涉及多个方面时组合阅读；例如修改存档后运行验证，需要架构、构建和 AutoTest 三部分。

## 项目记忆

从 Claude Code 迁移而来的项目记忆保存在 `docs/agent-memory/`，现作为面向 Codex 的项目文档维护。

- 当前数值/行为直接查源码或权威配置及注释；需要历史原因、例外或排查入口时，再用 `docs/agent-memory/MEMORY.md` 定位相关段落，不机械地先读历史。
- 记忆中的数值和测试结果是历史快照；只核实当前结论实际依赖的事实，不逐项复查整篇旧记录。
- 明确的任务指令、根目录 `AGENTS.md`、当前源码以及当前测试/构建证据优先于冲突的记忆记录。
- 何时保存设计、更新主题或索引，统一遵循 [AGENTS.md 的“文档与记忆”](../../AGENTS.md#文档与记忆)；简单调参不触发文档维护，已有设计直接链接，不复制数值摘要。
- 不要依赖旧的 `~/.claude` 副本。仓库内副本是后续 Codex 工作的权威项目记忆。

## 版本控制

- 提交与推送统一遵循根目录 [AGENTS.md 的“Git 与沟通”](../../AGENTS.md#git-与沟通)：主人已长期授权向指定 `origin` 的明确上游分支常规 push，完成验证后默认提交并推送，无需逐次确认。授权目的地、推送前检查、例外和审批边界只在该处维护。

## 沟通风格

称呼与沟通规则统一遵循 [AGENTS.md](../../AGENTS.md#git-与沟通)。

## 文档维护

本页只作导航。有效契约优先写在代码注释，跨任务设计和难从代码看出的原因按 AGENTS.md 按需保存；移动入口时修相关链接，历史方案保留当时语义，不作为实时配置维护。
