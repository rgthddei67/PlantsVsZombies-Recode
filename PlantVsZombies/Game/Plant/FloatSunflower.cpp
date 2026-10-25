#include "FloatSunflower.h"
#include "FloatSunflowerRules.h"
#include <cmath>

bool FloatSunflower::IsWaterForm() const
{
    return mBoard && !mIsPreview && mBoard->IsPoolSquare(mRow, mColumn);
}

void FloatSunflower::SetupPlant()
{
    SunFlower::SetupPlant();
    mPlantHealth = mPlantMaxHealth = FloatSunflowerRules::Health;
    mProduceTimer = FloatSunflowerRules::Interval - FloatSunflowerRules::FirstDelay;
    SyncLeaves();
}

float FloatSunflower::GetProductionInterval() const { return FloatSunflowerRules::Interval; }
int FloatSunflower::GetProductionSunCount() const
{
    return IsWaterForm() ? FloatSunflowerRules::WaterSunCount : FloatSunflowerRules::LandSunCount;
}

/** 保留原版茎、花瓣和面部的连续运动；浮叶跟随底茎，水中舒展。 */
void FloatSunflower::SyncLeaves() const
{
    const float width = IsWaterForm() ? 1.0f : .78f;
    for (const char* track : {"backleaf", "backleaf_left_tip", "backleaf_right_tip",
        "frontleaf", "frontleaf_left_tip", "frontleaf_right_tip"}) mAnimator->SetTrackVisible(track, false);
    mAnimator->SetTrackFollowerImage("stalk_bottom", "float_leaves",
        ResourceManager::GetInstance().GetTexture("IMAGE_FLOATSUNFLOWER_LEAVES", false),
        -80.0f * width, 14.0f, width, 1.0f, false, true, true);
    mAnimator->SetTrackFollowerVisible("stalk_bottom", "float_leaves", true);
}

void FloatSunflower::PlantUpdate()
{
    SyncLeaves();
    SunFlower::PlantUpdate();
}

/** 只恢复生产进度和派生叶形，不因读档或形态变化额外产阳光。 */
void FloatSunflower::LoadExtraData(const nlohmann::json& j)
{
    SunFlower::LoadExtraData(j);
    if (!std::isfinite(mProduceTimer) || !std::isfinite(mProductionGlowStartTimer))
        throw std::runtime_error("non-finite floating sunflower production");
    mProduceTimer = std::clamp(mProduceTimer, 0.0f, FloatSunflowerRules::Interval + 1.0f);
    mProductionGlowStartTimer = std::clamp(mProductionGlowStartTimer, 0.0f, FloatSunflowerRules::Interval);
    SyncLeaves();
}
