#include "HijackerZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceManager.h"

#include <algorithm>

namespace {
	constexpr int kBodyHealth = 1000;                    // 劫持者基础本体生命
	constexpr int kLockHealthBoost = 1000;               // 首次被雷荷锁定时同时增加的当前与最大本体生命
	constexpr float kGroundRootMotionRate = 12.0f;       // 小丑 _ground 时间线的根运动换算基准
	constexpr float kWalkClip = 1.0f;                    // 把小丑快跑时间线压回普通僵尸中值步速
	constexpr float kEatClip = 20.0f / 12.0f;            // 沿用小丑啃食每秒 20 帧节奏
	constexpr float kHijackClip = 16.0f / 12.0f;         // 16 帧以 16 FPS 播放，恰好一游戏秒
	constexpr float kLockedLoopVolume = 0.18f;           // 75% 锁定后的低电流机械循环音量
	constexpr float kWarningLoopVolume = 0.30f;          // 满电七秒预警期间的增强循环音量
	constexpr float kFinalLoopVolume = 0.46f;            // 最后一秒急促反馈的循环音量
	constexpr float kWarningPulseInterval = 0.9f;        // 满电预警警报脉冲间隔，单位游戏秒
	constexpr float kFinalPulseInterval = 0.22f;         // 最后一秒急促警报间隔，单位游戏秒
	constexpr float kLimbSoundVolume = 0.35f;            // 专属断肢断头音效音量
}

HijackerZombie::~HijackerZombie()
{
	ReleaseLockSound();
}

void HijackerZombie::SetupZombie()
{
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mAttackDamage = 50;
	mSpeed = kGroundRootMotionRate;

	if (mIsPreview) {
		PlayTrack("anim_idle");
		return;
	}

	RegisterFrameEvents();
	mPhase = Phase::NORMAL;
	PlayWalkAnimation(0.0f);
}

/** 沿用主人批准的小丑时间线帧号；处决由 Board 计时边沿触发，不注册动画事件。 */
void HijackerZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(45, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(89, [this]() { Die(); });
}

bool HijackerZombie::CanBeNightRoofHijackerCandidate() const
{
	return !mIsPreview && IsActive() && !mIsDead && !mIsDying
		&& mHasHead && GetCountableExecutionHealth() > 0;
}

void HijackerZombie::BeginNightRoofLock()
{
	if (!CanBeNightRoofHijackerCandidate() || mPhase == Phase::FINALIZING) return;
	if (!mLockHealthBoostApplied) {
		mBodyHealth += kLockHealthBoost;
		mBodyMaxHealth += kLockHealthBoost;
		mLockHealthBoostApplied = true;
	}
	mPhase = Phase::LOCKED;
	mWarningActive = false;
	mAlarmPulseTimer = 0.0f;
	ClaimLockSound(kLockedLoopVolume);
}

void HijackerZombie::BeginNightRoofWarning()
{
	if (!CanBeNightRoofHijackerCandidate()) return;
	mPhase = Phase::LOCKED;
	mWarningActive = true;
	mAlarmPulseTimer = 0.0f;
	ClaimLockSound(kWarningLoopVolume);
}

void HijackerZombie::BeginNightRoofFinalization()
{
	if (!CanBeNightRoofHijackerCandidate()) return;
	StopEatingForFinalization();
	mPhase = Phase::FINALIZING;
	mWarningActive = true;
	mAlarmPulseTimer = 0.0f;
	PlayTrack("anim_hijack", kHijackClip, 0.08f);
	// 定身状态仍保留，但最终能力动画必须跟随 Board 的一秒权威计时前进。
	UpdateAnimSpeed();
	ClaimLockSound(kFinalLoopVolume);
}

void HijackerZombie::ClearNightRoofLock()
{
	if (mPhase == Phase::RESOLVED) return;
	const bool wasFinalizing = mPhase == Phase::FINALIZING;
	mPhase = Phase::NORMAL;
	mWarningActive = false;
	mAlarmPulseTimer = 0.0f;
	ReleaseLockSound();
	UpdateAnimSpeed();
	if (wasFinalizing && !mIsDying && !mIsDead && !mIsEating) {
		PlayWalkAnimation(0.1f);
	}
}

void HijackerZombie::RestoreNightRoofPhase(bool locked, bool finalizing, bool warning)
{
	ReleaseLockSound();
	mPhase = Phase::NORMAL;
	mWarningActive = false;
	if (!locked || !CanBeNightRoofHijackerCandidate()) {
		if (!mIsEating && !mIsDying) PlayWalkAnimation(0.0f);
		return;
	}
	BeginNightRoofLock();
	if (warning) BeginNightRoofWarning();
	if (finalizing) BeginNightRoofFinalization();
}

void HijackerZombie::Update()
{
	Zombie::Update();
	if (mIsPreview || mIsDead || mIsDying || mPhase == Phase::NORMAL) return;

	if (!CanBeNightRoofHijackerCandidate()) {
		if (mBoard) mBoard->CancelNightRoofHijacker(mZombieID);
		return;
	}
	if (!mWarningActive) return;

	mAlarmPulseTimer -= DeltaTime::GetDeltaTime();
	if (mAlarmPulseTimer > 0.0f) return;
	const bool finalizing = mPhase == Phase::FINALIZING;
	mAlarmPulseTimer = finalizing ? kFinalPulseInterval : kWarningPulseInterval;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP,
		finalizing ? 0.42f : 0.26f);
}

void HijackerZombie::StartEat(ColliderComponent* other)
{
	if (mPhase == Phase::FINALIZING) return;
	Zombie::StartEat(other);
}

void HijackerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase == Phase::FINALIZING) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void HijackerZombie::PlayWalkAnimation(float blendTime)
{
	if (mPhase == Phase::FINALIZING) return;
	PlayTrack("anim_walk", kWalkClip, blendTime);
}

void HijackerZombie::OnStartEating()
{
	if (mPhase != Phase::FINALIZING) PlayTrack("anim_eat", kEatClip, 0.2f);
}

void HijackerZombie::StopEatingForFinalization()
{
	if (!mIsEating) return;
	if (mEatPlantID != NULL_PLANT_ID && mBoard) {
		if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
			plant && plant->mEaterCount > 0) {
			--plant->mEaterCount;
		}
	}
	mIsEating = false;
	mEatPlantID = NULL_PLANT_ID;
	mEatZombieID = NULL_ZOMBIE_ID;
	OnStopEating();
}

void HijackerZombie::HeadDrop()
{
	if (!mHasHead) return;
	const Vector anchor = mAnimator && mAnimator->HasTrack("anim_head1")
		? GetTrackWorldPosition("anim_head1") : GetPosition();
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	if (g_particleSystem) g_particleSystem->EmitEffect("HijackerHeadOff", anchor);
	if (mBoard) mBoard->CancelNightRoofHijacker(mZombieID);
	ReleaseLockSound();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kLimbSoundVolume);
}

void HijackerZombie::ArmDrop()
{
	if (!mHasArm) return;
	const Vector anchor = mAnimator && mAnimator->HasTrack("Zombie_jackbox_outerarm_lower")
		? GetTrackWorldPosition("Zombie_jackbox_outerarm_lower") : GetPosition();
	mAnimator->SetTrackImage("Zombie_jackbox_outerarm_lower",
		ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_OUTERARM_LOWER2));
	if (g_particleSystem) g_particleSystem->EmitEffect("HijackerArmOff", anchor);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kLimbSoundVolume);
}

void HijackerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
	}
	if (!mHasArm) {
		mAnimator->SetTrackImage("Zombie_jackbox_outerarm_lower",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_OUTERARM_LOWER2));
	}
}

void HijackerZombie::Die()
{
	if (mBoard) mBoard->CancelNightRoofHijacker(mZombieID);
	mPhase = Phase::RESOLVED;
	ReleaseLockSound();
	Zombie::Die();
}

void HijackerZombie::ClaimLockSound(float volume)
{
	if (mIsPreview || mIsDead || mIsDying) return;
	mLoopSoundClaimed = true;
	AudioSystem::PlayLoopingSound(ResourceKeys::Sounds::SOUND_HIJACKER_HUM, volume);
}

void HijackerZombie::ReleaseLockSound()
{
	if (!mLoopSoundClaimed) return;
	mLoopSoundClaimed = false;
	AudioSystem::StopLoopingSound(ResourceKeys::Sounds::SOUND_HIJACKER_HUM);
}

void HijackerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["warningActive"] = mWarningActive;
	j["alarmPulseTimer"] = mAlarmPulseTimer;
	j["lockHealthBoostApplied"] = mLockHealthBoostApplied;
}

void HijackerZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0),
		static_cast<int>(Phase::NORMAL), static_cast<int>(Phase::RESOLVED));
	mPhase = static_cast<Phase>(phase);
	mWarningActive = j.value("warningActive", false);
	mAlarmPulseTimer = std::max(0.0f, j.value("alarmPulseTimer", 0.0f));
	mLockHealthBoostApplied = j.value("lockHealthBoostApplied", false);
	// Board 交叉引用在全部僵尸按原 ID 登记后统一收敛；此处不声明声音或重播最终动画。
}

ZombieMovementRules::BirthProfile HijackerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.alternative=nullptr;
	p.rootMinimum=p.rootMaximum=kGroundRootMotionRate;
	p.animationMinimum=p.animationMaximum=kWalkClip;
	p.phaseDependent=true;
	return p;
}
