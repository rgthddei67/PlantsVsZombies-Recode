#pragma once

/** 豌豆式射手正式索敌轮询参数；射击冷却与索敌计时使用不同时间口径。 */
namespace ShooterRules {
inline constexpr float TargetCheckSeconds = .6f; // 射击冷却就绪后才累计的索敌游戏秒，不受攻击加速缩短
}
