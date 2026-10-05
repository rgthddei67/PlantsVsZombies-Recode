#include "DolphinRiderZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
	constexpr float kGroundRootMotionRate = 12.0f;   // 根轨每帧位移换算为每秒位移的资源帧率，禁止拿它直接放大移速
	constexpr float kFastGroundClipSpeed = 2.25f;    // 原版 0.9 px/tick、47 tick/s 对齐为约 27 FPS 和 42 px/s
	constexpr float kSlowGroundClipSpeed = 0.75f;    // 原版弃豚平均 0.3 px/tick 对齐为约 9 FPS 和 14 px/s
	constexpr float kRideWorldSpeed = 54.0f;         // anim_ride 没有根运动，按原版 0.9 px/tick 手动推进
	constexpr float kJumpWorldSpeed = 30.0f;         // 原版跳跃前半段 mVelX=0.5 px/tick
	constexpr float kEntryClipSpeed = 16.0f / 12.0f; // 原版入水 16 FPS 相对资源 12 FPS 的倍率
	constexpr float kJumpClipSpeed = 10.0f / 12.0f;  // 原版跃豚 10 FPS 相对资源 12 FPS 的倍率
	constexpr float kEntrySplashProgress = 0.56f;    // C# 入水水花节点
	constexpr float kJumpBlockProgress = 0.30f;      // C# 高坚果阻拦检查节点
	constexpr float kJumpSplashProgress = 0.49f;     // C# 海豚重新入水节点
	constexpr float kEntrySplashOffsetX = -46.0f;    // C# 抛豚入水水滴相对逻辑 X 的局部偏移，单位 px
	constexpr float kJumpSplashOffsetX = -26.0f;     // C# 越障落水水滴相对逻辑 X 的局部偏移，单位 px
	constexpr float kEntryShoreLead = 20.0f;         // 原版在池岸外 0～20 px 启动抛豚入水演出
	constexpr float kEntryWorldShift = 70.0f;        // 入水换轨时提交的相对根位移，单位 px
	constexpr float kDismountJumpWorldShift = 104.0f; // 跳跃末帧切 anim_swim 时抵消身体锚点差，单位 px
	constexpr float kRetainedJumpWorldShift = 106.0f; // 精英首跳切回 anim_ride 时抵消身体锚点差，单位 px
	constexpr float kRideVisualLift = 45.0f;         // 骑乘资源基准较低，抬升到与弃豚游泳一致的水面线
	constexpr float kJumpLandingExtraLift = 15.0f;   // 跳跃末帧与 anim_swim 首帧的剩余垂直基准差
	constexpr float kJumpAltitude = 10.0f;           // 跳跃中用于平滑轨道衔接的最大视觉高度，单位 px
	constexpr float kBlockedLandingGap = 5.0f;       // 阻拦后碰撞箱与植物右边缘保留的间距，单位 px
	constexpr float kPi = 3.14159265f;               // 跳跃视觉高度正弦插值使用的圆周率
	constexpr int kNormalDolphinJumpCapacity = 1;    // 普通海豚成功越过一株植物后立即弃豚
	constexpr float kEntryFirstClipEndProgress = 0.65f;    // C# 入水首段裁剪结束进度
	constexpr float kEntrySecondClipBeginProgress = 0.75f; // C# 入水末段裁剪开始进度
	constexpr float kEntryFirstClipBottomOffsetY = 126.0f; // C# 首段裁剪底线相对逻辑 Y 的偏移，单位 px
	constexpr float kEntrySecondClipBottomOffsetY = 136.0f; // C# 末段裁剪底线相对逻辑 Y 的偏移，单位 px

	float SmoothStep(float value)
	{
		const float t = std::clamp(value, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
}

bool DolphinRiderZombie::HasDolphin() const
{
	return mPhase == Phase::APPROACHING
		|| mPhase == Phase::ENTERING_POOL
		|| mPhase == Phase::RIDING
		|| mPhase == Phase::JUMPING;
}

int DolphinRiderZombie::GetDolphinJumpCapacity() const
{
	return kNormalDolphinJumpCapacity;
}

float DolphinRiderZombie::GetEntryProgress() const
{
	if (mPhase != Phase::ENTERING_POOL || !mAnimator) return 0.0f;
	const auto [begin, end] = mAnimator->GetTrackRange("anim_jumpinpool");
	if (end <= begin) return 0.0f;
	return std::clamp((GetCurrentFrame() - static_cast<float>(begin))
		/ static_cast<float>(end - begin), 0.0f, 1.0f);
}

float DolphinRiderZombie::GetJumpProgress() const
{
	if (mPhase != Phase::JUMPING || !mAnimator) return 0.0f;
	const auto [begin, end] = mAnimator->GetTrackRange("anim_dolphinjump");
	if (end <= begin) return 0.0f;
	return std::clamp((GetCurrentFrame() - static_cast<float>(begin))
		/ static_cast<float>(end - begin), 0.0f, 1.0f);
}

Vector DolphinRiderZombie::GetDolphinVisualCompensation() const
{
	if (mPhase == Phase::ENTERING_POOL && mAnimator) {
		const float progress = GetEntryProgress();
		const float liftProgress = SmoothStep(
			(progress - kEntrySplashProgress) / (1.0f - kEntrySplashProgress));
		return Vector(0.0f, -kRideVisualLift * liftProgress);
	}
	if (mPhase == Phase::RIDING) return Vector(kEntryWorldShift, -kRideVisualLift);
	if (mPhase != Phase::JUMPING) return Vector::zero();

	const float progress = GetJumpProgress();
	const float altitude = kJumpAltitude * std::sin(progress * kPi);
	const float landingProgress = SmoothStep(
		(progress - kJumpSplashProgress) / (1.0f - kJumpSplashProgress));
	return Vector(kEntryWorldShift + altitude,
		-kRideVisualLift - altitude - kJumpLandingExtraLift * landingProgress);
}

void DolphinRiderZombie::SetupZombie()
{
	mBodyMaxHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_DOLPHIN_RIDER).body;
	mBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_DOLPHIN_RIDER).body;
	mSpeed = kGroundRootMotionRate;
	mNeedDropArm = false;
	SetAnimationSpeed(1.0f);

	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(8.0f, 42.0f));
	}
	if (mIsPreview) {
		PlayTrack("anim_idle");
		return;
	}

	RegisterFrameEvents();
	if (mCollider) {
		mBaseColliderOffsetX = mCollider->offset.x;
	}
	PlayTrack("anim_walkdolphin", kFastGroundClipSpeed);
	ApplyPhasePresentation();
}

/** 注册主人按当前资源确认的两次啃食命中与死亡终点。 */
void DolphinRiderZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(182, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(201, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(288, [this]() { Die(); });
}

void DolphinRiderZombie::PlaySpawnSound()
{
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DOLPHIN_APPEARS, 0.45f);
}

void DolphinRiderZombie::StartEat(ColliderComponent* other)
{
	if (!other || mIsDying) return;
	auto* plant = dynamic_cast<Plant*>(other->GetGameObject());
	if (mPhase == Phase::RIDING) {
		if (plant && plant->mRow == mRow && plant->CanBeEaten()) {
			BeginJump(plant);
		}
		return;
	}
	if (mPhase != Phase::SWIMMING && mPhase != Phase::WALKING_WITHOUT_DOLPHIN) return;
	Zombie::StartEat(other);
}

void DolphinRiderZombie::BeginEnteringPool()
{
	if (mPhase != Phase::APPROACHING) return;
	mPhase = Phase::ENTERING_POOL;
	mEntrySplashPlayed = false;
	PlayTrackOnce("anim_jumpinpool", "anim_ride", kEntryClipSpeed, 0.1f, 1.0f);
	ApplyPhasePresentation();
}

void DolphinRiderZombie::FinishEnteringPool()
{
	if (mPhase != Phase::ENTERING_POOL) return;
	if (auto* transform = GetTransform()) {
		transform->Translate(mIsMindControlled ? kEntryWorldShift : -kEntryWorldShift, 0.0f);
	}
	mPhase = Phase::RIDING;
	mSpeed = kGroundRootMotionRate;
	// 演出从岸上开始，根位移提交后才正式切入水中介质并隐藏陆地阴影。
	UpdatePoolState(false);
	ApplyPhasePresentation();
}

void DolphinRiderZombie::BeginJump(Plant* target)
{
	if (mPhase != Phase::RIDING || !target) return;
	mPhase = Phase::JUMPING;
	mJumpTargetPlantID = target->mPlantID;
	mJumpSplashPlayed = false;
	mJumpBlockChecked = false;
	mJumpRetainsDolphinOnLanding =
		mSuccessfulJumpCount + 1 < GetDolphinJumpCapacity();
	// 精英第一次成功越障直接回到骑乘轨，避免先切弃豚游泳再切回海豚造成画面跳变。
	const std::string returnTrack =
		mJumpRetainsDolphinOnLanding ? "anim_ride" : "anim_swim";
	PlayTrackOnce("anim_dolphinjump", returnTrack, kJumpClipSpeed, 0.1f, 1.0f);
	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_DOLPHIN_BEFORE_JUMPING, 0.45f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PLANT_WATER, 0.35f);
	ApplyPhasePresentation();
}

void DolphinRiderZombie::FinishJump(bool blocked, Plant* blockingPlant)
{
	if (mPhase != Phase::JUMPING) return;

	if (blocked && blockingPlant) {
		if (const auto* plantCollider = blockingPlant->GetColliderComponent()) {
			const SDL_FRect bounds = plantCollider->GetBoundingBox();
			Vector position = GetPosition();
			position.x = bounds.x + bounds.w + kBlockedLandingGap - mBaseColliderOffsetX;
			SetPosition(position);
		}
	}
	else if (auto* transform = GetTransform()) {
		const float worldShift = mJumpRetainsDolphinOnLanding
			? kRetainedJumpWorldShift
			: kDismountJumpWorldShift;
		transform->Translate(mIsMindControlled ? worldShift : -worldShift, 0.0f);
	}

	if (!blocked) {
		++mSuccessfulJumpCount;
	}
	const bool retainDolphin = !blocked
		&& mJumpRetainsDolphinOnLanding
		&& mInPool;
	mPhase = retainDolphin
		? Phase::RIDING
		: (mInPool ? Phase::SWIMMING : Phase::WALKING_WITHOUT_DOLPHIN);
	mSpeed = kGroundRootMotionRate;
	mNeedDropArm = !retainDolphin;
	if (retainDolphin) {
		PlayTrack("anim_ride", 1.0f, 0.0f);
	}
	else {
		PlayWalkAnimation(0.0f);
	}
	ApplyPhasePresentation();
	if (!retainDolphin) {
		CheckDeferredArmDrop();
	}

	if (blocked && blockingPlant) {
		if (auto* plantCollider = blockingPlant->GetColliderComponent()) {
			StartEat(plantCollider);
		}
		OnDolphinJumpBlocked(*blockingPlant);
	}
	mJumpTargetPlantID = NULL_PLANT_ID;
	mJumpRetainsDolphinOnLanding = false;
}

void DolphinRiderZombie::ZombieUpdate(float)
{
	if (mPhase == Phase::APPROACHING && mBoard && mBoard->IsPoolRow(mRow)) {
		const float poolRight = CELL_INITALIZE_POS_X
			+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X;
		const float x = GetPosition().x;
		if (x >= poolRight && x <= poolRight + kEntryShoreLead) {
			BeginEnteringPool();
			return;
		}
	}

	if (mPhase == Phase::ENTERING_POOL) {
		const auto [begin, end] = mAnimator->GetTrackRange("anim_jumpinpool");
		const float progress = end > begin
			? std::clamp((GetCurrentFrame() - static_cast<float>(begin))
				/ static_cast<float>(end - begin), 0.0f, 1.0f)
			: 0.0f;
		if (!mEntrySplashPlayed && progress >= kEntrySplashProgress) {
			mEntrySplashPlayed = true;
			ApplyPhasePresentation();
			PlayPoolSplashVisual(GetPosition() + Vector(kEntrySplashOffsetX, 0.0f));
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_ENTERING_WATER, 0.4f);
		}
		if (GetCurrentTrackName() == "anim_ride") FinishEnteringPool();
		return;
	}

	if (mPhase != Phase::JUMPING) return;
	const float progress = GetJumpProgress();
	if (!mJumpBlockChecked && progress >= kJumpBlockProgress) {
		mJumpBlockChecked = true;
		if (mBoard) {
			Plant* plant = mBoard->mEntityRegistry.GetPlant(mJumpTargetPlantID);
			if (plant) {
				plant = mBoard->GetJumpBlockingPlantAt(
					plant->mRow, plant->mColumn, ZombieJumpType::DOLPHIN_RIDER);
			}
			if (plant && plant->mRow == mRow) {
				plant->OnZombieJumpBlocked(ZombieJumpType::DOLPHIN_RIDER);
				FinishJump(true, plant);
				return;
			}
		}
	}
	if (!mJumpSplashPlayed && progress >= kJumpSplashProgress) {
		mJumpSplashPlayed = true;
		PlayPoolSplashVisual(GetPosition() + Vector(kJumpSplashOffsetX, 0.0f));
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_ENTERING_WATER, 0.4f);
	}
	const char* returnTrack =
		mJumpRetainsDolphinOnLanding ? "anim_ride" : "anim_swim";
	if (GetCurrentTrackName() == returnTrack) FinishJump(false);
}

void DolphinRiderZombie::MoveManually(
	float speed, float scaledDelta, Transform* transform)
{
	if (!transform || !mAnimator) return;
	float multiplier = mAnimator->GetExtraSpeedMultiplier();
	if (mBoard) {
		multiplier *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(mIsMindControlled));
	}
	const float distance = speed * multiplier * scaledDelta;
	transform->Translate(mIsMindControlled ? distance : -distance, 0.0f);
}

void DolphinRiderZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase == Phase::ENTERING_POOL) return;
	if (mPhase == Phase::RIDING) {
		MoveManually(kRideWorldSpeed, scaledDelta, transform);
		return;
	}
	if (mPhase == Phase::JUMPING) {
		if (GetJumpProgress() < kJumpSplashProgress) {
			MoveManually(kJumpWorldSpeed, scaledDelta, transform);
		}
		return;
	}
	Zombie::ZombieMove(scaledDelta, transform);
}

void DolphinRiderZombie::PlayWalkAnimation(float blendTime)
{
	if (mPhase == Phase::APPROACHING) {
		if (mInPool) BeginEnteringPool();
		else PlayTrack("anim_walkdolphin", kFastGroundClipSpeed, blendTime);
		return;
	}
	if (mPhase == Phase::ENTERING_POOL || mPhase == Phase::JUMPING) return;
	if (mPhase == Phase::RIDING) {
		if (!mInPool) {
			// 未遇到植物便穿过整段泳池时，按 C# 回到带豚陆地步行，而不是让 anim_ride 滑上岸。
			mPhase = Phase::APPROACHING;
			mSpeed = kGroundRootMotionRate;
			// 阶段切换会同步撤销骑乘视觉补偿；禁用混合，避免旧 anim_ride 姿态在陆地首帧垂挂到脚下。
			PlayTrack("anim_walkdolphin", kFastGroundClipSpeed, 0.0f);
			ApplyPhasePresentation();
			return;
		}
		PlayTrack("anim_ride", 1.0f, blendTime);
		return;
	}
	if (mInPool) {
		mPhase = Phase::SWIMMING;
		PlayTrack("anim_swim", kSlowGroundClipSpeed, blendTime);
	}
	else {
		mPhase = Phase::WALKING_WITHOUT_DOLPHIN;
		PlayTrack("anim_walk", kSlowGroundClipSpeed, blendTime);
	}
}

void DolphinRiderZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	if (!mInPool) {
		// 粒子配方本身包含头部偏移；从逻辑原点发射，避免再次叠加该品种的视觉偏移。
		g_particleSystem->EmitEffect(GetDolphinHeadOffEffectName(), GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, 0.35f);
}

void DolphinRiderZombie::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_dolphinrider_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			GetLostOuterArmTextureKey()));
	if (!mInPool) {
		g_particleSystem->EmitEffect("ZombieArmOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LIMBS_POP, 0.35f);
}

void DolphinRiderZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
	}
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackImage("Zombie_dolphinrider_outerarm_upper",
			ResourceManager::GetInstance().GetTexture(
				GetLostOuterArmTextureKey()));
	}
	ApplyPhasePresentation();
}

Vector DolphinRiderZombie::GetVisualPosition() const
{
	return Zombie::GetVisualPosition() + GetDolphinVisualCompensation();
}

bool DolphinRiderZombie::CanBeCharmed() const
{
	return mPhase == Phase::SWIMMING || mPhase == Phase::WALKING_WITHOUT_DOLPHIN;
}

bool DolphinRiderZombie::CanBeFrozen() const
{
	return mPhase != Phase::ENTERING_POOL && mPhase != Phase::JUMPING;
}

bool DolphinRiderZombie::CanBeGrabbedByTangleKelp() const
{
	return mPhase == Phase::SWIMMING || mPhase == Phase::WALKING_WITHOUT_DOLPHIN;
}

bool DolphinRiderZombie::ShouldPlayDeathAnimation() const
{
	return mPhase == Phase::SWIMMING || mPhase == Phase::WALKING_WITHOUT_DOLPHIN;
}

const std::string& DolphinRiderZombie::GetLostOuterArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_DOLPHINRIDER_OUTERARM_UPPER2;
}

const char* DolphinRiderZombie::GetDolphinHeadOffEffectName() const
{
	return "ZombieDolphinRiderHeadOff";
}

void DolphinRiderZombie::CheckDeferredArmDrop()
{
	if (!mHasArm || mBodyHealth > static_cast<int64_t>(mBodyMaxHealth) * 2 / 3) return;
	ArmDrop();
	mHasArm = false;
}

void DolphinRiderZombie::ApplyPhasePresentation() const
{
	if (mCollider && !mIsPreview) {
		// reanim 的骑乘身体轨道自身已向左偏约 70 px；碰撞箱保持逻辑原点即可对齐可见身体。
		mCollider->offset.x = mBaseColliderOffsetX;
		mCollider->mEnabled = !mIsDying
			&& mPhase != Phase::ENTERING_POOL
			&& mPhase != Phase::JUMPING;
	}
	if (auto* shadow = GetShadow()) {
		shadow->SetEnabled(mPhase != Phase::ENTERING_POOL && mPhase != Phase::JUMPING);
	}
}

bool DolphinRiderZombie::TryGetDrawClipBottom(float& clipBottom) const
{
	if (mPhase != Phase::ENTERING_POOL) {
		return Zombie::TryGetDrawClipBottom(clipBottom);
	}
	if (mIsPreview || !mAnimator) return false;

	// C# 只在 0.56～0.65 与 0.75～结束裁剪；底线低于普通水面，避免把骑手和海豚横向切断。
	const float progress = GetEntryProgress();
	float bottomOffset = 0.0f;
	if (progress >= kEntrySplashProgress && progress <= kEntryFirstClipEndProgress) {
		bottomOffset = kEntryFirstClipBottomOffsetY;
	}
	else if (progress >= kEntrySecondClipBeginProgress) {
		bottomOffset = kEntrySecondClipBottomOffsetY;
	}
	else {
		return false;
	}
	clipBottom = static_cast<float>(static_cast<int>(
		std::lround(GetPosition().y + bottomOffset)));
	return true;
}

void DolphinRiderZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["jumpTargetPlantID"] = mJumpTargetPlantID;
	j["entrySplashPlayed"] = mEntrySplashPlayed;
	j["jumpSplashPlayed"] = mJumpSplashPlayed;
	j["jumpBlockChecked"] = mJumpBlockChecked;
	j["jumpRetainsDolphinOnLanding"] = mJumpRetainsDolphinOnLanding;
	j["successfulJumpCount"] = mSuccessfulJumpCount;
}

void DolphinRiderZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::WALKING_WITHOUT_DOLPHIN));
	mPhase = static_cast<Phase>(phase);
	mJumpTargetPlantID = j.value("jumpTargetPlantID", NULL_PLANT_ID);
	mEntrySplashPlayed = j.value("entrySplashPlayed", false);
	mJumpSplashPlayed = j.value("jumpSplashPlayed", false);
	mJumpBlockChecked = j.value("jumpBlockChecked", false);
	mJumpRetainsDolphinOnLanding = j.value("jumpRetainsDolphinOnLanding", false);
	mSuccessfulJumpCount = std::clamp(
		j.value("successfulJumpCount", 0), 0, GetDolphinJumpCapacity());
	mNeedDropArm = !HasDolphin();
	mSpeed = kGroundRootMotionRate;
	ApplyPhasePresentation();
}

ZombieMovementRules::BirthProfile DolphinRiderZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.clip="anim_walkdolphin";
	p.alternative=nullptr;
	p.rootMinimum=p.rootMaximum=kGroundRootMotionRate;
	p.animationMinimum=p.animationMaximum=kFastGroundClipSpeed;
	p.phaseDependent=true;
	return p;
}

float DolphinRiderZombie::GetMineSimulationMoveSpeed() const
{
	if (mPhase==Phase::RIDING) return ScaleSimulationMoveSpeed(kRideWorldSpeed);
	if (mPhase==Phase::JUMPING) return ScaleSimulationMoveSpeed(kJumpWorldSpeed);
	return Zombie::GetMineSimulationMoveSpeed();
}
