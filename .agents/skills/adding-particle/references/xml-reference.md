# XML 标签、字段与配方

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 心智模型

### 一个 XML 文件 = 一个特效

，可含多个 `<Emitter>`（同时全部点燃，如 PeaBulletHit=飞溅+碎屑两发射器）。
### 特效名 = 第一个 `<Emitter>` 的 `<Name>`，不是文件名

（文件名只是惯例上取一致）。后续 Emitter 的 `<Name>` 不会注册为独立 `EmitEffect` 键；需要两个可单独触发的名称时，必须拆成两个 XML 文件。
- 目录：权威资源 `build/clang-release/resources/particles/config/`，启动时全目录加载；其他 preset 通过 Junction 共享，纯数据改配置**不用重编译，但要重启游戏**。
- 触发：`g_particleSystem->EmitEffect("Name", GetPosition());`；完整可选参数依次为 `renderOrder, durationOverride, clipRightX`。名字打错启动不报错，**发射时** run.log 出 `ERROR 找不到粒子特效配置`。
- 贴图：`<Image>` 填资源键（`IMAGE_*`/`PARTICLE_*`）；**没有独立粒子贴图格式**，任何在发射前已经加载的纹理都能当粒子。
### 键来源必须与加载路径一致

`<GameImages>` 预加载的图用 `IMAGE_*`，`<ParticleTextures>` 用 `PARTICLE_*`；粒子专用 PNG 必须显式列入 `resources.xml` 的 `<ParticleTextures>`，只有文件与 manifest 不会加载。`image/reanim/` 会由 manifest 驱动的启动扫描生成标准 `IMAGE_<UPPERCASE_STEM>` 键；reanim XML **实际引用**并已加载的部件才另有 `IMAGE_REANIM_*` 别名。动态发射前若对应 reanim 不保证已加载，就不能依赖别名，应改入预加载段。写错前缀或时序未加载=粒子静默不生成（foot-gun ③）。
### 分份贴图

`<Texture Column="4" Row="1">` 会把图切成独立纹理 `PARTICLE_XXX_PART_0..3`（`基础键_PART_序号`，行优先）——逗号列出来即"每粒子随机一张"（splats 碎屑的原理）。**序列帧动画别用它**，用 `ImageFrames`（整图不切，见标签表）。

## 数值语法（三种，核心）

### 标量

`1.5`。
### 随机范围 ValueRange

`[60 100]`（空格分隔，方括号）→ **生成/初始化时随机一次**，之后不变。裸写 `16 32` 也会被读成范围 [16,32]。
### 插值轨迹 InterpolationTrack

关键帧序列 `值[,时间%] 值[,时间%] ...`，按粒子寿命归一化线性插值：
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

## 配方（照抄改数）

**一次性爆发云**（FumeCloud/IceFumeCloud）：`SpawnMinActive [16 32]` + `ParticleAlpha .9,80 0` + Position 场区间轨迹铺开 + `Shake 1` + `SystemDuration 1.25`。染色版只加三行 RGB；实体阻断长度走 `clipRightX`，XML 保持完整射程。

**掉落物**（ZombieHeadOff）：`SpawnMinActive 1` + `LaunchSpeed [60 100]` + `RandomLaunchSpin 1` + `ParticleGravity 140` + `ParticleSpinSpeed [-5 5]`。通用旧效果可保留 Position 场修正；新部件效果优先由代码传入轨道世界锚点并让 XML 偏移归零。
若帽子、头饰等必须与头保持固定连接，即使它只是无耐久装饰、从不走护甲破损，也要先在透明画布中预合成一张专属 PNG，再用单粒子整体抛飞；不要并发两颗带随机速度/自旋的粒子。断头提交时同步隐藏本体头轨与这条装饰轨，避免离体粒子出现后宿主仍残留帽子。可复现生成脚本应锁定合成图 SHA-256，AutoTest 断言专属效果一颗/一 quad、宿主装饰轨已隐藏且通用掉头效果为 0。

**命中飞溅**（PeaBulletHit，双发射器）：主溅斑（1颗、`ParticleScale 1.2 0.4` 缩小消失）+ 碎屑环（`EmitterType Circle` + `LaunchSpeed [65]` + `Friction 0.0,10 0.1` 先快后刹 + `Acceleration Y=5` 微下坠）。

**定向反弹物**（UmbrellaReflect）：`SpawnMinActive 1` + 现有飞行物贴图 + Position X 从 0 向目标方向推进、Y 用三点轨迹先升后落 + `ParticleSpinSpeed`；不用不受支持的 `LaunchAngle`，也不用 `RandomLaunchSpin` 丢失方向。

**范围爆炸云**（JackExplode/CherryBomb）：用固定初始爆发数量 + `EmitterType Circle` + 非零 `EmitterRadius` + `RandomLaunchSpin 1`，云团 `Friction` 保持约 `0.015～0.02`，寿命至少 `0.6s`；碎片可另用更高速度和重力。禁止用 `.15,40 1` 配合超高初速冒充扩散，必须以实际 `worldBounds.widthInt/heightInt` 验收覆盖面。

**原版 XML 语义移植口径**（Doom.xml→10 发射器大特效实证，逐项映射，禁止机械照搬）：
1. 时间字段全部**厘秒→秒（÷100）**：ParticleDuration 150→1.5；SystemDuration 别照抄 400→4（原版仅回收判定），取"最长粒子寿命+余量"即可（→1.6）。
### EmitterOffsetX/Y 在坐标系换算后减半

（本引擎双倍生效，foot-gun ②）：先求相对当前稳定锚点的目标局部偏移，再把该局部值除以 2；禁止机械套用原版绝对数值。
### SystemField/SystemPosition 折算进 EmitterOffset

（本引擎不消费）：先把原版 SystemPosition 的视觉语义换算到当前 1100×600 场景/对象锚点，再与局部 EmitterOffset 合并并按双倍生效规则减半。
4. `FullScreen` 闪光 → `ParticleScale 4000` + WhitePixel（1×1 白图，键 PARTICLE_WHITEPIXEL），RGB/Alpha 轨迹原样保留。
5. `AnimationRate`/`ImageFrames` 仅对**单行横排帧条**原值照抄（单位本就是帧/秒），对应贴图整图入库不加 Column 属性。若原版另带 `ImageRow` 或贴图实际为多行帧表，本引擎 `ImageFrames` 没有选行能力；需要静态随机碎片时改用 `<Texture Column="帧数" Row="行数">` 并枚举所需 `PARTICLE_*_PART_n`，需要逐帧动画则先产出单行权威贴图，禁止把多行整图直接交给 `ImageFrames`。**碎屑条即使原版 XML 写了 `ImageFrames` 也先看素材语义**：各格若是互不连续的碎块轮廓（坚果啃食碎屑实证），应拆为静态随机 Part，不能循环播放成会变形的单颗碎屑。
6. 负数区间改升序（foot-gun ⑧）；`Image` 键按素材入库段落改前缀（IMAGE_/PARTICLE_）。
### 特效名=第一个 Emitter 的 Name

把首发射器 Name 改成 EmitEffect 要用的名字（Doom.xml 首发射器 DoomStem→"Doom"）。
### Friction 按本引擎逐帧语义重调

原版爆炸云的高阻力关键帧不是无损迁移项；先用低恒定值恢复可见扩散，再以包围盒与截图收敛。
