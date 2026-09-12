# 僵尸状态与持续伤害

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

**僵尸新状态效果专属清单**（血泪教训浓缩）：

### 字段

加 `Zombie.h`，默认值=中性（无效果）；状态入口收敛成一个 `StartXxx()` 方法。
### 行为守卫放虚函数、不放 lambda 回调

——`onTriggerStay` 这类 lambda 会被别的路径绕过（魅惑撑杆实测教训）。
### 入口/出口清单

新状态改变的每个视觉/行为量（动画轨、翻转、手臂显隐…），把"进入状态、退出状态、死亡、断头、被魅惑、读档还原"每个入口逐一过——铁门僵尸 3 个 bug 全是入口漏同步。
### 三条创建路径

初始化各自正确：波次 `Board::CreateZombie`（出生初始化）、读档 `CreateZombieWithID`（**绝不**在此初始化，由 Load 还原）、预览 `InstantiateZombieFree`（跳过效果）。
### 存档

进 `Zombie::Save/LoadProtectedData`（基类无条件调用，不怕子类覆盖 SaveExtraData）；计时器类状态按 [植物生命周期与存档](plant-lifecycle.md) 核对受影响的计时状态。
### 交互矩阵

按已确认设计与统一规则核对，只对缺少依据的关键分歧提问：与魅惑、啃食状态机（走路权威=`PlayWalkAnimation`）、减速是否叠加、哪些僵尸免疫、持续时间、**效果期间新刷出的僵尸是否受影响**。
7. 动画冻结/变速类表现走 **Animator 三层速度模型**（`EffectiveSpeed=(clip?clip:base)*extra`，clip=0 回落 base，见 [[project_pvz_animator_clip_speed]]）；状态变色用 `OverrideColor`、特效用 `EmitEffect`。"全场"结算 = 对 0..mRows-1 行逐行 `ForEachZombieInRow`，没有整场 API。
### extra 速度层必须单点收敛

所有改速度状态的路径统一走 `Zombie::UpdateAnimSpeed()`（冻结0 > 减速×因子 > 常速；因子差异用 `GetSlowAnimFactor()` 覆写）。直调 `SetExtraSpeedMultiplier` 的旁路（如报纸狂暴）会把停格顶掉——寒冰菇实施时已把存量调用点收编，加新状态先 grep 这个函数。
### 视觉反馈别耦合在别的效果上

蓝色 overlay 原先绑在减速里，持盾僵尸免减速→冻结了却不变蓝（主人一眼抓出）。新状态的视觉在自己的入口/出口开关，别搭别的状态的便车。
### 豁免语义连伤害一起豁免

原版 HitIceTrap 的 20 伤害在免疫判定**之后**——魅惑/跳跃中撑杆连血都不掉。把"伤害+状态"整体放进 StartXxx()，别在植物侧拆开无差别结算。
### dump_state 加字段 + assert

（仿 `slowCooldown`/`frozen`/`armVisible`），否则 AutoTest 对新状态是瞎的。浮点计时器另配一个 bool 投影字段供 equals。
### 共享 overlay 必须单点合成优先级

魅惑、冻结/减速、中毒等共用 Animator overlay 时，让 `UpdateStatusOverlay()` 从全部权威状态派生最终颜色；每个进入、到期、清除、魅惑与读档路径都调用它。禁止某状态结束时直接 `EnableOverlayEffect(false)`，否则会抹掉仍有效的较低优先级状态。
### 目标级持续伤害要保存完整时间状态

共享层数上限放在目标实体，独立层保存各自剩余时间；跨帧小数余量按未减速的游戏 `deltaTime` 累积并与计时器一起入档，旧档默认中性。规格若要求魅惑清除延迟伤害，唯一魅惑入口和读档归一化都要清层；不绕盾的毒伤继续走 `TakeProjectileDamage(..., velocityX=0)`。专项覆盖满层后的刷新、存读档、魅惑、护盾以及不同 `timeScale` 下相同游戏时间等伤。
### 啃食触发的跨行反应必须脱离植物生命周期

第一口正式承伤后在 `Zombie` 建立独立 phase/timer，停止啃食时走统一目标清理以归零植物 `eaterCount`，后续碰撞不得重新开吃。换行直接更新权威 `mRow`，让通用行桶与碰撞桶下一帧从它重建；视觉 Y 再按 Board 地形追赶，并与 phase/计时/是否已换行一起保存中途 Y。目标行须按边界、介质和品种可生成条件筛选；死亡、掉装备等强制演出要原子取消，魅惑是否取消按规格单独决定。专项覆盖停顿、换行中快照、边界行、泳池、屋顶、装备破损和魅惑。
### 临时替换动画轨贴图时先比较有效像素框

同宽贴图也可能因画布高度或顶部透明留白不同而看似整体下沉。量取原图/替换图 alpha bounds 后用轨道局部 offset 补偿，禁止挪整只僵尸或改共享 reanim；所有退出、掉头、装备切换和读档恢复入口都要恢复原图、offset 与附属轨显隐，并用同步截图逐品种核对颈部基准。
