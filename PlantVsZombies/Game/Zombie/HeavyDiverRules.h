#pragma once

/** 重潜僵尸独有参数；水中减伤只作用于直射弹丸，不扩展到范围、持续或近战伤害。 */
namespace HeavyDiverRules {
inline constexpr int Health = 3000; // 本体基础生命值，无独立可磁吸防具
inline constexpr float MoveMultiplier = .7f; // 普通僵尸步频和移速倍率
inline constexpr float SubmergedSeconds = 8.0f; // 每次水下推进阶段，游戏秒
inline constexpr float BreathingSeconds = 4.0f; // 换气破绽持续时间，游戏秒
inline constexpr float DirectDamageMultiplier = .4f; // 水下直射只承受40%伤害
inline constexpr int SmashDamage = 600; // 抡锚基础单次伤害，按普通顶层受击顺序结算
inline constexpr float SmashCycle = 3.0f; // 连续抡锚两次命中间隔，基础游戏秒
inline constexpr float Windup = 1.2f; // 首击及每次抡锚的可见前摇，基础游戏秒
inline constexpr float VisualSubmerge = 34.0f; // 水下额外视觉下沉量，像素；不改逻辑坐标
}
