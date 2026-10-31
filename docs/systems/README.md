# 核心系统阅读地图

[返回文档导航](../README.md)

本页提供当前源码入口与阅读顺序。详细公共契约继续集中在项目指南，避免同时维护两份说明。下列路径和基础类型已于 2026-09-05 按源码核对；这不是运行验证记录。

## 对象、场景与关卡

使用说明先读 [对象生命周期与所有权](OBJECT_LIFECYCLE.md)：创建和 Start 的时机、停用与移除的区别、实体 ID、附件失效及场景切换边界。

先读 [GameObject.h](../../PlantVsZombies/Game/GameObject.h)，再读 [GameObjectManager.h](../../PlantVsZombies/Game/GameObjectManager.h)、[SceneManager.h](../../PlantVsZombies/Game/SceneManager.h) 和 [Board.h](../../PlantVsZombies/Game/Board/Board.h)。

| 入口 | 阅读时关注的问题 | 完整说明 |
|---|---|---|
| GameObject | 对象共有状态是什么，哪些空间／碰撞／点击附件由宿主显式拥有？ | [对象层次](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#对象层次)、[显式附件](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#显式附件) |
| GameObjectManager | 对象如何进入管理容器，如何安排更新与绘制？ | [游戏循环](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#游戏循环gameapprun) |
| SceneManager、Board | 场景生命周期与关卡玩法状态分别由谁管理？ | [所有权与场景边界](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#所有权与场景边界) |
| [BoardPresentation.h](../../PlantVsZombies/Game/Board/BoardPresentation.h) | 关卡逻辑如何向宿主场景发出展示请求？ | [关键系统类](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#关键系统类) |
| [EntityRegistry.h](../../PlantVsZombies/Game/EntityRegistry.h) | 如何按稳定 ID 引用和查询实体？ | [继承式玩法对象](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#架构决策继承式玩法对象) |

历史背景：[架构边界与文件拆分](../agent-memory/project_pvz_architecture_boundaries.md)。其中的文件行数和测试结果有各自日期，不能作为当前测量值。

## 存档

独立经营模式见 [戴夫工坊](WORKSHOP.md)：工厂物流、信号控制、前线战斗与独立基地存档。

入口为 [GameInfoSaver.h](../../PlantVsZombies/GameInfoSaver.h) 和[存档系统说明](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#存档系统)。阅读时先区分全局玩家状态与单局状态，再查看对象附加状态的保存／恢复入口，以及 schema 升级发生在恢复流程中的位置。

新增持久化字段前，沿说明找到对应迁移和测试入口；不要仅根据历史 JSON 样本推断当前格式。具体 schema 版本由实现与项目指南维护，本页不复制版本数字。

## 资源

入口为 [ResourceManager.h](../../PlantVsZombies/ResourceManager.h) 和[资源与资产说明](../agent-guide/ARCHITECTURE_AND_RESOURCES.md#资源与资产)。先分清资产所在位置、加载注册方式和运行时查询键，再追踪使用者。

查找资源问题时按指南中的注册表核对文件、loader、实际资源键和运行断言。API 签名直接看头文件；资源类型的详细注册表只在指南中维护。

## 什么时候再补独立 API 说明

当一个系统反复出现调用顺序、生命周期或组合使用问题时，再增加独立主题页，固定回答以下问题：

- 系统负责什么，状态和对象由谁拥有？
- 调用者从哪个公共接口进入，最小正确调用顺序是什么？
- 哪些单位、线程、生命周期或存档约束必须遵守？
- 最小使用示例及对应验证入口在哪里？

示例要来自当前源码，并链接定义处。全量函数列表和签名留在头文件，避免 Markdown 随代码变更失效。
