#pragma once
#include <algorithm>
#include <array>

/** 蓄洪僵尸炮击与选点/指挥官预测的共用规则。 */
namespace FloodMortarRules {
inline constexpr int Health = 1800; // 无防具本体生命
inline constexpr int Cost = 45; // 指挥官购买冰价
inline constexpr float FirstReload = 5; // 出生后首次装填，固定游戏秒
inline constexpr std::array<float,4> Reloads{10,8,6,4}; // 后续发射时按实际雨势锁定的装填秒数
inline constexpr int RangeCells = 6; // 向前选点最大水平距离，格
inline constexpr float FlightSeconds = 1.5f; // 炮弹发射至落地的预警游戏秒数
inline constexpr float SlowSeconds = 8; // 命中后攻击减速的固定游戏秒数
inline constexpr float AttackMultiplier = .4f; // 攻击间隔增加150%，只缩放攻击时钟
inline constexpr int PumpkinMultiplier = 3; // 每次爆炸对归并后的南瓜保护者伤害倍率
inline float Reload(int rain) { return Reloads[std::clamp(rain,0,3)]; }
inline int Damage(int rain) { return 100+50*std::clamp(rain,0,3); }
}
