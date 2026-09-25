#pragma once
#include <algorithm>

/** 曙光莲正式结算与冷藏站推演共用的能力参数；不包含玩家是否点击的策略。 */
namespace DawnLotusRules {
inline constexpr float MaxEnergy = 60.0f; // 一次释放需要的能量
inline constexpr float PolarEnergyRate = 2.5f; // 极夜每游戏秒基础充能
inline constexpr float NormalEnergyRate = 3.0f; // 其他地图每游戏秒充能
inline constexpr float DangerEnergyRate = 1.0f; // 每个红色极夜仪表的每秒额外充能
inline constexpr int Damage = 1400; // 每行最高威胁目标受到的普通伤害
inline constexpr int SplashDamage = 300; // 同行主目标附近其他敌人的溅射伤害
inline constexpr float SplashRadiusCells = 1.5f; // 同行溅射半径，格宽倍数
inline constexpr int HealthThreatWeight = 16; // 总生命相对推进距离的威胁权重
/** 总生命优先，向房屋推进的距离作为附加威胁；并列由调用方按稳定 ID 决定。 */
inline long long ThreatScore(long long health, float x, float rightEdge) {
	return health * HealthThreatWeight + static_cast<long long>(std::max(0.0f,rightEdge-x));
}
}
