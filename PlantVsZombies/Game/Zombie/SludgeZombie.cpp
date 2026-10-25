#include "SludgeZombie.h"
#include "SludgeZombieRules.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "Game/Bullet/Bullet.h"
#include "Graphics.h"
#include <cmath>

namespace {
    constexpr float GearX = -40.0f; // 装备相对躯干轨的横向偏移，像素
    constexpr float GearY = -2.0f; // 装备相对躯干轨的纵向偏移，像素
    constexpr float MuzzleX = 5.0f; // 泥管口在装备贴图中的横坐标，像素
    constexpr float MuzzleY = 70.0f; // 泥管口在装备贴图中的纵坐标，像素
}

void SludgeZombie::SetupZombie()
{
    ConeZombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = SludgeZombieRules::Health;
    mHelmHealth = mHelmMaxHealth = SludgeZombieRules::ArmorHealth;
    RefreshEquipmentPresentationAfterRepair();
    SyncEquipment();
}

/** 只锁第一组活动植物；坚果和南瓜可以遮住其后的输出阵地。 */
Plant* SludgeZombie::FindTarget() const
{
    if (!mBoard || IsMindControlled()) return nullptr;
    for (int col = mBoard->mColumns - 1; col >= 0; --col) {
        const Vector center = mBoard->GetCellCenterPosition(mRow, col);
        const float distance = GetPosition().x - center.x;
        if (distance < -CELL_COLLIDER_SIZE_X * .5f) continue;
        if (distance > SludgeZombieRules::RangeCells * CELL_COLLIDER_SIZE_X) break;
        Plant* p = mBoard->GetTopPlantAt(mRow, col);
        if (p && p->IsActive() && p->OccupiesGridSlot() && !p->IsSquished()
            && !p->IsBungeeTargeted() && p->mPlantHealth > 0
            && !mBoard->MineBlocksSegment(GetPosition(), center)) return p;
    }
    return nullptr;
}

void SludgeZombie::Update()
{
    if (mWindup > 0 && (IsImmobilized() || !mHasHead || mIsDying || mIsDead || IsMindControlled())) CancelSpray();
    if (mBoard && mBoard->GetState() == BoardState::GAME)
        mRepairFeedback = std::max(0.0f, mRepairFeedback - DeltaTime::GetDeltaTime());
    ConeZombie::Update();
}

/** 啃食时也可起手；占用身体动作后由唯一可行动入口推进前摇和装填。 */
void SludgeZombie::ZombieUpdate(float seconds)
{
    if (!mBoard || mIsPreview || !mHasHead || mIsDying || mIsDead || IsMindControlled()) return;
    seconds *= AmplifySpeedMultiplierForGoldenIce(mBoard->GetZombieRainSpeedMultiplier());
    if (mWindup > 0) {
        Plant* target = mBoard->mEntityRegistry.GetPlant(mTargetID);
        if (!target || target != FindTarget()) { CancelSpray(); return; }
        mWindup = std::max(0.0f, mWindup - seconds);
        if (mWindup <= 0) Spray();
        return;
    }
    mReload = std::max(0.0f, mReload - seconds);
    if (mReload > 0 || GetPosition().x > mBoard->GetCellCenterPosition(mRow, mBoard->mColumns - 1).x + CELL_COLLIDER_SIZE_X * .5f) return;
    Plant* target = FindTarget();
    if (!target) return;
    CancelEatingForSpecialAction();
    mTargetID = target->mPlantID;
    mWindup = SludgeZombieRules::Windup;
    PlayTrack("anim_idle", 0, .15f);
}

/** 弹丸成功创建即提交回血；后续拦截、落空、死亡不回滚，也不补长已经掉落的路障。 */
void SludgeZombie::Spray()
{
    mTargetID = NULL_PLANT_ID;
    const Vector muzzle = GetMuzzlePosition();
    Bullet* bullet = mBoard->CreateBullet(BulletType::BULLET_SLUDGE, mRow, muzzle);
    mReload = bullet ? SludgeZombieRules::Reload : SludgeZombieRules::Retry;
    PlayWalkAnimation(.15f);
    if (!bullet) return;
    bullet->SetVelocityX(-SludgeZombieRules::ProjectileSpeed);
    ++mShots;
    if (mHelmType != HelmType::HELMTYPE_NONE && mHelmHealth > 0 && mHelmHealth < mHelmMaxHealth) {
        const int restored = std::min(SludgeZombieRules::ArmorRepair, mHelmMaxHealth - mHelmHealth);
        mHelmHealth += restored;
        mRepairedTotal += restored;
        ++mRepairCount;
        mLastRepair = restored;
        mRepairFeedback = SludgeZombieRules::RepairFeedbackSeconds;
        RefreshEquipmentPresentationAfterRepair();
        if (g_particleSystem) g_particleSystem->EmitEffect("SludgeArmorRepair", GetRenderedTrackWorldPosition("anim_cone") + Vector(22, 20));
    }
    if (g_particleSystem) g_particleSystem->EmitEffect("SludgeMuzzle", muzzle);
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FLOOP, .35f);
}

void SludgeZombie::ZombieMove(float seconds, Transform* transform)
{
    if (mWindup <= 0) ConeZombie::ZombieMove(seconds, transform);
}

void SludgeZombie::StartEat(ColliderComponent* other)
{
    if (mWindup <= 0) ConeZombie::StartEat(other);
}

void SludgeZombie::CancelSpray()
{
    const bool acting = mWindup > 0;
    mWindup = 0;
    mTargetID = NULL_PLANT_ID;
    if (acting) mReload = SludgeZombieRules::Retry;
    if (acting && IsActive() && !mIsDying && !mIsDead) PlayWalkAnimation(.15f);
}

float SludgeZombie::GetInterruptibleSpecialActionRemaining() const
{
    return mWindup > 0 && mHasHead && !mIsDying ? mWindup : -1.0f;
}

bool SludgeZombie::InterruptUncommittedSpecialAction()
{
    if (GetInterruptibleSpecialActionRemaining() < 0) return false;
    CancelSpray();
    return true;
}

void SludgeZombie::OnMindControlled() { CancelSpray(); }
void SludgeZombie::HeadDrop() { CancelSpray(); ConeZombie::HeadDrop(); }

/** 装备附着躯干，蓄压只缩放罐体表现，不修改玩法坐标或前摇。 */
void SludgeZombie::SyncEquipment() const
{
    const float pressure = mWindup > 0 ? 1.0f + .05f * std::sin((1 - mWindup / SludgeZombieRules::Windup) * 3.14159265f) : 1.0f;
    mAnimator->SetTrackFollowerImage("Zombie_body", "sludge_pack",
        ResourceManager::GetInstance().GetTexture("IMAGE_SLUDGE_GEAR", false), GearX, GearY, pressure, pressure, false, true, false);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "sludge_pack", true);
}

void SludgeZombie::ZombieItemUpdate() const { ConeZombie::ZombieItemUpdate(); SyncEquipment(); }

Vector SludgeZombie::GetMuzzlePosition() const
{
    const float scale = GetTransform()->GetScale();
    return GetRenderedTrackWorldPosition("Zombie_body") + Vector((GearX + MuzzleX) * (IsMovingRight() ? -1.0f : 1.0f), GearY + MuzzleY) * scale;
}

void SludgeZombie::Draw(Graphics* g)
{
    ConeZombie::Draw(g);
    if (!g || mIsPreview) return;
    if (mWindup > 0 && !mIsDying && !mIsDead) {
        const Vector nozzle = GetMuzzlePosition();
        const float size = 8 + 12 * (1 - mWindup / SludgeZombieRules::Windup);
        const auto* mud = ResourceManager::GetInstance().GetTexture("IMAGE_SLUDGE_PROJECTILE", false);
        if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(mud, nozzle.x - size, nozzle.y - size*.4f, size, size*.8f, 0, glm::vec4(255));
        else g->DrawTexture(mud, nozzle.x - size, nozzle.y - size*.4f, size, size*.8f);
    }
    if (mRepairFeedback <= 0) return;
    const Vector p = GetRenderedTrackWorldPosition("anim_head1") + Vector(4, -22 - 25 * (1 - mRepairFeedback / SludgeZombieRules::RepairFeedbackSeconds));
    g->DrawGlyphRun("+" + std::to_string(mLastRepair), ResourceKeys::Fonts::FONT_FZCQ, 18,
        glm::vec4(157, 255, 111, 255 * mRepairFeedback / SludgeZombieRules::RepairFeedbackSeconds), p.x, p.y);
}

void SludgeZombie::SaveExtraData(nlohmann::json& j) const
{
    ConeZombie::SaveExtraData(j);
    j["sludgeTarget"] = mTargetID; j["sludgeWindup"] = mWindup; j["sludgeReload"] = mReload;
    j["sludgeShots"] = mShots; j["sludgeRepaired"] = mRepairedTotal; j["sludgeRepairs"] = mRepairCount;
}

/** 只重建耐久对应外观；不重发弹丸、回血、飘字或修复粒子。 */
void SludgeZombie::LoadExtraData(const nlohmann::json& j)
{
    ConeZombie::LoadExtraData(j);
    auto timer = [&](const char* key, float maximum) {
        const float v = j.value(key, 0.0f);
        return std::isfinite(v) ? std::clamp(v, 0.0f, maximum) : 0.0f;
    };
    mTargetID = j.value("sludgeTarget", NULL_PLANT_ID);
    mWindup = timer("sludgeWindup", SludgeZombieRules::Windup);
    mReload = timer("sludgeReload", SludgeZombieRules::Reload);
    mShots = std::max(0, j.value("sludgeShots", 0));
    mRepairedTotal = std::max(0, j.value("sludgeRepaired", 0));
    mRepairCount = std::max(0, j.value("sludgeRepairs", 0));
    mRepairFeedback = 0;
    if (!mHasHead || mIsDying || IsMindControlled() || mTargetID == NULL_PLANT_ID) CancelSpray();
    RefreshEquipmentPresentationAfterRepair();
    SyncEquipment();
}
