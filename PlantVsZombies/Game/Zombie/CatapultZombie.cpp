#include "CatapultZombie.h"

#include "CatapultCharred.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../GameObjectManager.h"
#include "../Plant/Caltrop.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"
#include "../../GameRandom.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kCatapultHealth = 850;                    // 原版投篮车整车本体生命
	constexpr float kDriveSpeedMin = 23.0f;                 // 出生随机基础车速下限，单位 px/s
	constexpr float kDriveSpeedMax = 37.0f;                 // 出生随机基础车速上限，单位 px/s
	constexpr float kWalkClipSpeed = 5.5f / 12.0f;          // 原版 anim_walk 5.5fps 相对资源 12fps 的倍率
	constexpr float kShootClipSpeed = 2.0f;                 // 原版 anim_shoot 24fps 相对资源 12fps 的倍率
	constexpr float kIdleClipSpeed = 1.0f;                  // 原版 anim_idle 12fps 相对资源 12fps 的倍率
	constexpr float kBounceClipSpeed = 1.0f;                // 原版 anim_bounce 12fps 相对资源 12fps 的倍率
	constexpr int kShootFrame = 46;                         // 主人提供的真实 AddFrameEvent 投篮帧
	constexpr int kInitialBasketballs = 12;                 // 主人调高后的初始篮球库存
	constexpr float kReloadSeconds = 3.0f;                  // 原版 mPhaseCounter=300，单位秒
	constexpr float kShootStartInsideBoard = 150.0f;        // 进入逻辑棋盘右缘该距离后允许投篮，单位 px
	constexpr float kMinimumTargetLead = 100.0f;            // 车辆至少位于植物右侧该距离才可锁定，单位 px
	constexpr float kLobDuration = 1.2f;                    // 篮球固定飞行时间，单位秒
	constexpr float kLobApexHeight = 210.0f;                // 篮球相对起终点连线的最高拱高，单位 px
	constexpr float kColliderFromVisualX = -5.0f;          // 碰撞框左缘相对稳定视觉原点的 X，单位 px
	constexpr float kColliderFromVisualY = 28.0f;           // 碰撞框上缘相对稳定视觉原点的 Y，单位 px
	constexpr float kAttackFromVisualX = -20.0f;            // 碾压攻击框左缘相对稳定视觉原点的 X，单位 px
	constexpr float kAttackFromVisualY = 28.0f;             // 碾压攻击框上缘相对稳定视觉原点的 Y，单位 px
	constexpr float kAttackWidth = 133.0f;                  // 原版车辆攻击矩形宽度，单位 px
	constexpr float kAttackHeight = 140.0f;                 // 原版车辆攻击矩形高度，单位 px
	constexpr float kRequiredPlantOverlap = 20.0f;          // 至少覆盖该水平像素数才判定碾压
	constexpr float kBasketballFromVisualX = 100.0f;        // 篮球弹心向车尾回收后的起点相对稳定视觉原点 X，单位 px
	constexpr float kBasketballFromVisualY = -3.0f;         // 篮球弹心起点相对稳定视觉原点的 Y，单位 px
	constexpr float kSmokeFromVisualX = 4.0f;               // 二段损坏烟雾相对稳定视觉原点的 X，单位 px
	constexpr float kSmokeFromVisualY = 118.0f;             // 二段损坏烟雾相对稳定视觉原点的 Y，单位 px
	constexpr float kSmokeInterval = 0.25f;                 // 二段损坏烟雾补发间隔，单位秒
	constexpr int kCriticalHealth = 200;                    // 低于该血量后车辆概率自损
	constexpr float kCriticalBrokenTime = 0.1f;             // 低血量自损判定间隔，单位秒
	constexpr int kCriticalSelfDamage = 3;                  // 每次命中概率后的本体自损值
	constexpr int kCriticalDamageRollMax = 4;               // 0..4 命中 0，即每次 1/5 概率
	constexpr float kCriticalShakeAmplitude = 0.35f;        // 低血量故障抖动幅度，单位 px
	constexpr float kCaltropDeathDelaySeconds = 2.8f;       // 原版 mPhaseCounter=280，单位秒
	constexpr float kTireFromVisualX = -14.0f;              // 爆胎碎屑相对稳定视觉原点的 X，单位 px
	constexpr float kTireFromVisualY = 155.0f;              // 爆胎碎屑相对稳定视觉原点的 Y，单位 px
	constexpr float kDeathEffectFromVisualX = 37.0f;        // 普通爆炸中心相对稳定视觉原点的 X，单位 px
	constexpr float kDeathEffectFromVisualY = 101.0f;       // 普通爆炸中心相对稳定视觉原点的 Y，单位 px
	constexpr float kCharredFromVisualX = -57.0f;           // 专属灰烬原点相对稳定视觉原点的 X，单位 px
	constexpr float kCharredFromVisualY = 11.0f;            // 专属灰烬原点相对稳定视觉原点的 Y，单位 px
	constexpr float kIceTrapBottomFromVisualX = 95.0f;      // 冰晶底边中心相对稳定视觉原点的 X，置于整车视觉中央
	constexpr float kIceTrapBottomFromVisualY = 143.0f;     // 冰晶底边中心相对稳定视觉原点的 Y，保持原脚底高度

	float HorizontalOverlap(const SDL_FRect& lhs, const SDL_FRect& rhs)
	{
		return std::max(0.0f,
			std::min(lhs.x + lhs.w, rhs.x + rhs.w) - std::max(lhs.x, rhs.x));
	}
}

void CatapultZombie::SetupZombie()
{
	mBodyMaxHealth = kCatapultHealth;
	mBodyHealth = kCatapultHealth;
	mNeedDropArm = false;
	mNeedDropHead = false;
	mHasArm = true;
	mHasHead = true;
	mDriveSpeed = GameRandom::Range(kDriveSpeedMin, kDriveSpeedMax);
	mBasketballCount = kInitialBasketballs;

	if (mCollider) {
		mCollider->size = Vector(150.0f, 140.0f);
		mCollider->offset = mVisualOffset
			+ Vector(kColliderFromVisualX, kColliderFromVisualY);
		mCollider->SetTriggerEnterCallback([this](ColliderComponent* other) { StartEat(other); });
		mCollider->SetTriggerStayCallback([this](ColliderComponent* other) { StartEat(other); });
		mCollider->SetTriggerExitCallback(nullptr);
	}
	RemoveShadow();

	if (!mAnimator) return;
	SetAnimationSpeed(1.0f);
	if (mIsPreview) {
		PlayTrack("anim_idle", kIdleClipSpeed);
		return;
	}
	mAnimator->AddFrameEvent(kShootFrame, [this]() { LaunchBasketball(); }, true);
	PlayWalking();
	ApplyBasketballPresentation();
}

void CatapultZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || scaledDelta <= 0.0f || mPhase != Phase::WALKING) return;
	float speed = mDriveSpeed * GetAmplifiedAbilitySpeedMultiplier();
	if (mBoard) {
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieRainSpeedMultiplier());
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(false));
	}
	transform->Translate(-speed * scaledDelta, 0.0f);
}

float CatapultZombie::GetCurrentHorizontalMoveSpeed() const
{
	if (mIsDead || mIsDying || IsImmobilized() || mPhase != Phase::WALKING) return 0.0f;
	float speed = mDriveSpeed * GetAmplifiedAbilitySpeedMultiplier();
	if (mCooldownTimer > 0.0f) speed *= 0.5f;
	if (mBoard) {
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieRainSpeedMultiplier());
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(false));
	}
	return std::max(0.0f, speed * AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier()) * GetAmberMovementMultiplier());
}

void CatapultZombie::ZombieUpdate(float scaledTime)
{
	if (!mBoard || mIsPreview || mIsDead) return;
	if (mPhase == Phase::CALTROP_DYING) {
		mPhaseTimer = std::max(0.0f, mPhaseTimer - scaledTime);
		if (mPhaseTimer <= 0.0f) Die();
		return;
	}
	CrushPlants();

	if (GetDamageStage() >= 2) {
		mSmokeTimer -= scaledTime;
		if (mSmokeTimer <= 0.0f) {
			mSmokeTimer += kSmokeInterval;
			if (g_particleSystem) {
				g_particleSystem->EmitEffect("ZamboniSmoke",
					GetPosition() + mVisualOffset
						+ Vector(kSmokeFromVisualX, kSmokeFromVisualY));
			}
		}
	}
	else {
		mSmokeTimer = 0.0f;
	}

	if (mBodyHealth < kCriticalHealth) {
		mSelfBrokenTimer += scaledTime;
		if (mSelfBrokenTimer >= kCriticalBrokenTime) {
			mSelfBrokenTimer = 0.0f;
			mDamageShakeOffset = Vector(
				GameRandom::Range(-kCriticalShakeAmplitude, kCriticalShakeAmplitude),
				GameRandom::Range(-kCriticalShakeAmplitude, kCriticalShakeAmplitude));
			if (GameRandom::Range(0, kCriticalDamageRollMax) == 0) {
				TakeBodyDamage(kCriticalSelfDamage);
				if (mIsDead) return;
			}
		}
	}
	else {
		mDamageShakeOffset = Vector::zero();
	}

	switch (mPhase) {
	case Phase::WALKING: {
		const float boardRight = mBoard->GetCellCenterPosition(mRow, mBoard->mColumns - 1).x
			+ CELL_COLLIDER_SIZE_X * 0.5f;
		if (mBasketballCount > 0
			&& GetPosition().x <= boardRight - kShootStartInsideBoard) {
			if (Plant* target = FindBasketballTarget()) BeginShooting(*target);
		}
		break;
	}
	case Phase::SHOOTING:
		if (mAnimator && !mAnimator->IsPlaying()) FinishShooting();
		break;
	case Phase::RELOADING:
		mPhaseTimer = std::max(0.0f, mPhaseTimer - scaledTime);
		if (mPhaseTimer <= 0.0f) {
			if (Plant* target = FindBasketballTarget()) BeginShooting(*target);
			else PlayWalking();
		}
		break;
	case Phase::CALTROP_DYING:
		break;
	}
}

void CatapultZombie::BeginShooting(Plant& target)
{
	if (!mAnimator || mBasketballCount <= 0) return;
	ColliderComponent* targetCollider = target.GetColliderComponent();
	if (targetCollider) {
		const SDL_FRect bounds = targetCollider->GetBoundingBox();
		mShotTarget = Vector(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f);
	}
	else {
		mShotTarget = target.GetPosition();
	}
	mShotFiredThisCycle = false;
	mPhase = Phase::SHOOTING;
	PlayTrackOnce("anim_shoot", "", kShootClipSpeed, 0.0f);
}

void CatapultZombie::LaunchBasketball()
{
	if (mPhase != Phase::SHOOTING || mShotFiredThisCycle || !mBoard) return;
	const Vector origin = GetPosition() + mVisualOffset
		+ Vector(kBasketballFromVisualX, kBasketballFromVisualY);
	Bullet* basketball = mBoard->CreateBullet(
		BulletType::BULLET_BASKETBALL, mRow, origin);
	if (!basketball) return;
	basketball->ConfigureLobbedMotion(mShotTarget, kLobDuration, kLobApexHeight);
	mShotFiredThisCycle = true;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BASKETBALL, 0.4f);
}

void CatapultZombie::FinishShooting()
{
	if (mPhase != Phase::SHOOTING) return;
	if (!mShotFiredThisCycle) {
		PlayWalking();
		return;
	}
	mBasketballCount = std::max(0, mBasketballCount - 1);
	ApplyBasketballPresentation();
	if (mBasketballCount <= 0) {
		PlayWalking();
		return;
	}
	mPhase = Phase::RELOADING;
	mPhaseTimer = kReloadSeconds;
	PlayTrack("anim_idle", kIdleClipSpeed, 0.15f);
}

Plant* CatapultZombie::FindBasketballTarget() const
{
	if (!mBoard) return nullptr;
	for (int col = 0; col < mBoard->mColumns; ++col) {
		Plant* plant = mBoard->GetCatapultTargetPlantAt(mRow, col);
		if (!plant) continue;
		if (plant->mPlantType == PlantType::PLANT_SPIKEWEED
			|| plant->mPlantType == PlantType::PLANT_SPIKEROCK) {
			continue;
		}
		float targetX = plant->GetPosition().x;
		if (ColliderComponent* collider = plant->GetColliderComponent()) {
			const SDL_FRect bounds = collider->GetBoundingBox();
			targetX = bounds.x + bounds.w * 0.5f;
		}
		if (GetPosition().x >= targetX + kMinimumTargetLead) return plant;
	}
	return nullptr;
}

bool CatapultZombie::CanCrushPlant(const Plant* plant) const
{
	if (!plant || plant->IsSquished() || plant->mRow != mRow) return false;
	switch (plant->mPlantType) {
	case PlantType::PLANT_CHERRYBOMB:
	case PlantType::PLANT_JALAPENO:
	case PlantType::PLANT_BLOVER:
	case PlantType::PLANT_SQUASH:
		return false;
	case PlantType::PLANT_ICESHROOM:
	case PlantType::PLANT_DOOMSHROOM:
		return plant->GetSleepState();
	case PlantType::PLANT_SPIKEWEED:
	case PlantType::PLANT_SPIKEROCK:
		return false;
	default:
		return true;
	}
}

void CatapultZombie::CrushPlants()
{
	const Vector stableVisualOrigin = GetPosition() + mVisualOffset;
	const SDL_FRect attackRect{
		stableVisualOrigin.x + kAttackFromVisualX,
		stableVisualOrigin.y + kAttackFromVisualY,
		kAttackWidth,
		kAttackHeight,
	};
	for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
		Plant* plant = mBoard->mEntityRegistry.GetPlant(id);
		if (!CanCrushPlant(plant)) continue;
		ColliderComponent* collider = plant->GetColliderComponent();
		if (!collider) continue;
		if (HorizontalOverlap(attackRect, collider->GetBoundingBox())
			>= kRequiredPlantOverlap) {
			plant->Squish();
		}
	}
}

int CatapultZombie::GetDamageStage() const
{
	if (mBodyHealth <= mBodyMaxHealth / 3) return 2;
	if (mBodyHealth <= static_cast<int64_t>(mBodyMaxHealth) * 2 / 3) return 1;
	return 0;
}

void CatapultZombie::ApplyBasketballPresentation() const
{
	if (!mAnimator) return;
	mAnimator->SetTrackVisible("Zombie_catapult_basketball", mBasketballCount >= 5);
	mAnimator->SetTrackVisible("Zombie_catapult_basketball2", mBasketballCount >= 4);
	mAnimator->SetTrackVisible("Zombie_catapult_basketball3", mBasketballCount >= 3);
	mAnimator->SetTrackVisible("Zombie_catapult_basketball4", mBasketballCount >= 2);

	ResourceManager& resources = ResourceManager::GetInstance();
	const bool damagedPole = GetDamageStage() >= 2;
	const bool hasBall = mBasketballCount > 0;
	const std::string& poleKey = GetCatapultPoleTextureKey(damagedPole, hasBall);
	mAnimator->SetTrackImage("Zombie_catapult_pole", resources.GetTexture(poleKey));
}

void CatapultZombie::ApplyDamageVisuals() const
{
	if (!mAnimator) return;
	ResourceManager& resources = ResourceManager::GetInstance();
	const std::string& sidingKey = GetCatapultSidingTextureKey(GetDamageStage() >= 1);
	mAnimator->SetTrackImage("Zombie_catapult_siding", resources.GetTexture(sidingKey));
	ApplyBasketballPresentation();
}

const std::string& CatapultZombie::GetCatapultSidingTextureKey(bool damaged) const
{
	return damaged
		? ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_SIDING_DAMAGE
		: ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_SIDING;
}

const std::string& CatapultZombie::GetCatapultPoleTextureKey(
	bool damaged, bool hasBall) const
{
	if (damaged) {
		return hasBall
			? ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_POLE_DAMAGE_WITHBALL
			: ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_POLE_DAMAGE;
	}
	return hasBall
		? ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_POLE_WITHBALL
		: ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_POLE;
}

const char* CatapultZombie::GetCatapultExplosionEffectName() const
{
	return "CatapultExplosion";
}

void CatapultZombie::TakeBodyDamage(int damage)
{
	if (damage <= 0 || mIsDead || mPhase == Phase::CALTROP_DYING) return;
	mBodyHealth = std::max(0, mBodyHealth - damage);
	ApplyDamageVisuals();
	if (mBodyHealth <= 0) Die();
}

bool CatapultZombie::HandleCaltropHit(Caltrop& caltrop)
{
	if (mIsDead || mPhase == Phase::CALTROP_DYING) return true;
	caltrop.Die();
	mPhase = Phase::CALTROP_DYING;
	mPhaseTimer = kCaltropDeathDelaySeconds;
	mBodyHealth = 0;
	mDamageShakeOffset = Vector::zero();
	ApplyDamageVisuals();
	if (mCollider) mCollider->mEnabled = false;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BALLOON_POP, 0.5f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZamboniTire",
			GetPosition() + mVisualOffset + Vector(kTireFromVisualX, kTireFromVisualY));
	}
	PlayTrackOnce("anim_bounce", "", kBounceClipSpeed, 0.1f);
	return true;
}

void CatapultZombie::Die()
{
	if (mIsDead) return;
	if (!mSuppressDeathEffects && !IsRetiringForTemporalReplacement()
		&& !mIsPreview && !mDeathEffectsEmitted) {
		mDeathEffectsEmitted = true;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect(GetCatapultExplosionEffectName(),
				GetPosition() + mVisualOffset
					+ Vector(kDeathEffectFromVisualX, kDeathEffectFromVisualY));
		}
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_EXPLOSION, 0.55f);
	}
	Zombie::Die();
}

void CatapultZombie::Charred()
{
	if (mIsDead || !mBoard) return;
	GameObjectManager::GetInstance().CreateGameObjectImmediate<CatapultCharred>(
		LAYER_GAME_ZOMBIE, ObjectType::OBJECT_ZOMBIE, mBoard,
		GetPosition() + mVisualOffset + Vector(kCharredFromVisualX, kCharredFromVisualY),
		AnimationType::ANIM_CATAPULT_CHARRED, ColliderType::BOX,
		Vector::zero(), Vector::zero(), 1.0f, "CatapultCharred", false, mRow);
	mSuppressDeathEffects = true;
	Die();
}

void CatapultZombie::StartEat(ColliderComponent* /*other*/)
{
	// 投篮车不进入啃食态；车辆接触由 CrushPlants 统一走 Plant::Squish()。
}

void CatapultZombie::ZombieItemUpdate() const
{
	ApplyDamageVisuals();
	if (mPhase == Phase::CALTROP_DYING && mCollider) {
		mCollider->mEnabled = false;
	}
}

Vector CatapultZombie::GetVisualPosition() const
{
	return Zombie::GetVisualPosition() + mDamageShakeOffset;
}

Vector CatapultZombie::GetIceTrapBottomAnchor() const
{
	// 冰晶属于车体控制状态，不跟随低血量受击抖动。
	return GetPosition() + mVisualOffset
		+ Vector(kIceTrapBottomFromVisualX, kIceTrapBottomFromVisualY);
}

void CatapultZombie::PlayWalking()
{
	mPhase = Phase::WALKING;
	mPhaseTimer = 0.0f;
	mShotFiredThisCycle = false;
	PlayTrack("anim_walk", kWalkClipSpeed, 0.15f);
}

void CatapultZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["phaseTimer"] = mPhaseTimer;
	j["basketballCount"] = mBasketballCount;
	j["driveSpeed"] = mDriveSpeed;
	j["shotTargetX"] = mShotTarget.x;
	j["shotTargetY"] = mShotTarget.y;
	j["shotFiredThisCycle"] = mShotFiredThisCycle;
	j["selfBrokenTimer"] = mSelfBrokenTimer;
	j["smokeTimer"] = mSmokeTimer;
}

void CatapultZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::CALTROP_DYING));
	mPhase = static_cast<Phase>(phase);
	mPhaseTimer = std::clamp(
		j.value("phaseTimer", 0.0f), 0.0f, kReloadSeconds);
	mBasketballCount = std::clamp(
		j.value("basketballCount", kInitialBasketballs), 0, kInitialBasketballs);
	mDriveSpeed = std::clamp(
		j.value("driveSpeed", 30.0f), kDriveSpeedMin, kDriveSpeedMax);
	mShotTarget = Vector(
		j.value("shotTargetX", GetPosition().x - 300.0f),
		j.value("shotTargetY", GetPosition().y));
	mShotFiredThisCycle = j.value(
		"shotFiredThisCycle", GetCurrentFrame() >= static_cast<float>(kShootFrame));
	mSelfBrokenTimer = std::clamp(
		j.value("selfBrokenTimer", 0.0f), 0.0f, kCriticalBrokenTime);
	mSmokeTimer = std::clamp(
		j.value("smokeTimer", 0.0f), 0.0f, kSmokeInterval);
	mDamageShakeOffset = Vector::zero();
	ApplyDamageVisuals();
	if (mPhase == Phase::CALTROP_DYING && mCollider) {
		mCollider->mEnabled = false;
	}
}
