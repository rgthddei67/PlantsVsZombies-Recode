#pragma once

/** 正式鼓舞与指挥官数值推演共用的时间、范围和加成；效果由目标按来源独立持有。 */
namespace CrystalDrummerRules {
inline constexpr int Health = 1600; // 鼓手本体生命，晶鼓不承担防具伤害
inline constexpr int Range = 3; // 连通格的曼哈顿距离，不能在棋盘外传播
inline constexpr float BeatInterval = 5.0f; // 包含停步前摇的基础周期，游戏秒
inline constexpr float Windup = 1.5f; // 停步敲鼓前摇，游戏秒
inline constexpr float FirstWait = .5f; // 出生至首次开始敲鼓的等待，游戏秒
inline constexpr float Duration = 6.0f; // 每个来源独立持续的游戏秒
inline constexpr float MoveBonus = .5f; // 每层额外移动倍率，异源加算
inline constexpr float BiteBonus = .75f; // 每层额外啃食频率倍率，不加速生产或修复
}
