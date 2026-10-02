#pragma once

/** 钟匠实体、Board 时间锚与指挥官预测共用的时间和容量；不改变正式技能。 */
namespace PolarClockRules {
inline constexpr int BodyHealth = 1000; // 钟匠本体生命
inline constexpr int ArmorHealth = 1200; // 非磁性星盘生命
inline constexpr float Preparation = 2; // 出生后首次准备，游戏秒
inline constexpr float Windup = 3.2f; // 可被打断的停步前摇，游戏秒
inline constexpr float RetryWait = 4; // 前摇被打断后的等待，游戏秒
inline constexpr float Cooldown = 10; // 提交至下一次前摇的等待，游戏秒
inline constexpr float AnchorDuration = 6; // 已提交锚独立存续至回溯，游戏秒
inline constexpr int TargetLimit = 12; // 单个锚最多记录的目标数，不重复记录已有锚的目标
}
