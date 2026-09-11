---
name: project-pvz-mainmenu-button-arbitration
description: 主菜单石碑排版、ButtonManager命中仲裁、全屏控制台，以及未到2-1时的一次性跳关入口
metadata:
  node_type: memory
  type: project
  originSessionId: a52f35c5-40db-482b-b658-161219bc38ea
---

2026-07-07 (e65d869..43241cf, 未push) 主菜单四石碑按原版放大紧凑，定稿=缩放1.0、x=545、y=85/175/252/325（主人5轮视觉迭代：0.82紧凑→0.95嫌大→0.90嫌偏上→1.0铺满碑面→左移13px）。

核心机制：`Button::ContainsPoint` 是轴对齐矩形而石碑贴图是斜的+带透明边距，紧贴后判定框必然重叠。解法=**ButtonManager::UpdateAll 两遍命中仲裁**：先收集所有 `CanReceiveHit(鼠标点)` 的按钮，只把 hover/click 判给中心距离最近者；`Button::Update(input, bool hitAllowed=true)` 默认参数保证其他调用点行为不变。所有按钮更新都走 ButtonManager::UpdateAll（UIManager 委托），仲裁一处覆盖全局。

foot-gun：
- "LEVEL x-x" 数字坐标（MainMenuScene.cpp DrawLevel）与冒险按钮位置/缩放**硬绑定**，动石碑必须按贴图内相对坐标换算数字位置（rel=(旧坐标-旧pos)/旧scale，新坐标=新pos+rel*新scale）。
- smoke_mainmenu_buttons.json 的重叠带点击测试有假绿风险：若仲裁失效误进冒险，后面 wait_state CHOOSE_CARD 照样过——必须 Read 中间截图 after_overlap_click_still_menu 确认仍在主菜单。
- 石碑贴图各自透明边距不同，间距均匀靠截图迭代调 y，不是等差数列。

## 2026-07-28：未到 2-1 时显示跳关按钮

主菜单左下角增加绿色文字的「跳到 2-1」按钮，仅在 `mAdventureLevel < 10`
时创建。点击后由 `MainMenuScene::Update` 消费 pending 状态，避免在按钮回调内
切场景；入口固定进入内部关卡 10（2-1），同时把永久冒险进度只提升、不回退到
至少 10。

跳关会先保证初始豌豆射手存在，再按 `AdventureProgression::GetPlantReward`
补齐 1-1～1-9 的正式植物奖励，去重后立即 `SavePlayerInfo`。达到或超过 2-1
后返回主菜单不再创建该按钮。

专项 `smoke_mainmenu_skip_second_area.json` 可见验证关卡 10、黑夜背景、
`supportsWeather=true`、冒险进度 10、9 张应有植物，以及返回主菜单后按钮消失；
同时重跑 `smoke_mainmenu_buttons.json` 并人工查看
`after_overlap_click_still_menu`，确认原石碑重叠命中仍正确。

## 2026-09-05：Android 图鉴入口误跳小游戏

`SwitchTo("AlmanacScene")` 当场销毁主菜单；原分支没有 return，继续读取已释放对象的
`mReadyToSwitchMiniGames`，在 Android 暴露为误跳小游戏。进入图鉴后立即返回，其他
入口保持原有命中与切换逻辑。MuMu Android 12 真实点按图鉴进入正确索引；Windows
可见 `clang-release` 的 `almanac_click` 增加 `scene == AlmanacScene` 断言并通过，
exit 0 / passed，截图正确。不要把该生命周期错误误修成触屏坐标偏移。

## 2026-07-31：右下角全屏控制台

主菜单右下角新增绿色「控制台」按钮，点击后打开覆盖整个 1100×600 逻辑画面的深色
设置页。目前只有“蒙特卡洛小丑选择植物”一项，使用
`GameMessageBox::Builder::Checkbox` 创建真实 `Button`，勾选态直接绑定
`GameAPP::mEnableMonteCarloAI`；关闭并重开面板会从当前字段恢复视觉状态，正式退出
游戏时继续由既有 `PlayerInfo.json` 保存。

模态页打开时由 `SetMainMenuButtonsEnabled(false)` 统一屏蔽石碑、图鉴、选项、退出、
跳关与控制台入口。单纯禁用不影响 `Button::Draw`：原退出按钮由 `UIManager` 在
GameMessageBox 之后绘制时曾穿透全屏背景，因此它也改为 `SetSkipDraw(true)`，与其他
主菜单小入口一起只在无模态页时由 `DrawButton` 手动绘制。

专项 `smoke_mainmenu_console` 走真实入口/CheckBox/关闭/重开点击链，断言开关
true→false、重开仍为 false，保存状态 JSON 并截图启用/禁用两态；可见
`clang-playtest` exit 0。同期重跑 `smoke_mainmenu_buttons` exit 0，并人工读取
`after_overlap_click_still_menu`，确认重叠带点击后仍停留主菜单。

## 2026-08-11：控制台承载高级暂停

控制台新增“高级暂停（暂停时可选卡和种植）”CheckBox，与
`GameAPP::mAdvancedPauseEnabled` 直接绑定，默认关闭并进入玩家设置存档。该开关只放在
主菜单控制台；战斗 Esc/右上“主菜单”的完整菜单明确不重复展示。控制台仍沿用全屏模态
与统一背景入口屏蔽。`smoke_mainmenu_console` 现同时真实点击蒙特卡洛与高级暂停两项，
关闭重开后断言 `false/true` 保持，并以当前桌面可见 `clang-release` 运行 exit 0。

## 2026-08-22：模态窗口保留背景入口绘制

主菜单选项框或控制台打开时，入口按钮仍由 `DrawButton` 在 `LAYER_UI - 100` 提交；
`SetMainMenuButtonsEnabled(false)` 只关闭命中并清除残留 hover/pressed 状态。活动
`GameMessageBox` 由 `UIManager` 在普通按钮和滑块之后绘制，因此背景入口不会反盖弹框，
也不再用 `overlayOpen` 条件整组消失。关框后重新启用输入，视觉无需额外重建。

`mainmenu_options_shot` 现会在选项框打开后点击背景冒险入口，断言场景仍为
`MainMenuScene`，分别截图弹框、背景点击后和关闭恢复态，再真实进入 `CHOOSE_CARD`
证明入口已重新启用。当前 `clang-release` LTO 与
Win7 378 项导入审计通过；桌面可见运行该专项、`smoke_mainmenu_buttons`、
`smoke_mainmenu_console`、`pause_menu_shot`、`smoke_particle_layers` 均退出 0 且
`status=passed`，截图确认背景入口保留、模态层最高及世界粒子仍在暂停框下方。

2026-08-28 后冒险入口正式先进入 `GameSelectScene`；该回归的末端断言已同步改为选择关卡场景，
仍只验证关闭模态后冒险入口恢复命中，不再等待已经过时的直达 `CHOOSE_CARD` 流程。

## 2026-08-28：台风天气总开关与条件选项

控制台新增默认开启的“会出现台风天气”，直接绑定持久化的
`GameAPP::mTyphoonWeatherEnabled`。关闭后 `Board::SupportsTyphoon()` 统一拒绝所有台风入口；
“开局台风保护（第1～5波）”只有在总开关开启时才创建。切换总开关会在当前输入回调完成后重建
控制台，使保护项立即出现或消失；隐藏只代表当前无玩法意义，不改写已保存的保护偏好。
桌面可见 `clang-release` 的 `smoke_mainmenu_console` 42 条命令 exit 0、`status=passed`、
`script finished OK`；真实点击证明关闭总开关后原保护项位置不再响应，重新开启后恢复并保留偏好。
三张关键截图目验四项布局无重叠、关闭时只剩三项、重开后保护项按原勾选态恢复。

## 2026-08-28：控制台整行悬停说明

四个控制台选项都增加简短 Tooltip，分别解释蒙特卡洛决策、高级暂停、台风总资格和前 5 波保护。
复选框绘制仍是 50×46，但独立命中区覆盖 620×46 整行，因此悬停文字或点击文字都有效。
说明框按实际文字宽度跟随鼠标，越过屏幕边缘前换侧并保留 8px 留白；移出行后立即消失。
总开关关闭后重建面板，开局保护的控件、命中区和说明一起移除，保存偏好仍不改写。

`smoke_mainmenu_console` 现以 75 条命令断言四项实文、Y 坐标随同一行内鼠标移动、移出/隐藏后
`Visible=false` 且坐标归零，并用截图目验换侧与布局；整行文字点击同时证明扩大命中区生效。
同次桌面可见 `mainmenu_options_shot` 15 条与 `pause_menu_shot` 8 条也均通过。

## 2026-09-05：控制台说明自动换行与尺寸测量

`GameMessageBox::Builder::TooltipPanel` 只接收最大宽度与字号，高度由内容决定。
说明复用 UTF-8 码点换行，按实际绘制字体 `FONT_FZCQ` 测量；背景宽度为最长行加左右
内边距，高度为各行高度、行间距及上下内边距之和。控制台宽度上限保持 770 逻辑像素，
屏幕边缘定位使用同一份排版尺寸。排版按当前说明缓存，窗口缩放变化时重新测量。
`smoke_mainmenu_console` 的实文断言同步覆盖主人加长的台风说明，并保留长文、短文、
鼠标移动及隐藏后的截图与状态验证。相关天气技能及 references 的悬停、边缘和隐藏契约
仍适用，无需修改技能。

当前 `clang-release` 构建成功、Win7 378 项导入审计通过；vcpkg applocal 提示未找到
objdump 工具，但未阻断链接与运行。桌面可见 `smoke_mainmenu_console` 75 条命令
exit 0、`status=passed`、`script finished OK`，日志无 WARN/ERROR/FATAL；截图确认
加长说明完整分成三行并扩高背景，开局保护短说明保持单行且背景按实测宽度收缩。

## 2026-09-11：HXY专属

控制台在原四项之上增加默认关闭的「HXY专属」，沿用整行命中与 Tooltip，原四项坐标不变。新局固定难度1的70%出怪预算、额外300阳光、所有防具75%当前/最大生命；玩家偏好与关卡分别保存，续局不重复发放或缩放，详见 [HXY专属](project_pvz_hxy_mode.md)。原控制台专项的四项 Tooltip 断言同步当前源码已有的推荐文案。
