---
name: project-pvz-hxy-mode
description: 主菜单 HXY专属开关、新局阳光、出怪预算与防具缩放的存档契约
---

# HXY专属

2026-09-11：主人批准主菜单控制台「HXY专属」开关，默认关闭，Tooltip 列出全部效果。

- 新开局出怪预算固定使用难度 1 的 70%，不叠乘菜单难度；原地图、旗帜波、生存轮次预算修正和固定教学出怪保留。因此实际只数不保证严格为 70%。
- 在各地图原起始阳光上额外加 300（普通场地 350、矿场 500）。仅新局发放，生存换轮和续局不重复。
- 所有僵尸的头盔、护盾和独立保护层（气球）当前及最大生命乘 0.75，本体不额外缩放。与生存/词条生命倍率合并后四舍五入一次，路障 370→278、铁桶/铁门 1100→825、适应头盔 100→75、气球 20→15。
- 玩家偏好 `GameAPP::mHxyModeEnabled` 存入 PlayerInfo 的 `hxyModeEnabled`；新 Board 锁定同名布尔并存入关卡。旧档缺键为 false，续局服从该局保存的模式，不跟随菜单后来切换。
- `Board::CreateZombie` 在派生 Setup 完成后通过 `ApplyHealthMultiplier(fullMultiplier, armorMultiplier)` 缩放。`CreateZombieWithID` 只恢复保存值；派生 Load/时间锚不能重写已恢复的上限。适应头盔 `ApplyAdaptedOriginState` 仅在无有效上限时回退出生常量。
- 治疗沿用现有当前/最大生命夹紧；关闭模式的新局恢复普通耐久和预算。没有新增动画、帧事件或资源。

三个可调常量集中在 `PlantVsZombies/Game/Board/Board.cpp` 匿名 namespace 开头：

| 常量 | 当前值 | 用途 |
|---|---|---|
| `kHxyWaveBudgetMultiplier` | 0.7 | 难度 1 出怪预算倍率 |
| `kHxyStartingSunBonus` | 300 | 新开局额外阳光 |
| `kHxyArmorHealthMultiplier` | 0.75 | 防具当前及最大生命倍率 |

调参时同步更新 `MainMenuScene::OpenConsole` 的 Tooltip 与专项预期。

验证入口：`smoke_hxy_mode` 覆盖真实控制台勾选/悬停/重开、难度独立、各生命层、治疗上限、连续残甲读档、新生成单位及关闭恢复；`smoke_hxy_mode_survival` 覆盖生存倍率、换轮不赠送、上限读档及矿场阳光；`smoke_mainmenu_console` 回归既有四项设置。AutoTest 不读写真实 PlayerInfo，玩家偏好跨进程保存路径另由源码审查覆盖。

2026-09-11 验证：clang-release 编译与378项Win7导入检查通过；当前桌面可见 `smoke_hxy_mode`（103命令）、`smoke_hxy_mode_survival`（39命令）、`smoke_mainmenu_console`（76命令）、`smoke_adaptive_helmet_zombie`（160命令）均 exit 0 / passed，日志与同步截图已核对。技能及 references 审计已完成，防具倍率创建/恢复契约补入 adding-zombie 并通过 quick_validate；本次不改本体、附件或绘制资源，无新增视觉占位符。
