---
name: adding-survival-perk
description: Use when adding or tuning any 生存模式词条 (survival perk) in PvZ — covers the current catalog, paired-selection flow, effect chokepoints, save contract, UI lifecycle, and AutoTest verification.
---

# 给生存模式添加或调整词条

当前实现以 `PlantVsZombies/Game/Perk/SurvivalPerkManager` 为唯一词条数据层，`Board` 以值成员持有它。核心契约是：**0 层 getter 必须返回中性值**，让非生存关和未获得词条的路径自然 no-op；不要在各效果钟点散写 `if (mIsSurvival)`。

开始前先读仓库根目录 `AGENTS.md`，并按任务范围阅读 `docs/agent-guide/PROJECT_GUIDE.md`。涉及既有设计时搜索 `docs/agent-memory/MEMORY.md`，当前源码和测试证据优先于历史记录。

## 原版参考边界

生存词条本身没有可机械照搬的 C# 架构。若某个词条复用原版伤害、承伤、冷却、产阳光或实体状态，只把 C# 当作该基础机制的玩家可感知行为证据；先核对本项目的唯一结算钟点、`DamageSource`、对象生命周期、存档和既有聚合倍率，再在 manager getter 与当前钟点接入。未获主人批准不得改变基础机制的原版结果，也不得为了贴近 C# 在词条层复制一套植物、僵尸或 Board 状态。

## 当前词条目录（2026-08-28）

| 枚举 | 类别/准入 | 当前效果 | 上限 | 主要钟点 |
|---|---|---:|---:|---|
| `PLANT_DAMAGE_UP` | 植物 | 伤害 +12%/层 | 9999（不限层） | `Zombie::TakeDamage` 仅对 `DamageSource::PLANT` 缩放；爆炸 Charred 阈值用相同聚合值预测 |
| `ZOMBIE_HEALTH_UP` | 僵尸 | 血量 +12%/层 | 9999（不限层） | `Board::GetZombieHpMultiplier` / `CreateZombie` 出生路径 |
| `ZOMBIE_DAMAGE_RESIST` | 僵尸 | 承伤 -3%/层 | 15，最低承伤 55% | `Zombie::TakeDamage` |
| `ZOMBIE_DAMAGE_UP` | 僵尸 | 对植物伤害 +5%/层 | 9999（不限层） | `Plant::TakeDamage` 仅对 `DamageSource::ZOMBIE` 缩放；攻击方传原始伤害 |
| `ZOMBIE_INVULN_HITS` | 僵尸 | 出生后前 4 次受击免伤/层 | 2 | `Zombie::TakeDamage` 消耗；`Board::CreateZombie` 初始化；僵尸存档持久化 |
| `PLANT_REGEN` | 植物 | 每 5 秒回复 65 HP/层 | 8；第 5 层起允许到 3 倍最大生命 | `Board::UpdateLevel` 全局脉冲 |
| `PLANT_ATTACK_SPEED` | 植物 | 开火速度 +15%/层 | 8 | 各射手的射击间隔与动画速度 |
| `PLANT_DAMAGE_REDUCTION` | 植物 | 承伤 -3%/层 | 15，最低承伤 55% | `Plant::TakeDamage` |
| `PLANT_SUN_BONUS` | 植物 | 收集阳光 +15%/层 | 10 | `Board::AddSun`；不缩放 `set_sun`、开局值和消费 |
| `PLANT_CARD_RECHARGE` | 植物 | 卡片冷却速度 +12%/层 | 10 | `Card::UpdateCooldown` |
| `PLANT_MIST_REFINING` | 植物/迷雾 | 雾火价值与本波并发接收上限 +50%/层 | 2 | `Board::CollectMistFuelFromZombie`、`Plantern::ReserveFuel`；仓储容量仍为 100 |
| `PLANT_CORROSIVE_TOXIN` | 植物/稀有 | 每层毒素每秒造成目标最大可计生命 1%，且不低于原 5 DPS | 1 | `Zombie::UpdateToxin`；百分比基数不吃全局植物增伤，仍过僵尸免伤 |
| `PLANT_UNYIELDING_ROOTS` | 植物/稀有 | 每株每轮首次致命普通伤害留 1 HP，免伤 3 秒 | 1 | `Plant::TakeDamage`、`Board::OnSurvivalRoundClear`；非伤害移除不触发 |
| `PLANT_DAMAGE_ECHO` | 植物 | 每第 10 次实际植物伤害命中使该击翻倍 | 1 | `Zombie::TakeDamage`、Board 共享计数；免费免伤不计、`PLANT_ASH` 排除 |
| `ZOMBIE_FOG_BREAKOUT` | 僵尸/迷雾 | 一生首次从浓雾走到可见区时解控并免控 3 秒 | 1 | `Zombie::UpdateSurvivalPerkStates` + `Board::IsZombieObscuredByFog` |
| `ZOMBIE_DEATH_RELAY` | 僵尸 | 死亡时给同排最靠近房屋的友方前锋 1 次受击免伤 | 1 | `Zombie::Die` → `Board::RelayZombieDeathWard` |
| `ZOMBIE_DEVOUR_REPAIR` | 僵尸 | 亲口吃掉植物时修满仍存在的本体、头盔与盾 | 1 | `Zombie::EatTarget`；不复活已丢失装备 |
| `ZOMBIE_ARMOR_BREAK_RUSH` | 僵尸 | 一生首次标准头盔/盾破碎时解控免控 5 秒并加速 60% | 1 | `Zombie::TakeDamage`、能力/动画速度聚合；魅惑取消增益 |

数值以 `SurvivalPerkManager.cpp` 的 `kPerks[]` 和聚合 getter 为准；表格若与源码不同，先更新本技能再继续。

## 先判断效果原型

| 原型 | 状态归属 | 适用例 | 必做事项 |
|---|---|---|---|
| A. 无状态倍率/数值 | 仅 manager 层数 | 增伤、承伤、血量、阳光、冷却 | 在唯一计算钟点读取聚合 getter；0 层返回单位元 |
| B. per-entity 状态 | 每个实体字段 | 僵尸出生前 N 次免伤 | 实体字段 + 出生初始化 + 消耗顺序 + Save/Load |
| C. Board 全局脉冲 | `Board` 计时器 | 全场植物回血 | `Board::UpdateLevel` 定时触发；无词条时跳过实体遍历 |
| D. Board 共享计数 | `Board` 字段 | 每 N 次全阵营命中触发 | 只在实际结算边沿计数；新局重置，生存档持久化 |
| E. 地图条件/稀有抽取 | 元数据 + `PerkOfferContext` | 迷雾词条、强力稀有词条 | 条件与稀有度分开表达；概率按整块面板控制，不按候选数量膨胀 |

优先选 A。只有“每个实体各自计数”才选 B；全场同频发生的周期效果才选 C。

## 新增词条的实现步骤

以下用于新增词条；仅调整已有倍率或上限时，直接定位现有定义、聚合入口和必要断言，不重复新增/注册流程。

1. 在 `Game/Perk/PerkType.h` 的 `COUNT` 前加入枚举项，并确定 `PerkCategory::PLANT_BUFF` 或 `ZOMBIE_CURSE`。
2. 在 `SurvivalPerkManager.cpp::kPerks[]` 的对应位置加入 `{key, nameZh, descZh, perStack, maxStacks, category, rarity, condition}`。顺序必须与枚举一致，`static_assert` 会检查数量。地图准入与稀有度是两个正交字段，不要用稀有度冒充地图条件。
3. 给 manager 增加聚合 getter/缩放函数。倍率使用 `RoundScale`；精确整数计数直接做整数乘法，不写伪四舍五入的 `static_cast<int>(x + 0.5f)`。
4. 把效果接到覆盖面最完整的唯一钟点。接入前用 `rg` 核实所有实际路径，尤其是新植物、新僵尸或旁路伤害。伤害调用必须显式传 `DamageSource`，不可增加默认来源来绕过编译器审计。
5. 在 `TestDriver.cpp` 的 `kPerkNames` 加 `PK(NEW_TYPE)`，并在 `dump_state.perks` 暴露层数和可精确断言的聚合结果。
6. 新增范围最小的 `autotest/scripts/smoke_perks_<name>.json`，用数学闭合值断言；再跑既有词条与选择 UI 回归。
7. 只有入口、设计原因、特殊例外或可复用陷阱变化时，才更新相关主题/技能；简单倍率调参直接改权威配置及必要注释，不把词条数值复制到记忆。记录范围遵循根目录 AGENTS.md。

## 三类原型的关键约束

### A. 无状态倍率

- 0 层必须返回 `1.0` 或原值，避免调用方额外判断模式。
- 伤害缩放走现有 `RoundScale`，其会保留 `INT32_MAX` 一类秒杀哨兵，并保证正常正伤害至少为 1。其他独立机制需要同一规则时调用公开的 `ScaleNumericDamage`，不要复制取整或哨兵判断；矿雾在防具分层前只缩放一次，灰烬致死预判必须使用同一倍率。
- `DamageSource` 是所有植物/僵尸受伤调用的必填参数：植物子弹、爆炸、寒冰菇为 `PLANT`，僵尸啃食/互啃为 `ZOMBIE`，小推车和无阵营直伤为 `OTHER`。新增攻击不标来源应直接编译失败，不能给参数补默认值。
- `Zombie::TakeDamage` 只对 `DamageSource::PLANT` 应用植物增伤，再对所有来源应用僵尸免伤。`Board::CreateBoom/CreateDoomBoom` 只能用 `ScaleTotalDamageToZombie` 预测植物爆炸的 Charred 阈值，传给 `TakeDamage` 的仍是未缩放原伤害，不能重复放大。
- `Plant::TakeDamage` 只对 `DamageSource::ZOMBIE` 应用僵尸增伤，再对所有来源应用植物韧性。僵尸攻击方传原始 `mAttackDamage`，不得自行调用 `ScaleZombieDamage` 或写回攻击字段。
- 攻速必须同时缩短射击间隔并提高射击动画 clip speed；否则高层时动画会在吐弹帧前被重启。新增射击植物时必须把它纳入攻速词条审计。
- 卡片加速只改变每帧冷却推进速度，不修改基础 cooldown 或已存的剩余进度。

### B. per-entity 状态

- 字段默认值必须中性，例如 `int mFreeHitsRemaining = 0;`。
- 免伤次数在 `Zombie::TakeDamage` 的 `damage <= 0` 守卫后、倍率缩放和 `SetGlowingTimer` 前消耗；完全吸收的攻击不应闪白。
- 波次出生只在 `Board::CreateZombie` 的 `!isPreview && !skipsettings` 路径初始化。不要在 `CreateZombieWithID` 读档路径重新赋值，否则会覆盖存档状态。
- 字段写入 `Zombie::Save/LoadProtectedData`，旧档用默认值 0；不要依赖可能被子类覆盖的额外存档钩子。

### C. Board 全局脉冲

- 计时器放在 `Board`，在 `Board::UpdateLevel` 的 GAME 状态内推进。
- 回血脉冲必须位于 `if (mCurrentWave >= mMaxWave) return` 之前，否则最后一只怪生成后会停止回血。
- 先取 `amount`，只有 `amount > 0` 才遍历实体。计时器无需存档；脉冲产生的生命值由实体既有存档自然保存。

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

## AutoTest 验证

基础命令：

- `add_perk {"type":"...","count":N}`：UI 前唯一词条注入入口。
- `damage_plant` / `damage_zombie`：走正式受伤链；`source` 可取 `PLANT/ZOMBIE/OTHER`（默认 `OTHER`），用于精确验证阵营增伤隔离。
- `survival_perk_open`：打开轮间选择。
- `survival_perk_pick {"index":0}`：选择候选；`index:-1` 只放弃当前一次机会。
- `survival_perk_refresh`：消耗本轮共享的一次刷新额度，重抽当前全部候选。
- `set_next_survival_perk_rare {"enabled":true|false}`：只覆盖下一次面板稀有判定，随后自动恢复正式随机。
- `apply_toxin_stacks {"row":N,"count":N}`：不经过子弹直伤，直接建立毒层，适合比较基础/百分比毒伤。
- `reserve_plantern_fuel {"value":N}`：直接走 `Plantern::ReserveFuel`，用于锁定实际并发接收上限；不要只断言重复计算出来的投影。若不创建飞行雾火，断言后用 `deliver_plantern_fuel` 清掉测试预留。
- `dump_state.perks`：词条层数与聚合数值。
- `dump_state.perkSelect`：除既有流程字段外，候选暴露 `plantRare/plantCondition/zombieCondition`，聚合暴露稀有植物候选数与条件候选数。steps 是已结算机会数，picks 是实际获得数；刷新不改变二者。

选择 UI 至少覆盖：

1. `smoke_perk_select.json`：两次都选；第 1 次刷新 1 次后第 2 次只剩 2 次，并在第 2 次耗尽，总计恰好 3 次；最终 `completedSteps=2`、`completedPicks=2`。
2. `smoke_perk_select_skip_all.json`：第 1 次放弃后仍 active 且进入第 2 次；再次放弃后 steps=2、picks=0。
3. `smoke_perk_select_skip_then_pick.json`：第 1 次放弃、第 2 次选择，最终 steps=2、picks=1。
4. `smoke_perk_view.json`：查看面板仍能显示和分页。
5. `smoke_perk_select_cob_input_modal.json`：正式轮清会清除已有炮击准星；词条页真实点击炮体与草坪均不得开始瞄准或发射。

数值或伤害钟点改动至少跑对应专项脚本、`smoke_perks_damage_sources.json`、`smoke_perks_balance.json` 和 `smoke_perks.json`。UI 改动要检查截图，不能只看退出码；同时检查对应 `run.log` 含 `script finished OK`。

地图条件、基础机制强化或稀有质变还必须做成对对照：同一地图、同一实体和同一时长先验证 0 层保持旧值，再开启词条只断言预期差异。当前扩展专项为 `smoke_survival_perk_expansion.json` 与 `smoke_survival_perk_fog_and_toxin.json`；后者同时锁定路灯花无词条/两层燃料价值与接收上限、毒液无词条/腐蚀词条伤害、迷雾出界清控及存档。`smoke_corrosive_toxin_lethal_catapult.json` 另锁定腐蚀毒素致死会同步释放毒素侧车的投篮车生命周期边界。

## 构建与交付

- 按 [构建与调试](../../../docs/agent-guide/BUILD_AND_DEBUG.md) 导入 x64 Visual Studio 环境。
- 所有生存词条任务的编译、F5、范围最小 AutoTest 和最终相关回归都默认直接使用 `clang-release`。同一份当前源码已用该产物完成相关验证时，不再重复编译 Debug 或重跑同一轮 AutoTest。只有主人明确要求 Debug CRT/Debug 语义，或 Release 问题确实需要辅助诊断时，才显式切换 `clang-debug`。
- 从 `build/<preset>/` 运行 `PlantsVsZombies.exe -AutoTest ...`，不要使用根目录旧的 `x64/Release` 产物。
- 若 sandbox 阻止 vcpkg 写外部 `buildtrees`，先报告真实阻塞；只有已有且可信的 Ninja 图时，才可把按图全量重编和链接作为当前工作区验证补充，不能把它写成标准构建方式。
- 功能、测试、技能和项目记忆一起复核后再提交。

## 关联记忆

- `docs/agent-memory/project_pvz_perk_system.md`
- `docs/agent-memory/project_pvz_autotest_suite.md`
- `docs/agent-memory/project_pvz_animator_clip_speed.md`
- `docs/agent-memory/reference_pvz_assets_worktree_autotest_gotchas.md`
