#pragma once

/** 打捞能力参数；权重、首次出波和生存轮次由 gamedata.json 维护。 */
namespace SalvageZombieRules {
    inline constexpr int Health = 1000; // 本体基础生命值，无额外防具
    inline constexpr float RangeCells = 3.0f; // 本行前方打捞射程，格宽
    inline constexpr float Windup = 2.5f; // 可中断抛钩与拉拽前摇，基础游戏秒
    inline constexpr float Cooldown = 10.0f; // 成功拆除后的装填时间，基础游戏秒
    inline constexpr float RetrySeconds = 4.0f; // 未提交动作中断后的重试间隔，基础游戏秒
    inline constexpr int TutorialLevel = 102; // 12-3 的内部关卡编号
    inline constexpr int TutorialWave = 6; // 首次教学波次，1-based
    inline constexpr int TutorialLimit = 1; // 教学关每波与场上同时上限，各自计数
    inline constexpr int CompositeLimit = 2; // 后续关每波与场上同时上限，各自计数
}
