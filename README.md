# 植物大战僵尸 - 同人游戏

使用 C++23、SDL2 和自研 Graphics 绘制接口开发的《植物大战僵尸》同人游戏，包含 Vulkan 与 OpenGL 渲染后端。

## 从这里开始

- [项目文档导航](docs/README.md)：按阅读目的查找指南、系统说明和历史方案。
- [构建与运行](docs/agent-guide/BUILD_AND_DEBUG.md#构建与运行)：环境准备、CMake 预设、运行目录和调试方法。
- [核心系统阅读地图](docs/systems/README.md)：对象、场景、关卡、存档和资源的源码入口。
- [AutoTest 验证指南](docs/agent-guide/AUTOTEST.md#autotest-测试套件)：自动验证的运行方式和取证要求。
- [Codex 工作规则](AGENTS.md)：开发任务的规则和技能路由。

## 开发入口

项目使用 CMake + vcpkg，普通开发默认使用 `clang-release`。在 Visual Studio 中以“打开文件夹”方式打开仓库根目录，使用 CMake 预设；详细环境准备与命令统一维护在上面的构建指南中。

当前预设包含本机工具链路径，换机器时先检查 [CMakePresets.json](CMakePresets.json) 中的 vcpkg、Vulkan SDK 与编译器配置。运行目录为 `build/<preset>/`，可执行文件为该目录下的 `PlantsVsZombies.exe`。

## 代码布局

| 目录或文件 | 用途 |
|---|---|
| [PlantVsZombies/Game](PlantVsZombies/Game/) | 玩法对象、关卡、场景和对象管理 |
| [PlantVsZombies/Game/Board](PlantVsZombies/Game/Board/) | Board 门面及按子域拆分的实现 |
| [PlantVsZombies/UI](PlantVsZombies/UI/) | UI 控件和输入处理 |
| [PlantVsZombies/Reanimation](PlantVsZombies/Reanimation/) | 动画系统 |
| [PlantVsZombies/ParticleSystem](PlantVsZombies/ParticleSystem/) | 粒子系统 |
| [PlantVsZombies/Graphics.h](PlantVsZombies/Graphics.h) | 公共绘制接口 |
| [PlantVsZombies/ResourceManager.h](PlantVsZombies/ResourceManager.h) | 资源加载与查询接口 |
| [autotest/scripts](autotest/scripts/) | 自动验证脚本 |

玩法对象采用继承式模型；空间、碰撞和点击等能力由宿主显式拥有。具体边界见[架构概览](docs/agent-guide/ARCHITECTURE_AND_RESOURCES.md#架构概览)。

## 致谢

- 感谢 PopCap Games 创造了经典的《植物大战僵尸》。
- 感谢 SDL2 等开源项目提供基础支持。
- 本项目仅供学习交流使用，不用于商业目的。

如有问题或建议，欢迎提交 Issue 或 Pull Request。
