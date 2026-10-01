#pragma once
#include "ZombieType.h"
#include <array>

/** 正式能力与只读推演共用参数。 */
namespace AuroraPriestRules {
inline constexpr int MaxReleases = 3; // 每只祭司累计释放上限；钟匠回溯可恢复记录时次数
inline constexpr int BodyHealth = 1200; // 极光祭司本体生命
inline constexpr int DeviceHealth = 800; // 非磁性极光仪器生命
inline constexpr int NormalBite = 50; // 仪器完整时单口伤害
inline constexpr int OverloadBite = 150; // 仪器破坏后的过载单口伤害
inline constexpr float OutsideSpeed = 2.0f; // 最右列右缘之外的行走速度倍率
inline constexpr float Preparation = 6.0f; // 实体完成创建后的仪式准备游戏秒
inline constexpr float Windup = 2.8f; // 裂隙提交前可被警铃草打断的完整前摇
inline constexpr float Retry = 5.0f; // 被打断后再次尝试前的等待游戏秒
inline constexpr float Cooldown = 5.0f; // 每次裂隙提交后至下一次前摇的循环冷却游戏秒
inline constexpr float NormalSpeed = .65f; // 仪器完整时行动倍率
inline constexpr float OverloadSpeed = 1.15f; // 仪器破坏后行动倍率
inline constexpr float Unfold = .8f; // 已提交裂隙到场延迟，游戏秒
inline constexpr int Summons = 3; // 每次常态裂隙数量
inline constexpr int WhiteoutSummons = 4; // 白毛风提交时裂隙数量
inline constexpr std::array<ZombieType,5> SummonTypes{ // 裂隙兵种顺序参与稳定来源 ID 选兵
	ZombieType::ZOMBIE_BUCKET, ZombieType::ZOMBIE_DOOR, ZombieType::ZOMBIE_LADDER,
	ZombieType::ZOMBIE_POGO, ZombieType::ZOMBIE_FOOTBALL
};
}
