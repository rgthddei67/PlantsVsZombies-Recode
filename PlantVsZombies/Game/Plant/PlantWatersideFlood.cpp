#include "Plant.h"
#include "Game/Board/Board.h"
#include "Game/Board/WatersideFlood.h"
#include <cmath>

void Plant::RestoreWatersideHazard(int kind, float elapsed) {
    mWatersideHazardKind = std::clamp(kind, 0, 3);
    mWatersideHazardElapsed = std::isfinite(elapsed)
        ? std::clamp(elapsed, 0.0f, WatersideFloodRules::DrowningSeconds) : 0.0f;
}

float Plant::GetWatersideHazardRemaining() const {
    if (mWatersideHazardKind == 0) return 0.0f;
    const float limit = mWatersideHazardKind == 1 ? WatersideFloodRules::DrowningSeconds
        : (mWatersideHazardKind == 2 ? WatersideFloodRules::PotGraceSeconds : WatersideFloodRules::AquaticDrySeconds);
    return std::max(0.0f, limit - mWatersideHazardElapsed);
}

/** 用真实游戏秒推进救援窗口；计时不受肥沃、雨势行动倍率或停机影响。 */
void Plant::UpdateWatersideHazard() {
    if (!mBoard || mIsPreview || mBoard->GetState() != BoardState::GAME || !IsActive()) return;
    const int hazard = mBoard->GetWatersidePlantHazard(this);
    if (hazard != mWatersideHazardKind || hazard == 0) mWatersideHazardElapsed = 0.0f;
    mWatersideHazardKind = hazard;
    if (hazard == 0) return;
    // 睡莲自身退水即死亡；Die 只清本层 ID，上层回到土地或开始补盆宽限。
    if (hazard == 3 && GetPlacementType() == PlantType::PLANT_LILYPAD) { Die(); return; }
    mWatersideHazardElapsed += DeltaTime::GetDeltaTime();
    if (GetWatersideHazardRemaining() <= 0.0f) Die();
}
