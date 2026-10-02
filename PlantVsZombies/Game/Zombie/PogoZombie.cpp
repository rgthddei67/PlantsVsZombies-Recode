#include "PogoZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kPogoBounceDuration = 80.0f / 60.0f; // 原版每次弹跳 80 tick，折算为游戏秒
	constexpr float kPogoWalkSpeed = 27.0f;              // 持杆普通弹跳水平速度，单位：px/s
	constexpr float kGroundRootMotionRate = 12.0f;       // 弃杆步行时根轨每帧位移换算率
	constexpr float kPogoClipSpeed = 40.0f / 12.0f;      // 原版 anim_pogo 40 FPS 相对资源 12 FPS 的倍率
	constexpr float kPogoDescentTimeMultiplier = 2.0f;   // 过顶点后按真实游戏时间加速下落，不受寒冰减速拖慢
	constexpr float kLandingAnimRemaining = 8.0f / 60.0f; // 落地前重播跳杆动画的剩余秒数
	constexpr float kLandingSoundRemaining = 5.0f / 60.0f; // 落地音效节点的剩余秒数
	constexpr float kJumpBlockProgress = 8.0f / 80.0f;   // C# 前跳剩余 71～73 tick 的阻拦检查进度
	constexpr float kBounceDeflection = 9.0f;             // 原版弹跳曲线最低视觉偏移，单位：px
	constexpr float kNormalBounceHeight = 40.0f;          // 普通弹跳曲线高度，单位：px
	constexpr float kHighBounceHeight = 50.0f;            // 原地高跳曲线高度，单位：px
	constexpr float kForwardBounceHeight = 90.0f;         // 越障前跳曲线高度，单位：px
	constexpr float kLandingGap = 5.0f;                   // 越障落地后碰撞框与植物保留的间距，单位：px
	constexpr float kContactRetentionGap = 10.0f;         // 落地查询允许的植物碰撞框水平间隙，单位：px
	constexpr float kLimbVolume = 0.35f;                  // 跳跳断肢与掉头音量
	constexpr float kMagnetDestinationX = 44.0f;          // 跳跳杆吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 10.0f;          // 跳跳杆吸附到磁力菇头部附近的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f;     // 离体装备落点随机扰动，单位 px
}

void PogoZombie::SetupZombie()
{
	mBodyMaxHealth = 500;
	mBodyHealth = 500;
	mSpeed = kGroundRootMotionRate;
	mNeedDropArm = true;
	mNeedDropHead = true;

	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(4.0f, 35.0f));
	}

	PlayTrack("anim_pogo", kPogoClipSpeed);
	const auto [begin, end] = mAnimator->GetTrackRange("anim_pogo");
	if (end >= begin) mAnimator->SetCurrentFrame(static_cast<float>(end));
	mAnimator->Pause();
	if (mIsPreview) {
		// 预览大图不走 Zombie 的实战更新路径，但仍应像原版一样原地弹跳。
		BeginBounce(Phase::BOUNCING);
		return;
	}

	RegisterFrameEvents();
	mBounceRemaining = static_cast<float>(GameRandom::Range(1, 80)) / 60.0f;
	// 原版以随机相位出生；若已经越过节点，首个残缺弹跳不会补播事件。
	mLandingAnimationStarted = mBounceRemaining <= kLandingAnimRemaining;
	mLandingSoundPlayed = mBounceRemaining <= kLandingSoundRemaining;
	UpdateBounceAltitude();
}

/** 让选卡与图鉴大图继续弹跳；图鉴网格缩略图仍遵守场景的显式 PauseAnimation。 */
void PogoZombie::Update()
{
	Zombie::Update();
	if (mIsPreview && !mIsUI) UpdatePreviewBounce();
}

/** 注册主人确认的两次啃食命中帧和死亡终点；帧号不做减一换算。 */
void PogoZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(86, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(107, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(154, [this]() { Die(); });
}

void PogoZombie::PlaySpawnSound()
{
	// 原版没有独立出生声；PogoZombie Foley 在每次弹跳落地前播放。
}

float PogoZombie::GetBounceProgress() const
{
	if (!mHasPogo || mPhase == Phase::WALKING) return 0.0f;
	return std::clamp(1.0f - mBounceRemaining / kPogoBounceDuration, 0.0f, 1.0f);
}

void PogoZombie::SetBounceRemainingForTesting(float seconds)
{
	if (!mHasPogo) return;
	mBounceRemaining = std::clamp(seconds, 0.0f, kPogoBounceDuration);
	mLandingAnimationStarted = mBounceRemaining <= kLandingAnimRemaining;
	mLandingSoundPlayed = mBounceRemaining <= kLandingSoundRemaining;
	UpdateBounceAltitude();
}

void PogoZombie::StartEat(ColliderComponent* other)
{
	if (!other || mIsDying) return;
	if (!mHasPogo) {
		Zombie::StartEat(other);
		return;
	}

	auto* plant = dynamic_cast<Plant*>(other->GetGameObject());
	if (!plant || !plant->CanBeEaten() || plant->mRow != mRow) return;
	if (mBoard) {
		if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
			plant = top;
		}
	}
	if (!plant->CanBeEaten()) return;
	mContactPlantID = plant->mPlantID;
}

void PogoZombie::BeginBounce(Phase phase)
{
	mPhase = phase;
	mBounceRemaining = kPogoBounceDuration;
	mJumpBlockChecked = false;
	mLandingAnimationStarted = false;
	mLandingSoundPlayed = false;
	mForwardDistanceTotal = 0.0f;
	mForwardDistanceApplied = 0.0f;
	if (phase != Phase::FORWARD_BOUNCE) {
		mForwardTargetPlantID = NULL_PLANT_ID;
	}
	UpdateBounceAltitude();
}

void PogoZombie::BeginForwardBounce(Plant& plant)
{
	BeginBounce(Phase::FORWARD_BOUNCE);
	mForwardTargetPlantID = plant.mPlantID;
	mContactPlantID = NULL_PLANT_ID;

	const ColliderComponent* plantCollider = plant.GetColliderComponent();
	const ColliderComponent* zombieCollider = GetColliderComponent();
	if (!plantCollider || !zombieCollider) return;

	const SDL_FRect plantBounds = plantCollider->GetBoundingBox();
	const SDL_FRect zombieBounds = zombieCollider->GetBoundingBox();
	const float currentX = GetPosition().x;
	if (IsMovingRight()) {
		const float relativeLeft = zombieBounds.x - currentX;
		const float landingX = plantBounds.x + plantBounds.w + kLandingGap - relativeLeft;
		mForwardDistanceTotal = std::max(0.0f, landingX - currentX);
	}
	else {
		const float relativeRight = zombieBounds.x + zombieBounds.w - currentX;
		const float landingX = plantBounds.x - kLandingGap - relativeRight;
		mForwardDistanceTotal = std::max(0.0f, currentX - landingX);
	}
}

Plant* PogoZombie::ResolveContactPlant() const
{
	if (!mBoard || mContactPlantID == NULL_PLANT_ID) return nullptr;
	Plant* plant = mBoard->mEntityRegistry.GetPlant(mContactPlantID);
	if (plant) {
		if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
			plant = top;
		}
	}
	if (!plant || !plant->IsActive() || !plant->CanBeEaten() || plant->mRow != mRow) {
		return nullptr;
	}

	const ColliderComponent* plantCollider = plant->GetColliderComponent();
	const ColliderComponent* zombieCollider = GetColliderComponent();
	if (!plantCollider || !zombieCollider) return nullptr;
	const SDL_FRect a = plantCollider->GetBoundingBox();
	const SDL_FRect b = zombieCollider->GetBoundingBox();
	float gap = 0.0f;
	if (a.x > b.x + b.w) gap = a.x - (b.x + b.w);
	else if (b.x > a.x + a.w) gap = b.x - (a.x + a.w);
	return gap <= kContactRetentionGap ? plant : nullptr;
}

Plant* PogoZombie::ResolveForwardTarget() const
{
	if (!mBoard || mForwardTargetPlantID == NULL_PLANT_ID) return nullptr;
	Plant* plant = mBoard->mEntityRegistry.GetPlant(mForwardTargetPlantID);
	if (plant) {
		if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
			plant = top;
		}
	}
	return plant && plant->IsActive() && plant->mRow == mRow ? plant : nullptr;
}

void PogoZombie::ResolveBounceLanding()
{
	Plant* plant = ResolveContactPlant();
	if (!plant) {
		mContactPlantID = NULL_PLANT_ID;
		BeginBounce(Phase::BOUNCING);
		return;
	}
	if (mPhase == Phase::HIGH_BOUNCE) {
		BeginForwardBounce(*plant);
		return;
	}
	mForwardTargetPlantID = NULL_PLANT_ID;
	BeginBounce(Phase::HIGH_BOUNCE);
}

void PogoZombie::RestartLandingAnimation()
{
	if (!mAnimator) return;
	PlayTrack("anim_pogo", kPogoClipSpeed, 0.0f);
	mAnimator->Play(PlayState::PLAY_ONCE);
}

void PogoZombie::UpdateBounceAltitude()
{
	if (!mHasPogo) {
		mAltitude = 0.0f;
		return;
	}
	float height = kNormalBounceHeight;
	if (mPhase == Phase::HIGH_BOUNCE) height = kHighBounceHeight;
	else if (mPhase == Phase::FORWARD_BOUNCE) height = kForwardBounceHeight;

	const float progress = GetBounceProgress();
	const float triangle = 1.0f - std::abs(1.0f - 2.0f * progress);
	float heightProgress = 0.0f;
	if (progress <= 0.5f) {
		// 上升仍沿用原版慢中段曲线，保留踩杆蓄力后升空的重量感。
		heightProgress = 2.0f * triangle - triangle * triangle;
	}
	else {
		// 下落改用 smoothstep：同样在顶点和落地点连续，但中后段明显低于原版曲线。
		heightProgress = triangle * triangle * (3.0f - 2.0f * triangle);
	}
	mAltitude = kBounceDeflection + height * heightProgress;
}

/** 推进纯展示弹跳，不查询植物、不水平移动，也不播放落地音效。 */
void PogoZombie::UpdatePreviewBounce()
{
	if (!mHasPogo || mPhase == Phase::WALKING) return;
	const bool isDescending = GetBounceProgress() > 0.5f;
	const float phaseDelta = DeltaTime::GetDeltaTime()
		* (isDescending ? kPogoDescentTimeMultiplier : 1.0f);
	mBounceRemaining = std::max(0.0f, mBounceRemaining - phaseDelta);

	if (!mLandingAnimationStarted && mBounceRemaining <= kLandingAnimRemaining) {
		mLandingAnimationStarted = true;
		RestartLandingAnimation();
	}
	UpdateBounceAltitude();
	if (mBounceRemaining <= 0.0f) BeginBounce(Phase::BOUNCING);
}

void PogoZombie::ZombieUpdate(float scaledTime)
{
	if (!mHasPogo || mPhase == Phase::WALKING || scaledTime <= 0.0f) return;
	const bool isDescending = GetBounceProgress() > 0.5f;
	// 弹跳是已经释放的空中运动：整段按真实游戏时间推进，不让寒冰后的 scaledTime
	// 和 Animator extra speed 重复延长上升与浮空；只有下落额外加速。
	const float phaseDelta = DeltaTime::GetDeltaTime()
		* (isDescending ? kPogoDescentTimeMultiplier : 1.0f);
	mBounceRemaining = std::max(0.0f, mBounceRemaining - phaseDelta);

	if (mPhase == Phase::FORWARD_BOUNCE
		&& !mJumpBlockChecked && GetBounceProgress() >= kJumpBlockProgress) {
		mJumpBlockChecked = true;
		Plant* plant = ResolveForwardTarget();
		if (plant && mBoard) {
			plant = mBoard->GetJumpBlockingPlantAt(
				plant->mRow, plant->mColumn, ZombieJumpType::POGO);
		}
		if (plant) {
			plant->OnZombieJumpBlocked(ZombieJumpType::POGO);
			if (HandlePogoJumpBlocked(*plant)) return;
		}
	}

	if (!mLandingAnimationStarted && mBounceRemaining <= kLandingAnimRemaining) {
		mLandingAnimationStarted = true;
		RestartLandingAnimation();
	}
	if (!mLandingSoundPlayed && mBounceRemaining <= kLandingSoundRemaining) {
		mLandingSoundPlayed = true;
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_POGO_ZOMBIE, 0.45f);
	}
	UpdateBounceAltitude();
	if (mBounceRemaining <= 0.0f) ResolveBounceLanding();
}

void PogoZombie::MovePogoDistance(float distance, Transform* transform)
{
	if (!transform || distance <= 0.0f) return;
	transform->Translate(IsMovingRight() ? distance : -distance, 0.0f);
}

void PogoZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!mHasPogo || mPhase == Phase::WALKING) {
		Zombie::ZombieMove(scaledDelta, transform);
		return;
	}
	const float multiplier = mAnimator
		? mAnimator->GetExtraSpeedMultiplier() : 1.0f;
	if (mPhase == Phase::BOUNCING) {
		float boardMultiplier = 1.0f;
		if (mBoard) {
			boardMultiplier = AmplifySpeedMultiplierForGoldenIce(
				mBoard->GetZombieWindMoveMultiplier(IsMovingRight()));
		}
		MovePogoDistance(kPogoWalkSpeed * multiplier * boardMultiplier * scaledDelta,
			transform);
		return;
	}
	if (mPhase == Phase::FORWARD_BOUNCE && mForwardDistanceTotal > 0.0f) {
		const bool isDescending = GetBounceProgress() > 0.5f;
		// 与空中计时共用同一时间基准；否则寒冰中会先落地、再因位移不足停在植物上方。
		const float movementDelta = DeltaTime::GetDeltaTime()
			* (isDescending ? kPogoDescentTimeMultiplier : 1.0f);
		const float distance = mForwardDistanceTotal / kPogoBounceDuration
			* movementDelta;
		const float remaining = std::max(0.0f,
			mForwardDistanceTotal - mForwardDistanceApplied);
		const float applied = std::min(distance, remaining);
		MovePogoDistance(applied, transform);
		mForwardDistanceApplied += applied;
	}
}

void PogoZombie::BreakPogo(bool emitParticle)
{
	if (!mHasPogo) return;
	// 粒子配方以僵尸逻辑原点为基准；只补当前弹跳高度，不能重复叠加 mVisualOffset。
	const Vector particlePosition = GetPosition() + Vector(0.0f, -mAltitude);
	mHasPogo = false;
	mPhase = Phase::WALKING;
	mAltitude = 0.0f;
	mBounceRemaining = 0.0f;
	mContactPlantID = NULL_PLANT_ID;
	mForwardTargetPlantID = NULL_PLANT_ID;
	mForwardDistanceTotal = 0.0f;
	mForwardDistanceApplied = 0.0f;
	mJumpBlockChecked = false;
	mSpeed = kGroundRootMotionRate;
	// 持杆时寒冰不减慢弹跳动画；弃杆后立即恢复普通 0.6x 动画倍率。
	UpdateAnimSpeed();
	PlayWalkAnimation(0.0f);
	if (emitParticle && g_particleSystem) {
		g_particleSystem->EmitEffect(GetPogoBreakEffectName(), particlePosition);
	}
}

bool PogoZombie::HandlePogoJumpBlocked(Plant& plant)
{
	ColliderComponent* collider = plant.GetColliderComponent();
	BreakPogo();
	if (collider) StartEat(collider);
	return true;
}

const std::string& PogoZombie::GetDamagedOuterArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_OUTERARM_UPPER2;
}

const std::string& PogoZombie::GetDamagedStickTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICKDAMAGE2;
}

const std::string& PogoZombie::GetDamagedStick2TextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICK2DAMAGE2;
}

bool PogoZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!mHasPogo) return false;
	item.textureKey = mHasArm
		? ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICK
		: ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICKDAMAGE2;
	item.worldPosition = GetTrackWorldPosition("Zombie_pogo_stick");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = 0.8f;
	BreakPogo(false);
	return true;
}

void PogoZombie::PlayWalkAnimation(float blendTime)
{
	if (mHasPogo) return;
	PlayTrack("anim_walk", 0.0f, blendTime);
}

void PogoZombie::ApplyArmDamagePresentation() const
{
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	auto& resources = ResourceManager::GetInstance();
	mAnimator->SetTrackImage("Zombie_outerarm_upper", resources.GetTexture(
		GetDamagedOuterArmTextureKey()));
	mAnimator->SetTrackImage("Zombie_pogo_stickhands", resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICKHANDS2));
	mAnimator->SetTrackImage("Zombie_pogo_stick", resources.GetTexture(
		GetDamagedStickTextureKey()));
	mAnimator->SetTrackImage("Zombie_pogo_stick2", resources.GetTexture(
		GetDamagedStick2TextureKey()));
}

void PogoZombie::ArmDrop()
{
	if (!mHasArm) return;
	ApplyArmDamagePresentation();
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PogoArmOff",
			GetPosition() + Vector(0.0f, -mAltitude));
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kLimbVolume);
}

void PogoZombie::HeadDrop()
{
	if (!mHasHead) return;
	// 先记录空中逻辑锚点；BreakPogo 会把高度归零，但掉头粒子应从原头部位置抛出。
	const Vector headPosition = GetPosition() + Vector(0.0f, -mAltitude);
	BreakPogo();
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_head_glasses", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	if (g_particleSystem) g_particleSystem->EmitEffect("ZombiePogoHeadOff", headPosition);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, kLimbVolume);
}

void PogoZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasArm) ApplyArmDamagePresentation();
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_head_glasses", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
}

void PogoZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (!mAnimator) return;
	if (mHasArm) {
		mAnimator->SetTrackImage("Zombie_pogo_stickhands", nullptr);
		mAnimator->SetTrackImage("Zombie_pogo_stick", nullptr);
		mAnimator->SetTrackImage("Zombie_pogo_stick2", nullptr);
	}
	if (mHasHead) mAnimator->SetTrackVisible("anim_head_glasses", true);
}

Vector PogoZombie::GetVisualPosition() const
{
	return Zombie::GetVisualPosition() + Vector(0.0f, -mAltitude);
}

bool PogoZombie::TakePlantInstantKill()
{
	if (mHasPogo) return false;
	return Zombie::TakePlantInstantKill();
}

int PogoZombie::AdjustRejectedChomperBiteDamage(int damage) const
{
	return mHasPogo ? 0 : damage;
}

void PogoZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["bounceRemaining"] = mBounceRemaining;
	j["altitude"] = mAltitude;
	j["hasPogo"] = mHasPogo;
	j["contactPlantID"] = mContactPlantID;
	j["forwardTargetPlantID"] = mForwardTargetPlantID;
	j["jumpBlockChecked"] = mJumpBlockChecked;
	j["landingAnimationStarted"] = mLandingAnimationStarted;
	j["landingSoundPlayed"] = mLandingSoundPlayed;
	j["forwardDistanceTotal"] = mForwardDistanceTotal;
	j["forwardDistanceApplied"] = mForwardDistanceApplied;
	j["pogoAnimationPlaying"] = mAnimator && mAnimator->IsPlaying();
}

void PogoZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::WALKING));
	mPhase = static_cast<Phase>(phase);
	mBounceRemaining = std::clamp(j.value("bounceRemaining", 0.0f),
		0.0f, kPogoBounceDuration);
	mAltitude = std::clamp(j.value("altitude", 0.0f), 0.0f,
		kForwardBounceHeight + kBounceDeflection);
	mHasPogo = j.value("hasPogo", mPhase != Phase::WALKING);
	if (mHasPogo && mPhase == Phase::WALKING) mPhase = Phase::BOUNCING;
	mContactPlantID = j.value("contactPlantID", NULL_PLANT_ID);
	mForwardTargetPlantID = j.value("forwardTargetPlantID", NULL_PLANT_ID);
	mJumpBlockChecked = j.value("jumpBlockChecked", false);
	mLandingAnimationStarted = j.value("landingAnimationStarted", false);
	mLandingSoundPlayed = j.value("landingSoundPlayed", false);
	mForwardDistanceTotal = std::max(0.0f, j.value("forwardDistanceTotal", 0.0f));
	mForwardDistanceApplied = std::clamp(j.value("forwardDistanceApplied", 0.0f),
		0.0f, mForwardDistanceTotal);
	mSpeed = kGroundRootMotionRate;

	if (!mHasPogo) {
		mPhase = Phase::WALKING;
		mAltitude = 0.0f;
		UpdateAnimSpeed();
		return;
	}
	// 基类读档时尚未恢复 hasPogo，需按最终持杆状态重算寒冰动画倍率。
	UpdateAnimSpeed();
	const auto [pogoBegin, pogoEnd] = mAnimator->GetTrackRange("anim_pogo");
	const float savedFrame = std::clamp(mAnimator->GetCurrentFrame(),
		static_cast<float>(pogoBegin), static_cast<float>(pogoEnd));
	PlayTrack("anim_pogo", kPogoClipSpeed, 0.0f);
	mAnimator->SetCurrentFrame(savedFrame);
	if (j.value("pogoAnimationPlaying", false)) {
		mAnimator->Play(PlayState::PLAY_ONCE);
	}
	else {
		mAnimator->Pause();
	}
	UpdateBounceAltitude();
}

ZombieMovementRules::BirthProfile PogoZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.linear=true;
	p.velocityMinimum=p.velocityMaximum=kPogoWalkSpeed;
	p.phaseDependent=true;
	return p;
}

float PogoZombie::GetMineSimulationMoveSpeed() const
{
	return mHasPogo ? ScaleSimulationMoveSpeed(kPogoWalkSpeed) : Zombie::GetMineSimulationMoveSpeed();
}
