#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/** 承压护卫实体与指挥官共用参数；窗口按游戏时间滚动，灰烬完全绕开。 */
namespace PressureGuardRules {
inline constexpr int BodyHealth = 600; // 本体生命，破盾后按普通伤害结算
inline constexpr int ShieldHealth = 4500; // 一类承压装置耐久，不接受治疗或自动修复
inline constexpr int Cost = 35; // 指挥官购买价格，冰块
inline constexpr int BiteDamage = 25; // 普通僵尸一半的基础单口伤害
inline constexpr int DamageLimit = 600; // 自身常规伤害与全部代伤的共同滚动上限
inline constexpr double WindowSeconds = 1.0; // 伤害记录保留的游戏秒数
inline constexpr float Share = .8f; // 队友原伤害中交由护卫承接的比例
inline constexpr float RadiusCells = 1.0f; // 前后各一格，上下各一行

/** 稀疏受伤账本；复制后只含数值，不引用实体或全局时钟。 */
struct DamageWindow {
    struct Hit { double expires = 0; float damage = 0; };
    std::vector<Hit> hits;
    /** 返回当前窗口已经实际消费的额度，过期记录不参与。 */
    float Used(double now) const {
        float used = 0;
        for (const auto& hit : hits) if (hit.expires > now + 1e-7) used += hit.damage;
        return used;
    }
    /** 只记录真正扣除的伤害；同一时刻的多个来源合并，封顶后不排队补伤。 */
    float Consume(float damage, double now) {
        std::erase_if(hits, [now](const Hit& hit) { return hit.expires <= now + 1e-7; });
        const float accepted = std::clamp(damage, 0.0f, (std::max)(0.0f, DamageLimit - Used(now)));
        if (accepted > 0) {
            if (!hits.empty() && std::abs(hits.back().expires - now - WindowSeconds) < 1e-7)
                hits.back().damage += accepted;
            else hits.push_back({now + WindowSeconds, accepted});
        }
        return accepted;
    }
    /** 将活体绝对截止时间转换为从零开始的快照余时，排除已过期记录。 */
    DamageWindow RelativeTo(double now) const {
        DamageWindow result;
        for (const auto& hit : hits) if (hit.expires > now + 1e-7)
            result.hits.push_back({hit.expires - now, hit.damage});
        return result;
    }
};
}
