# AGENTS.md

本文件包含本仓库特有的规则与授权；通用协作偏好继承 Codex 全局 `AGENTS.md`。详细说明通过下方路由按需读取。

## 任务路由

- 构建、运行、使用 AutoTest，或修改架构、资源、存档行为前，先按 `docs/agent-guide/PROJECT_GUIDE.md` 的导航，阅读对应的构建与调试、AutoTest 验证或架构与资源契约主题。
- 项目历史原因、特殊例外、已知陷阱与搜索入口见 `docs/agent-memory/MEMORY.md`，按主题读取相关段落。
- 涉及植物、粒子特效、生存模式词条或僵尸时，使用 `.agents/skills/` 下对应技能的相关流程与约束。
- 涉及第十大关／大混战指挥官 AI 的能力预测、出兵与经济决策、真人日志诊断或训练／策略发布时，使用 `.agents/skills/improving-commander-ai/SKILL.md`；普通单位调参不自动触发训练。
- 涉及新增或实质重绘可玩地图/背景、Board 网格与 Cell 对齐、背景资源注册或地图缩略图时，必须使用 `.agents/skills/creating-pvz-board-map/SKILL.md`。
- 复用现有 reanim 时间轴制作新角色动画，或修正分件脱节、换图偏移与循环接缝时，必须使用 `.agents/skills/adapting-classic-reanimation/SKILL.md`。
- 涉及雨天天气本身，或任何按小/中/大雨生效的能力、变异、条件生成与系统联动时，必须使用 `.agents/skills/adding-rain-weather/SKILL.md`。
- 新增经典植物、僵尸或子弹前，先查阅 `D:\PVZ\PlantsVsZombies.NET-master\Lawn_Shared\Lawn`。动画出问题时，同时检查对应的 `resources/reanim/` 文件和 C# 参考实现。
- C# 原版逻辑场景为 800×600，本项目为 `SCENE_WIDTH=1100`、`SCENE_HEIGHT=600`；原版绝对坐标、偏移、碰撞框与粒子触发点不得直接照抄，必须换算到当前场景、Board 网格或对象稳定视觉原点并用相对量与可见截图验证。
- 新工作需要添加动画帧事件时，必须先询问主人。
- 本项目需确认的关键决策包括未定的核心玩法、平衡、玩家体验与兼容承诺；当前源码统一规则和已批准方案可推导的边界自主处理。

## 构建与验证

- 本项目是以 x64 Windows 为正式平台的 C++23 CMake/vcpkg 项目，另有 Android ARM64 试玩构建。Codex 可以自主构建；明确的 Android 任务使用 `android/build.ps1`，入口与限制见 `android/README.md`，普通 Windows 工作仍默认 `clang-release`。
- 主人已长期授权本项目正常构建所需的 vcpkg 依赖安装、CMake 配置/生成和编译；若沙箱阻止写入工作区外的 vcpkg 目录，直接申请提升权限执行，无需再次询问是否允许构建。该授权不包含删除 vcpkg、清空缓存或其他破坏性操作。
- CMake 已加入系统 `PATH`，直接使用 `cmake` 命令，不要再定位或硬编码 Visual Studio 自带的 `cmake.exe`。运行 CMake 前仍需先把 Visual Studio Installer 目录加入 `PATH`，用 `vswhere` 定位 VS，再导入 `VsDevCmd.bat -arch=x64 -no_logo`；准确的 PowerShell 步骤见项目指南。
- 所有普通功能、逻辑、UI、资源、存档、性能与架构任务，编译、F5、范围最小的诊断 AutoTest 和最终相关回归统一默认使用 `clang-release`。只有主人明确要求 Debug CRT/Debug 语义，或 Release 问题确实需要辅助诊断时，才显式切换 `clang-debug`。
- `clang-release` 使用 `clang-cl + lld-link`，启用 Release 级优化与 LTO，并以 CodeView 最小行表 + GHASH 生成只供函数栈/源码行符号化的精简外置 PDB，不保留变量和类型。EXE 只保留 PDB 定位记录，不嵌入调试符号或本机构建绝对路径。`clang-release-noavx2` 只用于明确的 Win7/旧 CPU 兼容诊断；不存在 MSVC Release 预设。
- `clang-release` 出现 Fatal Error / Access Violation 时先保留崩溃报告、资源警告和最小复现脚本，并用同次构建的 EXE/PDB 符号化；若精简符号或 LTO 合并仍不足以定位，优先增加范围最小的状态投影、日志或断言；能在 `clang-debug` 复现时可显式用它辅助诊断，修复后仍用 `clang-release` 完成相关回归。
- 必须从 `build\<preset>\` 运行；可执行文件为 `build\<preset>\PlantsVsZombies.exe`。禁止使用根目录下陈旧的 `x64\Release` 产物。
- Codex 启动任何需要主人看到的游戏或 AutoTest 窗口时，必须以 `build\<preset>\` 为工作目录，通过申请 `sandbox_permissions="require_escalated"` 的 shell 使用 `Start-Process -WindowStyle Normal -PassThru` 启动到主人当前桌面；普通沙箱 shell 即使指定 Normal 也不算可见运行。完整命令见项目指南。
- 修改游戏逻辑后，从构建目录运行范围最小且相关的 `-AutoTest` 脚本。AutoTest 默认用主人当前桌面可见的游戏窗口依次运行（不得默认隐藏或仅后台执行），检查退出码、`run.log` 和相关状态断言；涉及外观、布局、动画、绘制或资源显示时再检查同步截图，纯数值/非视觉逻辑改动不强制补截图；只有主人明确要求后台运行或执行环境确实无法显示窗口时才可例外，并须说明。仅修改文档时无需构建游戏。
- 多对象交互优先用稳定实体 ID、格位或聚合状态断言，只有依赖数组下标的单体夹具才限制对象数量。
- AutoTest 在 `wait_seconds` 后取得的运动对象绝对 X/Y 会受当前倍速、逻辑步落点和实际取证时点影响，不得作为稳定断言。验证同步或相对运动时，优先导出同一状态下的相对量（例如 `round((maxX-minX)*1000)` 的整数投影）并断言跨度、次序或其他相对关系。

## 仓库规则

- 源文件由 `GLOB_RECURSE CONFIGURE_DEPENDS` 自动收集；新增 `.cpp` 无需手动修改构建列表。
- 每个新 `.h` 必须以 `#pragma once` 开头；pre-commit hook 会自动检查。
- `build\clang-release\resources` 与同级 `font` 是唯一实体运行资产；`clang-debug` 与 `clang-release-noavx2` 通过 NTFS 目录联接共享。资源只修改权威目录，禁止复制或维护其他 preset 的资源副本。
- `manifest.txt` 中出现文件不等于资源已按预期注册或能用目标键取得。新增或修改 reanim、运行时换图、粒子贴图时，必须核对“文件/清单 → 对应 loader 或 `resources.xml` 注册 → 实际资源键 → `HasReanimation`/`GetTexture(key,false)` AutoTest 断言”闭环；强制资源缺失应修注册或键来源，禁止用通用空 Animator/空纹理兜底掩盖根因。
- 代码文件统一使用 UTF-8（无 BOM），由根目录 `.editorconfig` 约束；中文文本保持 UTF-8。逻辑网格位置与视觉偏移（`mVisualOffset`）必须分离。
- 交付前只核对本次变更触及的技能契约；发现可复用的新接口、所有权、存档规则或验证陷阱时更新对应技能/reference，改过的技能须运行 skill-creator 的 `quick_validate.py`。不涉及运行行为的文档或技能整理无需构建游戏。

## 文档与记忆

- 复杂玩法只有在需跨任务/跨会话接续、暂不实现，或主人要求时，才保存一份简短设计：确认的决定、原因、关键边界和未定项。讨论中的每轮修改不另建文件，实施计划仅在依赖关系确实需要时使用。
- 实现前的设计可以保存待实现数值；实现后标明已落地并链接源码入口，旧数值只作为当时的决策记录，不要求随以后每次调参同步。只在实际行为改变导致仍有效的契约或操作指导失效时更新相关段落，不批量重写历史。
- 项目记忆索引只写主题及入口；已有交接设计直接链接，不另建重复摘要。
- 本项目授权由实际完成任务的窗口自行判断并维护有价值的项目记忆：已有同类经验则合并，无新经验就不写，无需重复询问主人。旧数值摘要按触及范围逐步清理，不为调参全量重写历史；这些轻量规则适用于本项目各技能及项目记忆。

## 视觉特效

- 技能与战场特效可以采用着色器实现流动、渐变、柔边、辉光、溶解等程序化表现，并按需要结合少量粒子；根据效果质量和性能选择实现方式，不要求所有特效都使用着色器。
- 几何图形可以用于轮廓、遮罩和结构，但不要仅把简单圆圈、方框或线条堆叠、放大后作为完整特效。成品应有与技能相符的明暗层次、边缘质感和阶段变化；已有获认可的简洁效果不因使用几何图形而被否定。
- 优先让局部演出清楚体现蓄力、生效、持续和结束，并保持战场可读。以实机尺寸和连续播放验收，不能只看放大的静态图；视觉时序跟随实际技能状态，不为演出擅改玩法时机。

## 代码注释

- 项目注释风格以 `Graphics.h/.cpp` 为参考。
- 公共接口优先使用 Doxygen，优先写在头文件，并仅在语义不直观时补充参数、返回值、单位、范围、副作用或生命周期。自解释的一行 getter/setter、简单转发和显然的重写可省略函数级注释。
- 在 `namespace`（尤其匿名命名空间）中集中声明的可调常量、权重、时间、倍率等参数，必须在声明行末接中文注释，说明用途、单位或调整含义，方便后期集中修改；纯实现细节且无调参含义的常量可按需注释。
- 注释中的代码标识符和必要术语保留原名以便搜索。

## Git 与交付

- 主人长期明确授权：Codex 完成本仓库任务并验证通过后，默认提交并 push，无需主人逐次回复“push”。授权目的地为 `origin`：`https://github.com/rgthddei67/PlantsVsZombies-Recode.git`，推送到当前工作分支已明确配置的上游分支（当前为 `master` → `origin/master`）；授权包含任务相关源码、资源、文档及对应提交历史的上传与发布。
- 任务相关改动验证通过且可常规 fast-forward 时，按上述授权直接推送；目标不符、上游不明、混有无关提交或无法快进时，保留本地提交并说明原因。
- 该默认授权不包含 force-push、改写已发布历史、删除远端分支、向其他目的地发布，或上传无关/敏感内容。主人本次明确要求“不提交”“不 push”或“只在本地”时优先遵从。
- 沙箱或网络限制要求提升权限时，引用上述长期授权按正常审批机制申请执行，无需额外向主人询问同一授权；若自动审批仍拒绝，说明被拒动作和具体原因，不绕过审批，也不宣称仓库规则能覆盖系统策略。
- 完整新角色交付时给出关键调参入口及必须联动修改的参数；新增或修改外观时检查受影响视觉是否完成。
