#include "SalvageZombie.h"
#include "SalvageZombieRules.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "Graphics.h"
#include <cmath>

using namespace SalvageZombieRules;

void SalvageZombie::SetupZombie()
{
    Zombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = Health;
    SyncEquipment();
}

Plant* SalvageZombie::FindFoundation() const
{
    if (!mBoard || IsMindControlled()) return nullptr;
    for (int col = mBoard->mColumns - 1; col >= 0; --col) {
        const float distance = GetPosition().x - mBoard->GetCellCenterPosition(mRow, col).x;
        if (distance < -CELL_COLLIDER_SIZE_X * .5f) continue;
        if (distance > RangeCells * CELL_COLLIDER_SIZE_X) break;
        Plant* top = mBoard->GetTopPlantAt(mRow, col);
        if (!top || !top->IsActive() || top->IsSquished()) continue;
        Plant* base = mBoard->IsPoolSquare(mRow, col) ? mBoard->GetUnderPlantAt(mRow, col) : nullptr;
        if (!base || !IsLilyPadPlantType(base->GetPlacementType()))
            base = mBoard->GetFlowerPotAt(mRow, col);
        if (!base || !base->IsActive() || base->IsSquished() || base->IsIceSealed()
            || base->IsBungeeTargeted() || base->IsDamageImmune()) return nullptr;
        return base;
    }
    return nullptr;
}

/** 硬控在基类暂停能力计时之前取消拉拽；无头、死亡或魅惑同样不能留下停步阶段。 */
void SalvageZombie::Update()
{
    if (mWindup > 0 && (IsImmobilized() || !mHasHead || mIsDying || mIsDead || IsMindControlled()))
        CancelSalvage();
    Zombie::Update();
}

/** 由基类每个可行动逻辑步调用一次；啃食和锚藕束缚不额外阻止打捞。 */
void SalvageZombie::ZombieUpdate(float seconds)
{
    if (!mHasHook || !mHasHead || mIsDying || mIsDead || IsMindControlled() || mIsPreview) return;
    const float rain = mBoard ? mBoard->GetZombieRainSpeedMultiplier() : 1.0f;
    seconds *= AmplifySpeedMultiplierForGoldenIce(rain);
    if (mWindup > 0) {
        Plant* target = mBoard->mEntityRegistry.GetPlant(mTargetID);
        if (!target || target != FindFoundation()) { CancelSalvage(); return; }
        mWindup -= seconds;
        if (mWindup > 0) return;
        // 先消费动作，再调用会注销支撑层的正式生命周期；提交后不能回滚或重入。
        mWindup = 0;
        mTargetID = NULL_PLANT_ID;
        mCooldown = Cooldown;
        target->KillByZombie();
        ++mSalvageCount;
        AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FLOOP, .4f);
        PlayWalkAnimation(.15f);
        return;
    }
    mCooldown = std::max(0.0f, mCooldown - seconds);
    if (mCooldown > 0 || !mBoard
        || GetPosition().x > mBoard->GetCellCenterPosition(mRow, mBoard->mColumns - 1).x + CELL_COLLIDER_SIZE_X * .5f)
        return;
    Plant* target = FindFoundation();
    if (!target) return;
    CancelEatingForSpecialAction();
    mTargetID = target->mPlantID;
    mWindup = Windup;
    PlayTrack("anim_idle", 0, .15f);
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, .3f);
}

void SalvageZombie::ZombieMove(float seconds, Transform* transform)
{
    if (mWindup <= 0) Zombie::ZombieMove(seconds, transform);
}

void SalvageZombie::StartEat(ColliderComponent* other)
{
    if (mWindup <= 0) Zombie::StartEat(other);
}

void SalvageZombie::CancelSalvage()
{
    const bool acting = mWindup > 0;
    mWindup = 0;
    mTargetID = NULL_PLANT_ID;
    if (acting) mCooldown = RetrySeconds;
    if (acting && IsActive() && !mIsDying && !mIsDead) PlayWalkAnimation(.15f);
}

float SalvageZombie::GetInterruptibleSpecialActionRemaining() const
{
    return mWindup > 0 && mHasHead && !mIsDying ? mWindup : -1.0f;
}

bool SalvageZombie::InterruptUncommittedSpecialAction()
{
    if (GetInterruptibleSpecialActionRemaining() < 0) return false;
    CancelSalvage();
    return true;
}

void SalvageZombie::OnMindControlled() { CancelSalvage(); }
void SalvageZombie::HeadDrop() { CancelSalvage(); Zombie::HeadDrop(); SyncEquipment(); }

bool SalvageZombie::ExtractMagneticItem(MagneticItem& item)
{
    if (!HasMagneticItem()) return false;
    item.textureKey = "IMAGE_SALVAGE_HOOK";
    item.worldPosition = GetHookPosition();
    item.destinationOffset = Vector(-12, -10);
    item.drawScale = .6f;
    mHasHook = false;
    CancelSalvage();
    return true;
}

void SalvageZombie::SyncEquipment() const
{
    auto& r = ResourceManager::GetInstance();
    mAnimator->SetTrackFollowerImage("Zombie_body", "salvage_reel",
        r.GetTexture("IMAGE_SALVAGE_REEL", false), 35, 3, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "salvage_reel", true);
    mAnimator->SetTrackFollowerImage("Zombie_body", "salvage_buoy",
        r.GetTexture("IMAGE_SALVAGE_BUOY", false), -10, 36, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "salvage_buoy", true);
    mAnimator->SetTrackFollowerImage("anim_head1", "salvage_hat",
        r.GetTexture("IMAGE_SALVAGE_HAT", false), -3, -18, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("anim_head1", "salvage_hat", mHasHead);
}

void SalvageZombie::ZombieItemUpdate() const { Zombie::ZombieItemUpdate(); SyncEquipment(); }

Vector SalvageZombie::GetHookPosition() const
{
    const float scale = GetTransform()->GetScale();
    const float direction = IsMovingRight() ? 1.0f : -1.0f;
    const Vector hand = GetRenderedTrackWorldPosition("anim_innerarm2") + Vector(direction * 12, 12) * scale;
    const Plant* target = mBoard ? mBoard->mEntityRegistry.GetPlant(mTargetID) : nullptr;
    if (mWindup <= 0 || !target) return hand + Vector(direction * 14, 12) * scale;
    const float t = std::clamp(1 - mWindup / Windup, 0.0f, 1.0f);
    const float reach = std::min(1.0f, t / .6f);
    return hand + (target->GetVisualAnchorPosition() + Vector(0, 12) - hand) * reach
        + Vector(0, -std::sin(reach * 3.14159265f) * 30 * scale);
}

/** 绳索连接真实握持点与目标基座，颜色提示拉拽临近提交；钩具可独立被磁吸。 */
void SalvageZombie::Draw(Graphics* g)
{
    Zombie::Draw(g);
    if (!g || !mHasHook || mIsDying || mIsDead) return;
    const float scale = GetTransform()->GetScale();
    const float direction = IsMovingRight() ? 1.0f : -1.0f;
    const Vector hand = GetRenderedTrackWorldPosition("anim_innerarm2") + Vector(direction * 12, 12) * scale;
    const Vector hook = GetHookPosition();
    const glm::vec4 rope = mWindup > 0 && mWindup < .75f
        ? glm::vec4(255, 123, 55, 255) : glm::vec4(176, 136, 77, 255);
    for (int i = -1; i <= 1; ++i) g->DrawLine(hand.x, hand.y + i, hook.x, hook.y + i, rope);
    if (mWindup > 0) g->DrawCircle(hook.x, hook.y, 18, rope, 24);
    const Texture* texture = ResourceManager::GetInstance().GetTexture("IMAGE_SALVAGE_HOOK", false);
    const auto tint = IsImmobilized() || GetCooldownTimer() > 0 ? glm::vec4(150, 185, 255, 255) : glm::vec4(255);
    if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(texture, hook.x - 12 * scale, hook.y - 8 * scale, 24 * scale, 49 * scale, 0, tint);
    else g->DrawTexture(texture, hook.x - 12 * scale, hook.y - 8 * scale, 24 * scale, 49 * scale, 0, tint);
}

void SalvageZombie::SaveExtraData(nlohmann::json& j) const
{
    j["hasHook"] = mHasHook; j["salvageTargetID"] = mTargetID;
    j["salvageWindup"] = mWindup; j["salvageCooldown"] = mCooldown;
    j["salvageCount"] = mSalvageCount;
}

/** 只恢复剩余阶段，终止状态自愈；不在实体恢复尚未完成时误判目标失踪。 */
void SalvageZombie::LoadExtraData(const nlohmann::json& j)
{
    mHasHook = j.value("hasHook", true);
    mTargetID = j.value("salvageTargetID", NULL_PLANT_ID);
    mWindup = j.value("salvageWindup", 0.0f);
    mCooldown = j.value("salvageCooldown", 0.0f);
    if (!std::isfinite(mWindup) || !std::isfinite(mCooldown))
        throw std::runtime_error("non-finite salvage action");
    mWindup = std::clamp(mWindup, 0.0f, Windup);
    mCooldown = std::clamp(mCooldown, 0.0f, Cooldown);
    mSalvageCount = std::max(0, j.value("salvageCount", 0));
    if (!mHasHook || !mHasHead || mIsDying || IsMindControlled() || mTargetID == NULL_PLANT_ID)
        CancelSalvage();
    SyncEquipment();
}
