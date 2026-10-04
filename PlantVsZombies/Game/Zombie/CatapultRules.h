#pragma once

/** 投篮车实体与指挥官射击时序共用规则；篮球伤害仍从 Bullet 的正式弹型读取。 */
namespace CatapultRules {
inline constexpr int kBodyHealth = 850; // 普通投篮车出生本体生命，实体与候选画像共用
inline constexpr int kEliteBodyHealth = 1000; // 导流投篮车出生本体生命，其余射击规则沿用普通车
inline constexpr int kInitialBasketballs = 12; // 出生篮球库存，耗尽后恢复步行
inline constexpr float kReloadSeconds = 3; // 每轮装填的内部行动秒，减速/硬控影响，不随射击 clip 加速
inline constexpr float kShootStartInsideBoard = 150; // 车身原点进入逻辑棋盘右缘的此像素数后可投篮
inline constexpr float kMinimumTargetLead = 100; // 车身原点须领先目标弹心的最小像素距离
inline constexpr float kLobDuration = 1.2f; // 已离膛篮球的独立飞行秒，不随来源状态变化
inline constexpr float kShootClipSpeed = 2; // anim_shoot 相对资源 FPS 的固定倍率
}
