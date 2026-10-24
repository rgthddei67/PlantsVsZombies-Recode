#include "WatersideFlood.h"
#include <algorithm>
#include <cmath>

float WatersideFlood::Rate(RainIntensity rain) {
    using namespace WatersideFloodRules;
    switch (rain) {
    case RainIntensity::CLEAR: return -ClearDrain;
    case RainIntensity::LIGHT: return LightGain;
    case RainIntensity::MEDIUM: return MediumGain;
    case RainIntensity::HEAVY: return HeavyGain;
    case RainIntensity::TORRENT: return TorrentGain;
    }
    return 0.0f;
}

void WatersideFlood::SetWater(float value) {
    mWater = std::isfinite(value) ? std::clamp(value, 0.0f, WatersideFloodRules::MaximumWater) : 0.0f;
    if (mWater < WatersideFloodRules::MaximumWater) mOverflow = 0.0f;
}

void WatersideFlood::Advance(float seconds, RainIntensity rain) {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return;
    mWarningRemaining = std::max(0.0f, mWarningRemaining - seconds);
    float amount = seconds * Rate(rain);
    if (amount > 0.0f) {
        const float excess = std::max(0.0f, mWater + amount - WatersideFloodRules::MaximumWater);
        mWater = std::min(WatersideFloodRules::MaximumWater, mWater + amount);
        mOverflow = std::min(WatersideFloodRules::OverflowCapacity, mOverflow + excess);
    } else {
        const float drainedOverflow = std::min(mOverflow, -amount);
        mOverflow -= drainedOverflow;
        mWater = std::max(0.0f, mWater + amount + drainedOverflow);
    }
}

bool WatersideFlood::CanBeginSurge() const {
    return mTargets.empty() && mOverflow >= WatersideFloodRules::OverflowCapacity;
}

void WatersideFlood::BeginSurge(std::vector<int> targets) {
    if (!CanBeginSurge() || targets.empty()) return;
    targets.resize(std::min(targets.size(), static_cast<std::size_t>(WatersideFloodRules::SurgeTargetLimit)));
    mTargets = std::move(targets);
    mOverflow -= WatersideFloodRules::OverflowCapacity;
    mWarningRemaining = WatersideFloodRules::SurgeWarningSeconds;
}

void WatersideFlood::FinishSurge() {
    mTargets.clear();
    mWarningRemaining = 0.0f;
    ++mSurgeCount;
}

nlohmann::json WatersideFlood::Save() const {
    return {{"water", mWater}, {"overflow", mOverflow}, {"warningRemaining", mWarningRemaining},
        {"targets", mTargets}, {"surgeCount", mSurgeCount}};
}

void WatersideFlood::Load(const nlohmann::json& value) {
    *this = WatersideFlood{};
    SetWater(value.is_object() ? value.value("water", 0.0f) : 0.0f);
    if (!value.is_object()) return;
    const float overflow = value.value("overflow", 0.0f);
    mOverflow = std::isfinite(overflow)
        ? std::clamp(overflow, 0.0f, WatersideFloodRules::OverflowCapacity) : 0.0f;
    const float warning = value.value("warningRemaining", 0.0f);
    mWarningRemaining = std::isfinite(warning)
        ? std::clamp(warning, 0.0f, WatersideFloodRules::SurgeWarningSeconds) : 0.0f;
    for (int id : value.value("targets", std::vector<int>{})) {
        if (id < 0 || std::find(mTargets.begin(), mTargets.end(), id) != mTargets.end()) continue;
        if (mTargets.size() >= WatersideFloodRules::SurgeTargetLimit) break;
        mTargets.push_back(id);
    }
    if (mTargets.empty()) mWarningRemaining = 0.0f;
    mSurgeCount = std::max(0, value.value("surgeCount", 0));
}
