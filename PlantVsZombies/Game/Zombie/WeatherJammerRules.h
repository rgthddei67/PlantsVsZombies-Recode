#pragma once

/** 正式设备与运输中断预测共用的本地时序。 */
namespace WeatherJammerRules {
inline constexpr float Channel = 4; // 全栏目黑障提交前的停步游戏秒
inline constexpr float Reboot = 5; // 中断后可正常行走的重启游戏秒
}
