#pragma once

/** 雾渡僵尸实体规则；购买与能力预测尚未接入指挥官。 */
namespace FogFerryRules {
inline constexpr int Health = 2200; // 无防具本体基础生命
inline constexpr int Cost = 40; // 后续指挥官接入时的购买冰价，传送不另收费
inline constexpr float FirstWait = 5.0f; // 首次准备游戏秒，不受无雾冷却倍率影响
inline constexpr float Cooldown = 10.0f; // 有雾时成功传送后的冷却游戏秒
inline constexpr float ClearCooldown = 15.0f; // 无雾时成功传送后的冷却游戏秒
inline constexpr float Windup = 2.0f; // 未受照明时的有效施法游戏秒
inline constexpr float LitRate = 0.5f; // 路灯花照明下的施法进度倍率
inline constexpr int ClearDistance = 3; // 无雾时相对运输者向左传送的格数
inline constexpr int ClearLeftColumn = 2; // 无雾最深落点，零基第2列即玩家第3列
inline constexpr float PulseInterval = 0.25f; // 前摇粒子补发间隔，游戏秒
inline constexpr int VisualPassengerLimit = 12; // 每次只限制粒子人数，不限制实际运输数量
}
