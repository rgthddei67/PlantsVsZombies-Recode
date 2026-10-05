#include "HealerZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_HEALER).body;                       // 急救员本体生命
	constexpr float kFullHealCooldown = 5.0f;             // 成功治疗后的完整冷却，单位游戏秒
	constexpr float kCastDuration = 1.0f;                 // 两种治疗共同的施法前摇，单位游戏秒
	constexpr float kRetryDelay = 0.5f;                   // 目标失效或无伤员时的重试间隔，单位游戏秒
	constexpr float kStrategicWaitStep = 0.5f;            // 蒙特卡洛一次等待候选延后的游戏秒
	constexpr float kStrategicWaitMaximum = 2.0f;         // 单次治疗机会允许累计等待的上限游戏秒
	constexpr float kAreaRadius = 140.0f;                 // 群疗判定与结算半径，单位像素
	constexpr float kFocusedRadius = 280.0f;              // 单疗锁定与结算半径，单位像素
	constexpr int kAreaWoundedThreshold = 3;              // 选择群疗所需的最少伤员数，包含施法者
	constexpr int kAreaHealAmount = 100;                  // 群疗对每个现存生命层的恢复量
	constexpr int kFocusedHealAmount = 400;               // 单疗对每个现存生命层的恢复量
	constexpr int kArmDisableDifficulty = 2;              // 难度不高于此值时断臂永久禁疗
	constexpr float kBiteRetentionGap = 6.0f;             // 治疗后恢复原啃食目标允许的最大碰撞箱间隙，单位像素
	constexpr float kGearFollowerOffsetX = 34.0f;         // 急救包/标志相对身体轨道的局部水平偏移
	constexpr float kGearFollowerOffsetY = -24.0f;        // 急救包/标志相对身体轨道的局部垂直偏移
	constexpr float kGearFollowerScale = 0.82f;           // 抵消身体轨道缩放后的急救装备尺寸
	constexpr const char* kGearFollowerSlot =
		"healer_treatment_gear";                           // 身体轨道上的独立治疗装备 follower 槽
	constexpr float kCastStartVolume = 0.22f;             // 急救包机械扣合声量
	constexpr float kAreaResolveVolume = 0.18f;           // 群疗结算提示声量
	constexpr float kFocusedResolveVolume = 0.30f;        // 单疗结算提示声量

	bool IsRepairablePool(int current, int maximum)
	{
		return current > 0 && maximum > 0;
	}

	bool IsDamagedPool(int current, int maximum)
	{
		return IsRepairablePool(current, maximum) && current < maximum;
	}
}

void HealerZombie::SetupZombie()
{
	// 普通僵尸时间线已经注册啃咬与死亡事件；治疗只使用逻辑计时，不增加帧事件。
	Zombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mAttackDamage = 50;
	mTreatmentState = TreatmentState::IDLE;
	mHealCooldown = kFullHealCooldown;
	mRetryTimer = 0.0f;
	mCastRemaining = 0.0f;
	mFocusedTargetID = NULL_ZOMBIE_ID;
	mResumePlantID = NULL_PLANT_ID;
	mResumeZombieID = NULL_ZOMBIE_ID;
	mStrategicWaitElapsed = 0.0f;
	mLastDecisionMode = DecisionMode::DETERMINISTIC;
	mLastDecisionAction = DecisionAction::NONE;
	mLastMonteCarloRolloutCount = 0;
	mLastMonteCarloCandidateCount = 0;
	mLastMonteCarloZombieCount = 0;
	mLastMonteCarloCardCount = 0;
	mLastMonteCarloBestScore = 0.0f;
	mHealingPermanentlyDisabled = false;
	ConfigureTreatmentPresentation();
	ApplyTreatmentPresentation();
	if (mIsPreview) PlayTrack("anim_idle");
}

void HealerZombie::ConfigureTreatmentPresentation()
{
	if (!mAnimator || !mAnimator->HasTrack("Zombie_body")) return;
	ResourceManager& resources = ResourceManager::GetInstance();
	if (const Texture* body = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_BODY, false)) {
		mAnimator->SetTrackImage("Zombie_body", body);
	}
	const Texture* gear = resources.GetTexture(TreatmentGearTextureKey(), false);
	if (!gear) return;
	mAnimator->SetTrackFollowerImage("Zombie_body", kGearFollowerSlot, gear,
		kGearFollowerOffsetX, kGearFollowerOffsetY,
		kGearFollowerScale, kGearFollowerScale,
		/*drawAfterAllTracks=*/true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kGearFollowerSlot, true);
	mGearFollowerConfigured = true;
}

const std::string& HealerZombie::TreatmentGearTextureKey() const
{
	switch (mTreatmentState) {
	case TreatmentState::AREA:
		return ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_AREA;
	case TreatmentState::FOCUSED:
		return ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_FOCUSED;
	case TreatmentState::DISABLED:
		return ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_DISABLED;
	default:
		return ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_IDLE;
	}
}

void HealerZombie::ApplyTreatmentPresentation() const
{
	if (!mAnimator || !mGearFollowerConfigured) return;
	mAnimator->SetTrackFollowerImage("Zombie_body", kGearFollowerSlot,
		ResourceManager::GetInstance().GetTexture(TreatmentGearTextureKey(), false),
		kGearFollowerOffsetX, kGearFollowerOffsetY,
		kGearFollowerScale, kGearFollowerScale,
		/*drawAfterAllTracks=*/true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kGearFollowerSlot,
		!mIsDead && !mIsDying);
}

bool HealerZombie::IsTreatmentGearVisible() const
{
	return mGearFollowerConfigured && mAnimator
		&& mAnimator->GetTrackFollowerVisible("Zombie_body", kGearFollowerSlot);
}

Vector HealerZombie::TreatmentCenter(const Zombie& zombie)
{
	if (const ColliderComponent* collider = zombie.GetColliderComponent()) {
		const SDL_FRect bounds = collider->GetBoundingBox();
		return Vector(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f);
	}
	return zombie.GetVisualPosition();
}

bool HealerZombie::IsWounded(const Zombie& zombie)
{
	return IsDamagedPool(zombie.mBodyHealth, zombie.mBodyMaxHealth)
		|| IsDamagedPool(zombie.mHelmHealth, zombie.mHelmMaxHealth)
		|| IsDamagedPool(zombie.mShieldHealth, zombie.mShieldMaxHealth);
}

float HealerZombie::LowestRepairableRatio(const Zombie& zombie)
{
	float ratio = 1.0f;
	if (IsRepairablePool(zombie.mBodyHealth, zombie.mBodyMaxHealth)) {
		ratio = std::min(ratio, static_cast<float>(zombie.mBodyHealth)
			/ static_cast<float>(zombie.mBodyMaxHealth));
	}
	if (IsRepairablePool(zombie.mHelmHealth, zombie.mHelmMaxHealth)) {
		ratio = std::min(ratio, static_cast<float>(zombie.mHelmHealth)
			/ static_cast<float>(zombie.mHelmMaxHealth));
	}
	if (IsRepairablePool(zombie.mShieldHealth, zombie.mShieldMaxHealth)) {
		ratio = std::min(ratio, static_cast<float>(zombie.mShieldHealth)
			/ static_cast<float>(zombie.mShieldMaxHealth));
	}
	return ratio;
}

bool HealerZombie::IsValidTreatmentTarget(
	const Zombie& zombie, float radius, bool allowSelf) const
{
	if (!allowSelf && zombie.mZombieID == mZombieID) return false;
	if (zombie.IsPreview() || !zombie.IsActive() || zombie.IsDying()
		|| !zombie.HasHead() || zombie.mBodyHealth <= 0
		|| zombie.IsMindControlled() != IsMindControlled() || !IsWounded(zombie)) {
		return false;
	}
	const Vector from = TreatmentCenter(*this);
	const Vector to = TreatmentCenter(zombie);
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	return dx * dx + dy * dy <= radius * radius;
}

std::vector<int> HealerZombie::CollectAreaTargets(float radius) const
{
	std::vector<int> result;
	if (!mBoard) return result;
	for (int zombieID : mBoard->mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mBoard->mEntityRegistry.GetZombie(zombieID);
		if (zombie && IsValidTreatmentTarget(*zombie, radius, true)) {
			result.push_back(zombieID);
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<int> HealerZombie::CollectFocusedTargets() const
{
	std::vector<int> result;
	if (!mBoard) return result;
	EntityRegistry& entities = mBoard->mEntityRegistry;
	for (int zombieID : entities.GetAllZombieIDs()) {
		Zombie* zombie = entities.GetZombie(zombieID);
		if (zombie && IsValidTreatmentTarget(*zombie, kFocusedRadius, false)
			&& !entities.IsHealerFocusedTargetReserved(zombieID, mZombieID)) {
			result.push_back(zombieID);
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

int HealerZombie::SelectFocusedTarget() const
{
	if (!mBoard) return NULL_ZOMBIE_ID;
	EntityRegistry& entities = mBoard->mEntityRegistry;
	const int lockedHijackerID = mBoard->GetNightRoofHijackerID();
	if (Zombie* hijacker = entities.GetZombie(lockedHijackerID);
		hijacker && IsValidTreatmentTarget(*hijacker, kFocusedRadius, false)
		&& !entities.IsHealerFocusedTargetReserved(lockedHijackerID, mZombieID)) {
		return lockedHijackerID;
	}

	std::vector<int> ids = entities.GetAllZombieIDs();
	std::sort(ids.begin(), ids.end());
	int selectedID = NULL_ZOMBIE_ID;
	float selectedRatio = 2.0f;
	for (int zombieID : ids) {
		Zombie* zombie = entities.GetZombie(zombieID);
		if (!zombie || !IsValidTreatmentTarget(*zombie, kFocusedRadius, false)
			|| entities.IsHealerFocusedTargetReserved(zombieID, mZombieID)) {
			continue;
		}
		const float ratio = LowestRepairableRatio(*zombie);
		if (ratio < selectedRatio) {
			selectedRatio = ratio;
			selectedID = zombieID;
		}
	}
	return selectedID;
}

bool HealerZombie::SelectMonteCarloTreatment(
	const std::vector<int>& areaTargets,
	const std::vector<int>& focusedTargets)
{
	if (!mBoard || (areaTargets.empty() && focusedTargets.empty())) return false;
	MonteCarloTreatmentRequest request;
	request.sourceZombieID = mZombieID;
	request.areaTargetIDs = areaTargets;
	request.focusedTargetIDs = focusedTargets;
	request.areaRadius = kAreaRadius;
	request.focusedRadius = kFocusedRadius;
	request.areaHealAmount = static_cast<float>(kAreaHealAmount);
	request.focusedHealAmount = static_cast<float>(kFocusedHealAmount);
	request.castSeconds = kCastDuration;
	request.waitSeconds = kStrategicWaitStep;
	request.allowWait = mStrategicWaitElapsed + kStrategicWaitStep
		<= kStrategicWaitMaximum + 0.001f;

	MonteCarloTreatmentDecision decision;
	MonteCarloTargetStats stats;
	if (!mBoard->PickMonteCarloZombieTreatment(request, decision, &stats)) {
		return false;
	}
	mLastDecisionMode = DecisionMode::MONTE_CARLO;
	mLastMonteCarloRolloutCount = stats.rolloutCount;
	mLastMonteCarloCandidateCount = stats.candidateCount;
	mLastMonteCarloZombieCount = stats.sampledZombieCount;
	mLastMonteCarloCardCount = stats.cardCount;
	mLastMonteCarloBestScore = stats.bestScore;
	switch (decision.action) {
	case MonteCarloTreatmentAction::WAIT:
		mLastDecisionAction = DecisionAction::WAIT;
		mStrategicWaitElapsed = std::min(
			kStrategicWaitMaximum, mStrategicWaitElapsed + kStrategicWaitStep);
		mRetryTimer = kStrategicWaitStep;
		return true;
	case MonteCarloTreatmentAction::AREA:
		mLastDecisionAction = DecisionAction::AREA;
		BeginTreatment(TreatmentState::AREA, NULL_ZOMBIE_ID);
		return true;
	case MonteCarloTreatmentAction::FOCUSED:
		if (Zombie* target = mBoard->mEntityRegistry.GetZombie(
			decision.targetZombieID);
			target && IsValidTreatmentTarget(*target, kFocusedRadius, false)
			&& !mBoard->mEntityRegistry.IsHealerFocusedTargetReserved(
				decision.targetZombieID, mZombieID)) {
			mLastDecisionAction = DecisionAction::FOCUSED;
			BeginTreatment(TreatmentState::FOCUSED, decision.targetZombieID);
			return true;
		}
		return false;
	}
	return false;
}

bool HealerZombie::IsReadyForTreatmentChoice() const
{
	return !mIsPreview && IsActive() && !mIsDead && !mIsDying && mHasHead
		&& !mHealingPermanentlyDisabled
		&& !IsImmobilized() && !IsGarlicRedirectPaused()
		&& mTangleKelpPlantID == NULL_PLANT_ID
		&& mTreatmentState == TreatmentState::IDLE
		&& mHealCooldown <= 0.0f && mRetryTimer <= 0.0f;
}

bool HealerZombie::IsFocusedOnTarget(int zombieID) const
{
	return zombieID != NULL_ZOMBIE_ID
		&& mTreatmentState == TreatmentState::FOCUSED
		&& mFocusedTargetID == zombieID && IsActive() && !mIsDying;
}

void HealerZombie::MakeTreatmentReadyForTesting()
{
	if (mTreatmentState != TreatmentState::IDLE || mHealingPermanentlyDisabled) return;
	mHealCooldown = 0.0f;
	mRetryTimer = 0.0f;
	mStrategicWaitElapsed = 0.0f;
}

void HealerZombie::Update()
{
	const bool wasEating = mIsEating;
	Zombie::Update();
	// 基类在啃食态会在品种逻辑前早退；急救冷却与选疗仍须推进，但不能双推普通非啃食帧。
	if (wasEating && mIsEating && !mIsPreview && IsActive() && !mIsDead
		&& !mIsDying && mHasHead && !IsImmobilized()
		&& !IsGarlicRedirectPaused() && mTangleKelpPlantID == NULL_PLANT_ID) {
		const float slowMultiplier = mCooldownTimer > 0.0f ? 0.5f : 1.0f;
		ZombieUpdate(DeltaTime::GetDeltaTime() * slowMultiplier);
	}
}

void HealerZombie::ZombieUpdate(float scaledTime)
{
	if (mIsPreview || mIsDead || mIsDying || !mHasHead
		|| mHealingPermanentlyDisabled || scaledTime <= 0.0f) {
		return;
	}
	if (mTreatmentState == TreatmentState::AREA
		|| mTreatmentState == TreatmentState::FOCUSED) {
		mCastRemaining = std::max(0.0f, mCastRemaining - scaledTime);
		if (mCastRemaining <= 0.0f) ResolveTreatment();
		return;
	}

	if (mHealCooldown > 0.0f) {
		mHealCooldown = std::max(0.0f, mHealCooldown - scaledTime);
		if (mHealCooldown > 0.0f) return;
	}
	if (mRetryTimer > 0.0f) {
		mRetryTimer = std::max(0.0f, mRetryTimer - scaledTime);
		if (mRetryTimer > 0.0f) return;
	}
	if (mBoard && mBoard->mEntityRegistry.HasReadyHealerBefore(mZombieID)) return;

	const std::vector<int> areaTargets = CollectAreaTargets(kAreaRadius);
	const std::vector<int> focusedTargets = CollectFocusedTargets();
	if (areaTargets.empty() && focusedTargets.empty()) {
		mStrategicWaitElapsed = 0.0f;
		mLastDecisionAction = DecisionAction::NONE;
		mRetryTimer = kRetryDelay;
		return;
	}
	if (GameAPP::GetInstance().mEnableMonteCarloAI && !IsMindControlled()) {
		// 多名急救员同时就绪时按实体 ID 分帧领取预算，避免动作边沿推演叠成单帧尖峰。
		if (mBoard && !mBoard->TryClaimMonteCarloHealerDecisionSlot()) return;
		if (SelectMonteCarloTreatment(areaTargets, focusedTargets)) return;
	}

	// 关闭总开关、魅惑侧暂不适用或推演失败时，完整保留原确定性规则。
	mLastDecisionMode = DecisionMode::DETERMINISTIC;
	mLastMonteCarloRolloutCount = 0;
	mLastMonteCarloCandidateCount = 0;
	mLastMonteCarloZombieCount = 0;
	mLastMonteCarloCardCount = 0;
	mLastMonteCarloBestScore = 0.0f;
	if (static_cast<int>(areaTargets.size()) >= kAreaWoundedThreshold) {
		mLastDecisionAction = DecisionAction::AREA;
		BeginTreatment(TreatmentState::AREA, NULL_ZOMBIE_ID);
		return;
	}
	const int focusedTargetID = SelectFocusedTarget();
	if (focusedTargetID != NULL_ZOMBIE_ID) {
		mLastDecisionAction = DecisionAction::FOCUSED;
		BeginTreatment(TreatmentState::FOCUSED, focusedTargetID);
		return;
	}
	mLastDecisionAction = DecisionAction::NONE;
	mStrategicWaitElapsed = 0.0f;
	mRetryTimer = kRetryDelay;
}

void HealerZombie::BeginTreatment(
	TreatmentState state, int focusedTargetID)
{
	if (state != TreatmentState::AREA && state != TreatmentState::FOCUSED) return;
	StopEatingForTreatment();
	mTreatmentState = state;
	mFocusedTargetID = state == TreatmentState::FOCUSED
		? focusedTargetID : NULL_ZOMBIE_ID;
	mStrategicWaitElapsed = 0.0f;
	mCastRemaining = kCastDuration;
	mLastHealTargetCount = 0;
	mLastHealTotalAmount = 0;
	PlayTrack("anim_idle", 0.0f, 0.12f);
	ApplyTreatmentPresentation();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BUTTONCLICK, kCastStartVolume);
}

int HealerZombie::ApplyTreatment(
	Zombie& target, int amount, const char* effectName) const
{
	int total = 0;
	auto repair = [amount, &total](int& current, int maximum) {
		if (!IsDamagedPool(current, maximum)) return;
		const int before = current;
		current = std::min(maximum, current + amount);
		total += current - before;
	};
	repair(target.mBodyHealth, target.mBodyMaxHealth);
	repair(target.mHelmHealth, target.mHelmMaxHealth);
	repair(target.mShieldHealth, target.mShieldMaxHealth);
	if (total <= 0) return 0;
	target.RefreshEquipmentPresentationAfterRepair();
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(effectName, TreatmentCenter(target));
	}
	return total;
}

void HealerZombie::ResolveTreatment()
{
	if (!mBoard) {
		CancelTreatment(false, true);
		return;
	}
	const TreatmentState resolvingState = mTreatmentState;
	std::vector<int> targetIDs;
	if (resolvingState == TreatmentState::AREA) {
		targetIDs = CollectAreaTargets(kAreaRadius);
	}
	else if (resolvingState == TreatmentState::FOCUSED) {
		if (Zombie* target = mBoard->mEntityRegistry.GetZombie(mFocusedTargetID);
			target && IsValidTreatmentTarget(*target, kFocusedRadius, false)) {
			targetIDs.push_back(mFocusedTargetID);
		}
	}
	std::sort(targetIDs.begin(), targetIDs.end());

	const int amount = resolvingState == TreatmentState::AREA
		? kAreaHealAmount : kFocusedHealAmount;
	const char* effectName = resolvingState == TreatmentState::AREA
		? "HealerAreaHeal" : "HealerFocusedHeal";
	int totalAmount = 0;
	int healedTargets = 0;
	for (int zombieID : targetIDs) {
		Zombie* target = mBoard->mEntityRegistry.GetZombie(zombieID);
		if (!target) continue;
		const int repaired = ApplyTreatment(*target, amount, effectName);
		if (repaired <= 0) continue;
		totalAmount += repaired;
		++healedTargets;
	}

	if (healedTargets <= 0) {
		CancelTreatment(false, true);
		return;
	}
	mLastHealTargetCount = healedTargets;
	mLastHealTotalAmount = totalAmount;
	mTreatmentState = TreatmentState::IDLE;
	mFocusedTargetID = NULL_ZOMBIE_ID;
	mCastRemaining = 0.0f;
	mRetryTimer = 0.0f;
	mHealCooldown = kFullHealCooldown;
	ApplyTreatmentPresentation();
	AudioSystem::PlaySound(
		resolvingState == TreatmentState::AREA
			? ResourceKeys::Sounds::SOUND_COLLECTSUN
			: ResourceKeys::Sounds::SOUND_CHOOSEPLANT1,
		resolvingState == TreatmentState::AREA
			? kAreaResolveVolume : kFocusedResolveVolume);
	ResumeEatingAfterTreatment();
}

void HealerZombie::CancelTreatment(bool permanent, bool resumeEating)
{
	const bool wasCasting = mTreatmentState == TreatmentState::AREA
		|| mTreatmentState == TreatmentState::FOCUSED;
	mFocusedTargetID = NULL_ZOMBIE_ID;
	mCastRemaining = 0.0f;
	if (permanent) {
		mHealingPermanentlyDisabled = true;
		mTreatmentState = TreatmentState::DISABLED;
		mRetryTimer = 0.0f;
		mStrategicWaitElapsed = 0.0f;
		mResumePlantID = NULL_PLANT_ID;
		mResumeZombieID = NULL_ZOMBIE_ID;
	}
	else {
		mTreatmentState = TreatmentState::IDLE;
		mRetryTimer = std::max(mRetryTimer, kRetryDelay);
	}
	ApplyTreatmentPresentation();
	if (resumeEating && !permanent) {
		ResumeEatingAfterTreatment();
	}
	else if (wasCasting && IsActive() && !mIsDead && !mIsDying) {
		// 魅惑、断肢或断头会直接结束前摇；不能把无根运动的 anim_idle 留成永久步态。
		PlayWalkAnimation(0.12f);
	}
}

void HealerZombie::StopEatingForTreatment()
{
	mResumePlantID = mIsEating ? mEatPlantID : NULL_PLANT_ID;
	mResumeZombieID = mIsEating ? mEatZombieID : NULL_ZOMBIE_ID;
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

bool HealerZombie::IsResumeTargetInBiteRange(const AnimatedObject& target) const
{
	const ColliderComponent* ownCollider = GetColliderComponent();
	const ColliderComponent* targetCollider = target.GetColliderComponent();
	if (!ownCollider || !targetCollider || !ownCollider->mEnabled || !targetCollider->mEnabled) {
		return false;
	}
	const SDL_FRect own = ownCollider->GetBoundingBox();
	const SDL_FRect other = targetCollider->GetBoundingBox();
	if (own.y >= other.y + other.h || own.y + own.h <= other.y) return false;
	float horizontalGap = 0.0f;
	if (own.x > other.x + other.w) horizontalGap = own.x - (other.x + other.w);
	else if (other.x > own.x + own.w) horizontalGap = other.x - (own.x + own.w);
	return horizontalGap <= kBiteRetentionGap;
}

void HealerZombie::ResumeEatingAfterTreatment()
{
	const int plantID = mResumePlantID;
	const int zombieID = mResumeZombieID;
	mResumePlantID = NULL_PLANT_ID;
	mResumeZombieID = NULL_ZOMBIE_ID;
	if (!mBoard || mIsDead || mIsDying || !mHasHead
		|| mTreatmentState != TreatmentState::IDLE) {
		return;
	}
	if (Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
		plant && IsPlantValidEatTarget(plant) && IsResumeTargetInBiteRange(*plant)) {
		mIsEating = true;
		mEatPlantID = plantID;
		mEatZombieID = NULL_ZOMBIE_ID;
		++plant->mEaterCount;
		PlayTrack("anim_eat", 2.1f, 0.12f);
		OnStartEating();
		return;
	}
	if (Zombie* zombie = mBoard->mEntityRegistry.GetZombie(zombieID);
		zombie && zombie->IsActive() && !zombie->IsDying()
		&& zombie->mBodyHealth > 0 && zombie->HasHead()
		&& zombie->mRow == mRow
		&& zombie->IsMindControlled() != IsMindControlled()
		&& IsResumeTargetInBiteRange(*zombie)) {
		mIsEating = true;
		mEatPlantID = NULL_PLANT_ID;
		mEatZombieID = zombieID;
		PlayTrack("anim_eat", 2.1f, 0.12f);
		OnStartEating();
		return;
	}
	PlayWalkAnimation(0.12f);
}

void HealerZombie::StartEat(ColliderComponent* other)
{
	if (mTreatmentState == TreatmentState::AREA
		|| mTreatmentState == TreatmentState::FOCUSED) return;
	Zombie::StartEat(other);
}

void HealerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mTreatmentState == TreatmentState::AREA
		|| mTreatmentState == TreatmentState::FOCUSED) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void HealerZombie::OnMindControlled()
{
	if (mTreatmentState == TreatmentState::AREA
		|| mTreatmentState == TreatmentState::FOCUSED) {
		CancelTreatment(false, false);
	}
	// 旧阵营啃食目标不能在治疗结束后复用；完整冷却保持原值。
	mResumePlantID = NULL_PLANT_ID;
	mResumeZombieID = NULL_ZOMBIE_ID;
	mStrategicWaitElapsed = 0.0f;
}

void HealerZombie::HeadDrop()
{
	if (!mHasHead) return;
	Zombie::HeadDrop();
	CancelTreatment(true, false);
}

void HealerZombie::ArmDrop()
{
	if (!mHasArm) return;
	Zombie::ArmDrop();
	if (!mIsPreview && GameAPP::GetInstance().Difficulty <= kArmDisableDifficulty) {
		CancelTreatment(true, false);
	}
}

void HealerZombie::Die()
{
	CancelTreatment(true, false);
	if (mAnimator) {
		mAnimator->SetTrackFollowerVisible("Zombie_body", kGearFollowerSlot, false);
	}
	Zombie::Die();
}

void HealerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mAnimator) return;
	if (const Texture* body = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_BODY, false)) {
		mAnimator->SetTrackImage("Zombie_body", body);
	}
	ApplyTreatmentPresentation();
}

void HealerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["treatmentState"] = static_cast<int>(mTreatmentState);
	j["healCooldown"] = mHealCooldown;
	j["retryTimer"] = mRetryTimer;
	j["castRemaining"] = mCastRemaining;
	j["focusedTargetID"] = mFocusedTargetID;
	j["resumePlantID"] = mResumePlantID;
	j["resumeZombieID"] = mResumeZombieID;
	j["strategicWaitElapsed"] = mStrategicWaitElapsed;
	j["lastDecisionMode"] = static_cast<int>(mLastDecisionMode);
	j["lastDecisionAction"] = static_cast<int>(mLastDecisionAction);
	j["mcRollouts"] = mLastMonteCarloRolloutCount;
	j["mcCandidates"] = mLastMonteCarloCandidateCount;
	j["mcZombies"] = mLastMonteCarloZombieCount;
	j["mcCards"] = mLastMonteCarloCardCount;
	j["mcBestScore"] = mLastMonteCarloBestScore;
	j["healingPermanentlyDisabled"] = mHealingPermanentlyDisabled;
}

void HealerZombie::LoadExtraData(const nlohmann::json& j)
{
	const int state = std::clamp(j.value("treatmentState", 0),
		static_cast<int>(TreatmentState::IDLE),
		static_cast<int>(TreatmentState::DISABLED));
	mTreatmentState = static_cast<TreatmentState>(state);
	mHealCooldown = std::clamp(j.value("healCooldown", kFullHealCooldown),
		0.0f, kFullHealCooldown);
	mRetryTimer = std::clamp(j.value("retryTimer", 0.0f), 0.0f, kRetryDelay);
	mCastRemaining = std::clamp(j.value("castRemaining", 0.0f), 0.0f, kCastDuration);
	mFocusedTargetID = j.value("focusedTargetID", NULL_ZOMBIE_ID);
	mResumePlantID = j.value("resumePlantID", NULL_PLANT_ID);
	mResumeZombieID = j.value("resumeZombieID", NULL_ZOMBIE_ID);
	mStrategicWaitElapsed = std::clamp(
		j.value("strategicWaitElapsed", 0.0f), 0.0f, kStrategicWaitMaximum);
	const int decisionMode = std::clamp(j.value("lastDecisionMode", 0),
		static_cast<int>(DecisionMode::DETERMINISTIC),
		static_cast<int>(DecisionMode::MONTE_CARLO));
	mLastDecisionMode = static_cast<DecisionMode>(decisionMode);
	const int decisionAction = std::clamp(j.value("lastDecisionAction", 0),
		static_cast<int>(DecisionAction::NONE),
		static_cast<int>(DecisionAction::WAIT));
	mLastDecisionAction = static_cast<DecisionAction>(decisionAction);
	mLastMonteCarloRolloutCount = std::max(0, j.value("mcRollouts", 0));
	mLastMonteCarloCandidateCount = std::max(0, j.value("mcCandidates", 0));
	mLastMonteCarloZombieCount = std::max(0, j.value("mcZombies", 0));
	mLastMonteCarloCardCount = std::max(0, j.value("mcCards", 0));
	mLastMonteCarloBestScore = j.value("mcBestScore", 0.0f);
	mHealingPermanentlyDisabled = j.value("healingPermanentlyDisabled", false);
	if (mIsPreview) {
		mTreatmentState = TreatmentState::IDLE;
		mHealCooldown = kFullHealCooldown;
		mRetryTimer = 0.0f;
		mCastRemaining = 0.0f;
		mFocusedTargetID = NULL_ZOMBIE_ID;
		mResumePlantID = NULL_PLANT_ID;
		mResumeZombieID = NULL_ZOMBIE_ID;
		mStrategicWaitElapsed = 0.0f;
		mLastDecisionMode = DecisionMode::DETERMINISTIC;
		mLastDecisionAction = DecisionAction::NONE;
		mLastMonteCarloRolloutCount = 0;
		mLastMonteCarloCandidateCount = 0;
		mLastMonteCarloZombieCount = 0;
		mLastMonteCarloCardCount = 0;
		mLastMonteCarloBestScore = 0.0f;
		mHealingPermanentlyDisabled = false;
	}
	else if (!mHasHead
		|| (!mHasArm && GameAPP::GetInstance().Difficulty <= kArmDisableDifficulty)) {
		mHealingPermanentlyDisabled = true;
		mTreatmentState = TreatmentState::DISABLED;
		mFocusedTargetID = NULL_ZOMBIE_ID;
		mCastRemaining = 0.0f;
		mStrategicWaitElapsed = 0.0f;
	}
	else if ((mTreatmentState == TreatmentState::AREA
			|| mTreatmentState == TreatmentState::FOCUSED)
		&& mCastRemaining <= 0.0f) {
		mTreatmentState = TreatmentState::IDLE;
		mFocusedTargetID = NULL_ZOMBIE_ID;
		mRetryTimer = kRetryDelay;
	}
	if (mHealingPermanentlyDisabled) mTreatmentState = TreatmentState::DISABLED;
	ApplyTreatmentPresentation();
}

ZombieMovementRules::BirthProfile HealerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
