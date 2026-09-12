# 坐标、分件与动画

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## 坐标换算铁律

**C# 原版逻辑场景是 800×600，本项目是 `SCENE_WIDTH=1100`、`SCENE_HEIGHT=600`。原版任何绝对 X/Y、范围端点、绘制偏移、碰撞框和粒子触发点都只能当语义参考，禁止直接抄入代码。**

- 场景范围由 `SCENE_WIDTH/SCENE_HEIGHT` 和 Board 当前背景几何重算；格子范围由 `GetCellCenterPosition`、`GetCellHeight` 与 `CELL_COLLIDER_SIZE_X` 派生。
- 屋顶植物只使用 `GetCellCenterPosition(row, col)` 的离散坡面格中心；不要复用僵尸的连续 `GetRowCenterYAtX`。花盆占 `under`、上层植物占 `normal`、南瓜占独立壳层，咖啡豆这类原版 flying 叠种体可占短时 `overlay`；普通植物/南瓜必须有花盆，地刺系仍拒绝屋顶。新局在选卡前且确认未进入读档生命周期后，按大关内编号生成 5-1 五列、5-2 四列、后续三列的初始花盆；C# 原关卡号分段与本项目九关制不同，必须按显示关卡语义映射，并保留外层列、内层行的创建顺序。
- 植物局部点位先换算到本项目以格子中心为 `GetPosition()` 的口径，再叠加当前 gamedata 视觉偏移；逻辑格位置与 `mVisualOffset` 永远分开。
- 发射点、范围边界和附加 Animator 基点优先表达成“相对稳定视觉原点/父轨基准姿态”的差值，不把 C# 的世界坐标塞进局部偏移。
- AutoTest 先执行同步 `screenshot`，再用 `animatedObjectsByTag.Plant` 的 `renderProbeReady/worldBounds/visualToRenderCenterD*Int/nearestPlant` 验证本项目最终绘制几何相对格子与植物 collider 的关系；优先用稳定实体 ID 或格位选择目标；只有依赖数组下标的单体夹具才限制为一株。
- 修改 gamedata offset、附件、整株变换或 `SetRenderScale` 时，在默认实例路径跑同一静止用例并比较整数 `worldBounds`；截图负责肉眼基线，运动对象瞬时绝对 X/Y 只供诊断、不作稳定断言。
- 战场主体按 `row N 植物 → row N 僵尸/扶梯 → row N+1 植物` 交错绘制；同排僵尸仍在植物之上，下一行植物遮挡上一行越界身体。植物运行期换行/搬格若改变 `mRow`，必须同步调用 `GameObjectManager` 的排序键刷新入口；小推车与子弹层不得顺带改动。专项同时断言语义 `renderLayer` 未变、实际 `renderOrder` 行带正确，并以默认屋顶跨行截图验收。

### 动画状态机

新植物优先完整复用动作语义相符的原版时间轴与核心分件，以换色加小型 follower/有限分件替换建立身份；AI 母图默认只做挂件或局部素材，不直接用大型合成图覆盖整株或核心头身。一次性→循环用 `PlayTrackOnce(track, returnTrack, speed, blendTime, returnSpeed, returnTrackBlendTime)` 自动接轨，完成信号 = `GetCurrentTrackName()` 变成 returnTrack（天然兼容存读档，勿自造 loop 计数）。复用经典角色时间轴并替换分件、校准轴心或循环时，同时使用 `adapting-classic-reanimation`，禁止把整株静态图硬切/交叉淡化冒充动态骨架。`blendTime` 只管进入一次性轨，`returnTrackBlendTime` 独立控制返回；原版若用 `SetFramesForLayer` 在重合/相邻包装轨边界硬切，最后一个参数显式传 `0.0f`，否则历史默认 0.5 秒可能插值出资源中不存在的部件姿态。包装循环轨的首帧必须与 body/face 等部件真正切换到该状态贴图的帧一致：若包装轨提前一帧开始，`PLAY_REPEAT` 每次回绕都会闪出上一姿态；逐帧核对 `<f>` 标记与 `<i>` 切图，并让专项在初始及跨多轮循环后都断言 Animator 不会进入切图前一帧。状态贴图内的细线发光元素还必须在最终战场缩放和实际昼夜/天气叠色下验色，不能只看高分辨率源图；描边占比过高会在缩小后吞掉内芯颜色，应让最终截图仍保留明确色相。原版若只给某一部件轨道做状态乘色/闪烁，使用 `Animator::SetTrackColor`，以 Board 的已保存游戏时钟驱动并在离开该状态、失败回滚和读档后显式恢复；不要给整株叠半透明矩形，也不要为纯派生周期增加存档字段。轨道乘色必须同时接入默认 instance 与 `-NoInstance`/OpenGL 矩阵路径，并用两张相隔半周期的同步截图确认只有目标轨道变化。返回速度与返回混合都属于待执行状态，主 Animator、Shooter 头和其他自管 Animator 必须一起保存；额外 Shooter 头统一调用 `Shooter::SaveHeadAnimatorState/LoadHeadAnimatorState`，禁止只存轨道/帧或复制一套私有序列化；旧档缺返回混合字段时默认 0.5 秒。
   - **运行时状态标志必须表达机制轮廓，不能把圆圈、十字或单条斜线占位直接当最终美术**：充能/就绪提示使用与能力一致的星芒、晶片、叶脉等分面轮廓，并在实机约 40～100px 尺度同时保留外轮廓、内切面和一处高光。跨多格范围边界在雪地、雾或高亮背景上不得只画单像素 `DrawRect`；用有宽度的半透明光带、主体色和高亮内芯分层绘制，避免健康数字、粒子或背景纹理一叠就消失。同步截图应分别保留完整状态标志与范围边界，不只截能力结算后的残片。
### 帧事件是全局帧号、跨轨道通用

（`Animator::mFrameEvents` 只按 int 帧号，不分轨道）：定下触发帧后必须核对**其他 anim 轨的活跃窗口扫不到它**（毁灭菇 51=explode(19..51) 末帧，sleep(52..76)/idle(0..19) 都够不着才安全；末帧触发安全——普通前进与循环回绕分支都覆盖）。原版动画速率≠reanim 基础 fps 时，用 `PlayTrack(track, 原版fps/reanim fps)` 折算（毁灭菇 23/12≈1.92）。`AddFrameEvent` 回调走一个指针大小的内联存储，只允许无捕获或 `[this]` 这类可无异常复制、平凡析构的小回调；需要 ID、字符串或可变上下文时把状态放回宿主并只捕获 `this`，禁止扩大捕获后改回每事件 `std::function`/堆分配。
### 整株世界变换必须覆盖复合 Animator 的两条 A/B 路径

默认路径会把根 Animator 与任意深度附件按轨道顺序递归写入 GPU `InstanceRecord`，`-NoInstance` 才整棵走矩阵慢路径；外层 `Graphics` 变换栈不会覆盖默认实例路径。应在 `Animator` 最终矩阵/`InstanceRecord` 两处统一实现世界变换，并递归同步现有与以后附加的子 Animator；AutoTest 同时断言根/子变换，默认与 `-NoInstance` 都要逐张检查截图。
### C# 复合头附件要补 `inverse(basePose)`

C# `AttachToAnotherReanimation` 的附件矩阵是父轨道当前姿态乘基准姿态逆矩阵，而本项目 `AttachAnimator` 目前只直接乘父轨道当前姿态。子 reanim 仍使用整株绝对坐标时，必须从根返回/待机轨首帧读取每条附件轨各自的基准姿态并在子 Animator 局部变换中抵消；基准旋转/缩放为单位时就是 `SetLocalPosition(-baseX, -baseY)`。没有 `anim_stem` 但身体/头分属两段包装轨的时间线，根 Animator 播身体轨，子 Animator 播头部待机/射击轨并挂到身体轨，同样要抵消身体轨首帧基准姿态；直接以 `(0,0)` 挂接会把该位移叠加两次。不能给多个头套同一个 `gamedata` 偏移，否则某个头会与茎错位；默认和射击轨都要可见截图校对。

12. **只有完整时间轴、没有 `anim_*` 包装轨的循环 reanim**（如 `FirePea.reanim`）用 `SetFrameRangeToDefault()` + `Play(PLAY_REPEAT)`，不要捏造轨道名或帧事件。非等比 `SetRenderScale` 的 pivot 是**世界坐标**；命中特效应传自身绘制基点，传 `(0,0)` 会把整个特效按比例拉向屏幕左上角。

### 瞄准光标、飞行阴影与落地焦痕必须分层建模

瞄准阶段可使用专属目标贴图，但开火后不得继续在冻结落点提交该贴图冒充弹丸阴影；在途投射物复用自身 `ShadowComponent` 与 `IMAGE_PLANTSHADOW`，按权威轨迹高度连续缩放，对象池复用和在途读档都重建派生布局。原版若给出非等比阴影（如 Cobbig 横向 3 倍）则保持其轴倍率，并把 C# 左上角偏移换算到本项目弹丸视觉中心，不能直接照抄世界坐标。落地后的 `Blastmark` 等焦痕仍由正式命中入口生成，不得因删除错误预告层而一并移除粒子。专项在无遮挡场景分别截高空与垂降帧，投影阴影启用、纹理资源和高度倍率，断言下降帧大于高空帧，再另行生成测试靶验证落地伤害/焦痕。
