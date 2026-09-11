#include "BalloonZombie.h"
#include "../../GameApp.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cstdint>
#include <climits>
#include <cmath>

namespace {
	constexpr int kBodyHealth = 270;                    // C# 气球僵尸落地后的本体生命值
	constexpr int kBalloonHealth = 20;                  // C# mFlyingHealth：气球额外生命层
	constexpr float kFlightVelocityMin = 23.0f;         // C# 0.23 px/tick 换算到秒的飞行速度下界
	constexpr float kFlightVelocityMax = 37.0f;         // C# 0.37 px/tick 换算到秒的飞行速度上界
	constexpr float kFlyingColliderRise = 60.0f;        // 空中碰撞框相对地面抬高量，单位：像素
	constexpr float kPopClipSpeed = 24.0f / 12.0f;      // C# anim_pop 24 FPS 相对资源 12 FPS
	constexpr float kEatClipSpeed = 20.0f / 12.0f;      // C# anim_eat 20 FPS 相对资源 12 FPS
	constexpr float kDeathClipSpeed = 24.0f / 12.0f;    // C# anim_death 24 FPS 相对资源 12 FPS
	constexpr float kPropellerAnchorInverseX = 1.875f;  // hat 首帧 x=-1.5、sx=0.8 的逆锚点 X
	constexpr float kPropellerAnchorInverseY = -42.75f; // hat 首帧 y=34.2、sy=0.8 的逆锚点 Y
	constexpr int kEatEventFrameOne = 70;               // 主人给定的第一处啃食伤害全局帧
	constexpr int kEatEventFrameTwo = 80;               // 主人给定的第二处啃食伤害全局帧
	constexpr int kDeathEventFrame = 152;               // 主人给定的死亡回收全局帧
	constexpr float kOneShotVolume = 0.4f;              // 气球充气、爆裂与断肢音效音量
	constexpr float kBloverHouseDisplacement = 400.0f;  // 三叶草吹向屋后时每次累计滑行距离，单位：像素
	constexpr float kBloverBlowSpeed = 600.0f;          // 三叶草吹飞的连续横移速度，单位：像素/秒
	constexpr float kBloverFrontExitPadding = 80.0f;    // 向前线吹飞后的画面外死亡安全余量，单位：像素
}

void BalloonZombie::SetupZombie()
{
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mBalloonHealth = kBalloonHealth;
	mBalloonMaxHealth = kBalloonHealth;
	mFlightVelocity = mIsPreview
		? (kFlightVelocityMin + kFlightVelocityMax) * 0.5f
		: GameRandom::Range(kFlightVelocityMin, kFlightVelocityMax);
	mPhase = Phase::FLYING;
	mNeedDropArm = false;
	mNeedDropHead = false;

	if (mCollider) {
		mGroundColliderOffsetY = mCollider->offset.y;
	}

	CreatePropellerAnimator();
	if (mIsPreview) {
		PlayTrack("anim_idle");
		KeepPropellerIndependent();
		return;
	}

	RegisterFrameEvents();
	PlayTrack("anim_idle");
	ApplyPhasePresentation();
	KeepPropellerIndependent();
}

/** 注册主人确认的两处啃食命中帧与死亡回收末帧。 */
void BalloonZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(kEatEventFrameOne, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(kEatEventFrameTwo, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(kDeathEventFrame, [this]() { Die(); });
}

void BalloonZombie::CreatePropellerAnimator()
{
	auto reanim = mAnimator ? mAnimator->GetReanimation() : nullptr;
	if (!reanim) return;

	mPropellerAnimator = std::make_shared<Animator>(reanim);
	mPropellerAnimator->PlayTrack("propeller");
	mPropellerAnimator->SetSpeed(1.0f);
	// 本项目附件矩阵只有 current，没有 C# 的 inverse(basePose)；先抵消 hat 首帧锚点，
	// 再让子动画自己的 propeller 轨道提供原始绝对姿态。
	mPropellerAnimator->SetLocalPosition(
		kPropellerAnchorInverseX, kPropellerAnchorInverseY);
	if (!mAnimator->AttachAnimator("hat", mPropellerAnimator)) {
		mPropellerAnimator.reset();
	}
}

void BalloonZombie::Update()
{
	// 父动画切轨会把 clip 速度递归到附件；螺旋桨只继承冻结/减速的 extra 层，
	// 自身始终按 propeller 轨道基础速度循环。
	KeepPropellerIndependent();
	Zombie::Update();
	if (mIsDying && GetCurrentTrackName() == GetDeathTrackName()
		&& std::abs(GetClipSpeed() - kDeathClipSpeed) > 0.001f) {
		SetClipSpeed(kDeathClipSpeed);
	}
	KeepPropellerIndependent();
}

void BalloonZombie::KeepPropellerIndependent() const
{
	if (mPropellerAnimator) {
		mPropellerAnimator->SetClipSpeed(0.0f);
	}
}

void BalloonZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase == Phase::FLYING) {
		if (!transform) return;
		if (mBloverBlowing) {
			// 吹飞态独占本帧横移，避免普通飞行和台风倍率污染“每株朝屋后 400px”的距离契约。
			const float step = kBloverBlowSpeed * scaledDelta;
			if (mBloverBlowDirection == WindDirection::TOWARD_HOUSE) {
				const float applied = std::min(step, mBloverBlowRemaining);
				transform->Translate(-applied, 0.0f);
				mBloverBlowRemaining =
					std::max(0.0f, mBloverBlowRemaining - applied);
				if (mBloverBlowRemaining <= 0.0f) {
					mBloverBlowing = false;
					mBloverBlowDirection = WindDirection::NONE;
				}
				return;
			}
			if (mBloverBlowDirection == WindDirection::TOWARD_FRONT) {
				transform->Translate(step, 0.0f);
				const float exitX =
					static_cast<float>(SCENE_WIDTH) + kBloverFrontExitPadding;
				if (GetPosition().x >= exitX) {
					SetPosition(Vector(exitX, GetPosition().y));
					Die();
				}
				return;
			}
			mBloverBlowing = false;
			mBloverBlowDirection = WindDirection::NONE;
			mBloverBlowRemaining = 0.0f;
		}
		const float direction = mIsMindControlled ? 1.0f : -1.0f;
		const float windMultiplier = mBoard
			? AmplifySpeedMultiplierForGoldenIce(
				mBoard->GetZombieWindMoveMultiplier(mIsMindControlled))
			: 1.0f;
		transform->Translate(
			direction * mFlightVelocity * windMultiplier * scaledDelta, 0.0f);
		return;
	}
	if (mPhase == Phase::WALKING) {
		Zombie::ZombieMove(scaledDelta, transform);
	}
}

void BalloonZombie::ZombieUpdate(float)
{
	if (mPhase == Phase::POPPING && GetCurrentTrackName() == "anim_walk") {
		FinishLanding();
	}
}

int BalloonZombie::TakeExtraProtectionDamage(int damage, DamageSource)
{
	if (mPhase != Phase::FLYING || mBalloonHealth <= 0) return damage;

	const int absorbed = std::min(damage, mBalloonHealth);
	mBalloonHealth -= absorbed;
	const int overflow = damage - absorbed;
	if (mBalloonHealth <= 0) {
		PopBalloon();
	}
	return mIsDead ? 0 : overflow;
}

void BalloonZombie::PopBalloon()
{
	if (mPhase != Phase::FLYING || mIsDead) return;

	mBloverBlowing = false;
	mBloverBlowDirection = WindDirection::NONE;
	mBloverBlowRemaining = 0.0f;
	mBalloonHealth = 0;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BALLOON_POP, kOneShotVolume);
	// 原版在泳池行击破气球后直接清除，避免没有水上落地轨道的僵尸站在水面。
	if (mBoard && mBoard->IsPoolRow(mRow)) {
		Die();
		return;
	}

	mPhase = Phase::POPPING;
	PlayTrackOnce("anim_pop", "anim_walk", kPopClipSpeed, 0.2f);
	ApplyPhasePresentation();
	KeepPropellerIndependent();
}

void BalloonZombie::FinishLanding()
{
	if (mPhase != Phase::POPPING) return;
	mPhase = Phase::WALKING;
	ApplyPhasePresentation();
	PlayWalkAnimation(0.0f);
	ResolveDeferredBodyParts();
}

void BalloonZombie::ApplyPhasePresentation()
{
	if (mCollider) {
		mCollider->offset.y = mGroundColliderOffsetY
			- (mPhase == Phase::WALKING ? 0.0f : kFlyingColliderRise);
	}
	const bool canLoseParts = mPhase == Phase::WALKING;
	mNeedDropArm = canLoseParts;
	mNeedDropHead = canLoseParts;
	if (!canLoseParts && mInPool) {
		mInPool = false;
		UpdatePoolVisualState();
	}
}

void BalloonZombie::ResolveDeferredBodyParts()
{
	if (mHasArm
		&& mBodyHealth <= static_cast<int64_t>(mBodyMaxHealth) * 2 / 3) {
		ArmDrop();
		mHasArm = false;
	}
	if (mHasHead && mBodyHealth <= mBodyMaxHealth / 3) {
		HeadDrop();
		mHasHead = false;
	}
}

void BalloonZombie::PlayWalkAnimation(float blendTime)
{
	if (mPhase != Phase::WALKING) return;
	PlayTrack("anim_walk", 0.0f, blendTime);
	KeepPropellerIndependent();
}

void BalloonZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::WALKING) return;
	Zombie::StartEat(other);
}

void BalloonZombie::OnStartEating()
{
	PlayTrack("anim_eat", kEatClipSpeed, 0.2f);
	KeepPropellerIndependent();
}

bool BalloonZombie::CanBeTargetedByProjectile(bool targetsFlying) const
{
	if (targetsFlying) return mPhase == Phase::FLYING;
	return mPhase == Phase::WALKING;
}

void BalloonZombie::PlaySpawnSound()
{
	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_BALLOONINFLATE, kOneShotVolume);
}

void BalloonZombie::TakePlantAshDamage(int damage)
{
	if (damage <= 0 || !mBoard) return;

	// 灰烬直消只取代致死表现；高血量生存模式下的非致死爆炸仍须走正式伤害链。
	const int scaledDamage =
		mBoard->ScaleMineFogDamage(mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage),this);
	const int64_t remainingHealth = static_cast<int64_t>(mBodyHealth)
		+ (mPhase == Phase::FLYING ? mBalloonHealth : 0);
	if (remainingHealth <= scaledDamage) {
		Die();
		return;
	}
	TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false,
		PlantDamageOrigin::Ash());
}

void BalloonZombie::ArmDrop()
{
	if (!mHasArm || mPhase != Phase::WALKING) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_BALLOON_OUTERARM_UPPER2));
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBalloonArmOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kOneShotVolume);
}

void BalloonZombie::HeadDrop()
{
	if (!mHasHead || mPhase != Phase::WALKING) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("hat", false);
	if (mPropellerAnimator) {
		mPropellerAnimator->SetAlpha(0.0f);
		mPropellerAnimator->Pause();
	}
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBalloonHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kOneShotVolume);
}

void BalloonZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackImage("Zombie_outerarm_upper",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_BALLOON_OUTERARM_UPPER2));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("hat", false);
		if (mPropellerAnimator) {
			mPropellerAnimator->SetAlpha(0.0f);
			mPropellerAnimator->Pause();
		}
	}
}

void BalloonZombie::ApplyExtraHealthMultiplier(double multiplier)
{
	if (multiplier <= 0.0 || multiplier == 1.0) return;
	auto scale = [multiplier](int value) {
		const double scaled = static_cast<double>(value) * multiplier + 0.5;
		return scaled >= static_cast<double>(INT_MAX)
			? INT_MAX : static_cast<int>(scaled);
	};
	mBalloonHealth = scale(mBalloonHealth);
	mBalloonMaxHealth = scale(mBalloonMaxHealth);
}

void BalloonZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["balloonHealth"] = mBalloonHealth;
	j["balloonMaxHealth"] = mBalloonMaxHealth;
	j["flightVelocity"] = mFlightVelocity;
	j["bloverBlowing"] = mBloverBlowing;
	j["bloverBlowDirection"] = static_cast<int>(mBloverBlowDirection);
	j["bloverBlowRemaining"] = mBloverBlowRemaining;
	if (mPropellerAnimator) {
		j["propellerFrame"] = mPropellerAnimator->GetCurrentFrame();
		j["propellerPlaying"] = mPropellerAnimator->IsPlaying();
	}
}

void BalloonZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::WALKING));
	mPhase = static_cast<Phase>(phase);
	mBalloonMaxHealth = std::max(0,
		j.value("balloonMaxHealth", kBalloonHealth));
	mBalloonHealth = std::clamp(
		j.value("balloonHealth", mBalloonMaxHealth), 0, mBalloonMaxHealth);
	mFlightVelocity = std::clamp(
		j.value("flightVelocity", 30.0f), 0.0f, kFlightVelocityMax);
	const int blowDirection = j.value("bloverBlowDirection",
		static_cast<int>(WindDirection::NONE));
	mBloverBlowDirection =
		blowDirection == static_cast<int>(WindDirection::TOWARD_HOUSE)
		? WindDirection::TOWARD_HOUSE
		: blowDirection == static_cast<int>(WindDirection::TOWARD_FRONT)
			? WindDirection::TOWARD_FRONT : WindDirection::NONE;
	mBloverBlowing = mPhase == Phase::FLYING
		&& j.value("bloverBlowing", false)
		&& mBloverBlowDirection != WindDirection::NONE;
	mBloverBlowRemaining = mBloverBlowing
		&& mBloverBlowDirection == WindDirection::TOWARD_HOUSE
		? std::max(0.0f,
			j.value("bloverBlowRemaining", kBloverHouseDisplacement))
		: 0.0f;
	if (mPropellerAnimator) {
		mPropellerAnimator->PlayTrack("propeller");
		mPropellerAnimator->SetSpeed(1.0f);
		mPropellerAnimator->SetCurrentFrame(std::clamp(
			j.value("propellerFrame", 153.0f), 153.0f, 154.0f));
		if (!j.value("propellerPlaying", true)) {
			mPropellerAnimator->Pause();
		}
	}
	ApplyPhasePresentation();
	KeepPropellerIndependent();
}

void BalloonZombie::SetFlightVelocity(float velocity)
{
	mFlightVelocity = std::clamp(velocity, 0.0f, kFlightVelocityMax);
}

void BalloonZombie::BlowAway(WindDirection direction)
{
	if (mPhase != Phase::FLYING || mIsDead || mIsDying || !IsActive()) return;

	if (direction == WindDirection::TOWARD_HOUSE) {
		if (mBloverBlowing
			&& mBloverBlowDirection == WindDirection::TOWARD_HOUSE) {
			mBloverBlowRemaining += kBloverHouseDisplacement;
		}
		else {
			mBloverBlowRemaining = kBloverHouseDisplacement;
		}
		mBloverBlowDirection = direction;
		mBloverBlowing = true;
		return;
	}
	if (direction == WindDirection::TOWARD_FRONT) {
		mBloverBlowDirection = direction;
		mBloverBlowRemaining = 0.0f;
		mBloverBlowing = true;
	}
}

float BalloonZombie::GetPropellerFrame() const
{
	return mPropellerAnimator ? mPropellerAnimator->GetCurrentFrame() : -1.0f;
}

bool BalloonZombie::IsPropellerPlaying() const
{
	return mPropellerAnimator && mPropellerAnimator->IsPlaying();
}
