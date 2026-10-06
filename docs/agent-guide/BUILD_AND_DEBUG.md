# 构建与调试

[返回项目指南](PROJECT_GUIDE.md) · [全部文档](../README.md)

本页维护环境准备、构建预设、运行与崩溃诊断。自动测试启动和证据要求见 [AutoTest 验证](AUTOTEST.md)。以下相对命令从仓库根目录开始执行，运行时按正文切换到构建目录。

## 构建与运行

Windows、Linux、Android 的游戏与测试目标统一由 `cmake/cxx_standard.cmake` 启用 C++23，禁止静默降级或关闭 `char8_t`。配置阶段检查语言模式和 UTF-8 窄字符串编码；这不表示当前标准库实现了全部 C++23 功能。新增标准库功能先核对 MSVC STL（Windows）、libstdc++/libc++（Linux）及 NDK libc++（Android），按实际用到的 `__cpp_lib_*` 特性宏和各平台编译验证支持，不能仅依赖 `__cplusplus`。

源码、普通 `"中文"` 字面量及 SDL/JSON/日志接口统一使用 UTF-8 `char` 字节。Windows 强制 `/utf-8`，Linux/Android 强制 UTF-8 输入与执行字符集；`path::u8string()` 返回的 `std::u8string` 通过 `Utf8::ToString` 显式复制为字节，不能改用受 Windows 代码页影响的 `path::string()`。Windows/Linux 的 `utf8-paths` CTest 覆盖中文、非 BMP 字符、内嵌 NUL 与文件/JSON 往返。

仅升级游戏语言标准不会强制第三方库使用同一标准；保留各 port 的构建配置、运行库与 ABI。平台迁移至少验证 Windows `clang-release` + CTest + 相关可见 AutoTest、`android/build.ps1` 的原生编译与打包，以及 Linux 的 CMake 构建 + CTest。编译/打包通过不代表 Android 真机或 Linux 图形运行已经验收。

这是一个使用 CMake + vcpkg（manifest 模式）的 C++ 项目，正式平台为 x64 Windows，另有 [Android ARM64 试玩构建](../../android/README.md)。明确的 Android 构建使用 `android/build.ps1`、NDK 与 `arm64-pvz-android` triplet，不套用下方 Windows 编译参数。构建系统已于 2026-06-13 统一迁移到 CMake，不再使用 `.sln/.vcxproj`（`CMakeLists.txt` + `CMakePresets.json` + `vcpkg.json`，Windows triplet 为 `x64-windows-static`）；仓库内专用依赖通过 `cmake/vcpkg-ports` overlay port 提供。

- **构建（Codex 可自主运行）：** CMake 已加入系统 `PATH`，应直接调用 `cmake`，不再定位或硬编码 Visual Studio 自带的 `cmake.exe`。构建仍必须在 VS 开发者环境中运行，以便提供编译器、Windows SDK 和相关工具链。**关键顺序：先把 `vswhere` 所在的 Installer 目录加入 `PATH`，再导入 `VsDevCmd.bat`**；否则 VsDevCmd 内部调用 vswhere 时会输出 `'vswhere.exe' is not recognized`（构建仍能成功，但会产生噪声）。无噪声的一次性环境导入与构建命令：

  ```powershell
  # 1) 导入 VS 开发者环境（先把 Installer 加入 PATH，避免 vswhere 噪声）
  $env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
  $vs = & vswhere -latest -property installationPath
  cmd /c "`"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -no_logo && set" |
    ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] } }

  # 2) 所有任务的默认构建、F5 和 AutoTest 产物
  cmake --preset clang-release
  cmake --build --preset clang-release

  # 3) 只有明确需要 Debug CRT/Debug 语义或辅助定位 Release 问题时才切换
  cmake --preset clang-debug
  cmake --build --preset clang-debug

  # 4) Win7/旧环境出现 0xC000001D 时的同配置无 AVX2 发布版
  cmake --preset clang-release-noavx2
  cmake --build --preset clang-release-noavx2

  # 5) 仅在排查内存越界、UAF 等问题时使用；不能替代 Release 验收
  cmake --preset clang-asan
  cmake --build --preset clang-asan

  ```

  四个预设统一使用 `clang-cl + lld-link`。`clang-release` 是所有普通功能、逻辑、UI、资源、存档、性能和架构任务的唯一默认预设：直接用它迭代、F5、跑范围最小的 AutoTest 并交付。同一份当前源码已用 Release 产物完成相关验证时，不再必跑一轮 Debug 构建或重复 AutoTest。`clang-debug` 仅在主人明确要求 Debug CRT/Debug 语义，或 Release 问题确实需要辅助诊断时显式使用；`clang-release-noavx2` 仅用于 Win7/旧 CPU 兼容诊断；`clang-asan` 仅用于排查内存越界、释放后使用等生命周期问题。`clang-release`/`clang-release-noavx2` 用 `/Z7 -gline-tables-only -gcodeview-ghash` 与 `/DEBUG:GHASH`，只保留符号化函数栈和源码行所需的信息，不写变量/类型，并缩短 LLD 合并；EXE 只保留同目录 PDB 文件名的定位记录，不嵌入调试符号或本机构建绝对路径。四个 Clang 预设都会报告 `-Wnonportable-include-path`、`-Wreorder-ctor`、`-Wunused-*`、`-Wswitch` 等诊断，并应保持零警告。

- **Release 崩溃取证：** `clang-release` 的 Fatal Error / Access Violation 先保留 `crash_report_*.txt`、现场资源 WARN、触发脚本和崩溃阶段，不凭异常地址猜源码；同次构建的精简 PDB 可符号化函数栈、内联关系和源码行，但不能检查变量或类型。若 LTO 内联/合并使调用栈仍难以定位，优先给最小复现补充针对性的状态投影、日志或断言；同一路径能在 `clang-debug` 复现时可显式用其辅助诊断。新动画对象若在构造或首帧崩溃，先查 reanim 注册、动画类型映射和轨道资源；不要在 `Zombie`/`AnimatedObject` 基类添加宽泛空 Animator 早退，这通常只会把崩溃推迟到 `Start()`/`SetupZombie()` 并掩盖强制资源缺失。修复后用 `clang-release` 重建并重跑原失败脚本及父类回归。

- **AddressSanitizer 诊断：** `clang-asan` 使用 `RelWithDebInfo`、`/fsanitize=address /O1 /Oy-`，关闭 LTO、AVX2 与 identical COMDAT folding；只给游戏目标插桩，clang 的动态 ASan runtime 会自动复制到游戏 EXE 旁，三个纯逻辑 CTest 保持普通 ABI。vcpkg 静态依赖也保持普通 Release ABI，因此该预设关闭 MSVC STL 的专用容器注解，但普通堆栈/堆越界、use-after-free 等插桩仍启用。Windows ASan 的 shadow memory 会使用首机会异常，故该预设不注册项目 `CrashHandler`，报告以 stderr 为准。它不属于 Win7 发布矩阵（runtime 最低 Windows 8.1），跳过游戏目标的 Win7 import audit；修复后的最终验收仍必须回到 `clang-release`。运行 AutoTest 时可在导入 VS 环境后设置 `$env:ASAN_SYMBOLIZER_PATH=(Get-Command llvm-symbolizer.exe).Source`，并把 stderr 重定向到独立日志。

- **运行：** 可执行文件位于 `build\<preset>\PlantsVsZombies.exe`。`build\clang-release\resources` 与同级 `font` 是唯一实体目录；`clang-release-noavx2`、`clang-debug`、`clang-asan` 在首次配置时只创建 NTFS 目录联接，不复制资源。Shader、存档与 AutoTest 输出仍由各预设独立持有。运行游戏或 AutoTest 时，**必须以 exe 所在的 `build\<preset>\` 本身作为工作目录**：`Push-Location build\clang-release; .\PlantsVsZombies.exe -AutoTest <absolute-path>.json`。（⚠️ 根目录的 `x64\Release` 是陈旧产物，**禁止使用**。）
- **在 VS 中开发：** 用 Visual Studio 的“打开文件夹”打开项目根目录，VS 会自动识别 CMakePresets。根目录 `launch.vs.json` 已包含 F5 调试配置、工作目录和 `-Debug` 变体。
- **调试模式：** 使用 `-Debug` 参数运行可显示碰撞框。
- **源文件管理：** `GLOB_RECURSE CONFIGURE_DEPENDS` 会自动收集源文件，新增 `.cpp` 无需修改构建文件；不参与编译的文件放入 `CMakeLists.txt` 的 `REMOVE_ITEM` 列表（当前为 `Reanimation/AttachmentSystem.cpp`）。

依赖：SDL2、SDL2_image、SDL2_ttf、SDL2_mixer、Vulkan 1.2、Volk、OpenGL 3.3 Core、glm、nlohmann/json、pugixml、YY-Thunks。Vulkan运行时入口由 SDL2 选定 loader 后交给 Volk动态加载；Vulkan SDK继续提供头文件、VMA 与 `glslc`，但 EXE 不直接链接 `vulkan-1.dll`。Vulkan 最低设备能力仍包含 `VK_KHR_swapchain`、Vulkan 1.2 bindless descriptor indexing 所需 feature，以及至少 8192 个 update-after-bind combined image sampler；OpenGL 兼容后端不降低 Vulkan 要求，也不使用 Bindless 或 GPU Instancing。它保留纯 3.3 CPU Batch 基线，但在设备能创建 4.3 Core Context 时自动使用矩阵 SSBO 快路。默认 `clang-release` 要求 x64 + AVX2；`clang-release-noavx2` 的项目源码回到 x64 基线指令集，只用于排除 CPU/系统 XState 状态造成的 `0xC000001D`，不会降低 GPU 要求。

渲染器启动参数为 `-Renderer=auto|vulkan|opengl`，缺省等价于 `auto`：优先 Vulkan，初始化失败时销毁 Vulkan 对象和 Vulkan 窗口，再创建独立 OpenGL 窗口；强制 `vulkan` 不回退，强制 `opengl` 不触碰 SDL Vulkan loader。OpenGL 先尝试 4.3 Core，成功则用 GLSL 430 + 矩阵 SSBO 在 GPU 完成 batch 顶点变换；Context、shader 或 buffer 任一失败都保留原提交顺序并回落纯 3.3 Core、GLSL 330、CPU 矩阵展开与单 sampler 动态 VBO/IBO Batch。两路都使用 CPU Reanimation 慢路径、关闭并行 Draw record，但 `GameObjectManager` 的多线程 Update 不变。`-OpenGL33` 强制原 3.3 CPU Batch 供兼容故障规避和 A/B；`-NoInstance` 在 OpenGL 下仍可接受，不改变当前 batch 能力路径。显式 AutoTest 故障注入仅使用 `-TestVulkanInitFailure`，不得接入正常玩家行为。

Windows 发布产物以 Windows 7 SP1 x64（PE subsystem 6.01）为最低系统。项目内 `yy-thunks` overlay port 固定官方 v1.2.2 的 Lib 与 Objs 资产：Clang/LLD 把 Win7 替代 import libraries 放在 WinSDK 前，MSVC `link.exe` 直接链接官方 `YY_Thunks_for_Win7.obj`；例如 `CopyFile2`、`CreateFile2`、`GetSystemTimePreciseAsFileTime` 等新 API 会运行时探测并在 Win7 走回退。每个 Windows 可执行目标链接后，`cmake/assert_win7_imports.ps1` 都会用 `llvm-readobj` 将直接 PE imports 与随包 Win7 x64 导出表逐项核对；新增依赖若引入 Win7 不存在且 YY-Thunks 未接管的入口，构建必须失败，禁止靠放宽白名单掩盖。此兼容层只解决系统 API 装载门槛；CPU 指令集由构建预设独立决定，GPU 的 Vulkan 1.2 与 bindless 设备能力要求不会因此降低。

Vulkan 运行时把 dynamic rendering 与 synchronization2 **分别**选路：Vulkan 1.3 优先核心入口；1.2 驱动若提供 `VK_KHR_dynamic_rendering` / `VK_KHR_synchronization2` 就使用对应 KHR 入口；缺少任一扩展时分别回退到传统 RenderPass / `vkCmdPipelineBarrier` + `vkQueueSubmit`，不会因为只缺其中一个而放弃另一个快路径。兼容矩阵开关为 `-Vulkan12`（把协商限制到 1.2）、`-VulkanLegacyRendering`、`-VulkanLegacySync`，`-Vulkan12Fallback` 等价于三者同时启用。它们只用于测试和故障诊断，正常启动保持 1.3 核心快路径。

工具链：C++23；源码使用 `/utf-8` 编码（中文 UI 字符串所必需）；Unicode 字符集；vcpkg 静态链接。无头运行时，`CrashHandler` 通过 Windows Vectored Exception Handler 生成的崩溃对话框不会出现在 stderr。

### TestDriver 的 Release 编译例外

`PlantVsZombies/Game/AutoTest/TestDriver.cpp` 只在 AutoTest 模式执行重逻辑；Clang Release 对该翻译单元保留正式 ABI、AVX2、`-flto` 和精简行表 PDB，但在源文件级最后追加 `/Od`，覆盖目标级 `/O2`。这不会切换 Debug CRT、定义 `_DEBUG` 或退出正式 LTO 链路；普通游戏每个逻辑步只多保留一次未优化的单例取址与 `mActive` 早退，现有名称映射仍在启动时构造，但不进入 JSON 命令重逻辑。2026-08-25 同机 Ninja 日志实测对象编译由 440.47 秒降至 4.12 秒，随后 LTO 链接及后处理为 43.07 秒，Win7 378 项导入审计与可见 `smoke_quit` 均通过。若调整此例外，必须分别比较对象与最终链接耗时，不能只用整次并行构建墙钟时间判断。
