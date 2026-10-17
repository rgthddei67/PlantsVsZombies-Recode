#include "NightRoofChargeCycle.h"
#include "NightRoofChargeRules.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using namespace NightRoofChargeRules;

float NightRoofChargeCycle::Add(float amount)
{
    if (amount <= 0.0f || !std::isfinite(amount)) return 0.0f;
    if (mPhase != NightRoofChargePhase::CHARGING) {
        RetainOverflow(amount);
        return 0.0f;
    }
    const float combined = mCharge + amount;
    mCharge = std::clamp(combined, 0.0f, kNightRoofChargeMaximum);
    return std::max(0.0f, combined - kNightRoofChargeMaximum);
}

void NightRoofChargeCycle::RetainOverflow(float amount)
{
    if (amount > 0.0f && std::isfinite(amount))
        mOvercharge = std::clamp(mOvercharge + amount, 0.0f, kNightRoofOverchargeMaximum);
}

void NightRoofChargeCycle::Leak(float amount)
{
    if (mPhase == NightRoofChargePhase::CHARGING && amount >= 0.0f && std::isfinite(amount))
        mCharge = std::clamp(mCharge - amount, 0.0f, kNightRoofChargeMaximum);
}

void NightRoofChargeCycle::BeginWarning(bool extended)
{
    if (mPhase != NightRoofChargePhase::CHARGING) return;
    mCharge = kNightRoofChargeMaximum;
    mPhase = NightRoofChargePhase::WARNING;
    mTimer = extended ? kNightRoofHijackerWarningDuration : kNightRoofChargeWarningDuration;
}

void NightRoofChargeCycle::Elapse(float seconds)
{
    if (mPhase != NightRoofChargePhase::CHARGING && seconds > 0.0f)
        mTimer = std::max(0.0f, mTimer - seconds);
}

NightRoofChargeCycle::Transition NightRoofChargeCycle::CommitExpiredPhase()
{
    if (mTimer > 0.0f) return Transition::None;
    if (mPhase == NightRoofChargePhase::WARNING) {
        mPhase = NightRoofChargePhase::DISCHARGING;
        mTimer = kNightRoofChargeDischargeDuration;
        return Transition::DischargeStarted;
    }
    if (mPhase == NightRoofChargePhase::DISCHARGING) {
        // 余电只在本轮演出结束时兑现一次；过大的本步时间不继续推进下一阶段。
        mCharge = mOvercharge;
        mOvercharge = 0.0f;
        mPhase = NightRoofChargePhase::CHARGING;
        mTimer = 0.0f;
        return Transition::CycleFinished;
    }
    return Transition::None;
}

void NightRoofChargeCycle::Restore(float charge, NightRoofChargePhase phase,
    float seconds, float overcharge, bool extended)
{
    Reset();
    if (!std::isfinite(charge) || !std::isfinite(seconds) || !std::isfinite(overcharge)) return;
    mCharge = std::clamp(charge, 0.0f, kNightRoofChargeMaximum);
    if (phase == NightRoofChargePhase::CHARGING) return;
    if ((phase != NightRoofChargePhase::WARNING && phase != NightRoofChargePhase::DISCHARGING)
        || seconds < 0.0f) { Reset(); return; }
    mCharge = kNightRoofChargeMaximum;
    mPhase = phase;
    mTimer = std::clamp(seconds, 0.0f, phase == NightRoofChargePhase::WARNING
        ? (extended ? kNightRoofHijackerWarningDuration : kNightRoofChargeWarningDuration)
        : kNightRoofChargeDischargeDuration);
    mOvercharge = std::clamp(overcharge, 0.0f, kNightRoofOverchargeMaximum);
}

void NightRoofChargeCycle::Save(nlohmann::json& j) const
{
    j["nightRoofCharge"] = mCharge;
    j["nightRoofOvercharge"] = mOvercharge;
    j["nightRoofChargePhase"] = static_cast<int>(mPhase);
    j["nightRoofChargePhaseTimer"] = mTimer;
}
