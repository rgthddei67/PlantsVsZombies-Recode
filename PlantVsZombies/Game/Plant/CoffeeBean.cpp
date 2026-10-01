#include "CoffeeBean.h"

#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../../DeltaTime.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kWaitBeforeCrumbleSeconds = 1.0f; // 原版 mDoSpecialCountdown=100cs
	constexpr float kWakeUpDurationSeconds = 1.0f;    // 原版 WAKE_UP_TIME=100cs
	constexpr float kReanimationFps = 12.0f;          // Coffeebean.reanim 基础帧率
	constexpr float kCrumbleFps = 22.0f;              // 原版 anim_crumble 播放帧率
	constexpr float kCoffeeSoundVolume = 0.5f;        // 咖啡豆碎裂音效音量
}

void CoffeeBean::SetupPlant()
{
	// 原版 InstantCoffee 属于 flying plant；叠在目标上时没有落地阴影或啃食碰撞。
	RemoveShadow();
	if (!mIsPreview) {
		RemoveCollider();
	}
	mPhase = Phase::WAITING;
	mWaitTimer = kWaitBeforeCrumbleSeconds;
}

void CoffeeBean::PlantUpdate()
{
	if (mPhase == Phase::WAITING) {
		mWaitTimer = std::max(0.0f,
			mWaitTimer - DeltaTime::GetDeltaTime());
		if (mWaitTimer <= 0.0f) StartCrumbling();
		return;
	}

	if (!IsAnimationPlaying()) Die();
}

void CoffeeBean::TakeDamage(int, DamageSource)
{
	// flying 覆盖层没有地面受击语义；目标蘑菇仍由普通层独立结算伤害。
}

void CoffeeBean::TakeWinterGroundImpactDamage(WinterGroundImpactKind kind,
	int damage, DamageSource source)
{
	if (kind != WinterGroundImpactKind::GROUND_CRACK) return;
	Plant::TakeDamage(damage, source);
}

void CoffeeBean::StartCrumbling()
{
	if (mPhase == Phase::CRUMBLING) return;
	mPhase = Phase::CRUMBLING;
	mWaitTimer = 0.0f;

	// 与原版 DoSpecial 一致：在真正开始碎裂时重新查询同格普通层，目标若仍睡着才唤醒。
	if (mBoard) {
		Plant* target = mBoard->GetNormalPlantAt(mRow, mColumn);
		if (target && target->IsActive()) {
			target->BeginWakeUp(kWakeUpDurationSeconds);
		}
	}
	PlayTrackOnce("anim_crumble", "",
		kCrumbleFps / kReanimationFps, 0.0f, 0.0f, 0.0f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_COFFEE,
		kCoffeeSoundVolume);
}

void CoffeeBean::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["waitTimer"] = mWaitTimer;
}

void CoffeeBean::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::CRUMBLING));
	mPhase = static_cast<Phase>(phase);
	mWaitTimer = mPhase == Phase::WAITING
		? std::clamp(j.value("waitTimer", kWaitBeforeCrumbleSeconds),
			0.0f, kWaitBeforeCrumbleSeconds)
		: 0.0f;
}

const char* CoffeeBean::GetPhaseName() const
{
	return mPhase == Phase::WAITING ? "WAITING" : "CRUMBLING";
}

int CoffeeBean::GetWaitTimeRemainingMs() const
{
	return static_cast<int>(std::lround(mWaitTimer * 1000.0f));
}

float CoffeeBean::GetFullWakeDelay()
{
	return kWaitBeforeCrumbleSeconds + kWakeUpDurationSeconds;
}

float CoffeeBean::GetPendingWakeDelay() const
{
	return mPhase == Phase::WAITING ? mWaitTimer + kWakeUpDurationSeconds : 0.0f;
}
