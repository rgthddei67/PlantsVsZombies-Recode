#pragma once

/** 雷鸣花正式攻击和指挥官推演共用参数；目标抗性由僵尸拥有。 */
namespace ThunderFlowerRules {
inline constexpr float Windup = 14.0f / 18.0f; // 继承豌豆射手50..64帧、12FPS与1.5倍发射轨，游戏秒
inline constexpr float Interval = 2.0f; // 两次攻击间隔，游戏秒
inline constexpr int Damage = 20; // 三行局部范围内每个目标的普通攻击伤害
inline constexpr float Paralysis = 0.4f; // 每次麻痹的游戏秒数，不刷新已有麻痹
inline constexpr float Resistance = 1.5f; // 自身麻痹结束后拒绝雷鸣花再次麻痹的秒数
inline constexpr int ControlLimit = 6; // 单次最多成功麻痹的不同目标数
inline constexpr float RadiusCells = 0.75f; // 落点两侧的水平溅射半宽，格
inline constexpr float ProjectileSpeed = 290.0f; // 豌豆式平射速度，像素/游戏秒
}
