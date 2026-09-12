# 注册、预览与原版适配

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 原版参考边界

- C# 参考实现用于确定玩家可感知的功能契约：状态机与触发顺序、时长和数值、目标规则、断肢/死亡结果、音效及资源表现；没有主人批准时不得自行改变这些行为。
- C# 不是本项目的类图或字段模板。实现前逐项核对当前僵尸继承体系、Board/实体所有权、碰撞回调、Animator 与根运动、绘制层、资源轨道、魅惑/死亡生命周期和存读档，再复用现有最窄入口；禁止复制平行状态或绕过公共系统。
- 玩法对象架构固定为继承式：新增僵尸继续选择 `Zombie` 或最窄既有派生基类，并用窄虚接口表达品种差异；不得为僵尸能力恢复通用 `Component` 基类/类型表、把品种状态拆成任意组件组合，或为形式统一复制基类生命周期。空间数据由宿主 `CreateTransform()` 创建并通过 `GetTransform()` 访问。Collider、Shadow 与 Clickable 是宿主显式独占的具名附件：分别通过 `CreateCollider()` / `GetCollider()` / `RemoveCollider()`、`CreateShadow()` / `GetShadow()` / `RemoveShadow()`、`CreateClickable()` / `GetClickable()` / `RemoveClickable()` 管理，禁止恢复 `AddComponent/GetComponent/RemoveComponent<T>` 或缓存可独立失效的附件裸指针。三个类的 `Component` 后缀只是过渡命名，不代表组件系统。`CreateClickable()` 保证 Collider 已就绪；`RemoveCollider()` 会同步注销 Clickable，运行时替换 Collider 则保持 Clickable 注册有效。
- 架构、坐标或资源差异必须做适配，并用状态投影、音效计数和默认可见截图证明功能等价。原版资源版本与当前 reanim 不一致时，以玩家结果忠实为目标，具体轨道方案按当前资产确认；缺少依据且会改变关键玩法时再询问主人；已授权改动和普通适配不重复确认。

## 新增僵尸的勘察

以下清单用于新僵尸或相应契约的实质修改。单纯调参直接检查权威参数、单位、相关限制及必要测试，不重做未涉及的动画、音效和注册勘察；历史原因按需查记忆。

### 读 reanim

`build/clang-release/resources/reanim/<Name>.reanim`，Grep `<name>` 提取全部 track，并 Grep `<i>` 列出每个实际图片键；`anim_*` 是剪辑轨（`<f>0/-1</f>` 定活跃帧区间），其余是部件轨。重点记下：头部组（`anim_head1`=头、`anim_head2`=下巴、`anim_hair`，可能有 tongue/earing 等挂件）、外臂三段（`*_outerarm_upper/lower/hand`，注意前缀可能不统一）、有无残肢轨（`_bone`/`upper2`）、`_ground` 轨（位移速度来源），以及 `rise2～6` 这类把帽子、脸和衣服烘在一起的阶段合成图。独立部件换色不会影响合成图，必须按实际 `<i>` 引用逐张盘点。
### 读 C# 参考并主动盘点音效

`D:\PVZ\PlantsVsZombies.NET-master\Lawn_Shared\Lawn\Zombie\Zombie.cs` grep 僵尸名，读 UpdateZombieXxx 状态机 + SetupReanimForLostArm/LostHead + 数值，先记录必须忠实的玩家行为，再按本项目当前所有权、更新、碰撞、动画、资源和存档契约实现；同时收集相关路径的全部 `PlayFoley` / `PlaySample`，不要等主人听出缺声才补。沿 `FoleyType → Sexy.TodLib/Foley/TodFoley.cs → Resources.SOUND_*` 得到精确资源键，以资源键去掉 `SOUND_` 后的小写名到 `D:\PVZ\中文年度加强版完整版\Test\sounds\` 查同名 `.ogg`。找到后复制到唯一权威 `build/clang-release/resources/sounds/` 合理子目录，并同步 `resources.xml` 与 `ResourceKeys.h`；找不到才问主人，禁止用相近声音静默替代。构建后检查 `manifest.txt` 和启动日志无 missing sound，并用可见行为路径及 `GetSoundPlayRequestCount` 投影验证触发次数（含读档不得重响）。出生/登场声若只属于“新刷新”，必须从 `Board::CreateZombie` 的正式新建路径调用品种钩子，禁止塞进 `SetupZombie()`（预览与 `CreateZombieWithID` 读档也会经过 Setup）。**警惕版本错位**：C# 是后期资源（舞王=disco 版有 `_upper_bone` 残肢轨；主人给的 MJ 版没有）——玩家可感知的状态机保持原版，断肢轨道和素材方案按手头 reanim 实际能力适配。
### 盘点已就位基建

（常常提前有了，别重复加）：`ZombieType.h` 枚举（**多半在 `NUM_ZOMBIE_TYPES` 哨兵之后，要移进去才可出怪**）、`TestDriver.cpp` kZombieNames、`GameScene.cpp` kDevZombieTable、`AnimationTypes.h`、resources.xml 的 `<Reanimation name>`、粒子贴图。缺哪补哪。**reanim 文件进入 manifest 不等于已注册**：新角色必须同时把文件放入权威 `resources/reanim/` 并在 `resources.xml` 写 `<Reanimation name="...">`；漏后者会让 `AnimatedObject` 得到空 Animator，僵尸构造阶段访问轨道时直接 Access Violation。AutoTest 在首次直造前用 `ResourceManager::HasReanimation` 断言注册键。不要给 `Zombie`/`AnimatedObject` 基类加宽泛空 Animator 早退来“止崩”，`Start()`/`SetupZombie()` 仍会访问它，且坏注册会被掩盖。每个可直造的新类型必须在同一改动同时加入 `TestDriver.cpp` 的 `kZombieNames` 与 `GameScene.cpp` 的 `kDevZombieTable`；提交前比较两表的新类型集合，不能用 `spawn_zombie` 专项通过替代开发者面板登记。开发者面板会把所选僵尸的**枚举名字符串**写入 `PlayerInfo.json`；禁止改存表下标或枚举整数，因为新类型移入哨兵前会让旧数值漂移。新增表项继续用 `DEVZ(ZOMBIE_X)`；需要自动回归时，让 `smoke_develop` 实际选到该类型并断言召唤及跨场景重建仍保持，不能只循环到早期旧类型。
### 若任务含冒险出怪表

按 [references/adventure-spawnlist-pacing.md](../references/adventure-spawnlist-pacing.md) 核对受影响关卡及相邻奖励/教学关系。重排整大关时再检查完整序列，单关调参不机械生成整大关文档。
### 若僵尸会部署脱离建造者后持续存在、独立承伤或拦截弹道的世界对象

完整阅读 [references/persistent-built-world-object.md](../references/persistent-built-world-object.md)，先定所有权、原子提交、碰撞优先级与双方存档边界，再写状态机。

## 实现清单

### 类

`Game/Zombie/<Name>.h/.cpp`，抄最像的现有僵尸（护盾换图=PaperZombie；状态机+覆写碰撞=Polevaulter；手臂显隐对称钩子=DoorZombie；召唤/编队=DancerZombie+BackupDancerZombie）。HP/速度硬编码在 `SetupZombie()`（不在 gamedata.json）。
### SetupZombie 先判定“复用父类”还是“完全接管”

- **同一套 reanim/轨道/事件时序的换皮或数值变体**，优先调用最近父类的 `SetupZombie()`，再覆盖 HP、攻击和速度差额；这样直接复用已经验证过的 Die/EatTarget 事件，禁止重复 `AddFrameEvent`。父类已经乘过移速时，用“目标倍率 / 父类倍率”补差（粉色橄榄球 1.85/1.7 实证），不要再乘完整目标倍率。
   - **新 reanim、事件时序不同或状态机需要替换父类初始化**，才不调基类并自己接管三件事：帧事件注册（Die 一次性 + EatTarget `repeating=true`，帧号=全时间线绝对帧，只在所属剪辑段播放时经过）、走路起播、`mIsPreview` 分支（预览只 PlayTrack 不注册事件）。`AddFrameEvent` 回调走一个指针大小的内联存储，只允许无捕获或 `[this]` 这类可无异常复制、平凡析构的小回调；额外上下文放宿主字段并只捕获 `this`，禁止扩大捕获后改回每事件 `std::function`/堆分配。任何新增帧号仍必须先问主人。
   - **预览运动不能假定会进入品种实战更新**：`Zombie::Update()` 对 `mIsPreview` 只推进 `AnimatedObject`，不会调用 `ZombieUpdate/ZombieMove`。若上下跳、附件或阶段姿态由品种状态机而非 Animator 自身产生，覆写 `Update()`，先调 `Zombie::Update()`，再只为非 UI 大图推进无碰撞、无位移、无音效的展示状态；图鉴网格设置 `mIsUI + PauseAnimation()` 时必须继续静止。AutoTest 分别在选卡与图鉴详情导出相对高度/阶段整数投影，不断言运动对象绝对 X/Y。
   - **预览的无 `Board` 不是动作失败**：品种 `Update()` 的终止/耗尽判定若把 `mBoard == nullptr` 视为异常，必须先为 `mIsPreview` 早退到纯展示更新；否则首帧会把预览初始 `anim_idle` 改成实战 `anim_walk`，还会重新启动已暂停的图鉴网格 Animator。专项至少在一帧以上后同时断言网格保持暂停、详情保持指定轨道，并覆盖默认与 `-NoInstance`。
   - **僵尸自身整体动画倍率只有一个出口**：覆写 `GetAbilityAnimSpeedMultiplier()`，固定品种值直接返回常量，阶段能力从已保存状态派生，出生随机值存派生类字段并由 `SaveExtraData/LoadExtraData` 持久化。状态变化后调用 `UpdateAnimSpeed()`；禁止子类直调 `Animator::SetExtraSpeedMultiplier()` 或再造一份通用基础倍率字段。`mSpeed` 只表示额外水平位移，`PlayTrack(..., clipSpeed)` 只表示当前轨道绝对速度。
   - **Board 权威倒计时必须与专属一次性动作同长时**，在该阶段通过基类的固定 extra 倍率虚入口收敛动画速度，让它先于冻结、黄油、麻痹、减速与天气倍率生效；退出阶段再 `UpdateAnimSpeed()` 恢复普通组合。只绕过动画推进，移动和啃食仍由状态机独立禁止。不要只把轨道 clip 调成目标 FPS，否则大雨或减速会让一秒动作提前结束或播不完。AutoTest 在有控制状态和非 1.0 天气倍率下同时断言倒计时、轨道与 `animExtraSpeedPct`。
   - **带 `_ground` 的根运动僵尸禁止把目标世界速度直接写进 `mSpeed`**：`GetTrackVelocity()` 已包含逐帧根位移与 `EffectiveSpeed()`，`ZombieMove()` 随后才乘 `mSpeed × delta`；因此 `mSpeed` 应承担资源 FPS 的时间基准换算，品种快慢主要用与 C# `mVelX` 对齐的 clip 速度同步改变步频和位移。直接把 42/54px/s 填给 `mSpeed` 会让身体滑行。对外提供投手、倭瓜或数值推演使用的未来水平速度时，必须对当前活动片段的 `_ground` 逐帧位移取平均，再组合能力、减速、天气、风力和场地倍率；禁止把当前单帧速度外推整段时间，否则非匀速步态会在不同帧随机前后偏移。AutoTest 至少断言 `effectiveAnimSpeed`，并从池外稳定起点检查进入泳池前仍保留可见步行时长；有固定飞行时间攻击时，再跨多个步态相位锁定相对提前量。
   - **非魅惑状态也会反向移动的品种必须集中方向权威**：在 `Zombie` 提供 `IsMovingRight()` 虚入口，让基类位移、台风顺逆风、目标提前量和房屋失败线都查询它；子类只按当前 phase/阵营覆写一次。禁止仅在子类 `ZombieMove()` 里改正负号，否则会出现“视觉向右但仍触发进家”或阵风/索敌预测反向。AutoTest 同时断言方向、位移趋势与 `CanTriggerGameOver()`，并在房屋附近验证反向阶段不触发失败。
   - **跨轨道换态先核对视觉坐标系，不能只看逻辑位移**：从同一身体部件轨读取旧轨末帧与新轨首帧锚点，再叠加各阶段 `mVisualOffset`/视觉补偿，世界提交位移要抵消总差；海豚 `anim_dolphinjump→anim_swim/anim_ride` 当前实测分别需 104/106 px，原版 94 只复制逻辑位移会在落地时倒退。若换态同帧撤销视觉补偿，禁止继续 blend 旧姿态（海豚 `anim_ride→anim_walkdolphin` 上岸须零混合），否则旧姿态会被新坐标绘制成短暂垂挂。C# 的 `mUsesClipping` 也不等于通用水线裁剪：海豚入水只在 0.56～0.65 与 0.75～结束使用较低的局部底线，中间关闭；在本项目用派生类裁剪钩子复刻并截图校准，默认实现必须保持其他水中僵尸原样，禁止通过隐藏海豚部件轨或裁整只僵尸冒充。
   - **内嵌 reanim 部件切换为独立实体时也要接续视觉锚点**：在隐藏旧部件前取得双方同一语义轨道的实际渲染世界原点（含各自 `mVisualOffset`、Transform scale、`SetFlipX` 支点和最终 render scale），新实体完成轨道、阵营与镜像配置后再补偿差值；原版逻辑出生量可保留作创建基准，但不能代替两套资源原点换算。只校正主人指出的轴，抛物线高度等由独立玩法合同继续拥有。AutoTest 应在既有切换事件前推进正式路径，以同一状态下的相对锚点整数投影覆盖正向/魅惑镜像，并配合默认与 `-NoInstance` 同步截图；不要断言运动对象的绝对世界坐标。
   - **把父类资源选择改成虚入口时必须双向回归**：派生类换色通过后，仍要触发父类的受损帽、残肢、掉落粒子和读档终态；新增虚入口可能把父类原先未被断言的错误键暴露出来。AutoTest 分别导出父类与派生类的实际资源键加载状态，不能只证明精英路径。
### 枚举移动 + 空工厂窗口

新类型必须**追加在全部既有已实现类型之后、`NUM_ZOMBIE_TYPES` 哨兵之前**；禁止插进旧类型中间，否则存档里的整数僵尸 ID 会错位。把枚举移到哨兵前的**同一提交**必须补齐权威 `gamedata.json` 条目（缺字段拒启动 exit -6）；若工厂注册在后续提交，**weight 先填 0**（哨兵前+非零权重+无工厂=生存随机抽中即空指针），注册后再解封。
### 注册

`GameDataManager.cpp` `#include` + `RegisterZombie(type, "ZOMBIE_X", ANIM_X, "ReanimName", &MakeZombie<T>)`——animName 必须与 resources.xml 的 `<Reanimation name>` 一致。
### gamedata.json

只改 `build/clang-release/resources/gamedata.json`，`{weight, appearWave, survivalRound, offset, scale}` 五字段缺一不可；只能被召唤的僵尸 `weight: 0`（永不被抽中，AutoTest spawn_zombie 仍可直造）。注意 weight 一物两用=抽中权重+生存点数成本。
### 粒子

照抄 `ZombieHeadOff.xml` 改 `<Name>`+`<Image>`（图键=贴图文件名的标准派生键，如 `ZombieDancerHead.png`→`PARTICLE_ZOMBIEDANCERHEAD`），放权威 `build/clang-release/resources/particles/config/`，其他 preset 自动共享。新粒子专用 PNG 还必须登记进 `resources.xml` 的 `<ParticleTextures>`；只有文件和 manifest 不会加载出 `PARTICLE_*` 键。XML 标签全参考/foot-guns 见 **adding-particle skill**（常规配置查标签参考；语义冲突或消费端变更时核对 ParticleSystem 源码）。
### 换色变体资源

优先用仓库内 PowerShell + `System.Drawing` 脚本按 HSV/亮度映射目标材质，保留原 Alpha、阴影、高光、描边和非目标部件；不要对整张图平涂或只靠 overlay。脚本是可复现源，只向 clang-release 权威资源生成一次。先把 reanim 全部 `<i>` 引用建立源→目标表，尤其逐张处理阶段合成图；空间/低饱和度遮罩必须在原分辨率与原图并排检查，防止把眼白、灯泡、牙齿等一起染色，**视觉检查通过后**才把最终 SHA-256 写回脚本并复跑锁定。批量替换 reanim 键必须按完整 token 或最长名称优先，禁止让短前缀误命中共享长键（如 `..._BASKET` 把未换色的 `..._BASKETBALL` 一并改名）；生成后列出全部新 `IMAGE_REANIM_*` 引用并检查启动日志无缺图 WARN。换色后还要沿死亡/受击入口检查粒子 XML 的每个车辆或身体部件 `<Image>`：本体 reanim 换色不会自动替换粒子里写死的普通资源键。**仅存在于 `image/reanim/` 但未被 reanim XML 引用的受损帽、残肢等运行时换图，manifest 驱动的启动扫描会生成文件标准键 `IMAGE_<UPPERCASE_STEM>`，不是 `IMAGE_REANIM_*`；后者只由 reanim loader 为时间线实际引用的图片建立。** 用 `GetTexture(key, false)` 导出加载断言，同时覆盖正常换图和读档重建。同一 reanim 只替换头部等分阶段部件时，把材质选择收敛到父类按伤势阶段查询的虚入口，并让 Setup、实时受伤、`ZombieItemUpdate()`/Load 共用；健康、轻伤、重伤逐档断言，避免初始换色在重伤换图或读档后退回父类颜色。派生换色品种的断肢应由父类虚入口同时选择“本体残留材质”和“飞出粒子效果”，并让 `ZombieItemUpdate()` 复用同一材质入口，避免受伤或读档时短暂变回普通配色。
   - **新增标志性部件不能退化成平涂矩形或儿童画轮廓**：先在 4× 工作分辨率沿既有材质语言补壳体厚度、倒角、铆钉、玻璃高光、内部层次、线缆与磨损，再高质量缩回最终贴图；细节取舍以游戏内约 40～100 px 合成尺度仍能辨认的轮廓、明暗层级和一两个身份锚点为准，不能只验高分辨率母图。生成脚本保留 Alpha、描边与阴影，并用夜间背景同步截图检查最终合成。
### ⚠️ build/ 下资源提交必须 `git add -f`

——被 .gitignore 静默挡下，`git commit` 照样"成功"但文件没进去。提交后 `git show --stat` 核对文件数。
### 图鉴

在权威 `build/clang-release/resources/info.txt` 同时添加 `[ZOMBIE_X]` 与 `[ZOMBIE_X_DESCRIPTION]`。`ZombieAlmanacScene` 按 `mAdventureLevel - 1` 之前已通关关卡的 `spawnlists.json` 并集解锁条目，并按首次遭遇顺序排列；当前正在玩的关卡不得提前泄露。召唤型 `weight: 0` 子单位（如伴舞）不能为了图鉴解锁写进随机池，而应由图鉴的“必然派生遭遇”映射随其召唤者解锁；固定 BOSS/关卡槽位同样不应伪造随机池权重，而要在对应关卡通关边沿按确定性遭遇映射加入。概率变异不能由 spawnlist 推断；若要求实际遇见后永久解锁，把独立遭遇标记存入 `PlayerInfo.json`，且只在正式波次的实际类型成功创建后记录，不能在 roll 命中、通用 `CreateZombie()`、读档或预览路径记录。缺 info key 不会构建失败，只会留下有图无标题/正文的空白条目；因此静态检查每个可解锁枚举名的两枚 key 均存在、非空且唯一。AutoTest 用 `set_adventure_level` 配合 UI 场景状态字段 `zombieAlmanacEntries` / `zombieAlmanacSelected`，同时断言当前关排除、下一关解锁并截图；AutoTest 会短路真实 PlayerInfo 磁盘写入，遭遇持久化须用内存字段断言加保存/加载源码审查。

## 冒险出怪编排

修改 `spawnlists.json` 时完整遵循 [references/adventure-spawnlist-pacing.md](../references/adventure-spawnlist-pacing.md)。核心不是“把新僵尸塞进一个可生成关卡”，而是围绕玩家在**关卡开始时**已有的植物，给重点敌人安排独立教学、无同场复习和最终综合，并用精简池保证重点敌人的实际抽中率与选卡预览可读性。

### 未定 BOSS 只登记关卡槽位

在 `AdventureProgression` 用 `BossSlot::RESERVED` 标记关卡性质，背景映射继续独立决定；禁止为了“先占位”把现有 `ZOMBIE_BOSS`、其他普通僵尸或伪造枚举写进出怪表。等主人明确身份、机制与投放时，再把槽位升级为具体契约并补正式生成、存档和 AutoTest。
### 常驻 BOSS 血条属于场景表现，不复制战斗状态

`GameScene` 每帧从 Board 当前有效实体派生可见性、当前/最大生命与阶段阈值；阶段阈值由 BOSS 暴露语义 getter，禁止 UI 再写一套魔数。热路径查找不得创建临时僵尸向量，可增加无分配的 `EntityRegistry` 语义查询或缓存实体 ID，并排除预览、垂死和已死亡对象。血条本身无需入档，实体恢复后自然重建；AutoTest 至少锁定出场前隐藏、正式最终波出场、满血与跨阶段比例/标线、快照往返、致死隐藏，并用同步截图校准是否遮挡最下路与底部关卡信息。
### 稀有僵尸的锁定信息属于派生场景表现

威胁线、目标名或阶段提示只在 Board 仍能解析出有效锁定实体时绘制；未锁定状态连占位文案和对应面板高度都移除，禁止常驻“未锁定”泄露未来机制或遮挡战场。展示每帧消费 Board 语义 getter，不复制锁定 ID、生命或存档字段；专项用锁定前后状态投影和同步截图同时确认文字与底板高度。
