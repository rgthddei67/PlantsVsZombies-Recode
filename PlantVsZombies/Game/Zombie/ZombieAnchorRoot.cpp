#include "Zombie.h"
#include "Game/Board/Board.h"
#include "Game/Plant/AnchorLotus.h"
#include <algorithm>

bool Zombie::CanBeAnchorRooted() const
{
    return IsActive() && !mIsPreview && !mIsDying && !mIsDead && !mIsMindControlled
        && CanHoldAnchorRoot() && !IsAnchorRooted() && mAnchorRootImmunity <= 0
        && !IsTangleKelpTarget();
}

bool Zombie::StartAnchorRoot(int plantID)
{
    if (!mBoard || !CanBeAnchorRooted()) return false;
    const auto* plant = mBoard->mEntityRegistry.GetPlant(plantID);
    if (!plant || plant->mPlantType != PlantType::PLANT_ANCHORLOTUS
        || plant->mRow != mRow || !plant->IsActive()) return false;
    mAnchorRootPlantID = plantID;
    mAnchorRootRemaining = AnchorLotusRules::HoldSeconds;
    return true;
}

void Zombie::ReleaseAnchorRoot(int plantID)
{
    if (mAnchorRootPlantID != plantID || plantID == NULL_PLANT_ID) return;
    mAnchorRootPlantID = NULL_PLANT_ID;
    mAnchorRootRemaining = 0;
    mAnchorRootImmunity = AnchorLotusRules::ImmunitySeconds;
}

void Zombie::UpdateAnchorRoot(float deltaTime)
{
    mAnchorRootImmunity = std::max(0.0f, mAnchorRootImmunity - deltaTime);
    if (!IsAnchorRooted()) return;
    const auto* source = mBoard ? mBoard->mEntityRegistry.GetPlant(mAnchorRootPlantID) : nullptr;
    // 来源消失、换行、魅惑或开始跳跃后松绑；不能让移动控制冻结品种的技能状态机。
    if (mIsDead || mIsDying || mIsMindControlled || !CanHoldAnchorRoot()
        || !source || !source->IsActive() || source->mRow != mRow) {
        ReleaseAnchorRoot(mAnchorRootPlantID);
        return;
    }
    mAnchorRootRemaining -= deltaTime;
    if (mAnchorRootRemaining <= 0) {
        const float overflow = -mAnchorRootRemaining;
        ReleaseAnchorRoot(mAnchorRootPlantID);
        mAnchorRootImmunity = std::max(0.0f, mAnchorRootImmunity - overflow);
    }
}
