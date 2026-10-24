#include "LotusPult.h"
#include "LotusPultRules.h"
#include "GameApp.h"
#include "Game/Board/Board.h"
#include "Game/Bullet/Bullet.h"
#include "Game/IceWall.h"
#include "Graphics.h"

namespace {
    const Vector kLaunchOffset(-21.0f, -45.0f); // 与复用投掷篮口对齐的局部发射偏移，像素
    constexpr float kTargetHeight = .35f; // 对准目标碰撞箱上部的高度比例
}

void LotusPult::SetupPlant()
{
    CabbagePult::SetupPlant();
    mPlantHealth = mPlantMaxHealth = LotusPultRules::Health;
    SyncForm();
}

bool LotusPult::IsWaterForm() const
{
    return mBoard && !mIsPreview && mBoard->IsPoolSquare(mRow, mColumn);
}

float LotusPult::NextShootInterval() const { return LotusPultRules::Interval; }

void LotusPult::SyncForm() const
{
    if (!mAnimator) return;
    const bool water = IsWaterForm();
    for (const char* track : {"Cabbagepult_backleaf1", "Cabbagepult_backleaf2",
        "frontleaf", "frontleaf_right_tip", "frontleaf_tip_left"})
        mAnimator->SetTrackVisible(track, !water);
    // 新头部包含完整眼睛，旧卷心菜的眨眼和眉毛不能覆盖莲蓬五官。
    for (const char* track : {"Cabbagepult_blink", "Cabbagepult_eyebrow1",
        "Cabbagepult_eyebrow2", "Cabbagepult_basket_overlay"})
        mAnimator->SetTrackVisible(track, false);
}

void LotusPult::PlantUpdate()
{
    SyncForm();
    CabbagePult::PlantUpdate();
}

/** 浮叶只表达水中形态；实际承载资格由植物能力与 Board 地形判断。 */
void LotusPult::Draw(Graphics* g)
{
    if (g && IsWaterForm()) {
        const Vector p = GetVisualAnchorPosition();
        const auto* leaf = ResourceManager::GetInstance().GetTexture("IMAGE_LOTUSPULT_FLOATLEAF", false);
        if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(leaf, p.x - 54, p.y + 7, 108, 38);
        else g->DrawTexture(leaf, p.x - 54, p.y + 7, 108, 38);
    }
    CabbagePult::Draw(g);
}

/** 现有第43帧回调虚派发至此；发射时固定水形态与来源，离膛后独立存活。 */
void LotusPult::FireCabbage()
{
    if (!mBoard || mIsPreview) return;
    const Vector launch = GetVisualAnchorPosition() + kLaunchOffset;
    Vector landing(static_cast<float>(SCENE_WIDTH + 20), launch.y);
    bool targetsWall = false;
    if (IceWall* wall = mBoard->GetIceWallInRow(mRow)) {
        landing = wall->GetProjectileAimPosition();
        targetsWall = true;
    } else if (Zombie* target = FindTarget()) {
        const auto bounds = target->GetColliderComponent()->GetBoundingBox();
        const float targetX = bounds.x + bounds.w * .5f;
        landing.x = target->GetTargetLeadX(LotusPultRules::FlightSeconds);
        landing.y = bounds.y + bounds.h * kTargetHeight
            + mBoard->GetRowCenterYAtX(mRow, landing.x) - mBoard->GetRowCenterYAtX(mRow, targetX);
    }
    Bullet* seed = mBoard->CreatePlantBullet(BulletType::BULLET_LOTUS_SEED, mRow, launch, mPlantType);
    if (!seed) return;
    seed->ConfigureLotusSeed(IsWaterForm());
    const bool guided = mBoard->IsPolarWindDangerous() && mBoard->PreparePolarLobbedNavigation(this);
    seed->ConfigureLobbedMotion(landing, LotusPultRules::FlightSeconds,
        LotusPultRules::ArcHeight, targetsWall, guided);
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, .3f);
}
