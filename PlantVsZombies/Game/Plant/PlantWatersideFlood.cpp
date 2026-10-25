#include "Plant.h"
#include "Game/Board/Board.h"
#include "Game/Board/WatersideFlood.h"
#include <cmath>

namespace {
    /** 各类救援窗口独立计时，读档不能用溺亡时限截短补盆或水生植物的已用时间。 */
    float HazardDuration(int kind) {
        switch (kind) {
        case 1: return WatersideFloodRules::DrowningSeconds;
        case 2: return WatersideFloodRules::PotGraceSeconds;
        case 3: return WatersideFloodRules::AquaticDrySeconds;
        default: return 0.0f;
        }
    }
}

void Plant::RestoreWatersideHazard(int kind, float elapsed) {
    mWatersideHazardKind = std::clamp(kind, 0, 3);
    mWatersideHazardElapsed = std::isfinite(elapsed)
        ? std::clamp(elapsed, 0.0f, HazardDuration(mWatersideHazardKind)) : 0.0f;
}

float Plant::GetWatersideHazardRemaining() const {
    return std::max(0.0f, HazardDuration(mWatersideHazardKind) - mWatersideHazardElapsed);
}

/** 用真实游戏秒推进救援窗口；计时不受肥沃、雨势行动倍率或停机影响。 */
void Plant::UpdateWatersideHazard() {
    if (!mBoard || mIsPreview || mBoard->GetState() != BoardState::GAME || !IsActive()) return;
    const int hazard = mBoard->GetWatersidePlantHazard(this);
    if (hazard != mWatersideHazardKind || hazard == 0) mWatersideHazardElapsed = 0.0f;
    mWatersideHazardKind = hazard;
    if (hazard == 0) return;
    // 睡莲自身退水即死亡；Die 只清本层 ID，上层回到土地或开始补盆宽限。
    if (hazard == 3 && IsLilyPadPlantType(GetPlacementType())) { Die(); return; }
    mWatersideHazardElapsed += DeltaTime::GetDeltaTime();
    if (GetWatersideHazardRemaining() <= 0.0f) Die();
}
