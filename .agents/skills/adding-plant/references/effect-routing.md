# 跨系统效果入口

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 特性侵入其他系统时（寒冰菇冻结、魅惑、穿透这类）

纯植物侧的清单不够用了，先按效果落点归类，逐类有先例可抄：

| 落点 | 先例 | 关键点 |
|---|---|---|
| **僵尸新状态效果**（冻结/减速/魅惑…） | 减速=SnowPea→`Zombie::SetCooldown`；冻结=IceShroom→`StartFrozen`；魅惑=HypnoShroom→`StartMindControlled` | 见下方专属清单 |
| **全场即时结算**（寒冰菇式） | `IceShroom`（帧事件→音效+白闪+逐行结算→`Die()`，仿 CherryBomb 骨架） | 植物先检查 `mBoard->GetPresentation()`，再用 `ShowScreenFlash(...)` 请求全屏效果；由 `GameScene` 实现 `BoardPresentation` 并注册绘制，禁止植物或 `Board` 恢复具体 `GameScene` 依赖；**isPreview 特判否则图鉴也结算**（主人叮嘱） |
| **格子占用系统**（弹坑/墓碑类） | `Crater`（毁灭菇弹坑，阻种 180s） | 轻量 GameObject（Trophy 先例，LAYER_GAME_OBJECT=背景上植物下）+Board weak_ptr 簿记+存档数组（旧档兼容=无字段则空）；**占格判定有两处独立口径都要改**：`CanPlaceInCell`（阻种）+`UpdatePlantPreviewPosition`（落点预览隐藏——漏改=悬停占用格仍显示预览，主人验收抓出）。AutoTest `plant` op 直连 CreatePlant **绕过闸门**：基础规则用 `assert_can_plant` 同时断言占用格 false 与旁格 true；若改了卡槽/悬停 UI，再补 `click` 真实路径与同步截图 |
| **外部附着格对象生命周期**（扶梯等） | `Ladder` → `Plant::Die/Squish` | 对象由 Board 提供唯一格接口与可选存档数组，植物只在正式死亡/压扁入口通知 Board，不持对象指针；悬浮 overlay 等原版豁免必须显式列出，行级清除能力在自身结算点调用 Board 同行接口。原版 `KillAllZombiesInRadius` 对僵尸按像素圆命中，却另按爆心所在格的方形 `rowRange` 清扶梯：樱桃/玉米炮为 ±1 格，毁灭菇为 ±3 格；统一收口到 Board 爆炸清梯入口，专项必须覆盖范围内非同格扶梯清除和边界外保留，不能让植物自身死亡清同格造成假绿。专项分别覆盖普通死亡、压扁、豁免层、同行/异行保留与快照往返，避免只测部署者而漏掉被附着植物的生命周期。 |
| **清除同格组合植物** | DoomShroom→`KillOtherPlantsInCell` | 按 EntityRegistry 的全部植物 ID 快照遍历，以逻辑 `row/column` 过滤并排除施法者，再逐株 `Die()`；不要只写死当前 under/normal 两层，否则以后南瓜等额外层会留在结算后的弹坑里 |
| **引爆倒计时无敌** | CherryBomb / DoomShroom 的 `TakeDamage` 覆写 | 只 `SetGlowingTimer(0.1f)` 不掉血；**有睡觉态的要放行睡觉分支**（白天=普通蘑菇照常被啃，毁灭菇实证） |
| **新子弹/运行时变种** | `Bullet final`；Torchwood→FirePea | `BulletType` + BulletPool；所有已接入弹型共用单一具体 `Bullet`，不要创建只继承构造函数的标记子类。动画子弹可组合独立 Animator，但命中语义必须按“当前类型”分派，不能依赖分配时的 C++ 子类 |
| **新粒子特效/染色变种** | IceFumeCloud（寒冰大喷菇） | XML 标签全参考在 **adding-particle skill**，常规配置查标签参考；语义冲突或消费端变更时核对 ParticleSystem 源码 |
| **TakeDamage 类钩子** | FumeShroom 的 `penetrateShield` 参数 | 穿透只对二类护盾（门/报纸），不穿头盔；改签名先看全部调用点 |
| **即时/范围结算** | CherryBomb/大喷菇锥形 | 帧事件触发结算帧（帧号问主人），范围判定用行桶不全扫 |
| **目标类型拥有特殊受击语义** | Caltrop → `ZamboniZombie::HandleCaltropHit` | 植物只负责命中与派发，目标基类拥有虚事件并处理消耗植物/动画/存活；精英变体覆写目标方法，禁止在植物攻击函数里继续堆具体精英类型分支 |
| **大嘴花拒吞伤害** | Chomper → `Zombie::TakePlantInstantKill/AdjustRejectedChomperBiteDamage` | `TakePlantInstantKill()` 只决定是否确实吞下；返回 false 后由大嘴花把默认 20 点交给目标调整入口，再走正式 `PLANT` 伤害链。未来拒吞品种不覆写调整入口就自动使用 20；只有已有明确平衡数值的特殊品种才覆写，禁止把默认值再次分散到各僵尸。持杆跳跳因高度咬不到，按当前装备状态把伤害调整为 0；失去弹跳器后恢复基类吞食。专项用普通僵尸锁定吞食+消化，用巨人/红眼巨人/首领锁定拒吞+20，并回归特殊数值与持杆跳跳 0 伤害。 |
| **能力剥离目标装备** | MagnetShroom → `Zombie::HasMagneticItem/ExtractMagneticItem/GetMagneticSimulationLayer` | 目标自己声明当前状态是否可剥离，并原子返回当前损伤贴图、轨道世界起点和植物侧落点后进入无装备终态；植物只做范围、优先级、飞行和充能，禁止按僵尸类型 `dynamic_cast`。轻量推演继续复用正式 `CanBeTargetedByMagnetShroom`，再由目标返回 `HELM/SHIELD/TOOL` 供副本消费；硬帽与磁性工具并存等特殊品种必须覆写层级，不能从剩余头盔血猜工具。剥离与受击破甲是两条路径，不得误触发掉落粒子、范围伤害等破甲副作用；高压变体可覆写资格为 false。派生植物若在成功吸取后追加能力，用“装备已剥离并接管离体物 → 品种成功钩子 → 提取者反噬”的顺序；场景扶梯不调用僵尸成功钩子，反噬致死也不回滚已结算能力。离体物视觉状态与植物计时入档，目标新 phase 也须走自身存档。若装备返回默认 0 之外的 `extractorSelfDamage`，必须先进入吸取/充能态并接管离体物，再直接扣植物本体；规格要求绕过南瓜和防御词条时不可调用通用 `TakeDamage`。C# `MagnetItem.mDestOffset` 是未缩放贴图左上角，而本项目飞行状态使用绘制中心；终点必须加 `texture.width/height * drawScale / 2` 转换，禁止用统一 X/Y 补偿掩盖锚点口径错误。屋顶关必须种在正式花盆组合上截图并断言最终世界包围盒相对同格逻辑中心，不能在无承载层的 AutoTest 捷径里验站位；用 `topPlantsByCell.<row>_<col>` 定位磁力菇，不得误把 `plants.0` 当作新种植物。 |
| **区域天气保护与每次事件反噬** | GroundingShroom → `Plant::CanGroundNightRoofChargeFor/AbsorbGroundedNightRoofCharge` | 由 Board 在天气事件唯一边沿先按稳定实体 ID 冻结全部目标与提供者分配，让所有受该区域影响的实体消费同一批范围后，再按提供者 ID 归并为每次事件一次反噬；这样提供者被反噬击杀也不会让同一次事件的后续目标漏保护或漏压制。重叠范围按规格的距离与稳定 ID 决胜。品种接口只声明资格、范围和直接本体反噬，不在 Board 堆植物类型表；免除离散天气状态不得顺带清除仍有效的连续区域暂停。若能力同时压制某种僵尸，植物继续声明空间语义，僵尸自己的能力入口查询 Board 聚合结果，保留目标所有者对承接、过载和存档的权威。专项覆盖多植物层归并、重叠、提供者致死、连续暂停不被免疫及目标僵尸能力仍保留其他抗性。 |
| **资源型区域保护阻止目标状态提交** | FurnaceCoreFlower → `Plant::TryPreventIceExecutionSealFor` | 威胁必须在目标状态首次变更前调用 Board 聚合入口，不能等伤害或进度已经提交后再补偿。Board 快照并按稳定植物 ID 排序提供者，过滤非活动、停机、冰封、被压扁/抓取和范围外实体；植物窄接口自行消费资源并显式处理“不能保护自己”，威胁不得 `dynamic_cast` 品种。若成功保护会使一次性威胁失效，由威胁在同一调用边沿原子进入 `SPENT`；保存提供者库存/未完成充能和威胁阶段，读档不重播反馈。专项至少覆盖目标从未进入状态、首 ID 消费、后继提供者、资源耗尽回退、自身排除和存档。 |
| **消耗资源拒绝 Board 的离散入场/回位事务** | BoundaryFlower → `Plant::CoversBoundaryEntryCell/TryConsumeBoundaryShard` | Board 在目标真正出生或回位前按范围距离、再按稳定植物 ID 选唯一提供者；植物只声明覆盖并原子消费一份资源，事务自身决定“取消、改道、留在当前位置或安全侧复活”，禁止植物识别裂隙、雪穴或时间锚类型。库存、未完成充能和停机暂停完整入档；每个目标事务最多消费一份，重叠保护者不叠加。专项覆盖最近/同距 ID 决胜、资源耗尽、停机、存活回位拒绝、死亡复活改道、存读档及同批多个目标逐份消费。 |
| **承载层紫卡原位升级并提供事件边沿保护** | LightningRodPot → `PlantUpgradeLayer::UNDER` + `Plant::IsRoofSupportPlant` | 升级规则必须声明替换 `under` 而不是默认 `normal`：先创建新 under 并切换 Cell ID，再让旧 under 正式死亡，原格 `normal/pumpkin/overlay` 全部保留；读档创建、渲染层、卡槽预览、屋顶放置、径流与台风等支持语义都改查能力接口，禁止继续写死 `PLANT_FLOWERPOT`。同格保护用 Cell 的 O(1) 分层查询，只在劫持/雷击结算边沿调用；同行非叠加光环在已锁定行的固定列上扫描一次并取最大倍率，空承载返回中性值，不保存可由当前层组合派生的活跃标志。专项覆盖原子升级、组合层保留、空盆 no-op、多盆不叠加、外伤毁盆留上层、存读档和默认实例/`-NoInstance` 同步截图。 |
| **场上唯一、死亡后可重种并复用卡槽作控制台** | Plantern → `Board::mActivePlanternID` + `CardSlotManager` | 唯一性从当前活动实体 ID 派生，创建/读档重建、死亡/压扁释放，不能复用“累计种植次数”计数；卡槽菜单只持 UI 瞬态，玩法状态留在实体/Board。CardSlotManager 是 GameScene 独占的普通控制器，不能重新挂回匿名 CardUI GameObject；卡片和本体只请求展开，同一按钮输入走真实 `click` + 截图验收。需要覆盖场景自定义面板时，`GameObjects` 绘制命令会早于后注册的天气面板，应把菜单拆成更晚的场景 UI 绘制命令；重叠区输入仍须由菜单优先消费。若资源经济要求前宽后紧，按每关当前波/总波数独立归一化，不把补给挂在后期天气压力上；至少用两种总波数验证首尾端点。每波“出生分配上限”不能防止跨波携带者被范围伤害同时兑现：另在 `ReserveFuel` 限制同批在途吸收量，并对高收益挡按同一波次进度提高消耗；专项须同帧结算超过上限的多份奖励 |
| **飞行资源抵达实体后才到账** | Plantern `ReserveFuel` → `MistFuel` → `DeliverReservedFuel` | 生成飞行物时只预留容量，抵达目标才增加玩家可见数值；多团在途必须共同占用容量，目标死亡则丢弃。飞行对象若不进入通用存档，实体须保存预留量，并在读档时结算或重建飞行，禁止奖励丢失或永久占仓 |
| **消耗型实体资源的低量警报** | Plantern `previousFuel >= T && fuel < T` | 只在正式消耗前后比较阈值下降沿，禁止保存或初始化 `warned` 标志，避免持续刷屏和低量读档伪触发；补回阈值后自然重置。瞬时反馈用既有音效 + `BoardPresentation` 的短时中央警报，高倍速下用未缩放时间；警报结束后由卡牌保留持续低量指示。专项断言音效请求次数、提示内容、5 倍速不提前消失和同步截图，测试 setter 只负责布置阈值上方状态，不直接伪造下降沿。 |
| **逐格可见性限制远程索敌** | Plantern/Fog → `Board::CanPlantAcquireZombie` | 继续以目标行平滑后的逐格 alpha 为唯一权威；若允许看入第一格薄雾，目标格超阈值后只检查它朝植物方向的相邻格是否已可见。不要按地图固定雾线或复制照明形状，否则开关灯过渡与画面不同步。AutoTest 锁定高 alpha 第一格 true、第二格 false，并覆盖照明边缘 |
| **空中/地面分层索敌** | Cactus → BalloonZombie | `Board::CanPlantAcquireZombie` 先调用植物虚接口决定能否索敌，再叠加雾可见性；普通植物默认只认地面，特殊植物分别缓存空中/地面目标并让空中优先驱动姿态。发射时把当前姿态写入子弹的命中层，子弹碰撞再向僵尸查询当前 phase；层标记须入档且在对象池复位。伸缩和射击的一次性动画回调必须同时核对 phase 与当前轨道，低/高姿态分别使用经截图验证的稳定发射偏移。AutoTest 用聚合计数断言空/地弹互斥，不依赖 `bullets.N` 顺序 |
