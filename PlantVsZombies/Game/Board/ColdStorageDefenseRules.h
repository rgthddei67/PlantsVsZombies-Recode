#pragma once

/** 正式棋盘、HUD 与 AI 共用的久守胜利时限；只由部队击杀植物重置。 */
namespace ColdStorageDefenseRules {
inline constexpr int LateWave=15; // 从第几波开始缩短无击杀等待
inline constexpr float EarlyWarningSeconds=180; // 前期连续没有部队击杀的游戏秒
inline constexpr float LateWarningSeconds=120; // 后期连续没有部队击杀的游戏秒
inline constexpr float FinalAssaultSeconds=60; // 最后攻势倒计时，游戏秒

/** 按当前已付款波次选取最后攻势触发时间，不重置已累计的无击杀计时。 */
constexpr float WarningSeconds(int wave) { return wave<LateWave ? EarlyWarningSeconds : LateWarningSeconds; }
/** 无击杀等待结束后仍保留完整最后攻势时长，返回总认输时限（游戏秒）。 */
constexpr float SurrenderSeconds(int wave) { return WarningSeconds(wave)+FinalAssaultSeconds; }
}
