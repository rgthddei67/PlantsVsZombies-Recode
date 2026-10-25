#include "Cactus.h"
#include "../../GameApp.h"

#include "../../ResourceKeys.h"
#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../Zombie/Zombie.h"

#include <algorithm>

namespace
{
	constexpr float kShootIntervalSeconds = 1.5f;       // 两次射击动画之间的基础间隔，单位：秒
	constexpr float kTargetCheckIntervalSeconds = 0.6f; // 达到攻击间隔后重新扫描本行目标的节流时间，单位：秒
	constexpr float kOriginalShootFps = 35.0f;          // C# 仙人掌射击动画播放速率，单位：帧/秒
	constexpr float kReanimFps = 12.0f;                 // Cactus.reanim 的基础帧率，单位：帧/秒
	constexpr float kShootClipSpeed = kOriginalShootFps / kReanimFps; // 原版帧率折算为 Animator clip 倍率
	constexpr int kGroundShootEventFrame = 26;          // 主人给定的低姿态尖刺发射全局帧号
	constexpr int kFlyingShootEventFrame = 70;          // 主人给定的高姿态尖刺发射全局帧号
	const Vector kGroundSpikeOffset(30.0f, -27.0f);     // 原版低姿态发射点换算到当前格子中心的相对像素
	const Vector kFlyingSpikeOffset(53.0f, -100.0f);    // 原版高姿态发射点换算到当前格子中心的相对像素
	constexpr float kTransitionBlendSeconds = 0.2f;     // 伸长、缩短与射击轨道切换的混合时间
	constexpr float kPlantGrowVolume = 0.3f;            // 仙人掌伸长提示音音量
}

void Cactus::SetupPlant()
{
	Plant::SetupPlant();
	if (mIsPreview || !mAnimator) return;

	mAnimator->AddFrameEvent(kGroundShootEventFrame, [this]() {
		if (mPhase == Phase::LOW && GetCurrentTrackName() == "anim_shooting") {
			ShootSpike(false);
		}
	}, true);
	mAnimator->AddFrameEvent(kFlyingShootEventFrame, [this]() {
		if (mPhase == Phase::HIGH && GetCurrentTrackName() == "anim_shootinghigh") {
			ShootSpike(true);
		}
	}, true);
}

void Cactus::SaveExtraData(nlohmann::json& j) const
{
	j["shootTimer"] = mShootTimer;
	j["checkZombieTimer"] = mCheckZombieTimer;
	j["attackCheckTimer"] = mAttackCheckTimer;
	j["phase"] = static_cast<int>(mPhase);
	j["hasGroundTarget"] = mHasGroundTarget;
	j["hasFlyingTarget"] = mHasFlyingTarget;
}

void Cactus::LoadExtraData(const nlohmann::json& j)
{
	mShootTimer = std::clamp(j.value("shootTimer", 1.0f),
		0.0f, kShootIntervalSeconds);
	mCheckZombieTimer = std::max(0.0f, j.value("checkZombieTimer", 0.0f));
	mAttackCheckTimer = std::max(0.0f, j.value("attackCheckTimer", 0.0f));
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::LOWERING));
	mPhase = static_cast<Phase>(phase);
	mHasGroundTarget = j.value("hasGroundTarget", false);
	mHasFlyingTarget = j.value("hasFlyingTarget", false);
}

void Cactus::PlantUpdate()
{
	UpdateTargetCache();
	const std::string& track = GetCurrentTrackName();
	const float attackSpeed = GetAttackSpeedMultiplier();
	mShootTimer += DeltaTime::GetDeltaTime() * attackSpeed;

	// 一次性伸缩轨结束后才提交稳定姿态，避免中途索敌抖动反复抢占动画。
	if (mPhase == Phase::RISING && track == "anim_idlehigh") {
		mPhase = Phase::HIGH;
		mShootTimer = kShootIntervalSeconds;
		mAttackCheckTimer = kTargetCheckIntervalSeconds;
	}
	else if (mPhase == Phase::LOWERING && track == "anim_idle") {
		mPhase = Phase::LOW;
	}

	if (track == "anim_shooting" || track == "anim_shootinghigh"
		|| mPhase == Phase::RISING || mPhase == Phase::LOWERING) {
		return;
	}

	// 空中目标优先级高于地面目标：低姿态先伸长，高姿态失去空中目标后先缩短。
	if (mPhase == Phase::LOW && mHasFlyingTarget) {
		mPhase = Phase::RISING;
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PLANTGROW, kPlantGrowVolume);
		PlayTrackOnce("anim_rise", "anim_idlehigh", 0.0f, kTransitionBlendSeconds);
		return;
	}
	if (mPhase == Phase::HIGH && !mHasFlyingTarget) {
		mPhase = Phase::LOWERING;
		PlayTrackOnce("anim_lower", "anim_idle", 0.0f, kTransitionBlendSeconds);
		return;
	}

	const bool hasTarget = mPhase == Phase::HIGH
		? mHasFlyingTarget : mHasGroundTarget;
	if (mShootTimer < kShootIntervalSeconds) return;

	// 攻击冷却完成后再确认目标；肥沃同步缩短确认间隔。
	mAttackCheckTimer += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
	if (mAttackCheckTimer < kTargetCheckIntervalSeconds) return;
	mAttackCheckTimer = 0.0f;
	if (!hasTarget) return;

	mShootTimer = 0.0f;
	if (mPhase == Phase::HIGH) {
		PlayTrackOnce("anim_shootinghigh", "anim_idlehigh",
			kShootClipSpeed * attackSpeed, kTransitionBlendSeconds);
	}
	else {
		PlayTrackOnce("anim_shooting", "anim_idle",
			kShootClipSpeed * attackSpeed, kTransitionBlendSeconds);
	}
}

void Cactus::UpdateTargetCache()
{
	if (!mBoard) {
		mHasGroundTarget = false;
		mHasFlyingTarget = false;
		return;
	}

	// 肥沃同步缩短攻击前索敌间隔，避免固定节流抵消高频攻击。
	mCheckZombieTimer += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
	if (mCheckZombieTimer < kTargetCheckIntervalSeconds) return;
	mCheckZombieTimer = 0.0f;

	const float cactusX = GetPosition().x;
	mHasGroundTarget = false;
	mHasFlyingTarget = false;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (!zombie || !zombie->IsActive()) return;
		const float zombieX = zombie->GetPosition().x;
		if (!zombie->IsMindControlled() && zombie->HasHead()
			&& zombieX >= cactusX && zombieX <= SCENE_WIDTH
			&& mBoard->CanPlantAcquireZombie(this, zombie)) {
			mHasFlyingTarget = mHasFlyingTarget
				|| zombie->CanBeTargetedByProjectile(true);
			mHasGroundTarget = mHasGroundTarget
				|| zombie->CanBeTargetedByProjectile(false);
		}
	});
}

bool Cactus::CanAcquireZombie(const Zombie* zombie) const
{
	return zombie && (zombie->CanBeTargetedByProjectile(false)
		|| zombie->CanBeTargetedByProjectile(true));
}

void Cactus::ShootSpike(bool targetsFlying)
{
	if (!mBoard) return;

	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT
		: ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);
	Bullet* spike = mBoard->CreatePlantBullet(BulletType::BULLET_SPIKE, mRow,
		GetVisualAnchorPosition()
			+ (targetsFlying ? kFlyingSpikeOffset : kGroundSpikeOffset), mPlantType);
	if (spike) {
		spike->SetTargetsFlying(targetsFlying);
	}
}

const char* Cactus::GetPhaseName() const
{
	switch (mPhase) {
	case Phase::LOW: return "LOW";
	case Phase::RISING: return "RISING";
	case Phase::HIGH: return "HIGH";
	case Phase::LOWERING: return "LOWERING";
	}
	return "LOW";
}
