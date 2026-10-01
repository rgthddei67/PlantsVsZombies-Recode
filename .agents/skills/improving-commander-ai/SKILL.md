---
name: improving-commander-ai
description: Diagnose and improve the PvZ Cold Storage commander AI, integrate unit or plant abilities into forecasts, investigate human play logs and decision stalls, and run controlled training or policy comparisons. Use for Area 10 and Brawl commander behavior; ordinary unit balance, art, or unrelated game AI alone does not require this workflow.
---

# 冷藏站指挥官 AI 改进

服务第十大关和大混战的出兵、经济、技能选点、预测、训练与真人反馈。遵循仓库 [AGENTS.md](../../../AGENTS.md)；改单位本身时再组合植物／僵尸技能。不要因为读取本技能就启动训练、改数值或扩大正式卡池。

## 新窗口从哪里开始

1. 看本次要求、`git status` 和相关最新提交，确认哪些是别的窗口尚未提交的改动。先定位当前源码，不重放旧聊天里的全部试错。
2. 核对实际运行的 EXE、工作目录、正式策略文件与加载资格；截图显示的波次或资源只能提示症状。构建与可见运行按 [项目指南](../../../docs/agent-guide/PROJECT_GUIDE.md)。
3. 读取当前 `resources/ai/cold_storage_policy.json` 的权重、预测开关、验证／试玩标记，并查 `ColdStoragePolicy::Get` 的正式关卡范围。训练实验能用不代表正式游戏已启用；换配置后需重启进程验证实际加载。
4. 需要接续设计时，查 [第十大关交接](../../../docs/superpowers/specs/2026-09-18-area10-ice-economy-design.md) 的相关段落。数值、出怪池、能力边界和搜索预算以当前源码／权威资源为准；不要把历史胜率当成本次证据。

## 定位顺序

| 症状或任务 | 先核对 | 后续路线 |
|---|---|---|
| 新兵不出、出现过早 | 正式卡池、解锁、地形、价格、剩余名额、策略加载 | 门槛小改通常不需要重训 |
| 大兵池忽略工人／护卫／鼓手 | 能力是否被投影，候选是否有机会比较协同，再看边际收益 | [能力预测](references/forecast-contracts.md) |
| 一次买很多兵被灰烬／小推车清掉 | 实际伤害链、卡槽资金与冷却、已承诺反制、进场时序和战损 | 先修预测，再决定是否训练 |
| 冰很多却循环单兵／长期观望 | 合法候选、队列、v1/v2 实际路径、计划与等待基线、资本拒绝原因 | 保留有效等待；区分运行缺陷和真实无收益 |
| 付费技能打了低价值目标 | 目标身份、瞄准期间存活、相对不施法收益、买兵机会成本 | 验证同样预算下的替代方案 |
| 决策瞬间卡顿 | 主线程采样／提交、工作线程搜索、过期重试、渲染分别计时 | 不以整体 FPS 代替决策耗时 |
| 需要训练、换陪练或替换配置 | 当前基线与实验身份、课程覆盖、独立留出证据 | [训练与发布](references/training-and-release.md) |

当前实现是 **Board 采样 → 数值搜索 → Board 重新校验并提交**；离线训练通过真实游戏比较参数。日志不会自动把真人打法学习进正式配置。“接入机制”“改变搜索空间”“训练参数”“发布参数”是不同操作，交付时分别说明。

## 稳定源码入口

以下路径均相对仓库根目录；函数名可用 `rg` 定位，移动后沿调用者找新入口。

| 职责 | 入口 |
|---|---|
| 实际钱包、技能事务、付费队列、正式采样与提交 | `PlantVsZombies/Game/Board/BoardColdStorage.cpp`、`BoardColdStorageAbilities.cpp`、`ColdStorageState.h` |
| 快照能力、有限时域推演、候选搜索与收益 | `PlantVsZombies/Game/AI/ColdStorageSearch.h/.cpp`、`ColdStorageStrategy.h/.cpp` |
| 后台所有权、取消和交付 | `PlantVsZombies/Game/AI/ColdStoragePlanner.h/.cpp`，Board 的 `PollColdStoragePlan` |
| 正式策略与实验覆盖 | `PlantVsZombies/Game/AI/ColdStoragePolicy.cpp`；唯一资源 `build/clang-release/resources/ai/cold_storage_policy.json` |
| 单位／植物真实契约 | 对应实体实现、共用 `*Rules.h`、`build/clang-release/resources/gamedata.json` |
| 自动陪练与日志 | `PlantVsZombies/Game/AutoTest/TestDriverCommander.cpp`、`TestDriver.cpp`，`autotest/train_commander_league.py` |

## 验证与交接

选择本次能力的反事实单测和最小可见 `clang-release` 专项，再视风险做配对实战。测试路由见两个 reference；不把训练胜利代替交易、生命周期或线程验证，也不为文档整理重编游戏。

交付说明：改的是哪一层、用了哪份正式配置、实际验证了什么、仍有哪些预测近似。跨窗口继续时给出当前提交、政策文件／备份、实验目录、尚未解决的问题即可。无需把每轮成绩和当前数值抄入技能；不自动创建新窗口或继续无期限训练。
