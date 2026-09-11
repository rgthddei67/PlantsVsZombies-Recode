#include "PolarClockmakerZombie.h"

#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "Game/Board/Board.h"
#include "../AudioSystem.h"

#include <algorithm>

namespace {
constexpr int kBodyHealth = 1000; // 极夜钟匠本体生命
constexpr int kClockDiskHealth = 1200; // 非磁性星盘生命
constexpr int kBiteDamage = 50; // 星盘完整或破坏后的单口伤害
constexpr float kPreparationSeconds = 2.0f; // 完成实体创建后的准备游戏秒
constexpr float kWindupSeconds = 3.2f; // 时间锚提交前可打断前摇
constexpr float kRetryWaitSeconds = 4.0f; // 被警铃草打断后的等待游戏秒
constexpr float kCycleCooldownSeconds = 10.0f; // 每次时间锚提交后至下一次前摇的循环冷却游戏秒
constexpr float kChannelPulseSeconds = 0.72f; // 前摇期间补充一轮星盘齿轮的游戏秒间隔
constexpr float kDiskOffsetX = 21.0f; // 星盘相对身体轨道的水平偏移，动画 px
constexpr float kDiskOffsetY = -21.0f; // 星盘相对身体轨道的垂直偏移，动画 px
constexpr float kDiskScale = 0.76f; // 星盘 follower 尺寸倍率
constexpr float kPendulumOffsetX = -7.0f; // 悬摆相对身体轨道的水平偏移，动画 px
constexpr float kPendulumOffsetY = 16.0f; // 悬摆相对身体轨道的垂直偏移，动画 px
constexpr float kPendulumScale = 0.62f; // 悬摆 follower 尺寸倍率
constexpr const char* kDiskSlot = "polar_clock_disk"; // 身体轨道星盘槽
constexpr const char* kPendulumSlot = "polar_clock_pendulum"; // 身体轨道悬摆槽

/** 返回各钟匠阶段允许持有的最大剩余时间，供读档和时间锚共同校验。 */
float MaxClockRemaining(PolarClockmakerZombie::ClockPhase phase)
{
	switch (phase) {
	case PolarClockmakerZombie::ClockPhase::PREPARING: return kPreparationSeconds;
	case PolarClockmakerZombie::ClockPhase::WINDUP: return kWindupSeconds;
	case PolarClockmakerZombie::ClockPhase::RETRY_WAIT: return kRetryWaitSeconds;
	case PolarClockmakerZombie::ClockPhase::COOLDOWN: return kCycleCooldownSeconds;
	case PolarClockmakerZombie::ClockPhase::COMMITTED:
	case PolarClockmakerZombie::ClockPhase::DISABLED: return 0.0f;
	}
	return 0.0f;
}
}

void PolarClockmakerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_CLOCK_DISK;
	mHelmHealth = mHelmMaxHealth = kClockDiskHealth;
	mAttackDamage = kBiteDamage;
	mClockPhase = mIsPreview ? ClockPhase::COMMITTED : ClockPhase::PREPARING;
	mClockRemaining = mIsPreview ? 0.0f : kPreparationSeconds;
	ConfigureFollowers();
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::Update()
{
	if (mClockPhase == ClockPhase::WINDUP && g_particleSystem
		&& IsActive() && !mIsDying) {
		mClockVisualPulseTimer -= DeltaTime::GetDeltaTime();
		if (mClockVisualPulseTimer <= 0.0f) {
			g_particleSystem->EmitEffect("PolarClockCharge", GetVisualPosition());
			mClockVisualPulseTimer = kChannelPulseSeconds;
		}
	}
	if (!mIsPreview && IsActive() && !mIsDying
		&& mClockPhase != ClockPhase::COMMITTED
		&& mClockPhase != ClockPhase::DISABLED) {
		if (!HasHead() || IsMindControlled()
			|| mHelmType != HelmType::HELMTYPE_CLOCK_DISK || mHelmHealth <= 0) {
			DisableUncommittedClock();
		}
		else if (!IsImmobilized()) {
			const float slow = GetCooldownTimer() > 0.0f ? 0.5f : 1.0f;
			mClockRemaining = std::max(0.0f, mClockRemaining
				- DeltaTime::GetDeltaTime() * slow);
			if (mClockRemaining <= 0.0f) {
				if (mClockPhase == ClockPhase::PREPARING
					|| mClockPhase == ClockPhase::RETRY_WAIT
					|| mClockPhase == ClockPhase::COOLDOWN) {
					BeginWindup();
				}
				else if (mClockPhase == ClockPhase::WINDUP) {
					if (mBoard) mBoard->CommitPolarClockAnchor(mZombieID, mRow);
					// 提交边沿立即开始下一轮冷却，不等待 Board 持有的六秒时间锚结算。
					mClockPhase = ClockPhase::COOLDOWN;
					mClockRemaining = kCycleCooldownSeconds;
					PlayWalkAnimation(0.12f);
				}
			}
		}
	}
	Zombie::Update();
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::BeginWindup()
{
	// 场外也可在准备或冷却到期后停步施法，独立时间锚仍由 Board 提交。
	if (!mBoard) return;
	if (mClockPhase == ClockPhase::COMMITTED
		|| mClockPhase == ClockPhase::DISABLED) return;
	CancelEatingForSpecialAction();
	mClockPhase = ClockPhase::WINDUP;
	mClockRemaining = kWindupSeconds;
	mClockVisualPulseTimer = kChannelPulseSeconds;
	PlayTrack("anim_idle", 0.62f, 0.12f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.4f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PolarClockCharge", GetVisualPosition());
	}
}

void PolarClockmakerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mClockPhase == ClockPhase::WINDUP) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

float PolarClockmakerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mClockPhase == ClockPhase::WINDUP ? mClockRemaining : -1.0f;
}

bool PolarClockmakerZombie::InterruptUncommittedSpecialAction()
{
	if (mClockPhase != ClockPhase::WINDUP) return false;
	mClockPhase = ClockPhase::RETRY_WAIT;
	mClockRemaining = kRetryWaitSeconds;
	PlayWalkAnimation(0.12f);
	return true;
}

void PolarClockmakerZombie::RestoreCommittedIrreversibleSpecialAction(bool submitted)
{
	if (!submitted) return;
	// v10 时间锚只有“已提交”位；循环技能以完整冷却承接其不可退款语义。
	mClockPhase = ClockPhase::COOLDOWN;
	mClockRemaining = kCycleCooldownSeconds;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.12f);
	SyncFollowerPresentation();
}

bool PolarClockmakerZombie::CaptureTemporalAbilityState(
	ZombieTemporalAbilityState& state) const
{
	state.phase = static_cast<int>(mClockPhase);
	state.remaining = mClockRemaining;
	return true;
}

void PolarClockmakerZombie::RestoreTemporalAbilityState(
	const ZombieTemporalAbilityState& state)
{
	const ClockPhase restoredPhase = static_cast<ClockPhase>(std::clamp(
		state.phase, static_cast<int>(ClockPhase::PREPARING),
		static_cast<int>(ClockPhase::COOLDOWN)));
	mClockPhase = restoredPhase;
	mClockRemaining = std::clamp(state.remaining, 0.0f,
		MaxClockRemaining(restoredPhase));
	mClockVisualPulseTimer = 0.0f;

	// 时间锚不撤销磁吸、断头或魅惑；资格已失效时不可重新进入施法链。
	if (mClockPhase != ClockPhase::COMMITTED
		&& mClockPhase != ClockPhase::DISABLED
		&& (!HasHead() || IsMindControlled()
			|| mHelmType != HelmType::HELMTYPE_CLOCK_DISK || mHelmHealth <= 0)) {
		DisableUncommittedClock();
	}
	else if (!mIsDying && IsActive()) {
		if (mClockPhase == ClockPhase::WINDUP) {
			CancelEatingForSpecialAction();
			PlayTrack("anim_idle", 0.62f, 0.12f);
		}
		else {
			PlayWalkAnimation(0.12f);
		}
	}
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::DisableUncommittedClock()
{
	if (mClockPhase == ClockPhase::COMMITTED
		|| mClockPhase == ClockPhase::DISABLED) return;
	mClockPhase = ClockPhase::DISABLED;
	mClockRemaining = 0.0f;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.1f);
}

void PolarClockmakerZombie::HelmDrop()
{
	const bool diskWasPresent = mHelmType == HelmType::HELMTYPE_CLOCK_DISK;
	Zombie::HelmDrop();
	if (diskWasPresent) {
		DisableUncommittedClock();
		if (g_particleSystem && IsActive() && !mIsPreview) {
			g_particleSystem->EmitEffect("PolarClockBreak", GetVisualPosition());
		}
	}
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::HeadDrop()
{
	if (!mHasHead) return;
	DisableUncommittedClock();
	Zombie::HeadDrop();
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::OnMindControlled()
{
	DisableUncommittedClock();
}

void PolarClockmakerZombie::Die()
{
	DisableUncommittedClock();
	Zombie::Die();
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::ConfigureFollowers()
{
	if (mFollowersConfigured || !mAnimator || !mAnimator->HasTrack("Zombie_body")) return;
	ResourceManager& resources = ResourceManager::GetInstance();
	const Texture* disk = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_POLAR_CLOCK_DISK, false);
	const Texture* pendulum = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_POLAR_PENDULUM, false);
	if (!disk || !pendulum) return;
	mAnimator->SetTrackFollowerImage("Zombie_body", kDiskSlot, disk,
		kDiskOffsetX, kDiskOffsetY, kDiskScale, kDiskScale,
		true, true, true);
	mAnimator->SetTrackFollowerImage("Zombie_body", kPendulumSlot, pendulum,
		kPendulumOffsetX, kPendulumOffsetY, kPendulumScale, kPendulumScale,
		true, false, false);
	mFollowersConfigured = true;
}

void PolarClockmakerZombie::SyncFollowerPresentation() const
{
	if (!mFollowersConfigured || !mAnimator) return;
	const bool alive = IsActive() && !mIsDying && !mIsDead;
	mAnimator->SetTrackFollowerVisible("Zombie_body", kDiskSlot,
		alive && mHelmType == HelmType::HELMTYPE_CLOCK_DISK && mHelmHealth > 0);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kPendulumSlot,
		alive && mClockPhase == ClockPhase::WINDUP);
}

void PolarClockmakerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mFollowersConfigured) {
		const_cast<PolarClockmakerZombie*>(this)->ConfigureFollowers();
	}
	SyncFollowerPresentation();
}

void PolarClockmakerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["clockPhase"] = static_cast<int>(mClockPhase);
	j["clockRemaining"] = mClockRemaining;
}

void PolarClockmakerZombie::LoadExtraData(const nlohmann::json& j)
{
	mClockPhase = static_cast<ClockPhase>(std::clamp(
		j.value("clockPhase", static_cast<int>(ClockPhase::PREPARING)),
		static_cast<int>(ClockPhase::PREPARING),
		static_cast<int>(ClockPhase::COOLDOWN)));
	mClockRemaining = std::clamp(j.value("clockRemaining", 0.0f),
		0.0f, MaxClockRemaining(mClockPhase));
	if (!HasHead() || IsMindControlled()
		|| mHelmType != HelmType::HELMTYPE_CLOCK_DISK || mHelmHealth <= 0) {
		DisableUncommittedClock();
	}
	ConfigureFollowers();
	SyncFollowerPresentation();
	UpdateAnimSpeed();
}
