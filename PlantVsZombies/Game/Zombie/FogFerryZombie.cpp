#include "FogFerryZombie.h"
#include "Game/Board/Board.h"
#include "DeltaTime.h"
#include "ParticleSystem/ParticleSystem.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "Game/AudioSystem.h"
#include <cmath>

/** 复用普通僵尸全部行走、啃食与死亡事件，只替换生命和背包。 */
void FogFerryZombie::SetupZombie()
{
    Zombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = FogFerryRules::Health;
    mAnimator->SetTrackFollowerImage("Zombie_body", "fog_ferry_pack",
        ResourceManager::GetInstance().GetTexture("IMAGE_FOG_FERRY_PACK", false),
        18, -30, .65f, .65f, false);
    SyncPresentation();
}

int FogFerryZombie::GetFerryTargetColumn() const
{
    return mBoard ? mBoard->GetFogFerryTargetColumn(*this) : -1;
}

float FogFerryZombie::GetChannelRate() const
{
    if (!mBoard) return 1.0f;
    const int column = static_cast<int>(std::floor((GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
    return mBoard->GetPlanternIllumination(mRow, column) > 0.0f ? FogFerryRules::LitRate : 1.0f;
}

void FogFerryZombie::SyncPresentation() const
{
    mAnimator->SetTrackFollowerVisible("Zombie_body", "fog_ferry_pack", IsActive() && !IsDying());
    // 基类读档/时间回溯可能恢复普通残臂图；本品种保留防护服上袖。
    if (!mHasArm) mAnimator->SetTrackImage("Zombie_outerarm_upper",
        ResourceManager::GetInstance().GetTexture("IMAGE_DISASTERENGINEER_OUTERARM_UPPER", false));
}

void FogFerryZombie::ZombieItemUpdate() const
{
    Zombie::ZombieItemUpdate();
    SyncPresentation();
}

void FogFerryZombie::EmitChannelPulse()
{
    if (!g_particleSystem || !mBoard) return;
    const auto center = GetPosition() + Vector(0, -45);
    g_particleSystem->EmitEffect("FogFerryCharge", center);
    const int column = GetFerryTargetColumn();
    if (column >= 0) {
        auto target = mBoard->GetCellCenterPosition(mRow, column);
        target.y = mBoard->GetZombieSpawnY(mRow, target.x) - 10;
        g_particleSystem->EmitEffect("FogFerryMarker", target);
    }
}

/** 计时独立于行走/啃食分支，硬控暂停进度，成功提交后才消费循环冷却。 */
void FogFerryZombie::Update()
{
    if (!mIsPreview && mBoard && !DeltaTime::IsPaused()
        && mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned
        && IsActive() && !IsDying()) {
        if (!HasHead() || IsMindControlled()) CancelChannel(true, true);
        else if (mPhase != Phase::DISABLED && !IsImmobilized() && !IsTangleKelpTarget()
            && !IsGarlicRedirectPaused()) {
            const float dt = DeltaTime::GetDeltaTime();
            const float rate = mPhase == Phase::CHANNELING ? GetChannelRate() : 1.0f;
            mRemaining = std::max(0.0f, mRemaining - dt * rate);
            if (mPhase == Phase::CHANNELING) {
                mPulseRemaining -= dt;
                if (mPulseRemaining <= 0) { EmitChannelPulse(); mPulseRemaining = FogFerryRules::PulseInterval; }
                if (mRemaining <= 0) {
                    mMovedCount = mBoard->CommitFogFerry(*this);
                    mPhase = Phase::WAITING;
                    mRemaining = mMovedCount > 0
                        ? (mBoard->GetEffectiveFogLeftColumn() < mBoard->mColumns ? FogFerryRules::Cooldown : FogFerryRules::ClearCooldown)
                        : FogFerryRules::Windup;
                    if (mMovedCount > 0) ++mUses;
                    PlayWalkAnimation(.12f);
                }
            }
            else if (mRemaining <= 0 && !mBoard->GetFogFerryPassengers(*this).empty()) {
                CancelEatingForSpecialAction();
                mPhase = Phase::CHANNELING;
                mRemaining = FogFerryRules::Windup;
                mPulseRemaining = 0;
                PlayTrack("anim_idle", 1.0f, .12f);
            }
        }
    }
    Zombie::Update();
    SyncPresentation();
}

void FogFerryZombie::ZombieMove(float dt, Transform* transform)
{
    if (mPhase != Phase::CHANNELING) Zombie::ZombieMove(dt, transform);
}

void FogFerryZombie::StartEat(ColliderComponent* other)
{
    if (mPhase != Phase::CHANNELING) Zombie::StartEat(other);
}

void FogFerryZombie::CancelChannel(bool disable, bool feedback)
{
    const bool channeling = mPhase == Phase::CHANNELING;
    if (channeling && feedback && g_particleSystem)
        g_particleSystem->EmitEffect("FogFerryCancel", GetPosition() + Vector(0, -45));
    mPhase = disable ? Phase::DISABLED : Phase::WAITING;
    mRemaining = disable ? 0.0f : FogFerryRules::Windup;
    if (channeling && IsActive() && !IsDying()) PlayWalkAnimation(.12f);
}

float FogFerryZombie::GetInterruptibleSpecialActionRemaining() const
{
    return mPhase == Phase::CHANNELING ? mRemaining / GetChannelRate() : -1.0f;
}

bool FogFerryZombie::InterruptUncommittedSpecialAction()
{
    if (mPhase != Phase::CHANNELING) return false;
    CancelChannel(false, true);
    return true;
}

/** 断头保留气象站护目镜贴图，避免换回普通僵尸的掉落头。 */
void FogFerryZombie::HeadDrop()
{
    if (!mHasHead) return;
    CancelChannel(true, true);
    for (const auto* track : {"anim_head1", "anim_head2", "anim_tongue", "anim_hair"})
        mAnimator->SetTrackVisible(track, false);
    if (g_particleSystem) g_particleSystem->EmitEffect("FogFerryHeadOff", GetPosition());
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, .25f);
}

/** 保留防护服上袖，只掉落本品种的前臂与手掌。 */
void FogFerryZombie::ArmDrop()
{
    if (!mHasArm) return;
    mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
    mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
    if (g_particleSystem) g_particleSystem->EmitEffect("FogFerryArmOff", GetPosition());
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, .25f);
}
void FogFerryZombie::Die() { CancelChannel(true, true); Zombie::Die(); }
void FogFerryZombie::OnMindControlled() { CancelChannel(true, true); }

void FogFerryZombie::SaveExtraData(nlohmann::json& j) const
{
    j["ferryPhase"] = static_cast<int>(mPhase);
    j["ferryRemaining"] = mRemaining;
    j["ferryUses"] = mUses;
    j["ferryMovedCount"] = mMovedCount;
}

/** 恢复余时与阶段，不重放已完成的传送；无头或魅惑旧状态不能停在施法态。 */
void FogFerryZombie::LoadExtraData(const nlohmann::json& j)
{
    mPhase = static_cast<Phase>(std::clamp(j.value("ferryPhase", 0), 0, 2));
    const float value = j.value("ferryRemaining", FogFerryRules::FirstWait);
    const float limit = mPhase == Phase::CHANNELING ? FogFerryRules::Windup : FogFerryRules::ClearCooldown;
    mRemaining = std::isfinite(value) ? std::clamp(value, 0.0f, limit) : limit;
    mUses = std::clamp(j.value("ferryUses", 0), 0, 1000000);
    mMovedCount = std::clamp(j.value("ferryMovedCount", 0), 0, 1000000);
    mPulseRemaining = FogFerryRules::PulseInterval;
    if (!HasHead() || IsMindControlled() || IsDying()) CancelChannel(true, false);
    SyncPresentation();
}
