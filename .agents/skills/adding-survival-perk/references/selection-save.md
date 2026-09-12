# 选择界面与存档

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 每轮最多两次的成对选择契约

一项候选始终是 `PerkPairing { plant, zombie }`，选择一次会同时给植物增益和僵尸诅咒各加 1 层。配对不是固定绑定：`RollPerkPairings` 先按 `PerkOfferContext` 过滤地图条件，再构造普通植物与普通僵尸词条的笛卡尔积。每块三选面板独立进行一次固定 1.5% 的稀有判定，命中时最多用一个稀有植物候选替换普通植物候选；不能对每个稀有词条逐个掷骰，否则目录扩充会偷偷抬高总概率。测试可用一次性 override 强制普通/稀有，正式 UI 不展示概率。普通植物池全部满层但稀有池仍可用时强制从稀有池出候选，不能生成空面板。

`Board::OnSurvivalRoundClear()` 只经非拥有的
`BoardPresentation::BeginSurvivalPerkSelect()` 发起展示请求；`GameScene` 实现下面的 UI 流程。
禁止恢复 `Board → GameScene` 具体依赖，也不要把词条玩法状态复制到场景。

- `SURVIVAL_PERK_PICKS_PER_ROUND = 2`。
- `BeginSurvivalPerkSelect()` 把已消耗机会数和实际选择数都归零、暂停游戏，并调用 `RenderSurvivalPerkSelectStep()`。
- `mSurvivalPerkStepsCompleted` 表示已经结算的机会数；选择或放弃都会 +1。`mSurvivalPerkPicksCompleted` 只表示实际获得的配对数；只有合法选择才 +1。不要再用 picks 推导当前第几次。
- 每一步重新 roll 3 个候选，标题显示“第 X/2 次”。第 1 次无论选择或放弃，都会关闭旧框、重新 roll 并显示第 2 次。
- 第 2 次结算后进入 `BeginSurvivalCardSelect()`。
- 点击“放弃本次”会调用 `ApplyPerkSelection(-1)`，只消耗当前机会；两次都放弃时本轮获得 0 对，先放弃后选择时获得 1 对。
- 每轮另有 `SURVIVAL_PERK_REFRESHES_PER_ROUND = 3` 次共享刷新额度。`BeginSurvivalPerkSelect()` 只在整轮开始时把 `mSurvivalPerkRefreshesRemaining` 重置为 3；第 1 次选择使用的刷新会从第 2 次选择可用额度中扣除。
- `RefreshSurvivalPerkSelection()` 每次只消耗 1 次刷新额度、关闭旧框、重新 roll 当前全部 3 项并重建同一步选择框；它不增加 steps/picks，也不应用任何词条。额度为 0 时按钮保留为不可点击的“刷新（已用完）”。
- 选择进度是轮间临时 UI 状态，不新增存档字段。最终词条层数由既有 manager 存档随之后的选卡流程一次保存。
- `BeginSurvivalPerkSelect()` 必须提前快照上一轮仍在冷却的卡牌，实际卡槽在词条结算后的 `BeginSurvivalCardSelect()` 清空。玩家若在词条页点 X，`CHOOSE_CARD` 存档只保存冷却快照，绝不能把上一轮卡槽当成下一轮已提交卡组落盘。

### 消息框生命周期

所有 `GameMessageBox`（含 `.Panel()`）已由 `UIManager` 自动执行“最后创建的活动框独占 Button/Slider 输入，底层保持绘制”；`Scene` 同时屏蔽世界鼠标及关闭当帧的穿透。不要再为词条面板逐个禁用背景按钮。若按钮打开保留父框的子弹窗，使用 `autoClose=false`，取消时只关闭子框；这不替代下方的轮间 Board 状态与手持目标清理契约。

词条候选、“刷新”和“放弃本次”按钮的 `autoClose` 必须为 `false`；**选择流程的关闭权统一由 `GameScene` 持有，弹窗内存与控件注册由当前场景 `UIManager` 持有**。`ApplyPerkSelection()` 和 `RefreshSurvivalPerkSelection()` 都通过 `CloseSurvivalPerkSelectBox()` 先把旧框设为 inactive 后再 `Close()`，然后刷新下一步、重抽当前步或结束流程。立即失活用于消除同帧重建的一帧双框残影；`Close()` 只提交关闭请求，UIManager 会在 Button/Slider 完成本帧遍历后安全解除旧框控件。真实点击与 AutoTest 直接调用走同一生命周期。

若修改选择次数、候选数或按钮语义，必须同步 `GameScene` 常量/计数/标题、`TestDriver.cpp` 的 `perkSelect` dump、两个选择脚本、本技能和项目记忆。

词条页是覆盖战场的轮间模态流程。正式入口已经把 `Board` 切到 `CHOOSE_CARD`；`BeginSurvivalPerkSelect()` 还必须清除上一轮未提交的 `CursorObjectManager` 手持状态（含玉米加农炮落点准星）。`CardSlotManager::CanAcceptGameplayInput()` 只在 `BoardState::GAME` 且未被普通暂停门禁时返回 true，卡牌点击、格子点击、路灯花和玉米炮交互都复用这一资格，不能只依赖 `DeltaTime::IsPaused()`。这样选择按钮释放的同一帧也不会穿透到底层 Cell；专项用正式 `force_survival_round_clear` 进入 `CHOOSE_CARD`，先带入一个活动炮击准星，再以真实 `click` 点击炮体与草坪并断言准星已清除、炮仍为 READY、没有炮弹。

## 存档契约

- manager 层数按稳定字符串 `PerkInfo::key` 保存，不能依赖 enum 序号；新增 key 缺失时自然加载为 0。
- `Load` 必须把层数夹到 `[0, maxStacks]`，因此调整上限会在读旧档时自动收敛。
- 零词条时不要用 `Save(j["perks"])` 直接物化 null。先保存到局部 json，非 null 才写入；读端保留 `j.is_object()` 守卫，兼容历史上的 `"perks": null`。
- 卡槽只在 `BoardState::GAME` 表示已提交卡组；`CHOOSE_CARD` 保存时必须写空卡组，读取时也不得恢复旧 `cards`。为兼容历史问题档，可把其中仍在冷却的旧卡迁移到 `survivalCardCooldowns`，但不能加入 `CardSlotManager`。
- `survivalCardCooldowns` 经 `BoardPresentation` 捕获/恢复 UI 瞬态；最终词条层数仍由 `Board` 持有的 manager 保存。禁止为存档重新依赖具体 `GameScene`。
- 新字段能用稳定 key 或中性默认值表示旧档时保持兼容；结构或语义变化无法只靠默认值表达时，提升 `SaveSchema::kCurrentLevelVersion`，增加连续迁移和 `SaveSchemaTests`。
- 原型 A/C 通常不增加独立存档；原型 B 必须持久化实体状态。
- Board 共享进度只在生存档保存，并在进入新棋盘时清零；读档再以存档覆盖。每轮一次的实体消耗状态要明确在 `OnSurvivalRoundClear` 重置，不能误做成每波或每局。
- 百分比持续伤害若设计为不吃全局植物增伤，应从目标最大可计生命生成基础伤害，并用非 `PLANT` 来源进入正式僵尸免伤链；同时用无词条/有词条同靶对照锁定基础毒伤未漂移。
- 批量或百分比持续伤害调用虚受伤入口后，必须假定派生僵尸会同步 `Die()` 并释放宿主侧车；返回后先重新检查宿主仍活动且侧车仍存在，再访问层数、余量或其他状态。专项至少包含一种本体归零即直接死亡的车辆，锁定“致死后实体消失且不会继续解引用侧车”。
