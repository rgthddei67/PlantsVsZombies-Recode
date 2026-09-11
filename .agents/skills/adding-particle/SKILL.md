---
name: adding-particle
description: Use when adding or tuning ANY particle effect (粒子特效) in PvZ — 新建/修改 resources/particles/config/*.xml、染色变种、爆发云/掉落物/命中飞溅配方。全部标签语义已从 ParticleXMLLoader/ParticleEmitter 源码实证，写粒子不用再读源码。
---

# 给 PvZ 写粒子特效（XML 配置全参考）

装备缩小时也核对掉落和带装备断头效果的最终尺寸；运行时 follower 缩放不会自动传给独立粒子。以场上合成尺寸对照粒子自身缩放和贴图尺寸，避免装备脱落后突然变大；同步更新确定性生成器及经过审核的输出哈希。

本文件每个标签的语义都是 2026-07-15 从 `ParticleSystem/` 源码逐行实证的（IceFumeCloud 蓝色孢子云实战），
2026-07-16 随毁灭菇 Doom.xml 移植更新（ImageFrames 序列帧实装 + 原版 XML 移植口径），
2026-07-20 随雨天特效补充 ParticleRotation 初始朝向，
2026-07-27 补充 AutoTest 最终粒子矩形与相对实体取证，
2026-08-01 补充 manifest/注册/运行时键闭环与多根 XML 校验口径。
**改了引擎消费端（ParticleEmitter/ParticleXMLLoader）要回来同步本文档。**

## 原版参考边界

- 原版 XML、C# 触发代码和实际画面用于确定玩家可感知的功能契约：何时出现、由谁触发、包含哪些视觉层、持续多久、如何运动以及配什么声音。没有主人批准时，成品功能与反馈不得自行删减或改义。
- 移植前逐项核对本引擎实际支持的标签、时间单位、坐标系、摩擦/字段语义、资源键和生命周期。原版参数只有在两边语义一致时才可原值采用；不一致时必须按当前引擎重建等价表现，禁止把 XML 当作可机械转写的数据表。
- 实现差异必须由状态投影、最终世界矩形和同步截图共同证明仍符合原版表现；单纯“成功加载”或逐项改写数字不算行为等价。

## 心智模型

- **一个 XML 文件 = 一个特效**，可含多个 `<Emitter>`（同时全部点燃，如 PeaBulletHit=飞溅+碎屑两发射器）。
- **特效名 = 第一个 `<Emitter>` 的 `<Name>`，不是文件名**（文件名只是惯例上取一致）。后续 Emitter 的 `<Name>` 不会注册为独立 `EmitEffect` 键；需要两个可单独触发的名称时，必须拆成两个 XML 文件。
- 目录：权威资源 `build/clang-release/resources/particles/config/`，启动时全目录加载；其他 preset 通过 Junction 共享，纯数据改配置**不用重编译，但要重启游戏**。
- 触发：`g_particleSystem->EmitEffect("Name", GetPosition());`；完整可选参数依次为 `renderOrder, durationOverride, clipRightX`。名字打错启动不报错，**发射时** run.log 出 `ERROR 找不到粒子特效配置`。
- 贴图：`<Image>` 填资源键（`IMAGE_*`/`PARTICLE_*`）；**没有独立粒子贴图格式**，任何在发射前已经加载的纹理都能当粒子。
- **键来源必须与加载路径一致**：`<GameImages>` 预加载的图用 `IMAGE_*`，`<ParticleTextures>` 用 `PARTICLE_*`；粒子专用 PNG 必须显式列入 `resources.xml` 的 `<ParticleTextures>`，只有文件与 manifest 不会加载。`image/reanim/` 会由 manifest 驱动的启动扫描生成标准 `IMAGE_<UPPERCASE_STEM>` 键；reanim XML **实际引用**并已加载的部件才另有 `IMAGE_REANIM_*` 别名。动态发射前若对应 reanim 不保证已加载，就不能依赖别名，应改入预加载段。写错前缀或时序未加载=粒子静默不生成（foot-gun ③）。
- **分份贴图**：`<Texture Column="4" Row="1">` 会把图切成独立纹理 `PARTICLE_XXX_PART_0..3`（`基础键_PART_序号`，行优先）——逗号列出来即"每粒子随机一张"（splats 碎屑的原理）。**序列帧动画别用它**，用 `ImageFrames`（整图不切，见标签表）。

## 坐标换算铁律

**C# 原版逻辑场景是 800×600，本项目是 `SCENE_WIDTH=1100`、`SCENE_HEIGHT=600`。原版粒子发射坐标、Emitter/SystemPosition 偏移、全屏边界和裁剪值都只能当语义参考，禁止直接抄数值。**

- 先确定特效应锚定当前对象的稳定视觉原点、Board 网格点还是当前场景边界，再把 C# 点位换算成相对该锚点的局部差值。
- `EmitEffect` 的传入世界坐标优先由 `Transform + mVisualOffset`、植物视觉基点、`GetCellCenterPosition` 或 `SCENE_WIDTH/HEIGHT` 派生；受伤抖动等临时绘制偏移默认不进入粒子物理位置。若 XML 的 `EmitterOffsetX/Y` 已按实体视觉原点写入完整偏移，代码只能传逻辑 `Transform`（再加能力自身的动态高度），禁止又叠 `mVisualOffset`；先把“代码世界锚点”和“XML 局部偏移”写成一条加法式，避免同一视觉偏移计算两次。
- 头、手臂等动画部件掉落优先使用该部件轨道的世界坐标（如 `GetTrackWorldPosition`）作为发射点，并把 XML 的 `EmitterOffsetX/Y` 归零或只保留粒子自身微调；禁止用整身视觉原点再猜一组固定偏移。轨道世界坐标通常是贴图变换原点而非视觉中心：若掉落图原先以 follower 绘制，先用“父轨原点 + follower 局部偏移 + 运行缩放后的贴图中心”求发射中心，再交给零偏移粒子。穿戴比例与离体粒子比例分别校准，不能把较大的装备 follower 缩放原样复制给掉落粒子。轨道锚点应在隐藏/换材质前读取，AutoTest 用粒子 `worldBounds` 与最近实体 collider 的相对中心和相交关系验收。
- 碰撞提示若与同一次伤害结算绑定，先从目标 collider 捕获中心或接触点，再调用可能消费状态或致死的响应/伤害入口；在碰撞提交边沿用该快照发射，禁止伤害后再解引用目标求位置。AutoTest 要验证 `nearestPlant` 时使用能存活到取证帧的靶子；致死专项改断言发射原点与 Board 格位，避免目标已失活后最近实体投影为空。
- 池沿、地形边界等状态切换特效必须复用决定切换的同一几何：通用僵尸入水用前后探针中点作 X、水面裁剪底线作 Y，禁止另抄一组 C# 800×600 点位。若原版表现同时含短 `AnimatedObject` reanim 与 ParticleSystem XML（`PoolSplash` 实证），两层保持独立资源与生命周期；AutoTest 分别断言 `animatedObjectTagCounts` 和 `particleEffectNameCounts`，不能只证明其中一层。
- XML 的 `EmitterOffsetX/Y` 仍有“双倍生效”陷阱，但“除以 2”只能用于**完成坐标系换算后的局部偏移**，不能把 800×600 原值直接减半搬入。
- 触发后先执行同步 `screenshot`，再用 `particleEffectsByName.<Name>.0` 的 `renderProbeReady/worldBounds/originToRenderCenterD*Int/nearestPlant/nearestZombie` 断言本项目实际提交的粒子矩形相对发射原点与实体 collider 的关系；随机范围用宽容区间而非单点值。
- 先确认视觉对象是不是 ParticleSystem XML：若代码用 `AnimatedObject(ObjectType::OBJECT_PARTICLE)` 播 reanim，它不会出现在 `particleEffectsByName`，应按自定义 tag 从 `animatedObjectsByTag` 取证；若只是实体 `Draw()` 中直绘的状态贴图（如僵尸脚底冰晶），则既不属于 ParticleSystem，也不会出现在这两类投影。后一类必须审计普通 batch 与 reanim instance 的跨队列顺序：默认实例路径的相对层级贴图用 `DrawTextureInstanced`，`-NoInstance` 才用 `DrawTexture`，并在对象数跨并行阈值后做无 glow/glow/结束三态截图。方向性命中特效的局部 X 偏移要按来源速度符号镜像，并断言 `nearestZombie.centerDxInt/boundsIntersect`，不能只凭截图估计。
- `clipRightXInt` 是裁剪语义，`worldBounds` 是裁剪前提交几何；两者分开断言。运动对象瞬时绝对 X/Y 只供诊断，不作稳定断言。

## 数值语法（三种，核心）

1. **标量**：`1.5`。
2. **随机范围 ValueRange**：`[60 100]`（空格分隔，方括号）→ **生成/初始化时随机一次**，之后不变。裸写 `16 32` 也会被读成范围 [16,32]。
3. **插值轨迹 InterpolationTrack**：关键帧序列 `值[,时间%] 值[,时间%] ...`，按粒子寿命归一化线性插值：
   - `1,80 0` = 前 80% 寿命恒 1（端点外**夹住**不外推），80%→100% 线性降到 0（淡出经典写法）。
   - `1 0` = 无显式时间 → 均匀铺 0..100%（=从 1 线性到 0）。
   - `0 [20 300],60 [25 310]` = 关键帧本身可以是区间；**Position 场里每颗粒子抽一个随机因子走自己的"轨道"**（云横向铺开的原理）；其他消费方取区间**中点**（确定性）。
   - `EaseOut` 等缓动词会被**跳过降级为线性**（解析器只认线性）。
   - 单独一个 `[a b]`（无时间）= 每粒子生成时随机一次、终生保持（ParticleScale 常用）。

## 标签参考（默认值 = 不写时的实际行为）

| 标签 | 类型 | 默认 | 实证语义 |
|---|---|---|---|
| `Name` | 字符串 | 必填 | EmitEffect 用的特效名（取第一个 Emitter 的） |
| `SpawnMinActive` | 范围 | 1 | **初始化瞬间爆发**的粒子数；**不计入 MaxLaunched 配额** |
| `SpawnMaxLaunched` | 范围 | 1 | 默认是 `SpawnRate` 涓流的总配额；若 `EmitEffect` 传入正的运行期时长，则发射器改为循环模式，此值变为可复用粒子池容量，应略高于峰值同时存活数 |
| `SpawnRate` | int/秒 | 0 | 持续每秒生成 N 颗；0=只有初始爆发 |
| `ParticleDuration` | 范围(秒) | 1.0 | 单粒子寿命；所有插值轨迹按它归一化 |
| `SystemDuration` | float(秒) | -1 | 到时停止发射（已有粒子自然消亡）+ SystemAlpha 的归一化基准。**见 foot-gun ①** |
| `ParticleAlpha` | 轨迹 | 1 | 透明度 0..1 |
| `ParticleScale` | 轨迹 | 1 | 尺寸倍率（乘贴图原始大小） |
| `ParticleStretch` | 轨迹 | 1 | 仅纵向(高度)拉伸倍率 |
| `ParticleRed/Green/Blue` | 轨迹 | 1 | **乘法染色 0..1**（染色变种不用做新贴图！IceFumeCloud=R.35 G.35 B1） |
| `ParticleBrightness` | 范围 | 1 | RGB 整体乘数（生成时随机一次） |
| `SystemAlpha` | 轨迹 | 1 | 整团透明度，按 `systemTimer/SystemDuration` 采样（没写 SystemDuration 则恒取 1） |
| `EmitterType` | Point/Box/Circle | Point | 出生点形状；写了 `EmitterRadius` 未写 Type 自动当 Circle |
| `EmitterBoxX/Y`、`EmitterRadius` | 范围 | 0 | Box 半边长 / Circle 半径（逐粒子随机） |
| `EmitterOffsetX/Y` | float | 0 | 出生点相对 EmitEffect 坐标的偏移。**见 foot-gun ②：实际生效两倍** |
| `LaunchSpeed` | 范围(px/s) | 0 | 初速度大小 |
| `RandomLaunchSpin` | bool | false | true=初速度方向 360° 随机；**false=一律沿 +X（向右）** |
| `ParticleRotation` | 范围(**度**) | 0 | 贴图生成时的初始朝向，每粒子采样一次；显式配置时走“围绕世界中心先旋转、再拉伸”的路径，所以非等比 `ParticleStretch` 也不会压扁角度，适合斜雨丝 |
| `ParticleSpinSpeed` | 范围(**度**/s) | 0 | 贴图自旋（DrawTexture 走 glm::radians，配置里是度：碎屑 ±130、掉头 ±5） |
| `ParticleGravity` | float(px/s²) | **0** | 每帧 `vy += g*dt`；头文件成员默认写 100 是幌子，loader 无标签时 as_float(0) 覆盖 |
| `Image` | 资源键 | 无 | **必填**：没有它粒子直接不生成（静默）；逗号分隔多个=每粒子随机选一张（碎片直接复用 splats 系列） |
| `ImageFrames` | int | 1 | **序列帧动画**（2026-07-16 实装）：贴图为**横排帧条**，帧宽=图宽/N（毁灭菇爆炸底座 471×85=3 帧翻滚蘑菇云）。绘制取当前帧列，>1 才生效 |
| `AnimationRate` | float(帧/s) | 12 | 序列帧推进速度，到尾**循环**；仅在 ImageFrames>1 时有意义 |

### Field 场（每个 `<Field>` 一个 `<FieldType>` + 可选 `<X>`/`<Y>` 轨迹）

| FieldType | 实证语义 |
|---|---|
| `Position` | **绝对偏移**（非累加）：`fieldOffset = 轨迹值`，叠加在物理位置上绘制。区间关键帧→逐粒子随机轨道（铺开的云/喷雾主力） |
| `Shake` | 每个推进游戏时间的逻辑步在 ±X/±Y 内均匀随机抖动（绘制偏移，不动物理位置）；暂停时保留最后一次采样，不随 UI 更新重抽 |
| `Friction` | 每个推进游戏时间的逻辑步 `v *= (1-x)`——**逻辑步率相关**的衰减，0.1 就已经很强；暂停时不继续衰减 |
| `Acceleration` | `v += x*dt`，帧率无关的恒加速（比 Gravity 多了 X 分量） |
| `Circle` | 相对发射器系统中心沿单位切线推进 `(X + radius*Y)*dt`；负 X 与 `Away` 组合可形成原版模仿烟的旋转扩散，出生点必须先有非零半径 |
| `Away` | 相对发射器系统中心沿单位径向推进 `(X + radius*Y)*dt`；正 X 向外扩散、负 X 向内收拢 |

### 解析了但引擎不消费（写了无效，勿浪费时间调）

`<SystemField>`、`FieldType=SystemPosition`（移动整个发射器——移植原版 XML 时把它的常量偏移**折算进 EmitterOffset 并减半**，见移植口径）、
`<FullScreen>`（全屏绘制——等效替代：`ParticleScale 4000` 的 WhitePixel 巨quad，Doom 紫闪实证）。
（`ImageFrames`/`AnimationRate` 已于 2026-07-16 实装，移入上方标签表。）

## 生命周期与渲染层

- 回收条件：发射停止（SystemDuration 到时 或 涓流配额打满）**且**存活粒子归零 → 特效对象自动销毁。`EmitEffect(..., durationOverride > 0)` 会让发射器持续复用池，直到覆盖时长到期再停止。
- `EmitEffect` 第三参默认 `LAYER_EFFECTS_WORLD`(35000)=世界层（植物/僵尸之上、UI 之下，GameAPP `DrawBelow(LAYER_UI)`）；传 `>= LAYER_UI` 的值则画在 UI 之上（Scene `DrawFrom(LAYER_UI)`）。
- `EmitEffect` 第五参 `clipRightX` 默认 -1（不裁剪）；传非负世界 X 后，本特效会与现有裁剪栈相交并仅绘制 `x<=clipRightX`。横向喷雾遇实体阻断时，先按传播顺序结算并取阻断者 collider 左沿，再把同一 X 传给粒子；**不要**改 Position 轨迹或维护多份长度 XML。
- `EmitEffect` 创建后的特效保存世界坐标，不会自动跟随发射者 Transform。移动实体的尘土/尾迹应按短间隔在当前稳定视觉原点重复发射短寿命爆发；不要用长 `durationOverride` 生成一个停在旧世界原点的持续效果。
- 发射、寿命、物理、动画和全部 Field 都只在缩放后的游戏时间推进时更新；暂停期间仍会运行 UI 逻辑步，但 `ParticleEmitter::Update` 必须完整保留粒子状态，尤其不能让 `Shake` 重抽或 `Friction` 继续衰减。倍速/timescale 仍由 DeltaTime 驱动。

## Foot-guns（血泪汇总）

1. **爆发型特效必须写 `SystemDuration`**：`SpawnRate=0` 时发射器的"配额打满"判定永远不成立（初始爆发不计入 particlesEmitted），没有 SystemDuration 的特效对象**永不回收**、每帧空转。所有现存配置都带它（≈ 最长粒子寿命 + 一点余量）。
2. **`EmitterOffsetX/Y` 生效两次**：ParticleEffect 定位发射器时加一次，每次 spawn 取出生点又加一次——FumeCloud 写 25 实际前移 50px。调偏移按"写入值 × 2"心算，或干脆改 EmitEffect 传入坐标。
3. **没写 `<Image>`、键打错或贴图未登记加载 = 粒子静默不生成**（这是刻意设计，供"纯计时"发射器用），特效"发了却看不见"先用 `GetTexture(key, false)` 断言资源存在，再查 `<ParticleTextures>` 与键前缀；特效名打错才有 run.log ERROR。
4. 染色走 `ParticleRed/Green/Blue`（0..1 乘法），等价心算：目标 overlay 色 (80,80,255)/255 ≈ (.31,.31,1)。**别去做染色贴图**。
5. `RandomLaunchSpin` 不写时初速度**恒向右**——掉落物（头/手臂）必须写 `1`，否则一律向右飞。
6. Position 场是**绝对偏移**：想让粒子"随时间飘远"，轨迹要从小值渐变到大值（`0 [20 300],60 ...`），写常量它就钉在那不动。
7. XML 只放权威 `build/clang-release/resources/particles/config/`；改完**重启**游戏才生效（启动时一次性加载）。
8. **长持续天气不要用总配额硬撑时长**：调用 `EmitEffect` 时传正的 `durationOverride`，引擎会把发射器切成循环池；`SpawnMaxLaunched` 按 `SpawnMinActive + SpawnRate × ParticleDuration` 的峰值并留余量即可。否则巨大配额会让每帧遍历成千上万个空粒子。
9. **负数随机区间写升序** `[-300 -200]`：原版 XML 里的 `[-200 -300]` 直接照抄会把 min/max 反着喂给 GameRandom::Range，行为未定义。
10. **移植原版 XML 只改权威资源**：贴图和 `resources.xml` 都放 `build/clang-release/resources/`，禁止再创建 `clang-debug` 副本；配置其他 preset 后用 Junction 属性确认共享即可。
11. **换色实体的部件粒子必须逐键审计**：reanim 换色不会改写死亡/受击粒子 XML 中固定的 `<Image>`。若爆炸会抛出车盖、车轮、帽子或手臂等部件，为变体建立独立效果名和配置，并把所有实体部件键换为变体资源；触发端的父类虚入口必须同时选择本体残留材质与飞出粒子，读档重建也复用该材质入口，避免受伤/读档回普通配色。普通烟云可用 RGB 轨迹统一染色。
12. **爆炸云的原版高阻力曲线不能直抄**：本引擎 `Friction` 是逐帧相乘，`.15,40 1` 会在极少数帧内把速度压到零，即使 `LaunchSpeed` 很高也只会聚成中心小团。需要持续向外扩散的云优先用约 `0.015～0.02` 的低恒定阻力，再以发射半径、速度和寿命调覆盖面；高阻力只留给需要立刻刹停的命中碎屑。
13. **粒子配置是多根 XML 片段**：一个文件可直接并列多个顶层 `<Emitter>`，因此用 PowerShell `[xml]` 或普通单根 XML validator 会报“已有 DocumentElement”。不要给实际配置包一层虚构根节点；静态校验时只在内存字符串外临时包 `<Root>...</Root>`，最终仍以 `ParticleXMLLoader` 启动加载和 AutoTest 发射为准。
14. **乘法染色不能凭空补颜色通道**：高饱和绿色飞溅要变成紫色时，若源图蓝/红通道接近 0，`ParticleRed/Green/Blue` 只能压暗，无法正确换相。用可复现脚本从权威图集生成同尺寸、同行列布局的独立换色图集，注册到 `<ParticleTextures>` 后让派生效果改用独立分片键；同步截图后断言全部分片加载、派生效果计数和基础效果计数互斥，并跑基础效果回归防止串色。
15. **运行时装备换色的粒子必须跟随当前样式**：初始普通、能力触发后变色的装备不能让派生类永久固定使用变体掉落效果；由装备状态统一选择普通/变体效果名，且两份 XML 的 `<Image>` 分别指向对应损伤阶段。若能力提示直接复用缩小装备贴图，使用发射前已强制加载的稳定 `IMAGE_*` 键，并断言能力粒子活动数、初始/变色贴图键和破损态切换截图，避免只看到特效却没证明本体同步换色。
16. **普通爆炸与专属灰烬必须在死亡入口互斥**：若同一实体有“普通死亡发粒子”和“灰烬死亡建独立残骸”两条表现，先按伤害语义选唯一分支，再移除本体；禁止通用 `Die()` 先发爆炸、灰烬覆写随后又补残骸。原版若让一次性植物在结算后立即 `Die()`，其短寿命灰烬、压扁主体与碎屑也应由同一复合粒子承担，禁止为了保留画面而让游戏植物继续占格。AutoTest 分别断言普通入口效果名恰为 1、灰烬入口同名计数为 0，并继续等待残骸自己的生命周期结束，不能只截两张看似不同的图；非占格爆炸粒子另需断言原格立即可种并用同步截图确认表现仍在。
17. **原版定向 `LaunchAngle` 不能直接移植**：本引擎没有该标签，`RandomLaunchSpin` 又只能在固定向右和 360° 随机间选择。单颗反弹飞行物需要稳定方向与弧线时，使用 `FieldType=Position` 的 X/Y 关键帧直接描述相对发射点的完整轨迹，再用 `ParticleRotation/ParticleSpinSpeed` 单独处理贴图朝向；发射点取实际飞行物中心，并用同步截图和 `originToRenderCenter*`/`nearestPlant.row,col` 验证方向与锚点。

18. **经典变体先找原版专属素材再考虑程序染色**：搜索 C# 资源包及现有权威资源中的同名 PNG/XML；若原版已有独立帧条或碎屑图集，按原布局和哈希导入并只做当前引擎必需的时间、名称与注册适配，禁止重新生成或用基础粒子乘色冒充。只有确认没有专属素材且乘法染色能够保留目标通道时，才选择染色变体。
19. **多 Emitter 仍只有首个 Name 是特效实例名**：第二个及后续 Emitter 会与首个一起生成粒子并增加实际 quad 数，但不会在 `particleEffectNameCounts` / `particleEffectsByName` 下形成独立效果键，也不能被 `EmitEffect("第二个Name")` 调用。增强旧特效时即使新增的芯层或帷幕更适合先绘制，也必须让首个 `<Name>` 保持调用端使用的正式效果名；否则发射会静默失败。两种需要在不同时机单独触发的反馈必须拆成两个 XML；负例零键只为真正独立调用的效果名预置。验证光环、芯层等同时附加发射器时，断言首个 Name 的实例数、总 quad 数与同步截图，不要伪造第二个 Emitter 的零键。
20. **原版 `Circle`/`Away` 不是坐标轴场**：两者分别绕系统中心沿切线、径向直接推进位置；禁止为了通过现有解析器把它们改写成 `Position`/`Acceleration`，否则 `Circle X=[-140 -70]` 会被误画成左移 70～140px 的脱体烟团。当前没有 `Attractor` 字段；向内旋转的效果用负 `Away X` 收拢、再叠 `Circle X` 切向推进。未知 `FieldType` 必须告警并按 INVALID 忽略，不能静默降级成 Position。
21. **全屏降水要按实际场景高度验位移**：发射器放在屏幕上沿时，`Position Y` 的寿命末位移必须覆盖 600px 战场并留出出生框余量；只增加粒子数而位移不足，会让雨雪长期挤在顶边。天气由另一环境维度切换雨/雪时，触发端先停止旧效果、再用同一雨势的持续时长重建新效果；地面水花、雷电和环境音由各自系统显式门禁，不能指望粒子 XML 一并关闭。
22. **同轨迹的语义变体仍使用独立图集键和首个效果名**：普通弹与特殊弹可以复用分片列数、寿命和 Field 几何，但可复现脚本应生成不同文件名的图集，分别登记 `<ParticleTextures>` 并让两份 XML 的首个 `Name`/`<Image>` 全部独立；否则运行时换色或后续调参会串改另一弹型。AutoTest 同时断言两组关键分片加载、普通/特殊命中计数互斥，并在同步截图确认颜色与目标锚点。
23. **新增、改名或删除资源文件后先刷新 manifest 再启动**：`LoadAllImagesFromPath` 不直接枚举 NTFS，而是按 `resources/manifest.txt` 扫描；只改 PNG/XML 而不构建会让旧清单继续打开已改名文件，启动最终以资源失败退出，即使 `resources.xml` 已是新路径。正常流程至少执行一次会生成清单的目标构建；只做纯资源诊断时可用 `cmake/gen_manifest.cmake` 按当前权威 `build/clang-release/resources` 立即重建，再核对新键存在且旧路径消失。其他 preset 经 Junction 读取同一份清单，禁止各自维护副本。
24. **持续天气跨阈值不能靠大批 `SpawnMinActive` 起效**：同一帧初始化几十颗会让风雪、沙尘等整层画面突然弹出，即使单粒子自身有淡入轨迹也来不及掩盖。保留少量首发粒子，用 `SpawnRate` 在约一到两个粒子寿命内建立原设计峰值密度，并让触发端从危险线前的连续实数推导视觉强度或发射间隔；玩法阈值仍独立判断。专项至少同步截取刚开始与稳定成形两帧，并同时断言视觉强度递增和目标效果名存在，不能只看峰值截图。
25. **同心但独立旋转的机械层必须拆成透明贴图**：钟面、时针、分针等需要不同角速度的部件各自使用以完全相同画布中心为枢轴的透明 PNG，再由同一效果的多个 Emitter 以相同 `ParticleScale` 叠合；禁止把指针烘进钟面后用整图自旋冒充倒计时。正反方向和快慢差由各自 `ParticleSpinSpeed` 表达，首个 Emitter 仍保留正式效果名。移动目标用略长于重发间隔的短寿命效果在当前视觉原点周期续显，不能用一个长寿命世界粒子留在旧坐标；AutoTest 连续截取两帧核对两针确实产生不同角位移，并分别断言三张资源键加载。

## 配方（照抄改数）

**一次性爆发云**（FumeCloud/IceFumeCloud）：`SpawnMinActive [16 32]` + `ParticleAlpha .9,80 0` + Position 场区间轨迹铺开 + `Shake 1` + `SystemDuration 1.25`。染色版只加三行 RGB；实体阻断长度走 `clipRightX`，XML 保持完整射程。

**掉落物**（ZombieHeadOff）：`SpawnMinActive 1` + `LaunchSpeed [60 100]` + `RandomLaunchSpin 1` + `ParticleGravity 140` + `ParticleSpinSpeed [-5 5]`。通用旧效果可保留 Position 场修正；新部件效果优先由代码传入轨道世界锚点并让 XML 偏移归零。
若帽子、头饰等必须与头保持固定连接，即使它只是无耐久装饰、从不走护甲破损，也要先在透明画布中预合成一张专属 PNG，再用单粒子整体抛飞；不要并发两颗带随机速度/自旋的粒子。断头提交时同步隐藏本体头轨与这条装饰轨，避免离体粒子出现后宿主仍残留帽子。可复现生成脚本应锁定合成图 SHA-256，AutoTest 断言专属效果一颗/一 quad、宿主装饰轨已隐藏且通用掉头效果为 0。

**命中飞溅**（PeaBulletHit，双发射器）：主溅斑（1颗、`ParticleScale 1.2 0.4` 缩小消失）+ 碎屑环（`EmitterType Circle` + `LaunchSpeed [65]` + `Friction 0.0,10 0.1` 先快后刹 + `Acceleration Y=5` 微下坠）。

**定向反弹物**（UmbrellaReflect）：`SpawnMinActive 1` + 现有飞行物贴图 + Position X 从 0 向目标方向推进、Y 用三点轨迹先升后落 + `ParticleSpinSpeed`；不用不受支持的 `LaunchAngle`，也不用 `RandomLaunchSpin` 丢失方向。

**范围爆炸云**（JackExplode/CherryBomb）：用固定初始爆发数量 + `EmitterType Circle` + 非零 `EmitterRadius` + `RandomLaunchSpin 1`，云团 `Friction` 保持约 `0.015～0.02`，寿命至少 `0.6s`；碎片可另用更高速度和重力。禁止用 `.15,40 1` 配合超高初速冒充扩散，必须以实际 `worldBounds.widthInt/heightInt` 验收覆盖面。

**原版 XML 语义移植口径**（Doom.xml→10 发射器大特效实证，逐项映射，禁止机械照搬）：
1. 时间字段全部**厘秒→秒（÷100）**：ParticleDuration 150→1.5；SystemDuration 别照抄 400→4（原版仅回收判定），取"最长粒子寿命+余量"即可（→1.6）。
2. **EmitterOffsetX/Y 在坐标系换算后减半**（本引擎双倍生效，foot-gun ②）：先求相对当前稳定锚点的目标局部偏移，再把该局部值除以 2；禁止机械套用原版绝对数值。
3. **SystemField/SystemPosition 折算进 EmitterOffset**（本引擎不消费）：先把原版 SystemPosition 的视觉语义换算到当前 1100×600 场景/对象锚点，再与局部 EmitterOffset 合并并按双倍生效规则减半。
4. `FullScreen` 闪光 → `ParticleScale 4000` + WhitePixel（1×1 白图，键 PARTICLE_WHITEPIXEL），RGB/Alpha 轨迹原样保留。
5. `AnimationRate`/`ImageFrames` 仅对**单行横排帧条**原值照抄（单位本就是帧/秒），对应贴图整图入库不加 Column 属性。若原版另带 `ImageRow` 或贴图实际为多行帧表，本引擎 `ImageFrames` 没有选行能力；需要静态随机碎片时改用 `<Texture Column="帧数" Row="行数">` 并枚举所需 `PARTICLE_*_PART_n`，需要逐帧动画则先产出单行权威贴图，禁止把多行整图直接交给 `ImageFrames`。**碎屑条即使原版 XML 写了 `ImageFrames` 也先看素材语义**：各格若是互不连续的碎块轮廓（坚果啃食碎屑实证），应拆为静态随机 Part，不能循环播放成会变形的单颗碎屑。
6. 负数区间改升序（foot-gun ⑧）；`Image` 键按素材入库段落改前缀（IMAGE_/PARTICLE_）。
7. **特效名=第一个 Emitter 的 Name**：把首发射器 Name 改成 EmitEffect 要用的名字（Doom.xml 首发射器 DoomStem→"Doom"）。
8. **Friction 按本引擎逐帧语义重调**：原版爆炸云的高阻力关键帧不是无损迁移项；先用低恒定值恢复可见扩散，再以包围盒与截图收敛。

## 验证

新增或调整粒子 XML、贴图、触发点本身只要求默认 `clang-release` 可见专项、资源键、几何投影和同步截图，不机械加跑 `-NoInstance` 或 OpenGL。只有实际改到渲染后端、后端兼容路径或跨后端提交实现时，才补默认 Vulkan + `-NoInstance` + 强制 OpenGL 回归。本条覆盖本 skill 前文少数沿用的旧双路径取证表述。

粒子寿命都是亚秒级，AutoTest 截图要卡时机：首次发射前先以 `GetTexture(imageKey, false)` 导出所有新增 `<Image>` 键的加载断言，不能只看 manifest 或控制台 WARN；帧事件/命中发生后 `wait_frames` 2~20 再 `screenshot`（多截几张挑）。截图成功后断言 `particleEffectNameCounts`、实际四边形数和相对包围盒，再逐张 Read 核对颜色、铺开范围、方向、有没有“看不见”（foot-gun ③）；参考 `smoke_particle_render_probe.json`。专属头部/装备先高分辨率合成再缩放时，验收对象必须是 XML 实际引用的最终 PNG 和游戏内缩放后的同步截图；高分辨率母图细节不能替代低分辨率轮廓、描边、明暗层级与透明边缘检查。范围爆炸必须给 `worldBounds.widthInt/heightInt` 设与设计半径相称的下界，不能只断言 quad 数，否则所有粒子挤在中心也会假绿。动画中段才触发的命中/阻拦粒子必须做一负一正两段取证：节点前断言仍在对应动作轨且计数为 0，越过节点后立即断言计数为 1 并截图，防止“效果存在但提前播放”的时序假绿。植物与荷叶/花盆同格时，`nearestPlant.type` 会按几何距离命中下层载体，改断言稳定的 `row/col` 与粒子包围盒，不能把“最近类型”当成触发者身份。改 XML 数值免编译，跑脚本前重启即可。

仅发现可复用的 XML 语义、坐标、生命周期或取证经验变化时，更新对应 skill/reference；已有规则足够或只是调参则不修改，不记录一次性配方日志。只审阅实际受影响的条款，改过的技能运行 skill-creator 的 `quick_validate.py`；设计与记忆按根目录 AGENTS.md 按需维护。

互斥效果测试需要断言“派生效果为 1、基础效果为 0”时，`BuildStateJson` 应为两者预置显式零键；仅按当前活动效果动态建 map 会让基础效果不存在而使 `assert_state equals 0` 无法表达负例。

## 关联

头与帽子需要一起飞出时，合成单张透明粒子而不是两套独立随机发射器；最终粒子尺寸必须与戴在身体上的实际尺寸对照，不能直接使用未乘宿主缩放的合成像素尺寸。完成时的冒烟等反馈由一次提交边沿发射，读档重建损坏贴图时不重放；同时保留提交瞬间粒子计数、截图和读档终态取证。

触发端惯例见 adding-plant / adding-zombie skill（帧事件里 EmitEffect）；渲染分层背景见记忆 [[project_pvz_particle_render_layer]]。
