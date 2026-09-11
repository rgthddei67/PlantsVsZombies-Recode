---
name: project_pvz_imitater
description: 经典模仿者的独立选卡入口、复合 Card 身份、同 ID 变身、褪色滤镜与跨后端验证契约
metadata:
  node_type: memory
  type: project
  updated_at: 2026-08-23
---

# 经典模仿者与原版褪色滤镜

2026-08-23 `PLANT_IMITATER` 是独立代理卡，不进入 `ChooseCardUI` 普通 8×6 分页。固定 66×93
`SeedChooser_ImitaterAddOn` 是紧贴选卡面板右下角的背景，无论卡在原位还是已飞入顶部槽位都保留；Card 本身位于
背景内并独立移动。点击后打开目标选择窗，窗内从已拥有的普通植物新建临时 Card，不搬动主面板对象，不允许选择
模仿者自身或紫色升级卡。普通选卡 Card 在半透明遮罩下继续绘制和移动，但其 collider 暂时关闭、不能穿透点击；
关闭窗口统一销毁临时对象并恢复主面板输入层级。

Card 的稳定身份仍为 `PLANT_IMITATER`，另持有 target 作为费用、冷却、预览和最终玩法目标。上次选卡以
`PLANT_IMITATER:PLANT_TARGET` 保存复合身份；取消选择时必须清空 target，使卡面恢复原始模仿者。Board 先按 target
验证放置与层级，再创建 Imitater 实体；它等待 2 秒并播放 `anim_explode`，到 80% 位置生成原版模仿烟，动画结束后由
Board 在原格以同一实体 ID 替换成 target，保持 Cell 引用、外部实体引用和存档连续。变身中阶段、计时、目标与 Animator
状态入档；加载不得重播音效、粒子或重复变身。

`ImitaterMorph.xml` 必须保留原版粒子场语义：`Circle` 的 X `[-140,-70]` 沿系统中心切线改位置，`Away` 的 X
`[100,150]` 沿径向改位置，两者按秒制 `dt` 缩放；不能把它们改写成 `Position`/`Acceleration`，否则云会整体向左
脱离植物。`ParticleField` 缺省轴为 0，未知 `FieldType` 记录警告并返回 `INVALID`，禁止静默回落为 Position。
`ImitaterClouds.png` 是一张 `ImageFrames=2` 的双帧条，外围八朵 puff 才使用切片纹理。专项用粒子 world bounds
与发射原点中心差锁定云团居中，并要求 bounds 与同格植物相交，防止只凭效果名存在掩盖坐标回归。

原版灰白不是纯白叠层，而是对目标纹理做 HSL：普通目标 lightness×1.8、saturation×0.2；
Hypnoshroom、Squash、PotatoMine、Garlic、LilyPad 使用较轻的 lightness×1.2、saturation×0.3。
Card 目标立绘、落点预览和最终植物都消费同一语义。Vulkan batch、Vulkan reanim instance、并行 replay、
`-NoInstance` 与 OpenGL 3.3 CPU batch 均有对应 filter pipeline/program；采样纹理已预乘 alpha，所以 shader 必须先除 alpha
还原 RGB、滤色、再乘回 alpha，避免纯白边缘或整株发灰发亮。

专项为 `smoke_imitater_ui.json` 与 `smoke_imitater_morph.json`。前者锁定固定背景、独立入口、43 张可选普通目标、
无紫卡、临时 Card 层级、选择/取消、复合上次选卡和卡面褪色；后者锁定同 ID 变身、粒子只触发一次、存读档、普通
WashedOut 豌豆射手和 LessWashedOut 睡莲 under 层。最终交付需在 `clang-release` 可见运行 Vulkan 默认、
Vulkan `-NoInstance` 与强制 OpenGL，并回归普通分页和上次选卡。

2026-08-23 最终 `clang-release` 可见证据：`smoke_imitater_ui` 在 Vulkan/OpenGL 均通过；
`smoke_imitater_morph` 在 Vulkan、Vulkan `-NoInstance`、OpenGL 均通过，居中探针为
`originToRenderCenterDxInt=-2`、`originToRenderCenterDyInt=0` 且与目标植物 bounds 相交；
`smoke_renderer_vulkan`、`smoke_renderer_vulkan_noinstance`、`smoke_renderer_opengl`、
`smoke_choose_card_pagination`、`smoke_last_selected_cards` 父回归均通过。默认 Alpha/Additive shader 未被替换，
两档滤镜只增加独立 pipeline/program，避免影响普通对象绘制。


2026-09-11 目标窗增加独立9×5分页：当前54个目标分45/9两页，底部左右箭头与页码，单页隐藏导航；
隐藏临时卡停用输入，选定/取消恢复主面板页，重开归首页。实现及93命令专项记录见
[选卡分页](project_pvz_choose_card_pagination.md#2026-09-11-模仿者目标窗独立翻页)。
本轮仅UI分页，按当前验证矩阵跑默认Vulkan可见 `smoke_imitater_pagination` 与 `smoke_imitater_ui`，均通过；
前述三后端矩阵为原滤镜实现的历史验收范围，不作为普通UI改动的重复要求。
