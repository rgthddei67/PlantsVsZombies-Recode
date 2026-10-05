#pragma once
#include "ShooterRules.h"
#include <algorithm>
#include <cmath>

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

/** 射击冷却、就绪后轮询与已起播发射分别推进；后台不读取附加头部 Animator。 */
struct AttackForecast {
    float cooldownRemaining = Interval; // 下一次起播前的射击冷却，攻击倍率作用于这些有效行动秒
    float checkRemaining = ShooterRules::TargetCheckSeconds; // 冷却就绪后才消耗的索敌游戏秒
    float pendingRemaining = -1; // 已起播但未吐出的雷种动画秒；负数表示没有待提交发射
    float sampledRate = 1; // 主线程采样时实际攻击倍率，调用方换算天气/领域后传给 Advance

    /** 按阶段边沿推进一次时间段；目标消失不取消已经起播的吐弹，轮询失败不重置射击冷却。 */
    int Advance(float seconds,float attackRate,bool hasTarget) {
        if (seconds <= 0 || attackRate <= 0 || !std::isfinite(seconds) || !std::isfinite(attackRate)
            || !std::isfinite(cooldownRemaining) || !std::isfinite(checkRemaining) || !std::isfinite(pendingRemaining)) return 0;
        int shots = 0;
        while (seconds > .000001f) {
            const bool cooling = cooldownRemaining > .000001f;
            float step = seconds;
            if (cooling) step = (std::min)(step,cooldownRemaining/attackRate);
            else step = (std::min)(step,(std::max)(0.0f,checkRemaining));
            if (pendingRemaining >= 0) step = (std::min)(step,pendingRemaining/attackRate);
            if (step > 0) {
                if (cooling) cooldownRemaining = (std::max)(0.0f,cooldownRemaining-step*attackRate);
                else checkRemaining = (std::max)(0.0f,checkRemaining-step);
                if (pendingRemaining >= 0) pendingRemaining = (std::max)(0.0f,pendingRemaining-step*attackRate);
                seconds -= step;
            }
            // 与实体帧事件先于 PlantUpdate 的顺序一致：同刻先兑现旧雷种，再开始下一次起播。
            if (pendingRemaining >= 0 && pendingRemaining <= .000001f) { ++shots; pendingRemaining = -1; }
            if (cooldownRemaining <= .000001f && checkRemaining <= .000001f) {
                cooldownRemaining = 0;
                checkRemaining = ShooterRules::TargetCheckSeconds;
                if (hasTarget) { cooldownRemaining = Interval; pendingRemaining = Windup; }
            }
        }
        return shots;
    }

    /** 1倍攻击速度下到下一发雷种的名义余秒；完整推演使用 Advance 独立推进两种时间。 */
    float NominalRemaining() const {
        const float next = (std::max)(0.0f,cooldownRemaining)+(std::max)(0.0f,checkRemaining)+Windup;
        return pendingRemaining >= 0 ? (std::min)(next,pendingRemaining) : next;
    }
};
}
