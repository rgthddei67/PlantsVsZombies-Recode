#include "HeavyDiverZombie.h"
#include "Game/Board/Board.h"
#include "Game/Bullet/BulletType.h"
#include "Graphics.h"
#include "ParticleSystem/ParticleSystem.h"
#include <cmath>

using namespace HeavyDiverRules;

namespace {
constexpr float kAnchorLift = 105.0f; // 前摇把船锚举出水面的最大高度，局部像素
constexpr float kRaiseFraction = .7f; // 前摇前70%举锚，剩余30%快速落锚
constexpr float kBubbleInterval = .8f; // 换气期间两次呼气的间隔，游戏秒
constexpr float kChainPitch = 5.0f; // 相邻链节的中心距，局部像素
}

ZombieMovementRules::BirthProfile HeavyDiverZombie::GetBirthMovementProfile()
{
    ZombieMovementRules::BirthProfile result;
    result.abilityMinimum = result.abilityMaximum = MoveMultiplier;
    return result;
}

/** 保留普通骨架的步态、死亡和断肢事件，只替换数值与独立装备。 */
void HeavyDiverZombie::SetupZombie()
{
    Zombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = Health;
    mAttackDamage = SmashDamage;
    SyncEquipment();
    UpdateAnimSpeed();
}

/** 换气按游戏秒独立推进，束缚和硬控均不能永久延长水下减伤。 */
void HeavyDiverZombie::Update()
{
    Zombie::Update();
    if (!IsActive() || mIsPreview || mIsDying || mIsDead) return;
    const float dt = DeltaTime::GetDeltaTime();
    if (!mInPool) {
        mBreathing = false;
        mAirRemaining = SubmergedSeconds;
    } else {
        if (!mWasInWater) { mBreathing = false; mAirRemaining = SubmergedSeconds; }
        const bool wasBreathing = mBreathing;
        const float previousAir = mAirRemaining;
        mAirRemaining -= dt;
        while (mAirRemaining <= 0) {
            mBreathing = !mBreathing;
            mAirRemaining += mBreathing ? BreathingSeconds : SubmergedSeconds;
            if (mBreathing && mHasHead) AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PUFF, .25f);
        }
        if (mBreathing && mHasHead && (!wasBreathing ||
            std::floor((BreathingSeconds - previousAir) / kBubbleInterval) <
            std::floor((BreathingSeconds - mAirRemaining) / kBubbleInterval))) {
            EmitEquipmentParticles("HeavyDiverBreath", "anim_head1", Vector(55, 34));
        }
    }
    mWasInWater = mInPool;
    const float desired = IsSubmerged() ? VisualSubmerge : 0;
    mSubmergeOffset += std::clamp(desired - mSubmergeOffset, -90.0f * dt, 90.0f * dt);
}

/** 同一计时驱动锚链扬起和命中，不添加动画帧事件，也不依赖旧啃食事件节拍。 */
void HeavyDiverZombie::ZombieUpdate(float scaledTime)
{
    if (!mIsEating || !mHasHead) { mSmashRemaining = Windup; return; }
    const float rain = mBoard ? mBoard->GetZombieRainSpeedMultiplier() : 1.0f;
    mSmashRemaining -= scaledTime * AmplifySpeedMultiplierForGoldenIce(rain)
        * AmplifySpeedMultiplierForGoldenIce(GetDrumBiteMultiplier());
    if (mSmashRemaining > 0) return;
    const float overflow = -mSmashRemaining;
    const float impactX = GetRenderedTrackWorldPosition("anim_innerarm2").x
        + (IsMovingRight() ? 35.0f : -35.0f) * GetAnimationScale();
    const bool water = mBoard && mBoard->IsPoolWorldPosition(mRow, impactX);
    // 基类负责接触复核、同格顶层重选、魅惑以及 eaterCount 生命周期。
    Zombie::EatTarget();
    if (g_particleSystem && mBoard && !mIsPreview) {
        g_particleSystem->EmitEffect(water ? "HeavyDiverAnchorSplash" : "HeavyDiverAnchorDust",
            Vector(impactX, mBoard->GetZombiePartSurfaceY(mRow, impactX, water) - 3));
    }
    ++mSmashCount;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP, .25f);
    mSmashRemaining = mIsEating ? std::max(0.0f, SmashCycle - overflow) : Windup;
}

void HeavyDiverZombie::OnStartEating() { mSmashRemaining = Windup; UpdateAnimSpeed(); }
void HeavyDiverZombie::OnStopEating() { mSmashRemaining = Windup; UpdateAnimSpeed(); }
void HeavyDiverZombie::OnMindControlled() { mSmashRemaining = Windup; }

Vector HeavyDiverZombie::GetVisualPosition() const
{
    return Zombie::GetVisualPosition() + Vector(0, mSubmergeOffset);
}

/** 只列真正的直射弹型；投掷物、范围伤害和毒素不借此入口减伤。 */
int HeavyDiverZombie::ModifyProjectileDamage(int damage, BulletType type) const
{
    if (!IsSubmerged()) return damage;
    switch (type) {
    case BulletType::BULLET_PEA: case BulletType::BULLET_SNOWPEA:
    case BulletType::BULLET_PUFF: case BulletType::BULLET_FIREBALL:
    case BulletType::BULLET_TOXICPEA: case BulletType::BULLET_TOXICFIREBALL:
    case BulletType::BULLET_AURORA_PEA: case BulletType::BULLET_RAIN_BAMBOO:
    case BulletType::BULLET_THUNDER_SEED:
        return std::max(0, static_cast<int>(std::lround(damage * DirectDamageMultiplier)));
    default: return damage;
    }
}

float HeavyDiverZombie::ModifySpikeFrameDamage(float damage, bool) const
{
    return damage * (IsSubmerged() ? DirectDamageMultiplier : 1.0f);
}

void HeavyDiverZombie::SyncEquipment() const
{
    auto& resources = ResourceManager::GetInstance();
    mAnimator->SetTrackFollowerImage("Zombie_body", "diver_tanks",
        resources.GetTexture("IMAGE_HEAVYDIVER_TANKS", false), 28, -12, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "diver_tanks", true);
    mAnimator->SetTrackFollowerImage("anim_head1", "diver_helmet",
        resources.GetTexture("IMAGE_HEAVYDIVER_HELMET", false), -8, -12, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("anim_head1", "diver_helmet", mHasHead);
}

void HeavyDiverZombie::ZombieItemUpdate() const { Zombie::ZombieItemUpdate(); SyncEquipment(); }
void HeavyDiverZombie::HeadDrop() { Zombie::HeadDrop(); mAnimator->SetTrackFollowerVisible("anim_head1", "diver_helmet", false); }

float HeavyDiverZombie::GetInterruptibleSpecialActionRemaining() const
{
    return mIsEating && mHasHead && !mIsDying && mSmashRemaining <= Windup ? mSmashRemaining : -1.0f;
}

bool HeavyDiverZombie::InterruptUncommittedSpecialAction()
{
    if (GetInterruptibleSpecialActionRemaining() < 0) return false;
    mSmashRemaining = SmashCycle;
    return true;
}

/** 锚链从实际握持点连到锚眼，整套装备随宿主在水线裁剪。 */
void HeavyDiverZombie::Draw(Graphics* g)
{
    Zombie::Draw(g);
    if (!g || mIsDying || mIsDead) return;
    const float scale = GetTransform()->GetScale();
    const float facing = IsMovingRight() ? 1.0f : -1.0f;
    const Vector hand = GetRenderedTrackWorldPosition("anim_innerarm2") + Vector(facing * 10, 14) * scale;
    const float progress = mIsEating && mHasHead && mSmashRemaining <= Windup
        ? std::clamp(1.0f - mSmashRemaining / Windup, 0.0f, 1.0f) : 0.0f;
    // 提前举出水线，再连续挥落到命中点；不能在伤害提交时从举起姿态瞬移落地。
    const float lift = progress <= kRaiseFraction
        ? std::sin(progress / kRaiseFraction * 1.5707963f)
        : std::cos((progress - kRaiseFraction) / (1 - kRaiseFraction) * 1.5707963f);
    const Vector anchor = hand + Vector(facing * (25 + lift * 22), 28 - lift * kAnchorLift) * scale;
    float clipBottom = 0;
    const bool clipped = TryGetDrawClipBottom(clipBottom);
    if (clipped) g->PushClipBottom(clipBottom);
    auto& resources = ResourceManager::GetInstance();
    const glm::vec4 tint = IsFrozen() || GetCooldownTimer() > 0 ? glm::vec4(150, 185, 255, 255) : glm::vec4(255);
    const bool instanced = g->IsInstancePathEnabled();
    const auto draw = [&](const Texture* texture, const Vector& center, float width, float height, float angle) {
        const float x = center.x - width * scale * .5f, y = center.y - height * scale * .5f;
        if (instanced) g->DrawTextureInstanced(texture, x, y, width * scale, height * scale, angle, tint);
        else g->DrawTexture(texture, x, y, width * scale, height * scale, angle, tint);
    };
    const float anchorAngle = facing * lift * 25;
    const float radians = glm::radians(anchorAngle);
    const Vector center = anchor + Vector(0, 18.5f) * scale;
    const Vector eye = center + Vector(std::sin(radians), -std::cos(radians)) * (23.0f * scale);
    const Vector chain = eye - hand;
    const int links = std::max(1, static_cast<int>(std::ceil(std::hypot(chain.x, chain.y) / (kChainPitch * scale))));
    const float chainAngle = glm::degrees(std::atan2(-chain.x, chain.y));
    const auto* chainTexture = resources.GetTexture("IMAGE_HEAVYDIVER_CHAIN", false);
    for (int i = 0; i < links; ++i) {
        const Vector link = hand + chain * ((i + .5f) / links);
        draw(chainTexture, link, i % 2 == 0 ? 5.0f : 3.5f, 8, chainAngle);
    }
    draw(resources.GetTexture("IMAGE_HEAVYDIVER_ANCHOR", false), center, 50, 53, anchorAngle);
    if (clipped) g->PopClipRect();
}

void HeavyDiverZombie::SaveExtraData(nlohmann::json& j) const
{
    j["airRemaining"] = mAirRemaining; j["breathing"] = mBreathing;
    j["wasInWater"] = mWasInWater; j["submergeOffset"] = mSubmergeOffset;
    j["smashRemaining"] = mSmashRemaining; j["smashCount"] = mSmashCount;
}

/** 恢复剩余阶段，不重新入水、不补打一锚；公共 Animator 恢复负责原帧位。 */
void HeavyDiverZombie::LoadExtraData(const nlohmann::json& j)
{
    auto bounded = [&](const char* key, float initial, float maximum) {
        const float value = j.value(key, initial);
        return std::isfinite(value) ? std::clamp(value, 0.0f, maximum) : initial;
    };
    mBreathing = j.value("breathing", false);
    mWasInWater = j.value("wasInWater", mInPool);
    mAirRemaining = bounded("airRemaining", SubmergedSeconds, mBreathing ? BreathingSeconds : SubmergedSeconds);
    mSubmergeOffset = bounded("submergeOffset", 0, VisualSubmerge);
    mSmashRemaining = bounded("smashRemaining", Windup, SmashCycle);
    mSmashCount = std::max(0, j.value("smashCount", 0));
    SyncEquipment();
    UpdateAnimSpeed();
}
