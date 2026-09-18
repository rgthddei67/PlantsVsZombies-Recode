# 冷藏站美术来源与导出

使用内置 ImageGen 制作背景和透明图标，原始结果分别保存为 `cold_storage_imagegen.png` 与 `cold_storage_ice_icon_imagegen.png`。参考图为权威资源目录的 `background_day.jpg`，用于约束平地透视、外围建筑与可玩区关系。

背景提示规格：将参考背景改为手绘 Plants vs. Zombies 风格的灼热废弃冷藏站。左侧保留完整青绿色仓库和结霜入口，上下边缘布置制冷设备、管道及货箱，右侧连接炎热荒地。可玩区为暖赭色平地，正面、轴对齐、无梯形收束；五行九列，设备不得侵入。按 1880×720 目标画布保留 x=492..1212、y=138..638 的平地布局。不要角色、文字、UI、网格或测试标记。

图标提示规格：透明背景的手绘游戏小图标，一颗鲜绿色嫩芽长在清透的青蓝冰块上；轮廓清晰，适合缩为进度条小头像，无文字、背景或阴影底板。

ImageGen 结果再经 `../generate_cold_storage_assets.ps1` 分段校准与缩图，输出到唯一运行资产：

- `build/clang-release/resources/image/background_hot_cold_storage.png`
- `build/clang-release/resources/image/cold_storage_ice_head.png`

最终网格以 Board 为准，不修改 Cell 来迎合图片。原白天图和成品均已对照格位覆盖图；`smoke_cold_storage.json` 在四角与中心种植并截图，验证最终显示和落点。
