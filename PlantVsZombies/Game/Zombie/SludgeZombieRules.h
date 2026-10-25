#pragma once

/** 淤泥喷射、植物挂泥和路障修复共享规则；能力时钟与挂泥寿命独立。 */
namespace SludgeZombieRules {
    inline constexpr int Health = 800; // 本体基础生命值
    inline constexpr int ArmorHealth = 1000; // 路障基础耐久
    inline constexpr int ArmorRepair = 300; // 成功喷出一次时恢复的现存路障耐久
    inline constexpr int Damage = 60; // 单团泥浆普通命中伤害
    inline constexpr int PumpkinMultiplier = 6; // 南瓜承伤倍率，击破不向下层溢出
    inline constexpr float RangeCells = 3.0f; // 索敌和泥团最大飞行距离，格
    inline constexpr float ProjectileSpeed = 320.0f; // 水平泥团速度，像素/游戏秒
    inline constexpr float Windup = 2.0f; // 停步蓄压前摇，基础行动秒
    inline constexpr float Reload = 5.0f; // 成功发射后的装填，基础行动秒
    inline constexpr float Retry = 4.0f; // 未提交动作取消后的重试，基础行动秒
    inline constexpr float SlowSeconds = 8.0f; // 挂泥寿命，固定游戏秒
    inline constexpr float ActionMultiplier = .5f; // 挂泥时正向能力倍率；同类不叠强度
    inline constexpr float RepairFeedbackSeconds = .9f; // 实际回血飘字寿命，游戏秒
    inline constexpr int TutorialLevel = 104; // 12-5 内部关卡编号
    inline constexpr int FirstWave = 6; // 首次候选波次，1-based
    inline constexpr int PerWaveLimit = 3; // 每波累计最多三只；无同时与全关累计上限
}
