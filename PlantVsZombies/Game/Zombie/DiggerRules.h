#pragma once

#include <algorithm>
#include <cmath>

/** 矿工自身的阶段规则；后台只保存数值副本，不访问实体、动画或资源。 */
namespace DiggerRules {
inline constexpr float TicksPerSecond = 100.0f; // 原版厘秒位移换算到像素/行动秒
inline constexpr float TunnelVelocityMinimum = .66f; // 地下随机速度下界，像素/厘秒
inline constexpr float TunnelVelocityMaximum = .68f; // 地下随机速度上界，像素/厘秒
inline constexpr float RightWalkVelocity = .12f; // 普通持镐折返速度，像素/厘秒
inline constexpr float LeftWalkVelocityMinimum = .23f; // 无镐步行随机速度下界，像素/厘秒
inline constexpr float LeftWalkVelocityMaximum = .37f; // 无镐步行随机速度上界，像素/厘秒
inline constexpr float RiseSeconds = 1.3f; // 出土有效行动秒，不受动画播放倍率影响
inline constexpr float PauseWithoutPickaxeSeconds = 2.0f; // 地下丢镐后原地停顿的有效行动秒
inline constexpr float StunnedSeconds = 3.5f; // 正常出土后眩晕的有效行动秒
inline constexpr float SurfaceOffset = 30.0f; // 出土对象X门槛相对首格起点的像素偏移

// 与既有存档 phase 编号保持一致，实体和预测共用，不能重新排序。
enum class Phase {
    TUNNELING, RISING, STUNNED, WALKING_WITH_PICKAXE,
    TUNNELING_PAUSE_WITHOUT_PICKAXE, RISING_WITHOUT_PICKAXE, WALKING_WITHOUT_PICKAXE,
};

struct Forecast {
    bool present = false;
    Phase phase = Phase::TUNNELING;
    float remaining = 0; // 当前阶段剩余有效行动秒；硬控由调用方暂停
    bool hasPickaxe = true;
    float surfaceX = 0; // 主线程提供首格起点+SurfaceOffset，严格小于该对象X时出土
    float tunnelSpeed = (TunnelVelocityMinimum+TunnelVelocityMaximum)*.5f*TicksPerSecond; // 地下中性像素/行动秒，仅活体保留已采样琥珀
    float rightWalkSpeed = RightWalkVelocity*TicksPerSecond; // 折返中性像素/行动秒，剥离能力/雨/风/鼓舞，保留活体突击令与琥珀
    float leftWalkSpeed = (LeftWalkVelocityMinimum+LeftWalkVelocityMaximum)*.5f*TicksPerSecond; // 无镐中性像素/行动秒，倍率契约与折返相同
    float abilityMultiplier = 1; // 未经金冰放大的自身能力；活体破甲狂潮按当前采样固定近似
    float rightWindMultiplier = 1; // 向右的原始风场倍率，后台按未来金冰层数放大
    float leftWindMultiplier = 1; // 向左的原始风场倍率，后台按未来金冰层数放大
    bool losePickaxeImmediatelyWhenStunned = false; // 精英变体预警中丢镐立即向房屋走；不预测额外爆破

    bool IsInteractive() const {
        return phase==Phase::STUNNED || phase==Phase::WALKING_WITH_PICKAXE
            || phase==Phase::WALKING_WITHOUT_PICKAXE;
    }
    bool CanTargetProjectile(bool targetsFlying=false) const { return !targetsFlying && IsInteractive(); }
    bool CanFreeze() const { return IsInteractive(); }
    bool CanChill() const { return IsInteractive(); }
    bool CanGroundHazard() const { return IsInteractive(); }
    bool CanMower() const { return IsInteractive(); }
    bool CanEat() const { return phase==Phase::WALKING_WITH_PICKAXE || phase==Phase::WALKING_WITHOUT_PICKAXE; }
    bool IsMovingRight() const { return phase==Phase::RISING || phase==Phase::STUNNED || phase==Phase::WALKING_WITH_PICKAXE; }
    bool CanTriggerGameOver() const { return phase==Phase::WALKING_WITHOUT_PICKAXE; }
    bool AshKillsDirectly(float damage) const { return damage>0 && !IsInteractive(); }
    bool HasMagneticItem() const { return hasPickaxe && (phase==Phase::TUNNELING || phase==Phase::STUNNED || phase==Phase::WALKING_WITH_PICKAXE); }
    float WalkingSpeed() const { return IsMovingRight() ? rightWalkSpeed : leftWalkSpeed; }

    /** 磁吸只移除合法阶段的镐子；普通右走阶段去镐后继续右走，不能擅自改向。 */
    bool LosePickaxe() {
        if (!HasMagneticItem()) return false;
        hasPickaxe=false;
        if (phase==Phase::TUNNELING) {
            phase=Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE;
            remaining=PauseWithoutPickaxeSeconds;
        } else if (phase==Phase::STUNNED && losePickaxeImmediatelyWhenStunned) {
            phase=Phase::WALKING_WITHOUT_PICKAXE;
            remaining=0;
        }
        return true;
    }

    /** 推进地下位移和出土/眩晕阶段，返回余下可步行行动秒；地面碰撞/啃食由调用方结算。
     * tunnelMoveMultiplier 只作用于地下位移（鼓舞/雾），不作用于阶段计时；风雨不改变地下速度。
     * 出土线采用连续边界近似，不重现正式固定步长的几像素越界；精英额外爆破不在此模型内。
     */
    float Advance(float actionSeconds,float& x,float tunnelMoveMultiplier=1) {
        if (!present || actionSeconds<=0 || !std::isfinite(actionSeconds) || !std::isfinite(x)
            || !std::isfinite(surfaceX) || !std::isfinite(remaining)
            || !std::isfinite(tunnelSpeed) || !std::isfinite(tunnelMoveMultiplier)) return 0;
        // 状态机没有循环能力；最多经过地下、出土、眩晕、步行四个边沿。
        for (int transitions=0; transitions<5; ++transitions) {
            if (CanEat()) return actionSeconds;
            if (phase==Phase::TUNNELING) {
                const float speed=(std::max)(0.0f,tunnelSpeed*tunnelMoveMultiplier);
                if (!std::isfinite(speed)) return 0;
                if (x>=surfaceX) {
                    if (speed<=0) return 0;
                    const float toSurface=(x-surfaceX)/speed;
                    if (actionSeconds<=toSurface) { x-=speed*actionSeconds; return 0; }
                    x=surfaceX;
                    actionSeconds-=toSurface;
                }
                phase=Phase::RISING;
                remaining=RiseSeconds;
            }
            const float elapsed=(std::min)(actionSeconds,(std::max)(0.0f,remaining));
            remaining=(std::max)(0.0f,remaining-elapsed);
            actionSeconds-=elapsed;
            if (remaining>0) return 0;
            switch (phase) {
            case Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE:
                phase=Phase::RISING_WITHOUT_PICKAXE; remaining=RiseSeconds; break;
            case Phase::RISING:
                phase=hasPickaxe ? Phase::STUNNED : Phase::WALKING_WITHOUT_PICKAXE;
                remaining=hasPickaxe ? StunnedSeconds : 0; break;
            case Phase::RISING_WITHOUT_PICKAXE:
                phase=Phase::WALKING_WITHOUT_PICKAXE; break;
            case Phase::STUNNED:
                phase=hasPickaxe ? Phase::WALKING_WITH_PICKAXE : Phase::WALKING_WITHOUT_PICKAXE; break;
            default: return 0;
            }
        }
        return 0;
    }
};
}
