#pragma once

#include <algorithm>

/** 气球实体与指挥官共用的额外生命、阶段和吹飞规则；不创建实体或读取正式随机数。 */
namespace BalloonRules {
inline constexpr float ColliderRise=60; // 飞行碰撞框相对地面抬高的像素
inline constexpr int Health = 20; // 气球额外生命，不混入本体或可修复头盔
inline constexpr float FlightVelocityMinimum = 23; // 出生飞行速度下界，像素/游戏秒
inline constexpr float FlightVelocityMaximum = 37; // 出生飞行速度上界，像素/游戏秒
inline constexpr float PopClipSpeed = 2; // 爆裂动画相对资源帧率的固定播放倍率
inline constexpr float BlowHouseDistance = 400; // 每株朝屋后吹飞追加的位移，像素
inline constexpr float BlowSpeed = 600; // 吹飞期间独占横移的基础速度，像素/内部行动秒
inline constexpr float BlowFrontPadding = 80; // 前线侧屏幕外清除余量，像素

enum class Phase { FLYING, POPPING, WALKING };

/** 单次额外层结算；调用方扣 absorbed 后，才将 remainder 交给普通防具/本体入口。 */
struct Hit {
    float absorbed = 0, remainder = 0;
    bool drowned = false;
};

/** 只读画像和纯数值阶段推进；health 属于总生命中的额外气球份额。 */
struct Forecast {
    bool present = false;
    Phase phase = Phase::FLYING;
    float health = Health, maximumHealth=Health; // 独立层出生上限，沿用正式生命倍率
    float popDuration = 0, popRemaining = 0; // 未放大的爆裂动画秒；资源范围由 Board 主线程采样
    float flightSpeed = 0, windMultiplier = 1; // 原始飞行像素/秒与独立风倍率；飞行不乘雨势或根运动动画倍率
    float walkSpeed = 0; // 落地后的稳态速度，采用与普通 Unit.body.speed 相同的采样倍率口径
    bool poolRow = false, blowing = false, towardHouse = false;
    float blowRemaining = 0; // 朝屋后剩余位移，像素；向前线以离屏位置为终点

    bool CanTargetProjectile(bool targetsFlying) const {
        return present ? (targetsFlying ? phase == Phase::FLYING : phase == Phase::WALKING) : !targetsFlying;
    }
    bool CanEat() const { return !present || phase == Phase::WALKING; }
    bool CanFreeze() const { return !present || phase == Phase::WALKING; }

    /** 先吸收额外层；爆裂后停止飞行，水路击破直接死亡，溢伤仍由本体结算。 */
    Hit AbsorbDamage(float damage) {
        Hit result{0, (std::max)(0.0f,damage), false};
        if (!present || phase != Phase::FLYING || health <= 0) return result;
        result.absorbed = (std::min)(health,result.remainder);
        health -= result.absorbed; result.remainder -= result.absorbed;
        if (health <= 0) {
            phase = Phase::POPPING; popRemaining = popDuration;
            blowing = false; blowRemaining = 0;
            result.drowned = poolRow;
        }
        return result;
    }

    /** 推进爆裂演出；动画倍率和硬控由调用方提供，完成时由调用方恢复行走速度。 */
    bool AdvanceLanding(float animationSeconds) {
        if (!present || phase != Phase::POPPING || animationSeconds <= 0) return false;
        popRemaining = (std::max)(0.0f,popRemaining-(std::max)(0.0f,animationSeconds));
        if (popRemaining > 0) return false;
        phase = Phase::WALKING;
        return true;
    }

    /** 三叶草仅作用于仍有气球的飞行阶段；同向朝屋后多次提交累计距离。 */
    void BeginBlow(bool house) {
        if (!present || phase != Phase::FLYING) return;
        blowRemaining = house ? (blowing && towardHouse ? blowRemaining : 0)+BlowHouseDistance : 0;
        blowing = true; towardHouse = house;
    }

    /** 吹飞占有本步横移；返回是否已从前线侧离屏清除，朝屋后仍由普通进屋判定收口。 */
    bool AdvanceBlow(float activeSeconds,float& x,float rightEdge) {
        if (!present || phase != Phase::FLYING || !blowing) return false;
        const float step = BlowSpeed*(std::max)(0.0f,activeSeconds);
        if (towardHouse) {
            const float applied = (std::min)(step,blowRemaining);
            x -= applied; blowRemaining -= applied;
            if (blowRemaining <= 0) blowing = false;
            return false;
        }
        x += step;
        if (x < rightEdge+BlowFrontPadding) return false;
        x = rightEdge+BlowFrontPadding;
        return true;
    }
};
}
