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
}

void WatersideFlood::Advance(float seconds, RainIntensity rain) {
    if (std::isfinite(seconds) && seconds > 0.0f) SetWater(mWater + seconds * Rate(rain));
}

nlohmann::json WatersideFlood::Save() const { return {{"water", mWater}}; }
void WatersideFlood::Load(const nlohmann::json& value) {
    SetWater(value.is_object() ? value.value("water", 0.0f) : 0.0f);
}
