# 新增注册与原版适配

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 原版参考边界

- C# 参考实现是玩家可感知功能的证据：先提取状态、触发顺序、时长、数值、目标规则、音效和资源表现；未获主人批准时，这些行为必须与原版一致。
- C# 不是本项目的架构模板。动手前逐项核对当前植物类型体系、Board/实体所有权、更新与 Animator 时序、占格/碰撞、绘制路径、资源键以及存读档入口，再接入现有最窄扩展点；禁止为了贴近 C# 类结构复制平行状态或旁路系统。
- 玩法对象架构固定为继承式：新增植物继续选择 `Plant` / `Shooter` / `Shroom` 等最窄共同基类，并用窄虚接口表达品种差异；不得为植物能力恢复通用 `Component` 基类/类型表、把品种状态拆成任意组件组合，或为形式统一复制基类生命周期。空间数据由宿主 `CreateTransform()` 创建并通过 `GetTransform()` 访问。Collider、Shadow 与 Clickable 是宿主显式独占的具名附件：分别通过 `CreateCollider()` / `GetCollider()` / `RemoveCollider()`、`CreateShadow()` / `GetShadow()` / `RemoveShadow()`、`CreateClickable()` / `GetClickable()` / `RemoveClickable()` 管理，禁止恢复 `AddComponent/GetComponent/RemoveComponent<T>` 或缓存可独立失效的附件裸指针。三个类的 `Component` 后缀只是过渡命名，不代表组件系统。`CreateClickable()` 保证 Collider 已就绪；`RemoveCollider()` 会同步注销 Clickable，运行时替换 Collider 则保持 Clickable 注册有效。
- 坐标按 [视觉与动画](visual-animation.md) 换算，资源按项目指南的注册契约接入。工程实现可以不同，但必须用 AutoTest 状态、音效请求和默认可见截图证明功能等价；验证失败时先修适配，不能用“原版就是这样写的”合理化当前项目中的错误表现。


## 新增植物的勘察

以下清单用于新植物或相应契约的实质修改。单纯调参直接检查权威参数、单位、相关限制及必要测试，不重做未涉及的动画、音效和注册勘察；历史原因按需查记忆。

### 读 reanim

`build/clang-release/resources/reanim/<Name>.reanim`，用 Grep `<name>` 提取全部 track 名，`anim_xxx` 即可用动画（以实际轨道数据和当前加载器判断是否为可播放剪辑，不从命名猜测）；`<f>-1/0</f>` 对定位 anim 轨活跃帧区间。
### 读 C# 参考并主动盘点音效

`D:\PVZ\PlantsVsZombies.NET-master\Lawn_Shared\Lawn\Plant\Plant.cs`，grep 植物名，读专属 Update 函数 + 发射物类型 + mShootingCounter/state 分支，先记录必须忠实的行为与数值，再按本项目现有所有权、坐标、更新、绘制和存档契约实现；同时收集相关路径的全部 `PlayFoley` / `PlaySample`，不要等主人听出缺声才补。受啃、受击等由外部对象触发的反馈还必须搜索消费方（例如 `Zombie::AnimateChewSound` 会按植物类型选择 `ChompSoft`），不能只读 `Plant.cs`。沿 `FoleyType → Sexy.TodLib/Foley/TodFoley.cs → Resources.SOUND_*` 得到精确资源键，以资源键去掉 `SOUND_` 后的小写名到 `D:\PVZ\中文年度加强版完整版\Test\sounds\` 查同名 `.ogg`。找到后复制到唯一权威 `build/clang-release/resources/sounds/` 合理子目录，并同步 `resources.xml` 与 `ResourceKeys.h`；找不到才问主人，禁止用相近声音静默替代。构建后检查 `manifest.txt` 和启动日志无 missing sound，并用可见行为路径及 `GetSoundPlayRequestCount` 投影验证触发次数（含读档不得重响）。
### 盘点已就位的基建

（常常提前有了，别重复加）：`PlantType.h` 枚举、`TestDriver.cpp` kPlantNames、`ResourceKeys.h` RKEY、`AnimationTypes.h`、卡片图 `PlantImage/<Name>.png`、reanim 部件图。缺哪补哪。
   如果植物枚举、冒险解锁位或 AutoTest 名称表已经预置但尚未注册，保留现有位置与整数 ID，只补缺失接线；动画枚举仍追加在末尾，禁止为追求排列整齐移动旧值或再加重复项。
   `image/reanim/` 全目录预加载生成 `IMAGE_<文件名大写>`；只有被 reanim XML 的 `<i>` 直接引用的部件才会额外获得 `IMAGE_REANIM_*` 别名。运行时动态换入、但不在 XML 时间线出现的受伤材质必须用前者。若完整状态图需要在启动预加载阶段就以 `IMAGE_REANIM_X` 取得，文件 stem 本身必须写成 `REANIM_X`（或让 reanim 确实引用并加载该键），不能把 `FrostMine_dormant.png` 误当成会自动注册 `IMAGE_REANIM_FROSTMINE_DORMANT`。更新派生阶段时先确认 `GetTexture` 非空，再提交阶段缓存，避免“状态断言通过、画面仍是旧图”的假绿。
   派生换色必须逐个核对目标 reanim 的实际 `<i>` 资源键，不能从 track 名或文件名猜部件归属：名字像 `backleaf` 的轨道可能属于地面叶座，头后小叶反而可能引用共享 `ANIM_SPROUT`。只给真正需要变色的共享部件派生独立纹理并替换新 reanim 的键，原植物仍保留共享资源；AutoTest 为该独立键增加 `GetTexture(key,false)` 断言并截图。

## 实现清单

### 类

`Game/Plant/<Name>.h/.cpp`。选基类：蘑菇→`Shroom`（白天睡觉自动处理）；豌豆系带独立头部动画→`Shooter`；其余→`Plant`。抄最像的现有植物结构（喷射蘑菇抄 `PuffShroom`）。
### 注册

`GameDataManager.cpp` 加 `#include` + `RegisterPlant(type, "PLANT_X", IMAGE_X, ANIM_X, "ReanimName", &MakePlant<T>)`。卡片由注册表数据驱动，**无需单独加卡**。
### gamedata.json

在 `build/clang-release/resources/gamedata.json` 加 `{cost, cooldown, offset, scale, simulation}` 条目（前四项缺任一字段拒启动 exit -6）；其他 preset 自动共享。`simulation` 是 Board 级轻量防线推演画像：所有植物填写 `baseHealth`；普通射手按稳定等效值填写 `attackDps`，跨行攻击另填 `attackRowRadius`；向日葵/阳光菇类填写 `sunPerSecond` 与 `firstSunDelay`；一次性或无法可靠简化的复杂能力明确填 `persistent:false`，不得为了让它参与推演而在 `Board` 写植物类型特判。普通花盆/睡莲这类只承载、没有额外推演能力的 under 层填写 `supportOnly:true`，由 Board 压缩进独立的每格支撑数组并从未来种植卡画像排除；避雷花盆等有特殊战略能力的支撑植物保持 false，继续占详细植物画像。新增植物必须在专项中断言对应 `plantDefinitions.<TYPE>.simulation*` 投影（含适用时的 `simulationSupportOnly`），防止画像漏配或未加载。

   低频条件能力若能用紧凑状态精确表达，不要硬摊成静态触发频率：把能力参数留在 `PlantSimulationProfile`，场上实例的真实剩余冷却放 `PlantSnapshot`，卡牌新种实例从就绪态开始；目标资格和会被消费的生命层放 `ZombieSnapshot`，在 `PlantDefenseMonteCarlo` 内用一个无 GameObject 的公共 step 函数原子消费目标、结算能力并进入真实冷却。普通攻击、治疗决策、蹦极/爆区选点与天气路线等 rollout 都必须调用同一函数，Board 只复制通用画像和正式能力接口结果，禁止按植物或僵尸类型分支。无合法目标时能力价值必须为零；睡眠类卡牌用 `daytimeDormant` 在白天只保留阻挡生命，不模拟主动能力。专项至少锁定“有目标生效、无目标零收益、场上剩余冷却继承、卡牌就绪”，并检查所有共用 rollout 入口。
### info.txt（图鉴文案）

在 `build/clang-release/resources/info.txt` 加两段——`[PLANT_X]` 下一行图鉴名字、`[PLANT_X_DESCRIPTION]` 下一行介绍（enum 名与注册的 "PLANT_X" 严格一致；解析器只认 `[key]`+正文，多行正文允许）。缺条目图鉴显示空白不报错，极易漏；提交前静态对照全部已注册植物，确认两枚 key 均存在、非空且唯一。
   通关奖励页复用 `PlantAlmanacScene` 的同一份介绍与无 Board 预览，不另写一份奖励文案。奖杯与选关跳过共用 `AdventureProgression::AdvanceProgress` 推进当前关并去重发卡，调用方仍须保存玩家信息；新增奖励入口不得复制奖励表或以返回无新卡推断推进失败。新增奖励植物需保证正文说明核心用途和限制，并在 `smoke_plant_reward_almanac` 类真实奖杯流程中检查预览、长文可读性与下一关选卡；无奖励、重打或已有卡不展示。奖杯刚生成时先等入场缩放完成再点击，截图和 dump 使用不同扩展名避免互相覆盖。
### 资源入库

reanim、贴图、声音、resources.xml 都只改 `build/clang-release/resources/` 这一份权威资源；严禁为 playtest/debug 建副本或 Copy-Item 同步。新增文件仍因 build/ 被忽略而需要 `git add -f`。
