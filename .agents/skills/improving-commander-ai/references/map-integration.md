# 新地图／新章节复用指挥官

第十一大关夜间气象站复用这套 AI。这里集中保留 2026-10-03 排查发现的接入陷阱与源码入口；不是第十一大关玩法方案，也不维护第二份实时配置。实施时按函数名核对当前源码，只更新仍有效的接入指导。

## 已解除的策略加载限制

`ColdStoragePolicy::Get()` 已不接收关卡号，只返回经过校验的共享策略或实验覆盖；返回空指针时调用方回退旧指挥官。此前正式加载限制在 10-1～10-7，导致正常游玩的 10-8、10-9 使用旧 AI；不要重新引入章节上限。

加载权重并不等于地图已接入指挥官。棋盘资格、AI 总开关、资源有效性、合法卡池与真实付款仍由各自入口控制。新地图新增的机制也需单独适配预测，不能把“共享策略能加载”当成“新地图能力已验证”。

入口：[ColdStoragePolicy.h](../../../../PlantVsZombies/Game/AI/ColdStoragePolicy.h)、[BoardColdStorage.cpp](../../../../PlantVsZombies/Game/Board/BoardColdStorage.cpp) 的 `PlanColdStorageAttack`、`PollColdStoragePlan`、`UpdateColdStorage`。

## 接入时仍须处理的边界

以下入口覆盖已接入的第十一大关。后续地图仍应显式决定机制资格，不统一删除全部门槛。

| 边界 | 已发现的影响 | 源码入口 |
|---|---|---|
| 棋盘资格与背景绑定 | `IsColdStorage()` 同时识别 `HOT_COLD_STORAGE` 与 `WEATHER_STATION`；换背景后，初始化、出兵、经济、商店、工人产冰与读档等调用链都需要核对，不能只改策略加载器。 | [Board.h](../../../../PlantVsZombies/Game/Board/Board.h) 的 `IsColdStorage`；沿调用者查关联机制 |
| 战前支援 | `SupportsColdStorageOpeningBonus()` 开放原第十章后段、大混战和气象站，并排除生存；其他关卡不会弹出支援选择。 | [BoardColdStorage.cpp](../../../../PlantVsZombies/Game/Board/BoardColdStorage.cpp)；[Board.cpp](../../../../PlantVsZombies/Game/Board/Board.cpp) 的 `GetColdStorageOpeningElitePlantLimit` |
| 时间干扰 | `SupportsTemporalInterference()` 使用同一关卡范围；不满足资格时商店入口不可见，读档也会清除不合资格的干扰状态。 | [BoardColdStorageAbilities.cpp](../../../../PlantVsZombies/Game/Board/BoardColdStorageAbilities.cpp)；`LoadColdStorage` |
| 精准清除 | `CanUseColdStoragePrecisionStrike()` 开放原第十章后段、大混战和气象站；另有波次、库存、冷却和目标状态条件。只改章节资格仍不能绕开真实交易条件。 | [BoardColdStorageAbilities.cpp](../../../../PlantVsZombies/Game/Board/BoardColdStorageAbilities.cpp)；[ColdStorageSkillRules.h](../../../../PlantVsZombies/Game/Board/ColdStorageSkillRules.h) |
| 正式章节登记 | 冒险已登记至第十一大关，气象站使用独立背景与无新植物奖励的章节登记。新增场景能被测试夹具打开不代表玩家流程已接入。 | [AdventureProgression.h](../../../../PlantVsZombies/Game/AdventureProgression.h)；[GameApp.cpp](../../../../PlantVsZombies/GameApp.cpp) 的 `GetBackgroundID`；[Board.cpp](../../../../PlantVsZombies/Game/Board/Board.cpp) 的 `LoadSpawnListFromJson` |
| 经济与旧 AI 的章内分段 | `InitializeColdStorage()` 按章内第 1～9 关取现有开局冰块表；旧 AI 的早／后段预算也按章内关号分段。气象站开局冰块使用 `WeatherStationRules`，阳光仍由 `spawnlists.json` 唯一维护。 | [BoardColdStorage.cpp](../../../../PlantVsZombies/Game/Board/BoardColdStorage.cpp) 的 `kOpeningIce`、`InitializeColdStorage` 与旧策略分支 |
| 训练场景名称解析 | `train_cold_storage.py` 只识别末尾 `_10_1`～`_10_9`；未匹配的名称默认使用内部关卡 82，直接写 `_11_1` 会测到错误地图。课程中的第十章留出场景也不覆盖新章节。 | [train_cold_storage.py](../../../../autotest/train_cold_storage.py) 的 `episode_commands`；[train_commander_league.py](../../../../autotest/train_commander_league.py) 的课程与留出构造 |

## 最小验证入口

共享策略加载与回退已有 [smoke_commander_policy_scope.json](../../../../autotest/scripts/smoke_commander_policy_scope.json)：覆盖原范围边界、10-8、10-9、大混战、关闭 AI 与空实验权重。气象站另用 `smoke_weather_station` 与 `smoke_weather_station_interactions` 验证设备事务、交互与真实搜索提交。

新地图接入后，在实际目标关卡断言 `coldStorage.commanderStrategy == learned_search`，核对真实卡池、付款和技能资格；修过训练解析时，断言实际场景的关卡号与背景。换背景或存档资格时，再覆盖受影响的初始化、UI、产冰与快照往返。地图美术与网格另按 `creating-pvz-board-map` 技能验证。

气象站环境与出兵放在同一 `ColdStorageSearch::Option` 搜索空间：设备候选带 `device/setting`，不能转成僵尸类型或占用兵力名额。付款前复核整份预算及所有设备可用性；设备改变递增规划身份，不接受旧环境的后台计划。预测用纯数值状态副本，不能访问活体 Board 或消耗正式 RNG。
