#pragma once

/** 正式棋盘、HUD 与 AI 共用的久守胜利时限；只由部队击杀植物重置。 */
namespace ColdStorageDefenseRules {
inline constexpr float WarningSeconds=300; // 连续没有部队击杀的游戏秒，随后显示最后攻势
inline constexpr float FinalAssaultSeconds=60; // 最后攻势倒计时，游戏秒
inline constexpr float SurrenderSeconds=WarningSeconds+FinalAssaultSeconds; // 自上次部队击杀起认输的总游戏秒
}
