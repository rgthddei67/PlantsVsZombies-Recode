#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/** 承压护卫实体与指挥官共用参数；自身含灰烬的承伤与代伤共用滚动窗口。 */
namespace PressureGuardRules {
inline constexpr int BodyHealth = 600; // 本体生命，破盾后按普通伤害结算
inline constexpr int ShieldHealth = 3000; // 一类承压装置耐久，不接受治疗或自动修复
inline constexpr int Cost = 45; // 指挥官购买价格，冰块
inline constexpr int BiteDamage = 25; // 普通僵尸一半的基础单口伤害
inline constexpr int DamageLimit = 600; // 自身常规/灰烬伤害与全部代伤的共同滚动上限
inline constexpr double WindowSeconds = 1.0; // 伤害记录保留的游戏秒数
inline constexpr float Share = .8f; // 第一名护卫承接原伤害的比例
inline constexpr float AdditionalShare = .5f; // 后续每名护卫承接剩余伤害的比例，不限制人数
inline constexpr float RadiusCells = 1.0f; // 前后各一格，上下各一行
inline constexpr int AshDivisor = 5; // 被保护队友只承受五分之一灰烬，护卫自身不享受

/** 灰烬在斩杀与扣血前逐层缩放；逐层向上取整，正伤害不会因人数多变成免疫。 */
inline int ReduceAsh(int damage, int guards) {
    if (guards <= 0 || damage <= 0) return damage;
    damage = damage / AshDivisor + (damage % AshDivisor != 0);
    for (int i=1; i<guards && damage>1; ++i) damage = damage / 2 + damage % 2;
    return damage;
}

/** 首层保留五分之一余量，后续层按剩余整数的一半分担；不丢失或重复生成伤害。 */
inline int TransferDamage(int remaining, int layer, int& firstRemainder) {
    if (layer > 0) return remaining / 2;
    const auto numerator = static_cast<long long>(remaining)*4 + firstRemainder;
    firstRemainder = static_cast<int>(numerator%5);
    return static_cast<int>(numerator/5);
}

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
    /** 只读预判本击可接受的伤害；化灰快路径必须使用同一额度，不能提前消费窗口。 */
    float Preview(float damage, double now) const {
        return std::clamp(damage, 0.0f, (std::max)(0.0f, DamageLimit - Used(now)));
    }
    /** 只记录真正扣除的伤害；同一时刻的多个来源合并，封顶后不排队补伤。 */
    float Consume(float damage, double now) {
        std::erase_if(hits, [now](const Hit& hit) { return hit.expires <= now + 1e-7; });
        const float accepted = Preview(damage,now);
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
