#include "SnowBurrowZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Cell.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kBodyHealth = 700;                       // 潜雪僵尸本体生命值
	constexpr int kBiteDamage = 50;                        // 单次啃咬基础伤害
	constexpr int kEmergenceImpactDamage = 150;            // 自然出雪对锁定格战斗顶层的伤害
	constexpr float kReburrowHealthThreshold = 350.0f;      // 首次触发第二次潜雪的本体生命阈值
	constexpr float kReburrowWindupSeconds = 0.8f;          // 第二次入雪的可中断前摇秒数
	constexpr float kReburrowRetrySeconds = 3.0f;           // 前摇被打断或强制出雪后的重试间隔
	constexpr float kFirstBurrowCells = 2.0f;               // 出生潜雪的最大格数
	constexpr float kSecondBurrowCells = 1.0f;              // 半血潜雪的最大格数
	constexpr float kBurrowSpeed = 92.0f;                   // 雪下水平移动速度，单位像素/游戏秒
	constexpr float kEmergenceRightOffsetCells = 0.65f;     // 出雪点相对目标格中心的右侧格宽比例
	constexpr float kResourceFps = 16.0f;                   // SnowBurrow.reanim 继承矿工时间线帧率
	constexpr float kCSharpTicksPerSecond = 100.0f;         // 经典速度值换算到像素/秒的基准
	constexpr float kWalkVelocityMin = 0.23f;               // 普通步行速度随机下界，单位像素/厘秒
	constexpr float kWalkVelocityMax = 0.37f;               // 普通步行速度随机上界，单位像素/厘秒
	constexpr float kGroundTrackDistance = 72.5f;           // anim_walk 的 _ground 总位移，单位像素
	constexpr float kGroundTrackIntervals = 36.0f;          // anim_walk 根运动区间数
	constexpr float kDeathBaseClip = 1.3f;                  // 基类死亡轨 clip 速度
	constexpr float kDeathFps = 18.0f;                      // 复用矿工死亡轨的目标帧率
	constexpr float kAbilityAnimMultiplier =
		kDeathFps / (kResourceFps * kDeathBaseClip);          // 令死亡轨维持 18 FPS 的整体倍率
	constexpr float kDigClip =
		(12.0f / kResourceFps) / kAbilityAnimMultiplier;      // anim_dig 目标 12 FPS
	constexpr float kRiseClip =
		(20.0f / kResourceFps) / kAbilityAnimMultiplier;      // anim_drill 目标 20 FPS
	constexpr float kLandingClip =
		(12.0f / kResourceFps) / kAbilityAnimMultiplier;      // anim_landing 目标 12 FPS
	constexpr float kEatClip =
		(20.0f / kResourceFps) / kAbilityAnimMultiplier;      // anim_eat 目标 20 FPS
	constexpr float kRiseDuration = 1.05f;                  // 潜雪出地完整时长，单位秒
	constexpr float kRiseCurveSwitch = 0.34f;               // 出地曲线切换到落地段的剩余秒数
	constexpr float kLandingStartRemaining = 0.26f;         // 开始播放落地轨的剩余秒数
	constexpr float kRiseDepth = -112.0f;                   // 出雪初始视觉深度，负值表示地下
	constexpr float kRiseOvershoot = 16.0f;                 // 出雪后短暂越过地面的视觉高度
	constexpr float kTrailInterval = 0.11f;                 // 移动雪尘短爆发间隔，单位秒
	constexpr float kFlipPivotX = 45.0f;                    // 矿工时间线镜像轴，单位局部像素
	constexpr float kGroundClipMargin = 38.0f;              // 出雪裁剪底边相对逻辑行 Y 的余量
	constexpr float kRiseSoundVolume = 0.48f;               // 出雪 Foley 音量
	constexpr float kLimbSoundVolume = 0.25f;               // 掉头与断臂音量
	constexpr float kPositionEpsilon = 0.01f;               // 到达地下目标点的浮点容差

	float EaseOut(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return 1.0f - (1.0f - t) * (1.0f - t);
	}

	float EaseIn(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t;
	}
}

void SnowBurrowZombie::SetupZombie()
{
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mAttackDamage = kBiteDamage;
	mHelmType = HelmType::HELMTYPE_NONE;
	mHelmHealth = 0;
	mHelmMaxHealth = 0;
	mShieldType = ShieldType::SHIELDTYPE_NONE;
	mShieldHealth = 0;
	mShieldMaxHealth = 0;
	mSpeed = kResourceFps;
	mBaseVisualOffsetY = mVisualOffset.y;
	mWalkVelocity = mIsPreview
		? (kWalkVelocityMin + kWalkVelocityMax) * 0.5f
		: GameRandom::Range(kWalkVelocityMin, kWalkVelocityMax);

	if (mIsPreview) {
		mPhase = Phase::WALKING;
		PlayTrack("anim_idle");
		return;
	}

	RegisterFrameEvents();
	BeginBurrow(Phase::FIRST_BURROW, kFirstBurrowCells);
}

/** 复用矿工时间线既有的两次啃食与死亡节点，不引入新的帧事件。 */
void SnowBurrowZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(66, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(81, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(127, [this]() { Die(); });
}

void SnowBurrowZombie::BeginBurrow(Phase phase, float maxCells)
{
	CancelEatingForSpecialAction();
	mPhase = phase;
	mPhaseRemaining = 0.0f;
	mAltitude = 0.0f;
	mBurrowOriginX = GetPosition().x;
	mBurrowLimitX = mBurrowOriginX - maxCells * CELL_COLLIDER_SIZE_X;
	mBurrowTargetX = mBurrowLimitX;
	mEmergenceColumn = -1;
	mNaturalImpactPending = false;
	mLandingStarted = false;
	mBurrowTrailTimer = 0.0f;
	PlayTrack("anim_dig", kDigClip, 0.12f);
	RefreshBurrowTarget();
	ApplyPhasePresentation();
}

/** 每帧按当前位置重找前方最近战斗顶层，使潜雪期间新种下的植物仍能截停。 */
void SnowBurrowZombie::RefreshBurrowTarget()
{
	mBurrowTargetX = mBurrowLimitX;
	mEmergenceColumn = -1;
	if (!mBoard) return;

	const float currentX = GetPosition().x;
	float nearestX = mBurrowLimitX;
	for (int col = mBoard->mColumns - 1; col >= 0; --col) {
		Plant* plant = mBoard->GetTopPlantAt(mRow, col);
		if (!plant || !plant->IsActive()) continue;
		const float emergenceX = mBoard->GetCellCenterPosition(mRow, col).x
			+ kEmergenceRightOffsetCells * CELL_COLLIDER_SIZE_X;
		if (emergenceX > currentX + kPositionEpsilon
			|| emergenceX < mBurrowLimitX - kPositionEpsilon) {
			continue;
		}
		if (mEmergenceColumn < 0 || emergenceX > nearestX) {
			nearestX = emergenceX;
			mEmergenceColumn = col;
		}
	}
	mBurrowTargetX = nearestX;
}

void SnowBurrowZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || scaledDelta <= 0.0f) return;
	if (IsUnderground()) {
		RefreshBurrowTarget();
		Vector position = transform->GetPosition();
		position.x = std::max(mBurrowTargetX, position.x - kBurrowSpeed * scaledDelta);
		transform->SetPosition(position);
		if (position.x <= mBurrowTargetX + kPositionEpsilon) {
			BeginSurfacing(true);
		}
		return;
	}
	if (mPhase == Phase::WALKING) Zombie::ZombieMove(scaledDelta, transform);
}

/** 让重试计时在啃食早退路径中仍继续，并在到期时原子停止啃食、重启前摇。 */
void SnowBurrowZombie::Update()
{
	if (!mIsPreview && !mIsDying && !mIsDead && mReburrowRetryRemaining > 0.0f) {
		mReburrowRetryRemaining = std::max(0.0f,
			mReburrowRetryRemaining - DeltaTime::GetDeltaTime());
	}

	Zombie::Update();
	if (!mIsPreview && !mIsDying && !mIsDead && mPhase == Phase::WALKING) {
		TryBeginReburrowWindup();
	}
}

void SnowBurrowZombie::ZombieUpdate(float scaledTime)
{
	if (mIsDying || mIsDead || scaledTime <= 0.0f) {
		UpdateFacing();
		return;
	}

	switch (mPhase) {
	case Phase::FIRST_BURROW:
	case Phase::SECOND_BURROW:
		mBurrowTrailTimer -= scaledTime;
		while (mBurrowTrailTimer <= 0.0f) {
			EmitBurrowTrail();
			mBurrowTrailTimer += kTrailInterval;
		}
		break;
	case Phase::SURFACING:
		UpdateSurfacing(scaledTime);
		break;
	case Phase::WALKING:
		TryBeginReburrowWindup();
		break;
	case Phase::REBURROW_WINDUP:
		mPhaseRemaining = std::max(0.0f, mPhaseRemaining - scaledTime);
		if (mPhaseRemaining <= 0.0f) CommitSecondBurrow();
		break;
	}
	ApplyPhasePresentation();
}

void SnowBurrowZombie::BeginSurfacing(bool naturalImpact)
{
	if (!IsUnderground()) return;
	mPhase = Phase::SURFACING;
	mPhaseRemaining = kRiseDuration;
	mAltitude = kRiseDepth;
	mNaturalImpactPending = naturalImpact && mEmergenceColumn >= 0;
	mLandingStarted = false;
	PlayTrack("anim_drill", kRiseClip, 0.1f);
	ApplyAltitude();
	ApplyPhasePresentation();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DIRT_RISE, kRiseSoundVolume);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("SnowBurrowRise",
			GetStableVisualOrigin() + Vector(55.0f, 112.0f));
	}
}

void SnowBurrowZombie::UpdateSurfacing(float scaledTime)
{
	const float oldRemaining = mPhaseRemaining;
	mPhaseRemaining = std::max(0.0f, mPhaseRemaining - scaledTime);
	if (mPhaseRemaining > kRiseCurveSwitch) {
		const float t = (kRiseDuration - mPhaseRemaining)
			/ (kRiseDuration - kRiseCurveSwitch);
		mAltitude = kRiseDepth + (kRiseOvershoot - kRiseDepth) * EaseOut(t);
	}
	else {
		const float t = (kLandingStartRemaining - mPhaseRemaining)
			/ kLandingStartRemaining;
		mAltitude = kRiseOvershoot * (1.0f - EaseIn(t));
	}
	ApplyAltitude();

	if (!mLandingStarted && oldRemaining > kLandingStartRemaining
		&& mPhaseRemaining <= kLandingStartRemaining) {
		mLandingStarted = true;
		PlayTrack("anim_landing", kLandingClip, 0.16f);
	}
	if (mPhaseRemaining <= 0.0f) FinishSurfacing();
}

void SnowBurrowZombie::FinishSurfacing()
{
	mAltitude = 0.0f;
	ApplyAltitude();
	ApplyNaturalEmergenceImpact();
	BeginWalking(0.14f);
}

/** 冲击提交时重新取锁定格战斗顶层，天然遵守南瓜头的顶层保护顺序。 */
void SnowBurrowZombie::ApplyNaturalEmergenceImpact()
{
	if (!mNaturalImpactPending || !mBoard || mEmergenceColumn < 0) {
		mNaturalImpactPending = false;
		return;
	}
	mNaturalImpactPending = false;
	if (Plant* target = mBoard->GetTopPlantAt(mRow, mEmergenceColumn);
		target && target->IsActive()) {
		const Vector impactPosition = mBoard->GetCellCenterPosition(mRow, mEmergenceColumn)
			+ Vector(0.0f, -12.0f);
		target->TakeDamage(kEmergenceImpactDamage, DamageSource::ZOMBIE);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("SnowBurrowImpact", impactPosition);
		}
	}
}

void SnowBurrowZombie::TryBeginReburrowWindup()
{
	if (mSecondBurrowSpent || mIsMindControlled || mBodyHealth <= 0
		|| mBodyHealth > static_cast<int>(kReburrowHealthThreshold)
		|| mReburrowRetryRemaining > 0.0f) {
		return;
	}
	CancelEatingForSpecialAction();
	mPhase = Phase::REBURROW_WINDUP;
	mPhaseRemaining = kReburrowWindupSeconds;
	PlayTrack("anim_idle", 0.0f, 0.1f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("SnowBurrowWindup",
			GetStableVisualOrigin() + Vector(52.0f, 108.0f));
	}
	ApplyPhasePresentation();
}

/** 前摇归零即提交第二次潜雪；只在此边沿消费次数并清除寒冷状态。 */
void SnowBurrowZombie::CommitSecondBurrow()
{
	if (mPhase != Phase::REBURROW_WINDUP || mSecondBurrowSpent) return;
	mSecondBurrowSpent = true;
	mBurrowsCommitted = 2;
	RemoveColdEffects();
	BeginBurrow(Phase::SECOND_BURROW, kSecondBurrowCells);
}

void SnowBurrowZombie::BeginWalking(float blendTime)
{
	mPhase = Phase::WALKING;
	mPhaseRemaining = 0.0f;
	mEmergenceColumn = -1;
	mNaturalImpactPending = false;
	PlayWalkAnimation(blendTime);
	ApplyPhasePresentation();
}

void SnowBurrowZombie::TakeDamage(int damage, DamageSource source,
	bool penetrateShield, bool discardShieldOverflow, bool bypassShield,
	PlantDamageOrigin plantOrigin)
{
	const int previousBodyHealth = mBodyHealth;
	Zombie::TakeDamage(damage, source, penetrateShield,
		discardShieldOverflow, bypassShield, plantOrigin);
	if (previousBodyHealth > static_cast<int>(kReburrowHealthThreshold)
		&& mBodyHealth <= static_cast<int>(kReburrowHealthThreshold)
		&& mBodyHealth > 0 && !mIsDying && !mIsDead
		&& mPhase == Phase::WALKING) {
		TryBeginReburrowWindup();
	}
}

void SnowBurrowZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::WALKING) return;
	Zombie::StartEat(other);
}

void SnowBurrowZombie::OnStartEating()
{
	PlayTrack("anim_eat", kEatClip, 0.18f);
	UpdateFacing();
}

void SnowBurrowZombie::PlayWalkAnimation(float blendTime)
{
	PlayTrack("anim_walk", GetWalkClipSpeed(), blendTime);
}

bool SnowBurrowZombie::ForceSurfaceFromGroundHazard()
{
	if (!IsUnderground() || mIsDying || mIsDead) return false;
	mNaturalImpactPending = false;
	if (!mSecondBurrowSpent
		&& mBodyHealth <= static_cast<int>(kReburrowHealthThreshold)) {
		// 只在半血能力已经具备资格时延迟重钻；满血教学出雪不能预埋未来冷却。
		mReburrowRetryRemaining = std::max(mReburrowRetryRemaining,
			kReburrowRetrySeconds);
	}
	BeginSurfacing(false);
	return true;
}

float SnowBurrowZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mPhase == Phase::REBURROW_WINDUP ? mPhaseRemaining : -1.0f;
}

bool SnowBurrowZombie::InterruptUncommittedSpecialAction()
{
	if (mPhase != Phase::REBURROW_WINDUP) return false;
	mReburrowRetryRemaining = kReburrowRetrySeconds;
	BeginWalking(0.1f);
	return true;
}

void SnowBurrowZombie::OnMindControlled()
{
	// 魅惑只允许发生在地表；未提交前摇被原子撤销，敌对潜雪能力不转交给玩家阵营。
	if (mPhase == Phase::REBURROW_WINDUP) {
		mSecondBurrowSpent = true;
		BeginWalking(0.1f);
	}
	UpdateFacing();
}

bool SnowBurrowZombie::CanTriggerGameOver() const
{
	return !mIsMindControlled && mPhase == Phase::WALKING;
}

bool SnowBurrowZombie::CanBeTargetedByProjectile(bool targetsFlying) const
{
	return !targetsFlying && IsSurfaceInteractive();
}

bool SnowBurrowZombie::CanBeCharmed() const
{
	return mPhase == Phase::WALKING || mPhase == Phase::REBURROW_WINDUP;
}

bool SnowBurrowZombie::TryGetDrawClipBottom(float& clipBottom) const
{
	if (!mIsPreview && mAltitude < 0.0f && mBoard) {
		clipBottom = static_cast<float>(static_cast<int>(std::lround(
			mBoard->GetZombieSpawnY(mRow, GetPosition().x) + kGroundClipMargin)));
		return true;
	}
	return Zombie::TryGetDrawClipBottom(clipBottom);
}

void SnowBurrowZombie::HeadDrop()
{
	if (!mHasHead) return;
	const Vector origin = GetTrackWorldPosition("anim_head1");
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("Zombie_digger_head_eye", false);
	mAnimator->SetTrackVisible("Zombie_digger_hardhat", false);
	if (g_particleSystem) g_particleSystem->EmitEffect("ZombieSnowBurrowHeadOff", origin);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbSoundVolume);
}

void SnowBurrowZombie::ArmDrop()
{
	if (!mHasArm) return;
	const Vector origin = GetTrackWorldPosition("Zombie_digger_outerarm_upper");
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackImage("Zombie_digger_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_SNOWBURROW_OUTERARM_UPPER2));
	if (g_particleSystem) g_particleSystem->EmitEffect("ZombieSnowBurrowArmOff", origin);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbSoundVolume);
}

void SnowBurrowZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	mAnimator->SetTrackVisible("_ground", false);
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackImage("Zombie_digger_outerarm_upper",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_SNOWBURROW_OUTERARM_UPPER2));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("Zombie_digger_head_eye", false);
		mAnimator->SetTrackVisible("Zombie_digger_hardhat", false);
	}
	if (mCollider) mCollider->mEnabled = !mIsDying && !mIsDead;
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(IsSurfaceInteractive() && !mIsDying && !mIsDead);
	}
}

void SnowBurrowZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (!mAnimator) return;
	if (mHasArm) {
		mAnimator->SetTrackImage("Zombie_digger_outerarm_upper", nullptr);
	}
	if (mHasHead) {
		mAnimator->SetTrackVisible("Zombie_digger_head_eye", true);
		mAnimator->SetTrackVisible("Zombie_digger_hardhat", true);
	}
}

void SnowBurrowZombie::ApplyPhasePresentation()
{
	ApplyAltitude();
	if (mCollider) mCollider->mEnabled = !mIsDying && !mIsDead;
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(IsSurfaceInteractive() && !mIsDying && !mIsDead);
	}
	if (mAnimator) {
		mAnimator->SetTrackVisible("_ground", false);
		if (!mHasHead) mAnimator->SetTrackVisible("Zombie_digger_hardhat", false);
	}
	UpdateFacing();
}

void SnowBurrowZombie::ApplyAltitude()
{
	mVisualOffset.y = mBaseVisualOffsetY - mAltitude;
}

void SnowBurrowZombie::UpdateFacing()
{
	if (mAnimator && !mIsPreview) mAnimator->SetFlipX(IsMovingRight(), kFlipPivotX);
}

void SnowBurrowZombie::EmitBurrowTrail()
{
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("SnowBurrowTrail",
			GetStableVisualOrigin() + Vector(54.0f, 112.0f));
	}
}

Vector SnowBurrowZombie::GetStableVisualOrigin() const
{
	return GetPosition() + Vector(mVisualOffset.x, mBaseVisualOffsetY);
}

float SnowBurrowZombie::GetWalkClipSpeed() const
{
	const float pixelsPerSecond = mWalkVelocity * kCSharpTicksPerSecond;
	const float groundPixelsPerFrame = kGroundTrackDistance / kGroundTrackIntervals;
	return pixelsPerSecond
		/ (groundPixelsPerFrame * kResourceFps * kAbilityAnimMultiplier);
}

bool SnowBurrowZombie::IsUnderground() const
{
	return mPhase == Phase::FIRST_BURROW || mPhase == Phase::SECOND_BURROW;
}

bool SnowBurrowZombie::IsSurfaceInteractive() const
{
	return mPhase == Phase::WALKING || mPhase == Phase::REBURROW_WINDUP;
}

float SnowBurrowZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kAbilityAnimMultiplier;
}

const char* SnowBurrowZombie::GetPhaseName() const
{
	switch (mPhase) {
	case Phase::FIRST_BURROW: return "FIRST_BURROW";
	case Phase::SURFACING: return "SURFACING";
	case Phase::WALKING: return "WALKING";
	case Phase::REBURROW_WINDUP: return "REBURROW_WINDUP";
	case Phase::SECOND_BURROW: return "SECOND_BURROW";
	}
	return "FIRST_BURROW";
}

void SnowBurrowZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["phaseRemaining"] = mPhaseRemaining;
	j["altitude"] = mAltitude;
	j["walkVelocity"] = mWalkVelocity;
	j["burrowOriginX"] = mBurrowOriginX;
	j["burrowLimitX"] = mBurrowLimitX;
	j["burrowTargetX"] = mBurrowTargetX;
	j["burrowTrailTimer"] = mBurrowTrailTimer;
	j["reburrowRetryRemaining"] = mReburrowRetryRemaining;
	j["emergenceColumn"] = mEmergenceColumn;
	j["burrowsCommitted"] = mBurrowsCommitted;
	j["secondBurrowSpent"] = mSecondBurrowSpent;
	j["naturalImpactPending"] = mNaturalImpactPending;
	j["landingStarted"] = mLandingStarted;
}

void SnowBurrowZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::SECOND_BURROW));
	mPhase = static_cast<Phase>(phase);
	mPhaseRemaining = std::clamp(j.value("phaseRemaining", 0.0f),
		0.0f, kRiseDuration);
	mAltitude = std::clamp(j.value("altitude", 0.0f), kRiseDepth, kRiseOvershoot);
	mWalkVelocity = std::clamp(j.value("walkVelocity", 0.30f),
		kWalkVelocityMin, kWalkVelocityMax);
	mBurrowOriginX = j.value("burrowOriginX", GetPosition().x);
	const float maxDistance = (mPhase == Phase::SECOND_BURROW
		? kSecondBurrowCells : kFirstBurrowCells) * CELL_COLLIDER_SIZE_X;
	mBurrowLimitX = std::clamp(j.value("burrowLimitX", mBurrowOriginX - maxDistance),
		mBurrowOriginX - maxDistance, mBurrowOriginX);
	mBurrowTargetX = std::clamp(j.value("burrowTargetX", mBurrowLimitX),
		mBurrowLimitX, mBurrowOriginX);
	mBurrowTrailTimer = std::clamp(j.value("burrowTrailTimer", 0.0f),
		0.0f, kTrailInterval);
	mReburrowRetryRemaining = std::clamp(
		j.value("reburrowRetryRemaining", 0.0f), 0.0f, kReburrowRetrySeconds);
	mEmergenceColumn = std::clamp(j.value("emergenceColumn", -1),
		-1, mBoard ? mBoard->mColumns - 1 : -1);
	mBurrowsCommitted = std::clamp(j.value("burrowsCommitted", 1), 1, 2);
	mSecondBurrowSpent = j.value("secondBurrowSpent", mBurrowsCommitted >= 2);
	mNaturalImpactPending = j.value("naturalImpactPending", false)
		&& mPhase == Phase::SURFACING && mEmergenceColumn >= 0;
	mLandingStarted = j.value("landingStarted", false);

	// 已提交的地下阶段不可被损坏档返还；反之，未提交前摇不得伪装成第二次潜雪。
	if (mPhase == Phase::SECOND_BURROW) {
		mSecondBurrowSpent = true;
		mBurrowsCommitted = 2;
	}
	if (mPhase == Phase::REBURROW_WINDUP && mSecondBurrowSpent) {
		mPhase = Phase::WALKING;
		mPhaseRemaining = 0.0f;
	}
	ApplyPhasePresentation();
}

ZombieMovementRules::BirthProfile SnowBurrowZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.linear=true;
	p.velocityMinimum=p.velocityMaximum=kBurrowSpeed;
	p.phaseDependent=true;
	return p;
}

float SnowBurrowZombie::GetMineSimulationMoveSpeed() const
{
	if (!IsUnderground()) return Zombie::GetMineSimulationMoveSpeed();
	return kBurrowSpeed*AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier())*GetAmberMovementMultiplier();
}
