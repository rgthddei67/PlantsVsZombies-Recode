# AutoTest 验证

指挥官的全兵种实战训练、经营压力陪练、断点续训与策略发布见 [冷藏站指挥官训练](../../autotest/COMMANDER_TRAINING.md)。旧战术专项显式使用 `commander_experiment` 的 `weights: null` 验证回退 AI；正式发布分支另做默认资源加载验收。

[返回项目指南](PROJECT_GUIDE.md) · [全部文档](../README.md)

本页维护可见运行、验证矩阵、测试输出和常用夹具。编译前先读 [构建与调试](BUILD_AND_DEBUG.md)。代码和资产的公共约束见 [架构与资源契约](ARCHITECTURE_AND_RESOURCES.md)。

## 按改动选择证据

- 普通功能验证仍默认 `clang-release` 当前桌面可见运行，检查退出码、日志与相关状态断言。外观、布局、动画、绘制或资源显示改动检查同步截图；纯数值和非视觉逻辑改动不强制补截图。
- 复用足以覆盖改动的既有专项；仅在缺少观测能力或用例时新增。不因技能列出了死亡、睡眠、存档或选择 UI 就全部执行；新增完整角色则覆盖它实际拥有的能力与生命周期。
- 单体数组下标只用于唯一目标夹具。多对象交互按稳定实体 ID、格位或聚合投影选择，运动同步用同状态相对量。
- 相关检查通过后，只有新增变化、失败或未解决疑点才扩大或重复测试。以下各夹具说明用于查找适合本任务的证据，不是每次交付的清单。

## AutoTest 测试套件

### 交互试玩信箱

脚本根对象加 `"interactive": true` 后，原 `commands` 作为开局脚本执行；结束时进入等待，不退出。
普通游戏和没有此字段的 AutoTest 不启用信箱。示例 `autotest/scripts/interactive_play_8_8.json` 使用正常开局和卡组，内部关卡 71 对应 8-8；不修改阳光、冷却或出怪。
仍按下方可见启动命令运行 `-AutoTest <脚本绝对路径> -Seed 42`。AutoTest 原有禁止玩家存档写入规则继续生效。

用 Python 标准库客户端发送一批命令（PowerShell 从仓库根目录执行）：

```powershell
python autotest/live.py build/clang-release/autotest/out/interactive_play_8_8
python autotest/live.py build/clang-release/autotest/out/interactive_play_8_8 '[{"op":"player_plant","slot":0,"row":2,"col":0},{"op":"advance","steps":120}]'
python autotest/live.py build/clang-release/autotest/out/interactive_play_8_8 '[{"op":"screenshot","name":"live.png"}]'
python autotest/live.py build/clang-release/autotest/out/interactive_play_8_8 '[{"op":"quit"}]'
```

- `slot/row/col` 全部从 **0** 开始。`player_plant` 使用实际卡槽，经过正式落种、费用、冷却、地形和暂停门禁；失败返回原因且不收费。搬搬藤的两阶段搬运暂不支持。交互指令不接受 `plant`、`set_sun` 等夹具作弊命令。
- `player_shovel` 用 `row/col` 点击格子中心，复用正式铲子命中、分层、冰封限制与归位逻辑；同样服从 gameplay input 门禁，不是按实体 ID 强制删除。
- `collect_sun` 用导出阳光的实体 `id` 触发正常收集，飞回阳光栏后才增加余额；重复收集返回 `sun_unavailable`。
- `advance` 接收 `steps=0..3600`，每步沿用正常固定逻辑步及当前倍速。预算耗尽后跳过整个场景 Update 和游戏时钟推进，避免按帧技能在等待中漂移；绘制、SDL 事件与窗口关闭继续运行。玩家的暂停没有解除时返回 `player_paused`。
- 每批结束自动返回精简状态、每项操作结果和 `simulationSteps`。无参数即 `observe`；`--full-state` 返回原完整诊断投影；`--file commands.json` 从文件读取命令数组。截图沿用原渲染器捕获屏障，不推进战斗；输出在脚本目录中。
- 同一游戏只用一个客户端控制。`status.json` 给出本次唯一 `session`、`liveDir`、`lastRequestId`；请求为该目录下严格递增的 `request_N.json`，包含 `session/id/commands`，先写临时文件再改名。响应 `response_N.json` 同样原子发布，旧请求/响应保留作记录。
- 超时只代表结果未知，使用**原指令和原 `--id N`** 重试。旧序号不会再次执行；新进程创建新会话目录，不重放旧请求。参数或玩法拒绝记录到结果并继续本批后续命令，不自动重试；渲染器/状态导出失败仍按 AutoTest 故障退出。
- 验证：可见启动 `interactive_contract.json`，随后运行 `python autotest/verify_interactive.py build/clang-release/autotest/out/interactive_contract`。检查其验证 JSON、退出码、`run.log` 和同步截图；普通卡槽/暂停路径回归用 `smoke_advanced_pause.json`。

- 冷藏站指挥官：`smoke_cold_storage_commander.json` 与 `verify_cold_storage_commander.py` 验证实际付款队伍、反制牌可用性、支援前锋、总攻冷却读档及有限观望。`coldStorage` 下的 `commanderMode`、`commanderBudget`、`commanderSpent`、`commanderReserve`、`commanderFocusRow` 和 `responseWindowMs` 只解释最近一次计划，不是独立资源；每波名额是上限，不能再断言指挥官必须填满。完整平衡体验使用 `interactive_marigold_10_1_doom.json` 与 `play_cold_storage_marigold.py --focused-fire --deny-income`，`--until` 限定本段游戏时间，便于以 1 倍速观察和调整操作。

`smoke_glyph_atlas_rebuild.json` 在同帧按血量串逐步扩充字形图集，并交错 Add 绘制；
`glyph_atlas_rebuild_probe` 后必须立即截图，后续帧会掩盖旧纹理提前释放问题。
运行 `python autotest/verify_glyph_atlas_rebuild.py <输出目录>`，比较重建帧与稳定帧的左右同串像素和绿色墨迹。
后端回归覆盖 Vulkan、`-NoInstance`、OpenGL SSBO 及 `-Renderer=opengl -OpenGL33` CPU 路径。

启动参数 `-AutoTest <script.json>` 会通过 JSON 脚本自动驱动游戏（进入关卡、选卡、种植、生成僵尸、截图、导出状态，然后退出）。Codex 可以独立完成“修改代码 → 构建 → 运行脚本 → 读取截图验证”的完整闭环，无需主人手动提供游戏截图。

- **验证矩阵按改动面分流：** 新增或修改植物、僵尸、粒子、出怪池、数值、逻辑或普通资源时，默认只跑 `clang-release` 的当前桌面可见用例与实际影响范围内的状态/资源/截图断言，不因“是新内容”就机械加跑 `-NoInstance` 或强制 OpenGL。只有实际改动渲染后端、后端兼容路径或跨后端提交实现（如 Vulkan instance/batch、`-NoInstance` CPU 矩阵路径、OpenGL CPU batch/shader/texture 生命周期）时，才要在默认 Vulkan 之外加跑 `-NoInstance` 和强制 OpenGL 兼容回归。

- **资格拒绝断言：** `set_typhoon` 可传 `expectedSuccess=false`，用正式设置入口验证当前地图或天气拒绝台风；缺省仍要求设置成功，保持旧脚本语义。

- **整组搬运夹具：** `relocate_plant_group` 用 `fromRow/fromCol` 选择来源、`row/col` 指定落点，调用正式 Board 入口；`expectedSuccess` 默认 true。卡费与冷却仍须用真实三段 `click` 验证。运行 `smoke_carry_vine` 后执行 `python autotest/verify_carry_vine.py`，比较稳定 ID、生命、生产/装填状态与多格别名；存档截图屏障只允许正常固定步推进。

- **脚本位置：** `autotest/scripts/*.json`（纯数据，不属于编译目标；修改脚本无需重新编译）。
- **音乐：** AutoTest 每次启动默认将音乐音量设为 0，音效仍沿用原设置；这是本次测试进程的初始值，不写回普通游玩偏好。状态投影 `adaptiveMusic.volumePct` 可校验菜单、换关与快照往返后的音乐音量。
- **矿道版本夹具：** 根字段或单条 `goto_level.mineLayoutRevision` 可显式设为 `0`，用于旧拓扑下的施工、转弯、回声和存档机制专项；缺省为正式新局版本 `2`；`1` 保留初版多线布局，用于旧档及该版对照。历史专项固定旧版本不代表新关卡已验收。当前九关用 `smoke_mine_strategy` 与 `verify_mine_strategy.py` 核对布局、预报、编队和读档；`summon_next_wave.clearPrevious=true` 只供波次专项释放上一波实体，保留已承诺预报，不作为实战难度证明。`probe_mine_strategy_defenses` / `verify_mine_strategy_defenses.py` 比较相同四株精英胆小菇集中两路和分布三路的成型阵地，不模拟开局经济。
- **开墙对照夹具：** `complete_mine_dig` 按 `row/col` 通过正式地形提交入口立即打开一块可挖岩壁，不收费、不推进施工时间；只用于从同一快照比较开墙后的战斗结果。玩家费用、工期和工兵施工风险仍须用 `mine_dig` 或真实开凿僵尸验证，不能用此夹具替代。
  `probe_mine_wall_choices.json` 交换上下路的经济植物与回声菇，选墙前先生成实际跟进队伍，再从同一阵容快照对比不拆墙、拆上路和拆下路；`python autotest/verify_mine_wall_choices.py [输出目录...]` 核对初始阵容一致、植物感知选墙、静音与战斗结果，并在每个输出目录写入 `comparison.json`。真实工兵的放弃/重试和施工结果另由 `smoke_excavator_tactics.json` 对比孤身工兵与红眼队伍；运行 python autotest/verify_excavator_tactics.py 核对提交边沿和后续破阵，不能只凭瞬间开墙探针断言施工可完成。
- **运行方式（工作目录必须是 exe 所在的 `build\<preset>\`）：** Codex 默认必须让窗口显示在主人当前桌面。GUI 启动属于沙箱外桌面操作，调用 shell 时使用 `sandbox_permissions="require_escalated"`；仅写 `-WindowStyle Normal` 而不提升权限，进程仍可能落入隔离会话、主人看不到。推荐命令：

  ```powershell
  Push-Location build\clang-release   # 默认构建、迭代诊断与交付回归
  $exe = (Resolve-Path '.\PlantsVsZombies.exe').Path
  $script = (Resolve-Path '..\..\autotest\scripts\demo_peashooter.json').Path
  $process = Start-Process -FilePath $exe `
    -ArgumentList @('-AutoTest', "`"$script`"", '-Seed', '42') `
    -WorkingDirectory (Get-Location).Path -WindowStyle Normal -PassThru
  $process.WaitForExit()
  $exitCode = $process.ExitCode   # 0=成功；1=命令失败/超时；100=脚本解析失败
  Pop-Location
  exit $exitCode
  ```

  启动后可用桌面窗口枚举确认出现 `PlantsVsZombies.exe` / “植物大战僵尸中文版”；主人报告未显示时，先核对提升权限和工作目录，不要重复普通 shell 启动。AutoTest 自动退出；需要主人亲自操作时改为同方案启动不带 `-AutoTest` 的普通游戏，或在测试脚本中加入明确观察停留时间。

- **产物：** 位于 `build\<preset>\autotest\out\<script-name>\`，包括 PNG、`status.json`、状态转储和 `run.log`。Release 会裁掉 Logger INFO，因此 `run.log` 是权威命令执行记录；首帧会记录 requested/selected Renderer、`-NoInstance`、SDL video driver，并按实际后端记录 Vulkan API/dynamic rendering/sync 路径，或 OpenGL Vendor/Renderer/Version/GLSL/framebuffer/VSync 及进程中是否已有 Vulkan loader。auto 回退另记录失败阶段和原始错误，防止测试只传参数却未命中目标分支。
- **离机归档：** `scripts/upload_autotest_artifacts.ps1 -TestName <script-name>` 可把单次输出打包上传到已配置的 SSH 备份节点；默认仍是 `clang-release`，其他预设必须显式传 `-Preset`。工具要求 `run.log` 与 `status.json`，记录提交、工作区脏状态、测试状态和逐文件 SHA-256；失败用例也允许归档，没有 PNG 时只警告。离机副本不改变当前桌面可见运行、退出码、日志、状态断言和截图均须现场检查的验收合同。部署与恢复见 `docs/operations/pvz-backup-node.md`。
- **西瓜投手夹具：** `set_melonpult_shoot_cycle` 按 `row/col` 固定当前活动西瓜家族植物的已累计时间与本轮间隔；紫卡升级同帧内会过滤已失活但尚未移除的基础株。`spawn_bullet` 名称表开放 `BULLET_MELON` 与 `BULLET_WINTERMELON`，可与抛物线参数组合覆盖溅射、落空、减速和对象池复用。
- **投手锁墙夹具：** `spawn_bullet` 的解析抛物线参数可加 `targetsIceWall=true`，显式锁定同行冰墙；状态 bullet 条目导出同名布尔值，供断言在途弹跳过墙后僵尸、落点复核墙体、快照往返与对象池复位。未给该字段时保持普通抛射语义。
- **BulletPool 压力夹具：** `spawn_bullet` 可用 `count=1..512` 批量创建同型弹丸，并用 `xStep/yStep` 给每发位置递增；缺省仍只创建一发。状态根节点导出 `bulletPoolStorageCount/ActiveCount/PeakCount/HitCount/MissCount/HitRateOn1000/ActiveSlotsValid`，其中 hit 只表示复用空闲对象，miss 表示必须新建。`stress_bullet_pool_active_slots.json` 以 256 发新建→全部回收→64 发复用锁定稠密活跃表、统计和阴影表现；性能取证加 `-Profile` 并读取 `5a.Draw_bulletShadows`，不能只凭结构变化声称帧率提升。
- **忧郁菇夹具：** `set_gloomshroom_shoot_cycle` 按 `row/col` 把已累计攻击周期固定为 `elapsed` 秒并清理未完成攻击；状态投影导出攻击内时间及下一云雾/伤害序号，供四段原版时间点和中途读档续播做确定性断言。
- **台风偏好夹具：** `set_typhoon_weather_enabled` 在进程内切换默认开启的台风天气总资格；关闭时所有地图的台风概率、pending、active 与强制入口统一失效。`set_opening_typhoon_protection` 只切换默认开启的前 5 波保护；它仅在总开关开启时有玩法意义，普通冒险保护开局第 1～5 波，无尽只保护第一轮第 1～5 波。两条命令都不触碰真实 `PlayerInfo.json`。
- **控制台悬停说明状态：** `MainMenuScene` 的状态根节点导出 `consoleTooltipVisible/Text/X/Y`。控制台每项复选框使用独立整行命中区；Tooltip 随逻辑鼠标移动，屏幕边缘自动换侧并夹紧，移出选项后清空。`smoke_mainmenu_console` 用真实 `move_mouse`、整行文字点击和截图覆盖四项说明及条件项隐藏。
- **默认对话框夹具：** `show_test_message_box` 在当前场景以 `title/message/scale` 构造默认 `GameMessageBox`；状态根节点的 `gameMessageBox` 导出自适应尺寸、居中点和正文行数，`dialogSkinResources` 断言原版十分件皮肤的运行时加载闭环。
- **命令集：** `goto_level` / `choose_cards` / `wait_state` / `set_sun` / `set_weather` / `set_opening_typhoon_protection` / `set_roof_runoff` / `set_typhoon` / `roll_typhoon` / `reroll_typhoon_direction` / `trigger_typhoon_gust` / `set_weather_forecast` / `show_image_prompt` / `show_crazy_dave_dialog` / `advance_crazy_dave_dialog` / `skip_crazy_dave_dialog` / `roll_weather_forecast` / `advance_weather_phase` / `trigger_lightning` / `set_adventure_level` / `force_trophy` / `add_crater` / `force_survival_round` / `force_survival_round_clear` / `summon_next_wave` / `plant` / `assert_can_plant` / `set_plantern_gear` / `set_plantern_fuel` / `award_plantern_fuel` / `toggle_plantern_menu` / `assert_can_target` / `spawn_bullet` / `set_starfruit_shoot_cycle` / `set_cabbagepult_shoot_cycle` / `set_kernelpult_shoot_cycle` / `set_furnace_core_state` / `spawn_zombie` / `attempt_ice_execution` / `apply_zombie_control` / `make_gargantuar_smash_ready` / `set_jack_pop_countdown` / `set_elite_jack_throw_countdown` / `spawn_wave_zombie` / `set_zombie_mist_fuel_reward` / `kill_zombie` / `damage_plant` / `squish_plant` / `damage_zombie` / `add_perk` / `survival_perk_open` / `survival_perk_pick` / `survival_perk_refresh` / `show_plant_hp` / `show_zombie_hp` / `wait_seconds` / `wait_frames` / `set_timescale` / `reset_test_state` / `set_last_selected_cards` / `save_level_snapshot` / `reload_level_snapshot` / `charm_zombie` / `move_mouse` / `click` / `key` / `screenshot` / `dump_state` / `assert_state` / `quit`。等待类命令接受 `timeout`（默认 15 秒）。`click target=main_menu_survival` 从当前 `MainMenuScene` 的真实生存按钮中心解析坐标，避免斜石碑矩形命中区重叠造成硬编码点击漂移；其余命令与参数契约见下文各专项说明。`set_opening_typhoon_protection` 只在进程内切换默认开启的前 5 波台风保护，不触碰真实 `PlayerInfo.json`；专项用它覆盖高难度玩家关闭保护后的原概率路径。`set_last_selected_cards` 只在进程内布置稳定植物枚举名数组，不触碰真实 `PlayerInfo.json`，供选卡恢复按钮和失效名称过滤专项使用。`plant` 对 `PLANT_BLOVER` 可选 `bloverDirection=HOUSE/FRONT`，用于固定实例方向；`assert_can_plant` 用 `type/row/col/expected` 直接断言正式 `Board::CanPlantAt`，适合覆盖睡莲承载层、水路禁种与弹坑等网格规则。`add_crater` 用 `row/col` 在当前棋盘直接创建弹坑，可选 `timeLeft` 固定剩余秒数，专用于验证不同格子地形和寿命阶段的绘制资源。`set_plantern_gear`、`set_plantern_fuel`、`award_plantern_fuel` 与 `toggle_plantern_menu` 固定路灯花玩法/UI 状态；`assert_can_target` 直接断言统一雾中索敌许可；`set_zombie_mist_fuel_reward` + `kill_zombie` 用确定性奖励走正式死亡发起入口，先断言 `pendingFuelTenths`、再等待飞行结束断言实际到账，避免用概率用例验证到账/丢弃边界。`set_roof_runoff` 对昼夜屋顶生效，用 `phase=IDLE/WARNING/FLOWING`、`charge`、活动阶段非空 `rows` 数组和可选 `remaining/retainedCharge` 固定径流状态；旧脚本的单个 `row` 仍兼容。`set_weather_forecast` 固定公开预报、真实天气和揭晓倒计时，只用于天气 UI/失败提示的确定性测试；当 `actual=HEAVY` 时可用 `typhoonStrength=NONE/TYPHOON/SEVERE/SUPER` 与 `promptVariant=0..2` 固定待生效台风和同级预警文案。`show_image_prompt` 用 `image=HUGE_WAVE/FINAL_WAVE` 显示既有图片提示，供多提示并存与绘制顺序测试。`show_crazy_dave_dialog` 按当前关卡显式打开戴夫闲聊（`force` 默认 true），`advance_crazy_dave_dialog` 与 `skip_crazy_dave_dialog` 分别推进或完成对话；状态根节点 `crazyDave` 导出触发、页码、台词、轨道和最终几何，`crazyDaveResources` 锁定 reanim 与全部部件资源。`roll_weather_forecast` 只在晴天用 1-based `weatherRoll` 走正式动态权重与弱天气保底，再发布必定准确的锁定预报，可用 `revealIn` 固定揭晓倒计时。`set_typhoon` 只在大雨中生效，用 `strength=NONE/TYPHOON/SEVERE/SUPER`、`direction=NONE/HOUSE/FRONT` 固定台风状态；可选 `gustIn`、`directionIn`、`gustsRemaining` 和 `decayIn` 固定阵风、转向、预算与衰减计时，`roll_typhoon` 用 1-based `chanceRoll`/`strengthRoll` 和固定方向走正式概率、连续落空保底与动态强度边界。`reroll_typhoon_direction` 用 `directionRoll=1..2` 走正式风向二选一重抽，确定性覆盖继续同向与切换方向。`trigger_typhoon_gust` 启动一次不消费自动预算的正式阵风，可用 `plantMoveIn` 固定阵风开始后多少游戏秒结算植物（默认 0 保持旧脚本的立即结算），活动期间仍会连续吹动僵尸。`force_survival_round` 直接定位测试轮次、重建出怪池并刷新轮次派生的天气速度；`force_survival_round_clear` 走正式轮清入口。`summon_next_wave` 直接走正式 `Board::SummonNextWave()`，可用 `count=1..100` 连续推进并验证波次派生状态；`spawn_zombie` 可加 `frozen=true` 让新目标立即走正式冻结入口；`set_jack_pop_countdown` 按 `row/index/value` 只覆盖 RUNNING 普通小丑的剩余开盒秒数；`set_elite_jack_throw_countdown` 按 `row/index/value` 选择精英小丑，可用 `targetRow/targetColumn` 固定下一只盒子的地图合法落点，供飞行、边界行、伤害与存档做确定性验证；`spawn_wave_zombie` 额外要求 `mutationRoll=1..100`，以正式天气变异解析器创建波次候选，用于确定性测试条件变异和每波上限；候选超过上限时命令成功但不创建回退类型，与正式挑选循环的 `continue` 一致。`spawn_bullet` 直接创建对象池子弹，可固定 `velocityX/velocityY/damage` 以及投掷物的 `lobTargetX/lobTargetY/lobDuration/lobApexHeight`，用于断言风力、伤害、解析抛物线与对象池复位；名称表同时开放豌豆系、孢子、尖刺、星弹、卷心菜、玉米粒和黄油。`set_starfruit_shoot_cycle`、`set_cabbagepult_shoot_cycle` 与 `set_kernelpult_shoot_cycle` 都按 `row/col` 固定植物已累计时间与本轮间隔，只布置正式射击周期，不直接触发动画或发弹；玉米投手命令另可用 `butter=true/false` 固定下一发。`damage_plant` 按 `row/col/index`、`damage_zombie` 按 `row/index` 选目标并走正式 `TakeDamage` 链；两者的 `source` 可取 `PLANT/ZOMBIE/OTHER`（默认 `OTHER`），后者另可选 `penetrateShield`，用于来源词条、护盾、断肢和死亡动画测试。`squish_plant` 按 `row/col/index` 调用植物基类正式压扁入口，供绕过巨人/冰车/投篮车攻击时序独立验证植物侧表现。`show_plant_hp` 与 `show_zombie_hp` 用可选 `on` 布置同层血量文字，供截图验证组合实体布局。`set_adventure_level` 与 `force_trophy` 仅用于冒险进度结算测试；`survival_perk_refresh` 消耗本轮共享的一次刷新额度并重抽当前全部词条候选。植物/僵尸类型直接使用枚举标识符（例如 `PLANT_PEASHOOTER`、`ZOMBIE_FASTPAPER`），新增类型需要在 `Game/AutoTest/TestDriver.cpp` 的名称表中添加一行。
- **炉芯花与冰封阻断夹具：** `set_furnace_core_state` 按 `row/col` 设置炉芯花的 `cores=0..2` 与 `progress=0..10`，只布置权威资源状态且不重播声光；`attempt_ice_execution` 按稳定 ID 选择指定行/序号的处刑者与目标格植物并调用正式封存入口。专项必须断言成功保护时目标从未进入冰封、来源能力进入 `SPENT`，并覆盖重叠提供者顺序、资源耗尽回退、自身排除和存档。
- **冰裂钻机夹具：** `set_ice_crack_drill_state` 按 `row/index` 布置 `MOVING/CHARGING/SPENT`、剩余蓄力秒数与能力消费态；`interrupt_zombie_special_action` 走品种通用的未提交动作中断入口；`apply_winter_corrosion_to_zombie` 只对目标仍存在的冰制装备层施加独立腐蚀，并可用 `expectedChanged` 断言是否实际结算。三者均不直接生成地裂或伪造音效，专项应继续从正式更新边沿验证提交。
- **天气预报夹具补充：** 上述命令集中的 `actual=HEAVY` 限制是旧口径；当前只要公开预报或真实下一天气涉及 `HEAVY`，`set_weather_forecast` 就可用 `typhoonStrength=NONE/TYPHOON/SEVERE/SUPER` 与 `promptVariant=0..2` 固定公开警报/新大雨待生效等级。当前已是大雨且预报同档续期时，`typhoonStrength` 只固定公开预报，当前 Board 台风是揭晓实况；两者不同也会激活失败提示。`weather.forecastDisplayText`、`weather.failedForecastTyphoonStrength/actualForecastTyphoonStrength` 导出公开文案及失败卡片两边的台风等级，`weather.panelHeight` 与 `weather.nightRoofCharge.executionLineVisible` 用于锁定动态详情行。
- **完整选卡夹具：** `set_all_owned_cards` 只在进程内按正式冒险奖励顺序布置当前全部已实装卡，供完整选卡面板专项使用，不改冒险进度或真实 `PlayerInfo.json`。选卡状态投影导出当前页、总页数、实际活动/隐藏植物列表及分页按钮的资源、角度和相对锚点；`click target=choose_card_page` 在执行时解析当前分页按钮中心并走真实输入路径。
- **巨人锤击测试夹具：** `make_gargantuar_smash_ready` 按 `row/index` 选择处于 `SMASHING` 且尚未结算命中的巨人，把正式 `anim_smash` 推进到既有第 93 帧事件前；后续等待逻辑帧仍走目标快照、植物分层反应和命中音画的正式路径。
- **巨人投掷测试夹具：** `make_gargantuar_throw_ready` 按 `row/index` 选择处于 `THROWING` 且尚未脱手的巨人，把正式 `anim_throw` 推进到既有第 131 帧事件前；后续等待逻辑帧仍走生成、阵营继承、视觉锚点对齐和音画的正式路径。
- **急救员测试夹具：** `set_difficulty` 用 `value=1..4` 设置当前进程测试难度；`make_healer_ready` 只把活动急救员的冷却与重试归零，仍走正式选疗、前摇和结算，传 `all=true` 时在同一命令边沿同步放开全部匹配行的急救员，专用于动作边沿性能压力测试。`damage_zombie` 可选 `type` 先筛僵尸品种，再按稳定实体 ID 应用 `index`，适合同场多种防具的确定性修复验证。
- **植物伤害测试参数：** `damage_plant` 按 `row/col` 选择目标，可选 `type` 先筛植物品种，再用 `index` 打破并列；用于同格多层植物的确定性外伤验证。
- **麻痹测试参数：** `spawn_zombie` 可用 `paralyzedFor` 设置通过正式入口施加的麻痹秒数；用于验证中立控制与品种状态机的并行计时，非法时长或免疫目标会让命令失败。
- **植物生命夹具：** `set_plant_health` 按 `row/col` 选取该格顶层植物，并将 `value` 设置为不超过最大生命的当前生命值，只用于稳定覆盖指定反噬致死边界。
- **黄油状态夹具：** `butter_zombie` 按 `row/index` 调用目标的正式 `ApplyButter()` 虚入口，并用 `expectedApplied` 断言施加或免疫结果，供品种抗连控与存档边界做确定性验证。
- **通用僵尸控制夹具：** `apply_zombie_control` 按稳定 ID 顺序用可选 `row/index` 选择目标，`effect=SLOW/FROZEN/BUTTER/PARALYSIS`，`duration` 控制减速/麻痹时长，`expectedApplied` 断言正式入口是否接受；状态投影导出四类临时免疫余时、合并永久免疫后的 `controlImmunityMask` 和三类硬控状态。冻结返回 false 仍可能按正式寒冰菇语义造成 20 点伤害并保留未免疫的减速尾巴，脚本须分开断言伤害与控制。
- **黑夜屋顶雷荷命令：** `set_night_roof_charge` 仅对 `NIGHT_ROOF` 生效，用 `phase=CHARGING/WARNING/DISCHARGING`、`charge`、活动阶段的 `row` 和可选 `remaining/overcharge` 固定雷荷状态；只有实际跨越 `WARNING -> DISCHARGING` 才会结算一次实体效果，直接恢复/设置 `DISCHARGING` 不重复命中。`overcharge` 只在活动阶段生效，封顶25并在放电结束兑现为下一轮主电荷。自然满电时普通行与有效引雷实体统一选路；`dump_state.weather.nightRoofCharge` 导出 `guided/guideID/guideCandidateCount`、路线是否使用蒙特卡洛、耗时微秒、rollout/候选/样本数、最佳分和既有积累/阶段字段；植物另导出 `shutdown/shutdownTimerMs`，僵尸导出 `paralyzed/paralysisTimerMs/canBeParalyzed/groundHazardEligible`，异品种测试靶可从 `zombiesByType.<枚举名>` 稳定读取。
- **极夜雪原夹具：** `set_polar_environment` 用 `temperature/humidity/wind/direction/phase` 固定三仪表和白毛风阶段；`set_snow_hole` 用 `row/col/phase` 固定形成中或活动洞口，`seal_snow_hole` 走正式清穴语义。`weather.polarNight` 导出锁定计划、三实数/目标/阈值、趋势、强风方向、阶段余时、雪穴和待提交出生事务。

- **地图覆盖夹具：** `goto_level` 可选 `background=GROUND_DAY/GROUND_NIGHT/WATER_POOL/NIGHT_WATER_POOL/ROOF/NIGHT_ROOF/WINTER_GARDEN/POLAR_NIGHT_SNOWFIELD`，只在 AutoTest 场景创建时覆盖正式关卡背景；字段缺省时始终使用 `GameAPP::GetBackgroundID`。当前 5-9 正式使用白天 `ROOF`，6-1～6-9（内部 46～54）正式使用 `NIGHT_ROOF`；显式背景覆盖只用于隔离测试，禁止接入正常玩家入口。
- **扶梯测试夹具：** `add_ladder` 用 `row/col` 经正式 `Board::AddLadder` 在当前棋盘创建唯一扶梯，供攀爬、植物死亡、磁吸、台风换格与存档边界做确定性验证；`set_elite_ladder_scan_countdown` 用 `row/index/value=0..5` 只缩短目标精英扶梯的一次性行扫描倒计时，不直接结算能力；`ladders[]` 同时导出样式/贴图键、宿主存在性、二维换格视觉偏移及附件—宿主偏移误差的千分整数投影。
- **同步截图与测试状态：** `screenshot` 是等待型屏障：渲染器只有在 `IMG_SavePNG` 成功后才发布 ticket 成功，TestDriver 随后校验 PNG 存在且非空并写 `done`；脚本无需在截图后人为等待即可直接切场景。`reset_test_state` 显式恢复 `timeScale=1`、`devNoCooldown=false`、`devFreePlant=false`、`devSpawnPaused=false`、`monteCarloAIEnabled=true`、`advancedPauseEnabled=false`；`goto_level` 可传 `"resetTestState":true` 在切场景前复用同一复位函数，字段缺省时保持旧语义。状态 JSON 用 `timeScaleOn1000` 做倍速整数断言。
- **轻量蒙特卡洛 AI 测试：** `set_monte_carlo_ai` 用 `value=true/false` 切换 `GameAPP::mEnableMonteCarloAI`；`reset_test_state` 和带复位的 `goto_level` 都恢复为默认开启。状态 JSON 的 `monteCarloAIEnabled` 锁定总开关；蹦极与精英小丑导出各自的 targeting mode 和 rollout/候选/僵尸/卡槽计数，蹦极另导出详细植物数与压缩支撑数，急救员导出 `healerDecisionMode`、`healerDecisionAction`、`healerStrategicWaitMs` 及同组推演统计，黑夜屋顶满雷路线另导出 `route*` 统计。四类决策共用每次 rollout 最多 16 只僵尸的硬上限；详细植物上限 128，普通花盆/睡莲由 `simulation.supportOnly` 进入独立 64 格支撑数组。`smoke_zombie_monte_carlo_cap.json` 用 16 只实体锁定运行时入口，`smoke_monte_carlo_support_compression.json` 锁定支撑层不占详细容量且避雷花盆仍保留完整画像，纯数值 `plant-defense-monte-carlo` 测试锁定延迟候选、劫持者处决效用和同格阻挡层级。`stress_healer_monte_carlo.json` 用 `make_healer_ready all=true` 同步放开 10 名急救员并等待 ID 顺序的三固定逻辑步间隔预算全部完成；性能取证需加 `-Profile`，读取 `MC.Healer.Snapshot/Rollouts/Total` 的调用次数、单次平均/最大耗时，并结合 `GOM_Update` 与 `SceneUpdate_total` 最大值判断动作边沿尖峰，不能只看 60 帧平均 FPS。关闭开关后调用者必须保留原随机或确定性回退。`MainMenuScene` 也允许 `assert_state` / `dump_state` 读取这些 GameAPP 级根字段，供主菜单控制台走真实 CheckBox 点击路径验证。
- **高级暂停测试：** `set_advanced_pause` 用 `value=true/false` 切换 `GameAPP::mAdvancedPauseEnabled`；默认及 `reset_test_state` 均为关闭。状态 JSON 的 `advancedPauseEnabled` 锁定设置，`pauseGameplayInputBlocked` 锁定卡槽/草坪玩法输入是否被屏蔽：普通空格暂停且未启用高级暂停时为 true，`CHOOSE_CARD` 等非 `GAME` 状态也恒为 true。控制台测试必须走主菜单真实 CheckBox；Esc 完整菜单不承载此设置。
- **蹦极僵尸测试命令：** `set_bungee_altitude` 按 `row/index/value` 强制目标蹦极进入当前下降/上升阶段的指定离地高度；`set_bungee_bottom_countdown` 按 `row/index/value` 固定落地等待秒数，用于不新增动画帧事件地确定性覆盖落地、抓取、快速上升与离场。
- **迷雾测试命令：** `set_fog_weather` 用 `intensity=DEFAULT/SMALL/NORMAL/DENSE` 与 `duration` 固定当前合格迷雾关卡的基础雾、小雾、普通迷雾或大雾；`set_fog_forecast` 用同一组四档 `forecast/actual` 和 `revealIn` 固定独立雾势预报；`set_fog_dispersal` 用 `value=0..1` 固定台风驱散进度。`dump_state.fog` 导出档位、渲染层数、基础/有效左边界、逐列最大 alpha、可见雾格数、驱散百分比、有符号风向位移、预报、贴图分片加载数与动态大雾概率。通用雾场仍由 `NIGHT_WATER_POOL` 背景提供；固定复用完整迷雾的冒险关集中登记在 `AdventureProgression::HasLevelSpecificFogMechanics()`，当前仅 6-9 的 `NIGHT_ROOF`。迷雾脚本必须同时覆盖非资格关卡 no-op、6-8/6-9 边界、四档覆盖距离与层数、双向风偏移、完全吹散、停风回流、预报和关卡快照往返。
- **最终绘制坐标取证：** AutoTest 模式会采集 Animator 默认实例化与 `-NoInstance` 慢路径实际提交的世界四边形；所有 `AnimatedObject`（植物、僵尸、动画子弹与动画特效）按 tag 导出到 `animatedObjectsByTag`，包含 `renderProbeReady`、`renderPath`、`worldBounds`、相对视觉原点投影以及最近植物/僵尸 collider 关系。粒子按效果名导出到 `particleEffectsByName`，包含裁剪前实际粒子包围盒、相对发射原点投影、`clipRightXInt` 与最近实体关系。新增内容只把 C# 800×600 坐标当行为语义参考；稳定断言使用当前项目的格子/collider/最终几何相对量整数投影，并配合同步截图。修改 Animator 世界变换时还须让默认与 `-NoInstance` 同一静止用例的整数 `worldBounds` 一致。
- **僵尸分层受击观测：** `zombies.N.hitFlashMask` 与 `renderedHitGlowMask` 均以 bit0 表示本体/头盔/飞行额外生命、bit1 表示二类护盾；前者证明伤害层计时器，后者证明 Animator 实际轨道高亮。普通正面子弹命中持盾目标应为 `2`，大喷穿透同时伤盾与后层应为 `3`，等待白光结束后回到 `0`。
- **隔离关卡快照：** `save_level_snapshot` / `reload_level_snapshot` 的 `name` 只允许 ASCII 字母、数字、`_`、`-`，文件固定在当前脚本的 `autotest/out/<script>/snapshots/<name>.json`。保存复用正式序列化；重载先销毁旧 `GameScene`，再让同关卡的新场景在正常加载阶段用正式反序列化读取一次性路径。bullet 状态额外导出只读 `fromPool` / `poolType`，用于确认动画变种读档后仍归属原对象池槽位。
- **长时序隔离：** `set_spawn_paused` 以 `value=true/false` 暂停或恢复自然出波，不影响 `spawn_zombie`、`summon_next_wave` 等显式命令，适合成长、恢复和长计时测试；脚本离开隔离段前应显式恢复 `false`。
- **静止测试靶：** `spawn_zombie` 可加 `stationary=true` 把该实例的基础 Animator 速度设为 0，从而停止 `_ground` 位移；它不伪造冻结/减速状态，适合长时间射击成长与承伤验证。
- **合成输入（所有场景共用真实 click/key 路径）：** 现有 `plant`、`spawn_zombie` 等操作直接调用游戏逻辑，只覆盖 GameScene。驱动图鉴等非 GameScene UI 时使用 `click` / `key`；它们通过 `SDL_PushEvent` 注入合成事件，走与真实输入相同的路径（在下一帧 poll 时消费，并使用同一套 letterbox 坐标逆变换）。正常游戏没有运行时开销，因为非 AutoTest 模式下 `TestDriver::Update` 第一行就会返回。
  - `move_mouse`：`{ "op":"move_mouse", "x":440, "y":298 }`，只注入鼠标移动事件而不按键，适合截取悬停预览和 hover 状态。
  - `click`：`{ "op":"click", "x":570, "y":490 }`，可选 `"button"`（`left`，默认 / `right` / `middle`）和 `"hold_frames"`（默认 1，即按下到释放之间保持的帧数）。`x,y` 是**逻辑坐标**，与 UI 布局和 `dump_state` 的 x/y 使用同一坐标系；`target=trophy`、`target=restore_last_cards`、`target=choose_card_page`、`target=zombie_almanac_previous_page` 与 `target=zombie_almanac_next_page` 可分别在执行时解析奖杯、选卡按钮或僵尸图鉴翻页按钮中心。关卡选择页另提供 `target=game_select_previous_page`、`target=game_select_next_page`，以及带内部关卡号 `level` 的 `target=game_select_level`；后者只允许点击当前页实际创建且可用的入口，未解锁或其他页关卡会令脚本失败。冒险入口默认定位当前可挑战关所在页；生存入口只有在对应大关全部通关后才创建。状态投影的 `gameSelectSurvivalUnlockAreas` 锁定集中定义表中的解锁大关顺序。一次 click 会跨帧完成（按下沿 → 保持 → 释放沿）；若下一条命令要观察释放回调产生的状态，至少再等待两个 `wait_frames` 计数，让释放事件经过一个真实输入处理帧。
  - `assert_state`：`{ "op":"assert_state", "path":"perks.stacks.PLANT_DAMAGE_UP", "equals":3 }`，断言对象与 `dump_state` 写出的状态 JSON 相同。`path` 使用点分段；纯数字段用于索引数组（如 `zombies.0.type`）。数值字段也可用 `atLeast` / `atMost`（可同时给出）做闭区间断言。不匹配或路径缺失会失败并返回 exit 1。不要对浮点字段（如 `zombieHealthMult`）使用 `equals`，因为它执行 JSON 精确比较；优先断言整数投影字段（如 `plantDamageOn100`）。
    `ZombieAlmanacScene` 也支持这两条命令，导出 `scene`、`adventureLevel`、`encounteredEliteDancer`、完整的 `zombieAlmanacEntries` / `zombieAlmanacEntryCount`、当前页 `zombieAlmanacPageIndex` / `Number` / `Count`、`zombieAlmanacVisibleEntries` / `EntryCount`、前后翻页按钮状态，以及 `zombieAlmanacSelected` 和右侧详情 `zombieAlmanacPreview`。这些字段用于断言图鉴随冒险进度解锁、当前关不提前泄露、分页和详情动画状态；`encounteredEliteDancer` 是 PlayerInfo 的永久遭遇标记，但 AutoTest 只验证同一进程内的内存状态，不会读写真实玩家存档。
  - `key`：`{ "op":"key", "name":"space" }`，可选 `"action"`（`press`，默认，完整点击 / `down`，仅按下沿 / `up`，仅释放沿）。`name` 是键名字符串：`a`–`z`、`0`–`9`、`space` / `enter` / `escape` / `tab` / `backspace`、方向键 `up` / `down` / `left` / `right`、`f1`–`f12` 等；新增键名需要在 `TestDriver.cpp` 的 `kKeyNames` 中添加一行。
- **隔离性：** AutoTest 模式默认短路所有玩家存档读写（不读取或写入 `saves/`）；每次进入关卡都是确定性的全新关卡；`-Seed N` 固定随机种子。只有必须复现真实关卡存档问题时才显式加 `-AutoTestLoadSave`：此参数允许 `LoadLevelData` 从当前构建目录的 `./saves/` 读取关卡存档，但玩家设置仍用 AutoTest 默认值，且保存和删除入口继续短路，因此是严格只读模式。脚本快照是唯一显式写入例外，只能写当前脚本输出目录；禁止临时关闭 `GameAPP::mAutoTestMode` 绕过保护，否则可能触发真实存档写入、删除或迁移。
- **植物状态观测：** `dump_state` 根节点提供 `plantCount`、`bulletCount`、`repeatingShootingHeadCount`；Shooter 植物条目另含 `headTrack`、`headAnimPlaying`、`headAnimPlayState`，可配合 `assert_state` 验证附加头部 Animator。`scaredyShroomsByCell.<row_col>.fearState` 按格稳定导出胆小菇四态，不受同格南瓜成为 `topPlantsByCell` 的影响。
- **累计种植次数：** `plant` 可传 `expectedSuccess=false`，断言正式创建入口拒绝且不生成实体；默认仍要求成功。`eliteScaredyShroomsPlanted` 导出本关累计次数，`loadRestoreActive` 导出读档生命周期是否尚未结束。`smoke_elite_scaredyshroom_quota` 覆盖轮间选卡读档、新局、模仿者预占/变身以及死亡和铲除不返还。
- **示例：** `autotest/scripts/demo_peashooter.json`（验收脚本）以及各子系统的 `smoke_*.json`。

- **冬季地面冲击夹具：** `resolve_winter_ground_impact` 按 `row/col` 选择当前战斗顶层植物，以 `kind=COLLISION/GROUND_CRACK` 调用植物通用冬季冲击语义；`expectedIntercepted`、`expectedContainsScatter` 与 `expectedDownstreamMultiplierOn1000` 直接断言原子响应。该命令只替代尚未实现威胁的动作提交，不施加伤害或伪造雪橇落点。
- **寒潮预报依赖夹具：** `disrupt_cold_wave_forecast` 只调用 Board 正式干扰入口；`set_melt_snow_pult_shoot_cycle` 与 `set_melt_snow_pult_salt_state` 分别固定融雪投手本次出手和库存/蓄力状态。`meltSnowPultsByCell` 导出库存、蓄力与观测预报状态，bullet 条目的 `winterCorrosionDamage` 只投影盐晶携带的独立目标层腐蚀值；脚本仍须断言普通目标只承受基础 20 点本体伤害，并覆盖干扰前已离手盐晶的存档往返。

### 矿场9-3/9-4专项

`smoke_mine_pair*` 覆盖第二组地形、正式波次、雾潮、声波、晶角冲撞与跨品种交互。
`set_mine_fog` 在 `SupportsMineFog()` 允许的关卡设置雾潮已过游戏秒（`elapsed=-1`为无雾）和下一波，用于隔离伤害与视觉；正式首次/重复触发由 waves 脚本验证。第三组使用 `smoke_mine_third*` 和 `autotest/verify_mine_third.py` 检查实际波次、经济账本与合法召唤。
`wait_value` 与 `assert_state` 使用同一状态投影和点路径，但等待 `equals` 状态出现才继续，不直接提交能力；适用于冲撞开始/结束边沿、时间锚结算和声波已发射，仍受命令 `timeout` 保护。
`mine` 投影增加岩壁数量、雾强度/计时/下一波和晶角当波计数；`echoWaves` 导出冻结路图、已命中ID及冰墙标志。`plant` 是直接创建夹具，不扣卡费；成本验证须走真实卡槽/草坪点击。

### 全局模态与图鉴往返

`smoke_plant_reward_almanac` 覆盖真实奖杯结算后的新植物奖励页、无奖励/重打/已有卡跳过，
以及下一关选卡和返回首页两条导航。植物图鉴与奖励页共享 `plantAlmanacReward/Selected/Name/DescriptionLineCount/PreviewReady`
投影；截图与 dump 必须使用不同文件名（例如 `.png` 与 `.json`），避免后者覆盖截图。

`smoke_modal_navigation` 覆盖三层弹窗、背景按钮/滑块隔离、逐层取消与图鉴返回；`activeMessageBoxCount` 导出当前活动层数，图鉴索引/植物页的 `almanacReturnLevel` 为游戏来源关卡，首页来源为 -1。往返前后的 dump 与正式 snapshot 用于比较阳光、卡槽、对象和 Board 状态；不要把场景跳转成功当成原局恢复的充分证据。

### 小游戏专项

`smoke_minigame_last_savings.json` 从 `click target=main_menu_minigames` 进入独立选关页，以一次真实卡槽/草坪点击验证扣费，再用总成本 2875 阳光的固定阵型夹具与 125 余款验证卡池限制、无阳光掉落、快照往返、自然十波与奖杯返回；固定阵型夹具不代表完整卡槽布阵操作已被验证。输入阶段用正常倍速，只有等待战斗时加速；可见执行期间保持玩家输入与脚本分开，避免真实操作改变预算。`miniGame` 根投影由关卡号派生，沿用 `sun/normalSunCount/smallSunCount/nextWaveCountdownMs` 等现有权威投影。`smoke_minigame_navigation.json` 覆盖零预算不能落种、失败重开与冒险上次选卡隔离。


- **可见点击诊断：** `click` 命令可选 `trace=true`，在按下后与释放前将实际鼠标位置、目标矩形内按钮的 pressed/hovered 状态写入 `run.log`；仅增加日志，不绕过 SDL 输入和模态命中。`smoke_adventure_skip` 保留此取证，并覆盖满页选关确认框的最终绘制层级。


### 模仿者目标分页

`smoke_imitater_pagination` 使用 `click target=imitater_previous_page/imitater_next_page` 走真实翻页按钮，
`imitaterPagination` 导出独立页码、页数、实际活动候选及导航显隐/可用/纹理状态；完整候选仍在
`chooseCardImitaterDialogOptions`。`set_all_owned_cards` 可指定 `maxCards` 截取正式奖励前缀，验证早期单页卡池；
省略该字段仍包含全部已实装奖励，夹具只修改AutoTest内存。
