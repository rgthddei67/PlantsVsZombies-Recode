#include "CobCannon.h"

#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../ShadowComponent.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <cstdlib>

namespace {
	constexpr int kLaunchFrame = 78;                    // 主人给出的 AddFrameEvent 全局发射帧，直接使用
	constexpr float kInitialArmingSeconds = 5.0f;       // 刚升级完成到首次充能的等待时间，单位：游戏秒
	constexpr float kReloadArmingSeconds = 30.0f;       // 每次发射后到下一次充能的等待时间，单位：游戏秒
	constexpr float kReanimationFramesPerSecond = 12.0f; // CobCannon.reanim 基础帧率，单位：fps
	constexpr float kChargeFramesPerSecond = 12.0f;     // anim_charge 播放帧率，单位：fps
	constexpr float kShootingFramesPerSecond = 12.0f;   // anim_shooting 播放帧率，单位：fps
	constexpr float kTrackBlendSeconds = 0.0f;          // 原版机械动作硬切，避免跨轨重影
	constexpr float kCobFlightSeconds = 2.0f;           // 玉米棒升空到落地的总游戏时间，单位：秒
	constexpr int kCobDamage = 1800;                    // 爆心范围内的灰烬伤害
	const Vector kLaunchOffset(37.2f, -145.9f);         // CobCannon_cob 第 77 帧最终仿射四边形中心，相对本体 Animator 基点，单位：px
	constexpr float kShoopVolume = 0.45f;               // 开始机械装填的音效音量
	constexpr float kLaunchVolume = 0.55f;              // 第 78 帧发射音效音量
	constexpr int kReadyFlashCycleFrames = 45;           // 原版 75 个 100Hz 计数换算到当前 60Hz，周期 0.75 秒
	constexpr int kReadyFlashMinimum = 55;               // 原版 GetFlashingColor 的最暗 RGB 通道值
	constexpr float kChargeSeconds = 16.0f / 12.0f;      // anim_charge 19～34 共 16 帧，单位：游戏秒
	constexpr float kReadyToLaunchSeconds = 24.0f / 12.0f; // anim_shooting 54～78 的离膛前摇，单位：游戏秒
	constexpr float kPostLaunchShootingSeconds = 18.0f / 12.0f; // 离膛到 anim_unarmed_idle 的剩余演出，单位：游戏秒
	const SDL_Color kWhiteTrackColor{ 255, 255, 255, 255 }; // 非 READY 状态的炮弹轨道乘色
	const char* kCobTrackName = "CobCannon_cob";         // 待发玉米棒所在的独立 reanim 轨道

	/** 复刻原版 GetFlashingColor 的等通道三角波，并以当前 60Hz Board 帧计数驱动。 */
	SDL_Color GetReadyFlashColor(int boardFrame)
	{
		const int phase = boardFrame % kReadyFlashCycleFrames;
		const int halfCycle = kReadyFlashCycleFrames / 2;
		const int shade = std::clamp(kReadyFlashMinimum
			+ std::abs(halfCycle - phase)
			* (255 - kReadyFlashMinimum) / halfCycle, 0, 255);
		return SDL_Color{ static_cast<Uint8>(shade), static_cast<Uint8>(shade),
			static_cast<Uint8>(shade), 255 };
	}
}

void CobCannon::SetupPlant()
{
	Plant::SetupPlant();
	if (mCollider) {
		mCollider->size = Vector(140.0f, 80.0f);
		mCollider->offset = Vector(-30.0f, -35.0f);
	}
	RemoveShadow();

	if (!mAnimator) return;
	mAnimator->AddFrameEvent(kLaunchFrame, [this]() {
		if (mPhase == Phase::FIRING
			&& GetCurrentTrackName() == "anim_shooting") LaunchCob();
	}, true);

	if (mIsPreview) {
		mPhase = Phase::READY;
		mArmingTime = 0.0f;
		PlayTrack("anim_idle", 1.0f);
		UpdateCobTrackColor();
		return;
	}
	mPhase = Phase::ARMING;
	mArmingTime = kInitialArmingSeconds;
	mShotLaunched = false;
	PlayTrack("anim_unarmed_idle", 1.0f);
	UpdateCobTrackColor();
}

void CobCannon::PlantUpdate()
{
	if (!mAnimator || mIsPreview) return;
	const std::string track = GetCurrentTrackName();
	if (mPhase == Phase::ARMING) {
		mArmingTime = std::max(0.0f, mArmingTime - GetWeatherActionDeltaTime());
		if (mArmingTime <= 0.0f) BeginCharge();
		UpdateCobTrackColor();
		return;
	}
	if (mPhase == Phase::CHARGING && track == "anim_idle") {
		mPhase = Phase::READY;
	}
	if (mPhase == Phase::FIRING && track == "anim_unarmed_idle") {
		mPhase = Phase::ARMING;
		mArmingTime = kReloadArmingSeconds;
		mPendingTargetRow = -1;
	}
	UpdateCobTrackColor();
}

void CobCannon::BeginCharge()
{
	mPhase = Phase::CHARGING;
	const float speed = (kChargeFramesPerSecond / kReanimationFramesPerSecond)
		* GetWeatherActionSpeedMultiplier();
	PlayTrackOnce("anim_charge", "anim_idle", speed,
		kTrackBlendSeconds, speed, kTrackBlendSeconds);
	UpdateCobTrackColor();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOP, kShoopVolume);
}

bool CobCannon::FireAt(const Vector& target, int targetRow)
{
	if (mIsPreview || !mBoard || !mAnimator || mPhase != Phase::READY
		|| targetRow < 0 || targetRow >= mBoard->mRows) return false;
	mPendingTarget = target;
	mPendingTargetRow = targetRow;
	mShotLaunched = false;
	mPhase = Phase::FIRING;
	UpdateCobTrackColor();
	const float speed = (kShootingFramesPerSecond / kReanimationFramesPerSecond)
		* GetWeatherActionSpeedMultiplier();
	if (PlayTrackOnce("anim_shooting", "anim_unarmed_idle", speed,
		kTrackBlendSeconds, speed, kTrackBlendSeconds)) {
		return true;
	}
	// 资源轨道异常时保留可点击的就绪态，不能吞掉本次完整充能。
	mPhase = Phase::READY;
	mPendingTargetRow = -1;
	UpdateCobTrackColor();
	return false;
}

float CobCannon::GetSecondsUntilFrame(int frame) const
{
	if (!mAnimator) return 0.0f;
	const float speed = mAnimator->EffectiveSpeed();
	if (speed <= 0.0001f) return 0.0f;
	return std::max(0.0f, static_cast<float>(frame) - mAnimator->GetCurrentFrame())
		/ (kReanimationFramesPerSecond * speed);
}

float CobCannon::GetSimulationAbilityCooldownRemaining() const
{
	switch (mPhase) {
	case Phase::ARMING:
		return std::max(0.0f, mArmingTime) + kChargeSeconds
			+ kReadyToLaunchSeconds + kCobFlightSeconds;
	case Phase::CHARGING: {
		if (!mAnimator) return kChargeSeconds
			+ kReadyToLaunchSeconds + kCobFlightSeconds;
		const auto [beginFrame, endFrame] = mAnimator->GetTrackRange("anim_charge");
		const float speed = mAnimator->EffectiveSpeed();
		const float remainingCharge = beginFrame >= 0 && endFrame > beginFrame
			&& speed > 0.0001f
			? std::max(0.0f, static_cast<float>(endFrame)
				- mAnimator->GetCurrentFrame())
				/ (kReanimationFramesPerSecond * speed)
			: kChargeSeconds;
		return remainingCharge + kReadyToLaunchSeconds + kCobFlightSeconds;
	}
	case Phase::READY:
		return kReadyToLaunchSeconds + kCobFlightSeconds;
	case Phase::FIRING: {
		// 当前锁定炮击由 Board 单独采集；本体计时只推进演出结束后的下一轮装填。
		float remainingShooting = kPostLaunchShootingSeconds;
		if (mAnimator) {
			const auto [beginFrame, endFrame] =
				mAnimator->GetTrackRange("anim_shooting");
			if (beginFrame >= 0 && endFrame > beginFrame) {
				remainingShooting = GetSecondsUntilFrame(endFrame);
			}
		}
		return remainingShooting
			+ kReloadArmingSeconds
			+ kChargeSeconds + kReadyToLaunchSeconds + kCobFlightSeconds;
	}
	}
	return 0.0f;
}

float CobCannon::GetPendingSimulationBlastDelay() const
{
	if (mPhase != Phase::FIRING || mShotLaunched || mPendingTargetRow < 0) {
		return -1.0f;
	}
	return GetSecondsUntilFrame(kLaunchFrame) + kCobFlightSeconds;
}

float CobCannon::GetSimulationShotWindup()
{
	return kReadyToLaunchSeconds + kCobFlightSeconds;
}

float CobCannon::GetSimulationFlightSeconds()
{
	return kCobFlightSeconds;
}

void CobCannon::UpdateCobTrackColor()
{
	if (!mAnimator) return;
	const SDL_Color color = mPhase == Phase::READY && mBoard && !mIsPreview
		? GetReadyFlashColor(mBoard->mBoardFrame) : kWhiteTrackColor;
	mAnimator->SetTrackColor(kCobTrackName, color);
}

void CobCannon::LaunchCob()
{
	if (mShotLaunched || !mBoard || mPendingTargetRow < 0) return;
	mShotLaunched = true;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_COBLAUNCH, kLaunchVolume);
	Bullet* cob = mBoard->CreatePlantBullet(BulletType::BULLET_COBBIG, mPendingTargetRow,
		GetVisualPosition() + kLaunchOffset, mPlantType);
	if (!cob) return;
	cob->SetBulletDamage(kCobDamage);
	const bool polarGuided = mBoard->IsPolarWindDangerous()
		&& mBoard->PreparePolarLobbedNavigation(this);
	cob->ConfigureCobCannonMotion(
		mPendingTarget, mPendingTargetRow, kCobFlightSeconds, polarGuided);
}

void CobCannon::Die()
{
	if (mBoard) mBoard->CancelCobCannonTargeting(mPlantID);
	Plant::Die();
}

void CobCannon::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["armingTime"] = mArmingTime;
	j["targetX"] = mPendingTarget.x;
	j["targetY"] = mPendingTarget.y;
	j["targetRow"] = mPendingTargetRow;
	j["shotLaunched"] = mShotLaunched;
}

void CobCannon::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::FIRING));
	mPhase = static_cast<Phase>(phase);
	mArmingTime = std::clamp(j.value("armingTime", kInitialArmingSeconds),
		0.0f, kReloadArmingSeconds);
	mPendingTarget = Vector(
		j.value("targetX", GetPosition().x), j.value("targetY", GetPosition().y));
	mPendingTargetRow = j.value("targetRow", -1);
	mShotLaunched = j.value("shotLaunched", false);

	const std::string track = GetCurrentTrackName();
	if (mPhase == Phase::CHARGING
		&& track != "anim_charge" && track != "anim_idle") {
		BeginCharge();
	}
	else if (mPhase == Phase::READY && track != "anim_idle") {
		PlayTrack("anim_idle", 1.0f);
	}
	else if (mPhase == Phase::FIRING
		&& track != "anim_shooting" && track != "anim_unarmed_idle") {
		mPhase = Phase::ARMING;
		mArmingTime = kReloadArmingSeconds;
		mShotLaunched = false;
		PlayTrack("anim_unarmed_idle", 1.0f);
	}
	UpdateCobTrackColor();
}
