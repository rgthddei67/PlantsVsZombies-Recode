#pragma once

/** 急救员正式治疗与指挥官有界预测共用规则；不改变实体治疗决策模式。 */
namespace HealerRules {
inline constexpr float Cooldown = 5; // 每次成功治疗后的有效行动冷却，游戏秒
inline constexpr float CastDuration = 1; // 群疗与单疗共同的有效行动前摇，游戏秒
inline constexpr float RetryDelay = .5f; // 无伤员或原目标失效后的有效行动等待，游戏秒
inline constexpr float StrategicWaitStep = .5f; // 实体蒙特卡洛一次主动等待，游戏秒
inline constexpr float StrategicWaitMaximum = 2; // 实体单次治疗机会累计主动等待上限，游戏秒
inline constexpr float AreaRadius = 140; // 按实际碰撞中心计算的群疗圆半径，像素
inline constexpr float FocusedRadius = 280; // 单疗锁定和结算圆半径，像素
inline constexpr int AreaWoundedThreshold = 3; // 确定性回退选择群疗的最少伤员数，含施法者
inline constexpr int AreaHealAmount = 100; // 群疗对每个尚存生命层恢复量，生命点
inline constexpr int FocusedHealAmount = 400; // 单疗对每个尚存生命层恢复量，生命点
inline constexpr int ArmDisableDifficulty = 2; // 不高于此难度时断臂永久禁用治疗

/** 独立施法阶段副本；前摇停步不使用外部硬控计时，避免把自己锁成永久暂停。 */
struct Forecast {
    enum class Phase { IDLE, AREA, FOCUSED };
    bool present = false, enabled = true;
    Phase phase = Phase::IDLE;
    float cooldown = Cooldown, retry = 0, remaining = 0;
    float disableBodyHealth = 0; // 当前难度下断臂或掉头的最大本体生命阈值，整数规则投影
    int focusedTargetID = 0; // 已锁定的真实或预测实体身份；零代表没有目标
    float movementActivity = 1; // 本步未用于前摇的可活动比例，0..1；移动和啃食共用
};
}
