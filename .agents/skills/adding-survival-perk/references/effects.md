# 词条状态归属与结算

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

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
6. 复用或补充范围最小的词条脚本，用数学闭合值断言；只在触及共享结算或选择流程时追加对应回归。
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
