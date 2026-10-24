#pragma once
#include "Game/Zombie/Zombie.h"

/** 祭司/钟匠 shader 与光屑共用的视觉锚点；不参与位置、碰撞或技能资格。 */
namespace PolarFinaleVisuals {
inline constexpr float ChannelX = 50.0f; // 普通骨架胸部相对视觉原点的 X，动画像素
inline constexpr float ChannelY = 55.0f; // 普通骨架胸部相对视觉原点的 Y，动画像素
inline constexpr float MarkLift = 28.0f; // 钟印中心相对脚底语义锚点向上偏移，世界像素
inline constexpr float AbsentFootY = 35.0f; // 已消失目标相对记录行心的落地偏移，世界像素

inline Vector ChannelCenter(const Zombie& zombie) {
	return zombie.GetVisualPosition()+Vector(ChannelX,ChannelY)*zombie.GetTransform()->GetScale();
}
inline Vector ClockCenter(const Zombie& zombie) {
	return zombie.GetIceTrapBottomAnchor()+Vector(0,-MarkLift);
}
}
