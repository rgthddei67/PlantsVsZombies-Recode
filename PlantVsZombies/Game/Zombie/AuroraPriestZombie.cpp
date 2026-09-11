#include "AuroraPriestZombie.h"

#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "Game/Board/Board.h"
#include "../AudioSystem.h"

#include <algorithm>

namespace {
constexpr int kMaxRitualReleases = 3; // 每只祭司累计释放上限；钟匠回溯可恢复记录时次数
constexpr int kBodyHealth = 1200; // 极光祭司本体生命
constexpr int kDeviceHealth = 800; // 非磁性极光仪器生命
constexpr int kNormalBiteDamage = 50; // 仪器完整时单口伤害
constexpr int kOverloadBiteDamage = 150; // 仪器破坏后的过载单口伤害
constexpr float kOutsideWalkMultiplier = 2.0f; // 最右列右缘之外的行走速度倍率
constexpr float kPreparationSeconds = 6.0f; // 实体完成创建后的仪式准备游戏秒
constexpr float kWindupSeconds = 2.8f; // 裂隙提交前可被警铃草打断的完整前摇
constexpr float kRetryWaitSeconds = 5.0f; // 被打断后再次尝试前的等待游戏秒
constexpr float kCycleCooldownSeconds = 5.0f; // 每次裂隙提交后至下一次前摇的循环冷却游戏秒
constexpr float kChannelPulseSeconds = 0.62f; // 前摇期间补充一次极光旋涡的游戏秒间隔
constexpr float kDeviceOffsetX = 24.0f; // 仪器相对身体轨道的水平偏移，动画 px
constexpr float kDeviceOffsetY = -20.0f; // 仪器相对身体轨道的垂直偏移，动画 px
constexpr float kDeviceScale = 0.72f; // 极光仪器 follower 尺寸倍率
constexpr float kPrismOffsetX = -4.0f; // 胸前光谱片相对身体轨道的水平偏移，动画 px
constexpr float kPrismOffsetY = 4.0f; // 胸前光谱片相对身体轨道的垂直偏移，动画 px
constexpr float kPrismScale = 0.58f; // 胸前光谱片 follower 尺寸倍率
constexpr const char* kDeviceSlot = "aurora_priest_device"; // 身体轨道极光仪器槽
constexpr const char* kPrismSlot = "aurora_priest_prism"; // 身体轨道光谱状态槽

/** 返回各仪式阶段允许持有的最大剩余时间，供读档和时间锚共同校验。 */
float MaxRitualRemaining(AuroraPriestZombie::RitualPhase phase)
{
	switch (phase) {
	case AuroraPriestZombie::RitualPhase::PREPARING: return kPreparationSeconds;
	case AuroraPriestZombie::RitualPhase::WINDUP: return kWindupSeconds;
	case AuroraPriestZombie::RitualPhase::RETRY_WAIT: return kRetryWaitSeconds;
	case AuroraPriestZombie::RitualPhase::COOLDOWN: return kCycleCooldownSeconds;
	case AuroraPriestZombie::RitualPhase::COMMITTED:
	case AuroraPriestZombie::RitualPhase::DISABLED: return 0.0f;
	}
	return 0.0f;
}
}

void AuroraPriestZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_AURORA_DEVICE;
	mHelmHealth = mHelmMaxHealth = kDeviceHealth;
	mAttackDamage = kNormalBiteDamage;
	mRitualPhase = mIsPreview ? RitualPhase::COMMITTED : RitualPhase::PREPARING;
	mRitualRemaining = mIsPreview ? 0.0f : kPreparationSeconds;
	mRitualReleaseCount = 0;
	mOverloaded = false;
	ConfigureFollowers();
	SyncFollowerPresentation();
}

void AuroraPriestZombie::Update()
{
	if (mRitualPhase == RitualPhase::WINDUP && g_particleSystem
		&& IsActive() && !mIsDying) {
		mRitualVisualPulseTimer -= DeltaTime::GetDeltaTime();
		if (mRitualVisualPulseTimer <= 0.0f) {
			g_particleSystem->EmitEffect("AuroraPriestCharge", GetVisualPosition());
			mRitualVisualPulseTimer = kChannelPulseSeconds;
		}
	}
	if (!mIsPreview && IsActive() && !mIsDying
		&& mRitualPhase != RitualPhase::COMMITTED
		&& mRitualPhase != RitualPhase::DISABLED) {
		if (mRitualReleaseCount >= kMaxRitualReleases || !HasHead() || IsMindControlled()
			|| mHelmType != HelmType::HELMTYPE_AURORA_DEVICE
			|| mHelmHealth <= 0) {
			DisableUncommittedRitual();
		}
		else if (!IsImmobilized()) {
			const float slow = GetCooldownTimer() > 0.0f ? 0.5f : 1.0f;
			mRitualRemaining = std::max(0.0f, mRitualRemaining
				- DeltaTime::GetDeltaTime() * slow);
			if (mRitualRemaining <= 0.0f) {
				if (mRitualPhase == RitualPhase::PREPARING
					|| mRitualPhase == RitualPhase::RETRY_WAIT
					|| mRitualPhase == RitualPhase::COOLDOWN) {
					BeginWindup();
				}
				else if (mRitualPhase == RitualPhase::WINDUP) {
					const bool whiteout = mBoard && mBoard->IsPolarSnowBlindActive();
					// 仅成功释放裂隙才消费额度，前摇中断和无落点提交不计数。
					if (mBoard && mBoard->CommitAuroraPriestRitual(
						mZombieID, mRow, whiteout)) ++mRitualReleaseCount;
					// 提交边沿立即开始下一轮冷却，不等待裂隙的独立到场事务。
					mRitualPhase = RitualPhase::COOLDOWN;
					mRitualRemaining = kCycleCooldownSeconds;
					if (mRitualReleaseCount >= kMaxRitualReleases) DisableUncommittedRitual();
					PlayWalkAnimation(0.12f);
				}
			}
		}
	}
	// 场内外资格由当前位置派生；每步在动画推进前刷新，读档和回溯无需另存倍率。
	if (!mIsPreview) UpdateAnimSpeed();
	Zombie::Update();
	SyncFollowerPresentation();
}

bool AuroraPriestZombie::IsOutsideEntryBoundary() const
{
	return mBoard && GetPosition().x > mBoard->GetCellCenterPosition(
		mRow, mBoard->mColumns - 1).x + CELL_COLLIDER_SIZE_X * 0.5f;
}

float AuroraPriestZombie::GetAbilityAnimSpeedMultiplier() const
{
	const float baseMultiplier = mOverloaded ? 1.15f : 0.65f;
	const bool outsideWalking = !mIsPreview && !mIsEating && !mIsDying
		&& mRitualPhase != RitualPhase::WINDUP && IsOutsideEntryBoundary();
	return baseMultiplier * (outsideWalking ? kOutsideWalkMultiplier : 1.0f);
}

void AuroraPriestZombie::BeginWindup()
{
	// 准备和冷却可在入场途中完成，但必须走进最右列才停步施法。
	if (!mBoard || IsOutsideEntryBoundary()) return;
	if (mRitualPhase == RitualPhase::COMMITTED
		|| mRitualPhase == RitualPhase::DISABLED
		|| mRitualReleaseCount >= kMaxRitualReleases) return;
	CancelEatingForSpecialAction();
	mRitualPhase = RitualPhase::WINDUP;
	mRitualRemaining = kWindupSeconds;
	mRitualVisualPulseTimer = kChannelPulseSeconds;
	PlayTrack("anim_idle", 0.72f, 0.12f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.44f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("AuroraPriestCharge", GetVisualPosition());
	}
}

void AuroraPriestZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mRitualPhase == RitualPhase::WINDUP) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

float AuroraPriestZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mRitualPhase == RitualPhase::WINDUP ? mRitualRemaining : -1.0f;
}

bool AuroraPriestZombie::InterruptUncommittedSpecialAction()
{
	if (mRitualPhase != RitualPhase::WINDUP) return false;
	mRitualPhase = RitualPhase::RETRY_WAIT;
	mRitualRemaining = kRetryWaitSeconds;
	PlayWalkAnimation(0.12f);
	return true;
}

void AuroraPriestZombie::RestoreCommittedIrreversibleSpecialAction(bool submitted)
{
	if (!submitted) return;
	// v10 时间锚只有“已提交”位；循环技能以完整冷却承接其不可退款语义。
	mRitualPhase = RitualPhase::COOLDOWN;
	mRitualRemaining = kCycleCooldownSeconds;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.12f);
	SyncFollowerPresentation();
}

bool AuroraPriestZombie::CaptureTemporalAbilityState(
	ZombieTemporalAbilityState& state) const
{
	state.phase = static_cast<int>(mRitualPhase);
	state.remaining = mRitualRemaining;
	state.releaseCount = mRitualReleaseCount;
	return true;
}

void AuroraPriestZombie::RestoreTemporalAbilityState(
	const ZombieTemporalAbilityState& state)
{
	// 次数随钟匠记录回溯，已提交的独立裂隙不会被撤销。
	mRitualReleaseCount = std::clamp(state.releaseCount, 0, kMaxRitualReleases);
	const RitualPhase restoredPhase = static_cast<RitualPhase>(std::clamp(
		state.phase, static_cast<int>(RitualPhase::PREPARING),
		static_cast<int>(RitualPhase::COOLDOWN)));
	mRitualPhase = restoredPhase;
	mRitualRemaining = std::clamp(state.remaining, 0.0f,
		MaxRitualRemaining(restoredPhase));
	mRitualVisualPulseTimer = 0.0f;

	// 磁吸、断头或魅惑不会被核心快照撤销；这些资格丢失时不得复活前摇。
	if (mRitualPhase != RitualPhase::COMMITTED
		&& mRitualPhase != RitualPhase::DISABLED
		&& (mRitualReleaseCount >= kMaxRitualReleases || !HasHead() || IsMindControlled()
			|| mHelmType != HelmType::HELMTYPE_AURORA_DEVICE || mHelmHealth <= 0)) {
		DisableUncommittedRitual();
	}
	else if (!mIsDying && IsActive()) {
		if (mRitualPhase == RitualPhase::WINDUP) {
			CancelEatingForSpecialAction();
			PlayTrack("anim_idle", 0.72f, 0.12f);
		}
		else {
			PlayWalkAnimation(0.12f);
		}
	}
	SyncFollowerPresentation();
}

void AuroraPriestZombie::DisableUncommittedRitual()
{
	if (mRitualPhase == RitualPhase::COMMITTED
		|| mRitualPhase == RitualPhase::DISABLED) return;
	mRitualPhase = RitualPhase::DISABLED;
	mRitualRemaining = 0.0f;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.1f);
}

void AuroraPriestZombie::HelmDrop()
{
	const bool deviceWasPresent = mHelmType == HelmType::HELMTYPE_AURORA_DEVICE;
	Zombie::HelmDrop();
	if (deviceWasPresent) {
		mOverloaded = true;
		mAttackDamage = kOverloadBiteDamage;
		DisableUncommittedRitual();
		UpdateAnimSpeed();
		if (g_particleSystem && IsActive() && !mIsPreview) {
			g_particleSystem->EmitEffect("AuroraDeviceBreak", GetVisualPosition());
		}
	}
	SyncFollowerPresentation();
}

void AuroraPriestZombie::HeadDrop()
{
	if (!mHasHead) return;
	DisableUncommittedRitual();
	Zombie::HeadDrop();
	SyncFollowerPresentation();
}

void AuroraPriestZombie::OnMindControlled()
{
	DisableUncommittedRitual();
}

void AuroraPriestZombie::Die()
{
	DisableUncommittedRitual();
	Zombie::Die();
	SyncFollowerPresentation();
}

void AuroraPriestZombie::ConfigureFollowers()
{
	if (mFollowersConfigured || !mAnimator || !mAnimator->HasTrack("Zombie_body")) return;
	ResourceManager& resources = ResourceManager::GetInstance();
	const Texture* device = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_AURORA_DEVICE, false);
	const Texture* prism = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_AURORA_PRISM, false);
	if (!device || !prism) return;
	mAnimator->SetTrackFollowerImage("Zombie_body", kDeviceSlot, device,
		kDeviceOffsetX, kDeviceOffsetY, kDeviceScale, kDeviceScale,
		true, true, true);
	mAnimator->SetTrackFollowerImage("Zombie_body", kPrismSlot, prism,
		kPrismOffsetX, kPrismOffsetY, kPrismScale, kPrismScale,
		true, false, false);
	mFollowersConfigured = true;
}

void AuroraPriestZombie::SyncFollowerPresentation() const
{
	if (!mFollowersConfigured || !mAnimator) return;
	const bool alive = IsActive() && !mIsDying && !mIsDead;
	mAnimator->SetTrackFollowerVisible("Zombie_body", kDeviceSlot,
		alive && mHelmType == HelmType::HELMTYPE_AURORA_DEVICE && mHelmHealth > 0);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kPrismSlot,
		alive && mRitualPhase == RitualPhase::WINDUP);
}

void AuroraPriestZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mFollowersConfigured) {
		const_cast<AuroraPriestZombie*>(this)->ConfigureFollowers();
	}
	SyncFollowerPresentation();
}

void AuroraPriestZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	// 时间锚可把被击碎的极光仪器恢复出来，过载数值必须跟随防具快照同步回退。
	mOverloaded = mHelmType != HelmType::HELMTYPE_AURORA_DEVICE || mHelmHealth <= 0;
	mAttackDamage = mOverloaded ? kOverloadBiteDamage : kNormalBiteDamage;
	SyncFollowerPresentation();
}

void AuroraPriestZombie::SaveExtraData(nlohmann::json& j) const
{
	j["ritualPhase"] = static_cast<int>(mRitualPhase);
	j["ritualRemaining"] = mRitualRemaining;
	j["ritualReleaseCount"] = mRitualReleaseCount;
	j["overloaded"] = mOverloaded;
}

void AuroraPriestZombie::LoadExtraData(const nlohmann::json& j)
{
	mRitualPhase = static_cast<RitualPhase>(std::clamp(
		j.value("ritualPhase", static_cast<int>(RitualPhase::PREPARING)),
		static_cast<int>(RitualPhase::PREPARING),
		static_cast<int>(RitualPhase::COOLDOWN)));
	mRitualRemaining = std::clamp(j.value("ritualRemaining", 0.0f),
		0.0f, MaxRitualRemaining(mRitualPhase));
	// 旧档未记录历史释放量，以零作为兼容起点。
	mRitualReleaseCount = std::clamp(j.value("ritualReleaseCount", 0), 0, kMaxRitualReleases);
	mOverloaded = j.value("overloaded", false)
		|| mHelmType != HelmType::HELMTYPE_AURORA_DEVICE || mHelmHealth <= 0;
	mAttackDamage = mOverloaded ? kOverloadBiteDamage : kNormalBiteDamage;
	if (mRitualReleaseCount >= kMaxRitualReleases || !HasHead() || IsMindControlled()) DisableUncommittedRitual();
	ConfigureFollowers();
	SyncFollowerPresentation();
	UpdateAnimSpeed();
}
