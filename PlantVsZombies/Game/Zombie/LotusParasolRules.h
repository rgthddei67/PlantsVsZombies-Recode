#pragma once

/** 荷伞拦截与正式波次共用参数；伤害只消费伞面，不属于普通防具层。 */
namespace LotusParasolRules {
inline constexpr int Health = 1000; // 本体基础生命值
inline constexpr float Durability = 1500.0f; // 独立伞面耐久，保留半点伤害
inline constexpr float DamageMultiplier = .5f; // 拦截时按单发直击伤害扣伞比例
inline constexpr int Radius = 1; // 自身逻辑格与八邻格，共3×3
inline constexpr int TutorialLevel = 106; // 12-7内部编号
inline constexpr int FirstWave = 8; // 允许首次自然出场波次，1-based
inline constexpr int PerWaveLimit = 2; // 每波累计上限，死亡与魅惑不返还
inline constexpr int ActiveLimit = 3; // 同时在场及已承诺出生的数量上限
inline constexpr float ReflectPulse = .22f; // 伞面受击弹性演出，游戏秒
}
