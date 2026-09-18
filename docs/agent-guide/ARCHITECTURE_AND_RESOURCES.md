# 架构与资源契约

[返回项目指南](PROJECT_GUIDE.md) · [全部文档](../README.md)

本页维护对象所有权、场景边界、网格、存档、资源注册与实现约定。查源码入口可先读 [核心系统阅读地图](../systems/README.md)；构建与验收分别见 [构建与调试](BUILD_AND_DEBUG.md) 和 [AutoTest 验证](AUTOTEST.md)。正文中的源码路径以仓库或 PlantVsZombies 源码目录为基准，资产相对路径以运行目录为基准。

## 架构概览

### 对象层次

```text
GameObject（基类：显式可选 Transform/Collider/Shadow/Clickable、渲染顺序、激活状态）
└── AnimatedObject（增加 Animator 精灵动画）
    ├── Plant → Shooter → PeaShooter
    │          SunFlower、WallNut、CherryBomb……
    ├── Zombie → ConeZombie、Polevaulter……
    └── Coin（可收集物）
Bullet（独立类型；通过 BulletPool 使用对象池）
```

`Bullet` 是单一 `final` 具体类型，弹型差异由稳定的 `BulletType`、分型对象池槽位和窄行为函数表达。豌豆、寒冰豌豆、孢子、火球与尖刺不得仅为名称建立无覆写、无独立状态的空派生类；火炬树桩运行时换型仍须区分当前 `mBulletType` 与固定 `mPoolType`，并由 `Reset` 完整恢复槽位状态。`BulletPool` 与 GOM 共同以 `shared_ptr` 持有池对象；`Bullet` 不反向持有池，不构成循环。每颗弹丸只保存一个不入档、复用时不重置的稳定池槽位下标，Release 以边界和指针一致性校验后直接定位，禁止恢复逐弹丸指针哈希或在逐帧阴影路径做 `weak_ptr::lock()`。池另维护稠密活跃槽位下标：Acquire 记录槽位在活跃表的位置，Release 用末项交换 O(1) 移除并拒绝重复回收，活跃计数直接由该表派生；hit 只统计空闲对象复用，miss 只统计新建。GOM 仍保留全部池对象以维持排序与附件生命周期，但 Update/Draw 是否进入并行路径只按总候选数扣除休眠池弹丸后的数量判断，不能让历史池高水位把小场景误判为大场景。若未来弹型出现无法由同一生命周期和复位合同安全表达的独立行为，再以实质覆写和专属状态为依据重新评估继承，而不是预先建立标记子类。

### 架构决策：继承式玩法对象

自 2026-08-16 起，本项目正式采用**继承式玩法对象**作为植物、僵尸及其他有独立生命周期玩法实体的主模型：公共状态与流程收敛在稳定基类，品种差异通过派生类、窄虚接口和注册式工厂表达。不得仅为追求形式统一，把植物或僵尸能力拆成通用 ECS 组件、复制一套平行状态，或让组件组合取代现有 `Plant` / `Zombie` 生命周期、动画与存档契约。

早期通用 `Component` 容器已于 2026-08-22 完整删除，不是项目未来的玩法对象模型。新增代码不得恢复 `Component` 基类、按 `type_index` 索引的类型表、`Add/Get/RemoveComponent<T>` 服务定位或通用 Start/Update/Draw 生命周期；跨多个无继承关系宿主复用且确实可选的横切能力，应由宿主用命名明确的值或小对象显式拥有。最终迁移契约见 `docs/superpowers/specs/2026-08-16-inheritance-gameplay-object-architecture-design.md` 与 `docs/superpowers/plans/2026-08-16-component-system-contraction.md`。

`EntityRegistry` 与上述组件容器相互独立：它是 Board 范围内的稳定实体 ID 注册表和查询索引，服务跨对象引用、存档恢复与热路径检索，不是 ECS，组件收缩期间不得删除或把其职责重新塞回对象指针。

### 显式附件

`GameObject` 通过 `std::optional<Transform>` 直接保存非多态空间值；只有空间对象才调用 `CreateTransform()`，调用方用 `GetTransform()` 读取唯一权威的位置、旋转和缩放。Collider 也已脱离通用组件表，由宿主用 `unique_ptr` 可选独占；只能通过 `CreateCollider()` / `GetCollider()` / `RemoveCollider()` 创建、访问或移除，入口原子维护 owner、CollisionSystem 注册、ID 与缓存，场景销毁和运行时移除都不得直接重置字段。`ColliderComponent` 仅保留过渡名称，不再继承 `Component`，Debug 绘制由 `GameObject::Draw()` 显式提交。

Shadow 同样由 `GameObject` 用 `unique_ptr<ShadowComponent>` 显式可选独占；`ShadowComponent` 仅保留过渡名称、不再继承 `Component`。创建、访问和移除统一走 `CreateShadow()` / `GetShadow()` / `RemoveShadow()`；介质/出土等生命周期显隐用 `SetVisible()`，跳跃/投掷等动作阶段门控用 `SetEnabled()`，两者独立并取 AND，禁止互相覆盖。普通对象由 `GameObject::Draw()` 在本体前固定提交；Bullet 不调用该阶段，仍由 `BulletPool::DrawShadows()` 在植物层前跨对象提交，并只遍历稠密活跃槽位，不得让历史池高水位放大阴影阶段扫描量。默认实例路径必须使用 `DrawTextureInstanced()` 与 reanim 保序，`-NoInstance` 继续走普通批次兜底。

Clickable 也由 `GameObject` 用 `unique_ptr<ClickableComponent>` 显式可选独占；创建、访问和移除统一走 `CreateClickable()` / `GetClickable()` / `RemoveClickable()`。`CreateClickable()` 会先保证 Collider 完整就绪再注册，`RemoveCollider()` 会同步注销 Clickable；仅替换 Collider 时保持 Clickable 注册有效。Clickable 继续使用主线程稀疏自注册表，输入处理保持渲染顺序降序、`ConsumeEvent`、悬停光标计数以及 UI/世界坐标选择，禁止退回每帧扫描全部 GameObject。

当前运行源码已不存在通用 `Component` 基类、派生类、类型表、待初始化/更新/绘制视图或模板访问接口。`ColliderComponent`、`ShadowComponent`、`ClickableComponent` 的 `Component` 后缀仅是兼容性的过渡命名：它们是 `GameObject` 通过具名 API 显式独占的附件，不构成组件系统。不得把 Transform、Collider、Shadow、Clickable 或新玩法状态重新抽回通用容器。

`Card` 已直接拥有单卡的冷却、选中、三叶草方向、可用性和显示缓存，并在 `Card::Start/Update/Draw` 中显式管理点击回调、玩法更新与卡面绘制。不要重新引入 `CardComponent` / `CardDisplayComponent`，也不要通过组件容器查询单卡状态。场景级多卡仲裁由 `GameScene` 通过 `unique_ptr<CardSlotManager>` 明确拥有；实战 `Card` 由该控制器直接绑定非拥有指针，禁止恢复匿名 `CardUI` 宿主或每帧扫描组件表定位 manager。

### 关键系统类

| 类 | 文件 | 职责 |
|---|---|---|
| `Board` | `Game/Board/Board.h`、`Game/Board/Board*.cpp` | 关卡玩法权威：天气、僵尸波次、阳光生成、胜负逻辑；大型环境和战术决策子域按实现文件拆分，公共门面与状态权威仍属于 Board |
| `BoardPresentation` | `Game/Board/BoardPresentation.h` | `Board` 到宿主场景的窄展示端口：提示、进度条及 UI 瞬态存取 |
| `GameObjectManager` | `Game/GameObjectManager` | 创建/销毁对象、渲染顺序、线程池 |
| `CollisionSystem` | `Game/CollisionSystem` | 每帧碰撞检测与回调 |
| `EntityRegistry` | `Game/EntityRegistry` | 按 ID 跟踪实体（存档系统使用） |
| `SceneManager` | `Game/SceneManager` | 持有唯一活动场景并在各场景间切换；场景在 `GameApp.cpp` 注册 |
| `ResourceManager` | `ResourceManager` | 加载/缓存资源；资源键定义在 `ResourceKeys.h` |
| `Graphics` | `Graphics.cpp` | 游戏公共绘制入口；Vulkan 保留 bindless/InstanceRecord/worker 快路，OpenGL 3.3 使用 CPU 展开和动态 VBO/IBO Batch；资源通过后端无关 `RenderTexture`/`TextureBackend` 生命周期接入 |
| `Animator` | `Reanimation/Animator` | 命名轨道动画系统；提供 `PlayTrack()`、`PlayTrackOnce()` 和帧事件 |
| `ParticleSystem` | `ParticleSystem/` | 由 `resources/particles/` 下 XML 配置驱动的粒子系统 |
| `AudioSystem` | `Game/AudioSystem.h` | SDL2_mixer 音效与音乐封装，管理声道 |
| `InputHandler` | `UI/InputHandler.{h,cpp}` | 将 SDL 事件转换成 Update 阶段查询的鼠标/键盘状态 |

### 游戏循环（`GameApp::Run`）

1. **输入：** SDL 事件 → `InputHandler`。
2. **更新：** `SceneManager` → `Board::Update()` + `GameObjectManager::Update()`，处理生成、AI 和碰撞。
3. **渲染：** `Draw()` 按渲染顺序遍历对象；Vulkan 可由 worker 并行 record/replay 并提交 InstanceRecord/Batch，OpenGL 强制在 Context 主线程按相同顺序串行 CPU 展开并调用 `Graphics::FlushBatch()`。两者都不得跨提交序列重排。

### 所有权与场景边界

创建、Start、停用、延迟移除和引用有效期的操作说明见 [对象生命周期与所有权](../systems/OBJECT_LIFECYCLE.md)。

- `SceneManager` 只持有一个 `unique_ptr<Scene>`。`SwitchTo()` 会先执行当前场景的 `OnExit()` 再销毁；本项目的 `Scene::OnExit()` 会清理全局 `GameObjectManager`，因此不支持把旧场景压栈后恢复。需要覆盖式 UI 时应留在当前场景内：`UIManager` 直接拥有 `Button`、`Slider` 和 `GameMessageBox`，模态框关闭请求在控件遍历结束后统一清理，不得再借用 `GameObjectManager` 的玩法对象生命周期。普通控件由 `UIManager::DrawControls` 在原 UI 层绘制，`Scene::Draw` 在所有场景注册命令结束后统一调用 `DrawMessageBoxes`，避免自定义入口、标签和 HUD 覆盖弹窗；最后创建的活动框位于最上层；`UIManager` 自动让顶层框独占 Button/Slider 输入，底层继续绘制且保留原有 enabled/canDrag 状态。`Scene::Update` 在 UI 阶段后屏蔽世界鼠标查询及 Clickable 阶段，关闭最后一层的当帧释放也不穿透。调用方无需逐个禁用背景按钮；打开子框的父按钮须设 `autoClose=false`，子框取消只关闭自身。图鉴往返通过 `GameInfoSaver` 的一次性内存关卡快照恢复到原局暂停菜单，复用正式 JSON 数据契约，不借用场景压栈也不依赖磁盘文件。
- `GameScene` 用 `unique_ptr` 独占 `Board`；多数运行对象由 `GameObjectManager` 的 `shared_ptr` 持有。`Board` 中的 `Cell*`、预览僵尸指针以及场景缓存指针均为非拥有索引，奖杯、弹坑等可失效引用优先使用 `weak_ptr`。
- `Board` 不依赖具体 `GameScene`，只保存非拥有的 `BoardPresentation*`。天气、波次和生存模式玩法状态只能由 `Board` 持有；场景只实现提示、闪屏、进度条和 UI 计时快照。新增展示请求应扩充这个窄端口，不能重新加入 `Board::mGameScene` 或在场景复制玩法状态。

### Board 网格

棋盘是 `vector<vector<Cell*>>` 非拥有寻址网格，`Cell` 的实际所有权在 `GameObjectManager`。植物放置在 `(row, column)`，僵尸按行移动。`Board` 管理僵尸波次以及 `BoardState` 状态转换：`CHOOSE_CARD → GAME → WIN` 或 `LOSE_GAME`；`NONE` 表示尚未初始化。

`Board` 拥有当前关卡的行数、首行 Y 与行高：普通草地为 5×100px，泳池背景为 6×85px（水路是 0-based 第 2/3 行）。位置、弹坑、子弹影子与小推车必须优先调 `GetCellCenterPosition` / `GetCellHeight`，不要再硬编 `CELL_INITALIZE_POS_Y + row*100`。`Cell` 当前分 `under/normal/pumpkin/overlay` 四层植物槽；战斗顶层优先级仍为 `pumpkin > normal > under`，短时飞行覆盖层不参与啃食 top。正式放置入口是 `Board::CanPlantAt`，僵尸啃咬只选 `GetTopPlantAt`。明确作用于整格植物组合的通用效果走 `Board::ForEachActivePlantInCell`：按 `overlay/pumpkin/normal/under` 快照实体 ID，并在每次动作前重新解析活动实体；需要聚合拦截或传播时序的效果另设窄专用入口。跳跃阻拦另走 `GetJumpBlockingPlantAt` 按层询问能力，非阻拦南瓜不会遮蔽内层高坚果。铲子单独按格内可见区域选层：南瓜中空中心选 `normal`，外圈选 `pumpkin`，空壳整格仍选南瓜；命中区域必须按当前 Cell 宽高派生，悬停高亮与最终铲除结果共用同一目标。需要南瓜拦截的僵尸范围扣血统一走 `ApplyPumpkinProtectedZombieAreaDamage`：先按技能原几何找命中植物，再为每个命中层选择自身逻辑九宫格内最近的活动南瓜，稳定打破并列并按保护者 ID 归并为一次默认 5 倍外壳伤害；爆破工头显式使用 4 倍重载，无保护者仍逐层结算，南瓜之间不连锁，普通小丑直接清除不走此入口。新增层必须同步创建/读档创建、释放/清理、render order、台风整组搬运、外部范围伤害与 AutoTest 投影。

战场主体绘制按行交错：`row N 植物 → row N 僵尸/扶梯 → row N+1 植物`，所以同排僵尸仍盖住植物，而下一行植物会正确遮住上一行越界伸下来的身体。`LAYER_GAME_PLANT` / `LAYER_GAME_ZOMBIE` 继续表示对象语义层，`GameObjectManager` 只在二者到 `LAYER_GAME_BULLET` 之间编排实际绘制号；小推车的 `LAYER_GAME_OBJECT` 和子弹层不变。任何运行期 `mRow` 变化必须通过排序键刷新入口重新分配绘制号，不能只改字段。

### 存档系统

使用 nlohmann/json 进行 JSON 序列化（`GameInfoSaver`）。植物和僵尸通过 `SaveExtraData(json&)`、`LoadExtraData(const json&)` 保存和恢复自定义状态。`PlayerInfo.json` 保存全局状态，`level{N}_data.json` 保存各关卡状态。Windows 通过 `FOLDERID_SavedGames` 写入系统“保存的游戏”目录（默认 `%USERPROFILE%\Saved Games\PlantsVsZombies\saves`）；Android 仍使用 `SDL_GetPrefPath`，Linux 暂沿用 `./saves/`。

两类 JSON 根节点都写入独立的 `schemaVersion`，并在任何运行状态被修改前由纯逻辑 `SaveSchema` 事务式升级。缺版本的历史档视为 v0；高于当前程序的未来版本、非对象根节点或非法版本字段一律拒绝加载，失败时输入文档和游戏状态均不应被部分修改。新增持久化结构变化时，应在 `SaveSchema` 增加逐版本迁移并同步 `SaveSchemaTests`，不要把一次性兼容分支继续散落到对象恢复过程。

玩家 schema v5 在 v4 `lastSelectedCards` 之外增加 `crazyDaveTutorialsSeen`，按稳定冒险关卡号保存已经完整看完或主动跳过的戴夫闲聊；v4 及更早旧档迁移为空数组。恢复时闲聊记录只接收当前冒险流程内的整数并排序去重；上次选卡仍只从当前选卡面板已有卡中按名解析、去重并遵守 11 张上限，未知、未注册或未拥有的卡会跳过；按钮恢复必须复用 `Card::SetTargetPosition` 的既有飞行动画，不能直接改卡片坐标。

关卡 schema v15 为矿场保存 `mine.layoutRevision`，v16 允许补强岩墙模板并阻止旧程序误读新图；v15 以前的矿场档迁移为版本0，保留原入口、岩壁和已提交预报。新局使用版本2（后半章补强岩墙），版本1仍保留初版多线模板；恢复时先按保存版本重建模板，再叠加已挖岩壁与实体；不能用新版地图过滤旧档而改变玩家阵地或施工节点。

Windows 首次发生真实存档访问时，会把当前工作目录旧 `./saves/` 中的普通文件复制到中央目录、逐字节校验后再删除源文件，跨磁盘同样安全。目标已有相同文件时只清理重复源文件；同名但内容不同则中央档优先、旧档原地保留且记录警告；迁移失败的缺失文件仍可逐文件回退旧目录读取。AutoTest 不触发迁移，`-AutoTestLoadSave` 始终只读构建目录下的 `./saves/`。

### 资源与资产

- 资源键是在 `ResourceKeys.h` 中手写的字符串常量，命名为 `PREFIX_UPPERCASE`（如 `IMAGE_PEASHOOTER`、`SOUND_CHERRYBOMB`、`MUSIC_DAY`、`PARTICLE_EXPLOSIONCLOUD`）。它们是 `ResourceManager::GenerateStandardKey` 根据文件名生成的实际键的防拼写错误镜像：去掉目录和扩展名 → 转大写 → 非字母数字转 `_` → 添加前缀。键值与常量名相同时，用 `RKEY(X)` 宏（展开为 `inline const std::string X = "X"`），避免重复书写；键值与名称不同时（例如 `IMAGE_HUGE_WAVE_APPROACHING = "IMAGE_APPROACHING"`、`SOUND_SHOOTER_SHOOT = "SOUND_THROW"`、值为 CamelCase 的 `REANIM_*`、字体路径），必须显式声明。
- 资产根目录：图片在 `./resources/image/`，粒子 XML 在 `./resources/particles/config/`，reanim 文件在 `./resources/reanim/`，字体在 `./font/`。
- **资源加载闭环：** `manifest.txt` 是构建期文件清单，也是 `image/reanim/` 等目录在 Android/桌面的枚举来源，但它不代替各资源类型自己的注册与键规则：

  | 资源类型 | 进入加载器的条件 | 实际运行时键 | 最小断言 |
  |---|---|---|---|
  | `.reanim` | 文件进入 manifest，且 `resources.xml/<Reanimations>` 有同名 `<Reanimation name>` | `REANIM_*` 常量的值必须等于 `name` | `HasReanimation(key)` |
  | `image/reanim/*.png` 运行时换图 | 文件进入 manifest；启动时 `LoadAllImagesFromPath` 全量加载 | `IMAGE_` + 大写文件名 stem；只有 reanim `<i>` 实际引用的图另有 `IMAGE_REANIM_*` 别名 | `GetTexture(key, false) != nullptr` |
  | 游戏图片 | `resources.xml/<GameImages>` 明确列出 | `IMAGE_` 标准键 | `GetTexture(key, false) != nullptr` |
  | 粒子专用 PNG | `resources.xml/<ParticleTextures>` 明确列出 | `PARTICLE_` 标准键 | `GetTexture(key, false) != nullptr` |
  | 音效 | `resources.xml/<Sounds>` 明确列出 | `SOUND_` + 大写文件名 stem | `HasSound(key)` |
  | 粒子配置 | XML 位于 `particles/config/` 并进入启动扫描 | 第一个 `<Emitter>` 的 `<Name>` | 发射前计数 0、发射后计数 1 + 同步截图 |

  原版部分 reanim 素材用 `Name.jpg` 保存颜色、用同尺寸 `Name_.png` 保存灰度透明遮罩；两者同时存在时 `ResourceManager` 在解码 JPG 后自动把遮罩亮度写入 alpha。reanim 按 `.png`、`.jpg` 候选路径加载时，前一个合法候选缺失不是资源错误，只有全部候选失败才记录一次 ERROR。不得把带黑底的 JPG 单独当作最终纹理，也不得把 `_` PNG 白色轮廓误当作彩色替代图。

  文件存在、manifest 存在、效果肉眼偶尔可见都不是单独的充分证据；运行时换图和掉落物应把加载状态导出到 AutoTest。Release 的资源 WARN 不保证进入 `run.log`，因此不能只 grep 日志。强制 reanim/纹理缺失必须修正注册或键来源，不能用通用 null guard 将坏对象留在场上。
- **Reanimation：** `Reanimation/` 是自定义骨骼动画系统，不是精灵表播放器。它加载 `.reanim` XML 文件，通过 `Animator::PlayTrack()` 播放命名轨道（如 `anim_walk`）。帧事件可在指定帧注册一次性回调。
- **粒子特效：** 粒子效果通过 `./resources/particles/config/` 下 XML 配置；一个文件可并列多个顶层 `<Emitter>` 片段（包含 `<Image>`、`<LaunchSpeed>`、`<Field>` 等），`ParticleXMLLoader` 以首个 Emitter 的 Name 缓存整组效果。

## 参考与实现指引

新增**经典植物、僵尸或子弹（projectile）**时，建议通过以下方式核对原版行为与数值：

1. **搜索网络**
   查阅 *Plants vs. Zombies* 社区 Wiki、Mod 文档或开源复刻，确认经典单位的攻击方式、生命值、速度和特殊能力。

2. **查阅 C# 参考代码（强烈建议）**
   本项目最初参考 Lawn 引擎的 C# 实现，完整代码位于：
   `D:\PVZ\PlantsVsZombies.NET-master\Lawn_Shared\Lawn`

   目录包括：

   - `Plant/`：全部植物逻辑（射击、产阳光、防御等）。
   - `Zombie/`：僵尸行为（移动、攻击、头盔掉落等）。
   - `Projectile/`：子弹属性与碰撞逻辑（豌豆、寒冰豌豆、火球等）。

   实现新的 `Plant`、`Zombie` 或 `Bullet` 子类前，**先查阅对应 C# 实现**，确保数值和行为与原版一致。

3. **动画故障排查**
   新植物或僵尸无法播放某段动画时，先检查 `./resources/reanim/` 中对应 `.reanim` 文件的轨道名称和帧数据，再对照 C# 参考代码确认预期动画序列与时序（动画文件基本一致）。

4. **帧事件要求**
   新植物或僵尸需要帧事件时，必须先询问主人。

## 新增植物

使用 `adding-plant` 技能（`.agents/skills/adding-plant/SKILL.md`）；它取代了原先放在本文件中的检查清单。

## 新增僵尸

使用 `adding-zombie` 技能（`.agents/skills/adding-zombie/SKILL.md`）；它取代了原先放在本文件中的检查清单。该技能已在舞王僵尸 + 伴舞僵尸上验证，覆盖断肢断头、召唤编队、出土裁剪、魅惑交互、帧事件陷阱和调参量交付。

### 生成僵尸的两条不同路径

- **游戏逻辑路径（绑定网格）：** `Board::CreateZombie(type, row, x, ...)` / `CreateZombieWithID(...)`。可以传任意像素 `x`，但 **`y` 始终通过 `GetZombieSpawnY(row)` 由 `row` 推导**，有意不提供 `y` 参数。真实僵尸、波次生成和存档恢复都使用此路径；存档只持久化 `row + x`。
- **自由放置路径（仅展示）：** `GameAPP::InstantiateZombieFree(type, board, x, y)` 用于必须放在任意 `y`、不能吸附到行的预览/UI 僵尸（选卡预览散布、`AlmanacScene` / `ZombieAlmanacScene`）。它封装 `InstantiateZombie(..., row = -1, isPreview = true)`。当 `board != nullptr` 时，这类僵尸会计入 `mBoard->mZombieNumber`，并在 `Zombie::Die` 中递减，因此必须保持增减平衡。

## 编码约定

- 视觉偏移使用 `mVisualOffset`，与逻辑网格位置分离。
- `mRow`、`mColumn` 表示游戏网格单元；像素位置存放在宿主唯一的 `Transform`，视觉偏移继续单独使用 `mVisualOffset`。
- 代码文件统一使用 UTF-8（无 BOM），由根目录 `.editorconfig` 约束；中文 UI 字符串使用 UTF-8。
- **头文件保护（每个 `.h`）：** 每个头文件必须以 `#pragma once` 开头。旧有的 `#pragma once` + `#ifndef _NAME_H` 双重形式也可接受。运行 `cmake --preset` 时会安装 `.githooks/pre-commit` hook（`git config core.hooksPath .githooks`），拒绝暂存区中缺少保护的头文件；同一配置步骤也会用 WARNING 列出仓库里已有的无保护头文件。检查支持 BOM：在前 512 字节内匹配 token，而不是锚定 `^`，因此 UTF-8 BOM 不会造成误报。原因是迁移掉 `.sln` 后，VS 的“添加新项”模板不再自动插入保护。
