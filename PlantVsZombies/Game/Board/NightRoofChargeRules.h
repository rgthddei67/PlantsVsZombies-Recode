#pragma once

/** 黑夜屋顶与气象站正式结算、指挥官预测共用的雷荷参数。 */
namespace NightRoofChargeRules {
inline constexpr float kNightRoofChargeMaximum = 100.0f;     // 黑夜屋顶进入导电瓦路预警所需的满电值
inline constexpr float kNightRoofChargeLightPerSecond = 1.0f; // 小雨每游戏秒增加的黑夜屋顶电荷点数
inline constexpr float kNightRoofChargeMediumPerSecond = 2.0f; // 中雨每游戏秒增加的黑夜屋顶电荷点数
inline constexpr float kNightRoofChargeHeavyPerSecond = 3.0f; // 大雨每游戏秒增加的黑夜屋顶电荷点数
inline constexpr float kNightRoofChargeClearLeakPerSecond = 0.5f; // 晴夜每游戏秒自然泄漏的黑夜屋顶电荷点数
inline constexpr float kNightRoofChargeWarningDuration = 4.0f; // 满电锁定导电瓦路后的预警游戏秒数
inline constexpr float kNightRoofHijackerLockThreshold = 75.0f; // 每轮首次跨过此雷荷百分比时，从现存劫持者中锁定一次
inline constexpr float kNightRoofHijackerWarningDuration = 7.0f; // 满电且锁定仍有效时的全局预警游戏秒数
inline constexpr float kNightRoofHijackerFinalDuration = 1.0f; // 处决前停止移动和啃食、播放专属动画的游戏秒数
inline constexpr int kNightRoofHijackerSurvivalLineCap = 1200; // 生存模式处决线封顶，避免后期超高血量失去反制空间
inline constexpr float kNightRoofHijackerExecuteVolume = 0.55f; // 全场处决提交时的短促紫电碎裂声音量
inline constexpr float kNightRoofHijackerRainChargeBonusPerSecond = 4.1f; // 场上至少一只有效劫持者时，雨中每秒额外增加的雷荷；多只不叠加
inline constexpr float kNightRoofChargeDischargeDuration = 0.65f; // 基础坡面放电的可见游戏秒数
inline constexpr float kNightRoofOverchargeMaximum = 25.0f;   // 满电后最多截留给下一轮的余电点数
inline constexpr float kNightRoofPlantShutdownDuration = 8.0f; // 普通瓦面植物在放电快照中停机的游戏秒数
inline constexpr float kNightRoofWetPlantShutdownDuration = 20.0f; // 正在冲刷的湿坡面植物强导电停机秒数
inline constexpr int kNightRoofZombieDamage = 200;             // 普通瓦面地面僵尸承受的环境伤害
inline constexpr int kNightRoofWetZombieDamage = 600;         // 正在冲刷的湿坡面地面僵尸承受的强导电伤害
inline constexpr float kNightRoofZombieParalysisDuration = 1.5f; // 普通瓦面非车辆僵尸的麻痹游戏秒数
inline constexpr float kNightRoofWetZombieParalysisDuration = 5.5f; // 正在冲刷的湿坡面非车辆僵尸麻痹游戏秒数
}
