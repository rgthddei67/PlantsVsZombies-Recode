#pragma once
#include <array>

/** 气压四连发正式帧与预测时序；原动画是12FPS，片段50..88。 */
namespace PressureShooterRules {
inline constexpr int Health = 1000; // 无独立防具的本体生命
inline constexpr int Cost = 20; // 指挥官购买冰价
inline constexpr int Damage = 25; // 每颗气弹的独立伤害
inline constexpr float Reload = 1.5f; // 第四发出膛到下轮起播的有效行动秒数
inline constexpr float ClipSpeed = 3.0f; // 36FPS；原版四帧点形成清晰分离的连射
inline constexpr float ProjectileSpeed = 420; // 平射像素/游戏秒；不受来源之后死亡或控制影响
inline constexpr std::array<int,4> Frames{60,68,74,80}; // 主人确认的真实帧事件编号
inline constexpr int FirstFrame = 50; // anim_shooting 起始全局帧
inline constexpr float FramesPerSecond = 12*ClipSpeed; // 枪头有效基准帧率
inline constexpr float MuzzleOffset = -27; // 普通朝向头轨原点到枪口的横向距离，像素
}
