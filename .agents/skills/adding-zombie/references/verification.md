# 僵尸专项验证

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 按改动范围选择验证

只处理本次构建引入的警告；新增 .cpp 未进入编译时先重新配置 `cmake --preset clang-release`。

### AutoTest 冒烟

`autotest/scripts/smoke_<name>.json`。默认按 [AutoTest 验证](../../../../docs/agent-guide/AUTOTEST.md) 的“当前桌面可见启动”方案运行：从 `build/<preset>/` 工作目录，用提升权限的 `Start-Process -WindowStyle Normal -PassThru` 启动并等待退出；普通沙箱 shell 即使写了 `WindowStyle Normal` 也可能落在隔离会话，主人桌面完全看不到。首次直造前断言 `HasReanimation`，运行时帽子/残肢/粒子贴图用 `GetTexture(key,false)` 导出加载状态；Release WARN 不保证写进 `run.log`，manifest 也不能替代这些断言。按稳定实体 ID、类型/行筛选或明确单体夹具选择目标，再断言 `type/hasArm/armVisible/hasHead/track/mindControlled`，不要依赖多对象数组下标；几何断言用 `animatedObjectsByTag.Zombie.N` 的最终世界包围盒及相对 collider 投影，禁止把 C# 绝对坐标写成期望值。**exit 0 ≠ 通过**：核对相关状态断言；新增或改变视觉时读取同步截图（断肢前后、编队站位、出土中段——换色变体必须截取真正使用 `rise*` 合成图的中段；注意升起初期整体在地面线下被裁掉是正确的，截图要卡升起 60% 时点）。
### 新增品种或改变死亡、动画、生命周期时专测死亡消失

（末-1 帧陷阱专项）：豌豆打死→dump 确认该 type 消失+run.log 无 WATCHDOG。炸弹类走 Die() 直杀路径，**测不到**死亡帧事件。若品种有出生随机动画倍率，等待上限必须按倍率范围下限计算并在 WATCHDOG 前留裕量，禁止沿用平均速度或旧固定秒数导致慢实例假失败。

### 夹具与诊断

- 时序：`wait_seconds` 是游戏秒；关卡 20 秒起第一波普通僵尸会混入 dump。精确断言碰撞目标或僵尸数量的隔离专项应在进入 `GAME` 后立即 `set_spawn_paused=true`；只有测试自然波本身时才保留生成，不能把首波当固定对照数。
- 站位/影子不对 → 本体调 gamedata offset（免编译）、影子通过 `GetShadow()` 调 `ShadowComponent` 参数。
- 父类测试钩子或状态投影使用 `dynamic_cast` 时会同时命中派生精英；先判断具体派生类，或以 `mZombieType` 排除变体，避免普通品种计数和命令误操作精英。
- Release Fatal Error / Access Violation 先保留崩溃报告与最小脚本，并用同次构建的 EXE/PDB 符号化；若仍不足以定位，给最小复现补充针对性的状态投影、日志或断言，能在 `clang-debug` 复现时可辅助诊断。优先检查资源注册/键和首个空对象来源；修复后重跑 Release 的新类型脚本和父类回归，禁止只交付 Debug 结果。
