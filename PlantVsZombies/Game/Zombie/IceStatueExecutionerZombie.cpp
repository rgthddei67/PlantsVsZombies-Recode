#include "IceStatueExecutionerZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <limits>

namespace {
	constexpr int kExecutionerBodyHealth = 300;            // 处刑者本体生命；与 2700 黑帽合计 3000
	constexpr int kExecutionerHelmetHealth = 2700;         // 黑色橄榄球头盔生命
	constexpr int kStrikeDamage = 40;                      // 每次已提交锤击的普通僵尸伤害
	constexpr int kDefaultRequiredExecutionProgress = 3;   // 普通植物达到此进度时立即处决
	constexpr int kMaximumSerializedExecutionProgress = 255; // 防损坏读档上限；恢复后再按实际目标锤数钳制
	constexpr float kStrikeIntervalSeconds = 1.5f;         // 每次处决锤击从起手到提交的游戏秒数
	constexpr float kResourceFps = 12.0f;                  // Zombie_ladder.reanim 基础帧率
	constexpr float kStrikeClipSpeed = 0.3667f;            // 11 帧放梯动作约播放 2.5 游戏秒
	constexpr float kEatClipSpeed = 3.0f;                  // 复用扶梯僵尸普通啃食速度
	constexpr float kWalkFrameCount = 47.0f;               // anim_walk 的帧数
	constexpr float kCSharpTicksPerSecond = 47.0f;         // 原版速度口径换算用逻辑 tick 频率
	constexpr float kWalkGroundDistance = 66.0f;           // anim_walk 的 _ground 总位移，单位：px
	constexpr float kGroundRootMotionScale = 12.0f;        // 根轨逐帧位移换算为每秒位移的倍率
	constexpr float kMagnetDestinationX = 20.0f;           // 红帽吸向磁力菇的局部 X
	constexpr float kMagnetDestinationY = 20.0f;           // 红帽吸向磁力菇的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f;      // 离体红帽落点随机扰动，单位：px
	constexpr float kHelmetFollowerOffsetX = -2.0f;        // 头盔相对 anim_head1 的局部水平校准，单位：px
	constexpr float kHelmetFollowerOffsetY = -4.0f;        // 头盔相对 anim_head1 的局部垂直校准，单位：px
	constexpr float kHelmetFollowerScale = 0.88f;          // 头盔相对扶梯头部轨道的缩放，兼顾头部右下缘覆盖
	constexpr float kHelmetDrawScale = 0.82f;              // 磁力菇吸取后的离体头盔显示比例
	constexpr float kHelmetDropCenterX = 22.0f;             // 从头部贴图原点换算到 0.70 倍掉落粒子中心的 X
	constexpr float kHelmetDropCenterY = 22.0f;             // 从头部贴图原点换算到 0.70 倍掉落粒子中心的 Y
	constexpr float kLimbVolume = 0.25f;                   // 复用扶梯断肢音量
	constexpr const char* kHelmetFollowerSlot =
		"ice_executioner_helmet";                           // anim_head1 上独立于黄油的命名 follower 槽
}

float IceStatueExecutionerZombie::WalkClipFromVelocity(float velocity)
{
	return velocity * kWalkFrameCount * kCSharpTicksPerSecond
		/ (kWalkGroundDistance * kResourceFps);
}

void IceStatueExecutionerZombie::SetupZombie()
{
	mBodyHealth = kExecutionerBodyHealth;
	mBodyMaxHealth = kExecutionerBodyHealth;
	mHelmType = HelmType::HELMTYPE_FOOTBALL;
	mHelmHealth = kExecutionerHelmetHealth;
	mHelmMaxHealth = kExecutionerHelmetHealth;
	mHelmetStage = ArmorBrokenState::NO_BROKEN;
	mShieldType = ShieldType::SHIELDTYPE_NONE;
	mShieldHealth = 0;
	mShieldMaxHealth = 0;
	mAttackDamage = 50;
	mSpeed = kGroundRootMotionScale;
	mWalkVelocity = 0.30f;
	mExecutionPhase = ExecutionPhase::READY;
	mExecutionTargetPlantID = NULL_PLANT_ID;
	mExecutionProgress = 0;
	mExecutionUsed = false;
	mTargetingMode = TargetingMode::NONE;
	mTargetingRolloutCount = 0;
	mTargetingCandidateCount = 0;
	mTargetingZombieCount = 0;
	mTargetingBestScore = 0.0f;

	RegisterFrameEvents();
	ApplyExecutionerTextures();
	ConfigureHelmetFollower();
	SyncHelmetPresentation();
	if (mIsPreview) PlayTrack("anim_idle");
	else PlayWalkAnimation(0.0f);
}

/** 只复用主人已确认过的扶梯啃食帧和死亡终点；处决提交使用一次性轨道播完边沿。 */
void IceStatueExecutionerZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(194, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(131, [this]() { Die(); });
}

void IceStatueExecutionerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || mExecutionPhase == ExecutionPhase::EXECUTING) return;
	if (mExecutionPhase == ExecutionPhase::READY && IsFullyOnBattlefield()
		&& CanOwnExecution() && mBoard) {
		MonteCarloTargetStats stats;
		if (Plant* target = mBoard->SelectIceStatueExecutionTarget(
			mZombieID, kStrikeIntervalSeconds, kStrikeDamage, &stats)) {
			mTargetingMode = stats.rolloutCount > 0
				? TargetingMode::MONTE_CARLO
				: TargetingMode::STRATEGIC_FALLBACK;
			mTargetingRolloutCount = stats.rolloutCount;
			mTargetingCandidateCount = stats.candidateCount;
			mTargetingZombieCount = stats.sampledZombieCount;
			mTargetingBestScore = stats.bestScore;
			if (BeginExecution(*target)) return;
		}
	}
	Zombie::ZombieMove(scaledDelta, transform);
}

void IceStatueExecutionerZombie::ZombieUpdate(float)
{
	if (mExecutionPhase != ExecutionPhase::EXECUTING) return;
	if (!CanOwnExecution()) {
		AbortExecution(true, !mIsDying);
		return;
	}
	Plant* target = ResolveExecutionTarget();
	if (!target || target->GetIceSealOwnerZombieID() != mZombieID) {
		AbortExecution(false, true);
		return;
	}
	if (!mBoard || !mBoard->IsPlantFootprintFrozen(
		target->mPlantType, target->mRow, target->mColumn)) {
		AbortExecution(false, true);
		return;
	}
	if (mAnimator && !mAnimator->IsPlaying()) CommitStrike();
}

void IceStatueExecutionerZombie::StartEat(ColliderComponent* other)
{
	if (mExecutionPhase == ExecutionPhase::EXECUTING) return;
	Zombie::StartEat(other);
}

void IceStatueExecutionerZombie::OnStartEating()
{
	PlayTrack("anim_eat", kEatClipSpeed, 0.2f);
}

void IceStatueExecutionerZombie::PlayWalkAnimation(float blendTime)
{
	PlayTrack("anim_walk", WalkClipFromVelocity(mWalkVelocity), blendTime);
}

bool IceStatueExecutionerZombie::IsFullyOnBattlefield() const
{
	if (!mBoard || !mCollider) return false;
	const SDL_FRect bounds = mCollider->GetBoundingBox();
	const float battlefieldRightX = CELL_INITALIZE_POS_X
		+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X;
	return bounds.x + bounds.w <= battlefieldRightX;
}

bool IceStatueExecutionerZombie::CanOwnExecution() const
{
	return mBoard && !mIsPreview && IsActive() && !mIsDying
		&& !IsMindControlled() && HasHead() && HasArm() && !mExecutionUsed
		&& mBoard->SupportsWinterTemperature();
}

bool IceStatueExecutionerZombie::BeginExecution(Plant& target)
{
	if (mExecutionPhase != ExecutionPhase::READY || !CanOwnExecution()) {
		return false;
	}
	// 炉芯花在封存关系提交前响应；成功后目标从未进入冰封，处刑者的一次性能力直接耗尽。
	if (mBoard->TryPreventIceExecutionSeal(target)) {
		mExecutionUsed = true;
		mExecutionPhase = ExecutionPhase::SPENT;
		mExecutionTargetPlantID = NULL_PLANT_ID;
		mExecutionProgress = 0;
		return true;
	}
	if (!target.BeginIceSeal(mZombieID)) return false;
	if (mIsEating && mEatPlantID != NULL_PLANT_ID) {
		StopEatingInvalidPlantTarget(0.0f);
	}
	mExecutionPhase = ExecutionPhase::EXECUTING;
	mExecutionTargetPlantID = target.mPlantID;
	mExecutionProgress = 0;
	BeginStrike(0.0f);
	return true;
}

void IceStatueExecutionerZombie::BeginStrike(float blendTime)
{
	if (!mAnimator || mExecutionPhase != ExecutionPhase::EXECUTING) return;
	PlayTrack("anim_placeladder", kStrikeClipSpeed, blendTime);
	mAnimator->Play(PlayState::PLAY_ONCE);
}

void IceStatueExecutionerZombie::CommitStrike()
{
	Plant* target = ResolveExecutionTarget();
	if (!target || !mBoard || !target->TakeIceExecutionDamage(mZombieID, kStrikeDamage)) {
		AbortExecution(false, true);
		return;
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LADDER_ZOMBIE, 0.45f);
	if (!target->IsActive()) {
		mExecutionUsed = true;
		mExecutionPhase = ExecutionPhase::SPENT;
		mExecutionTargetPlantID = NULL_PLANT_ID;
		mExecutionProgress = 0;
		PlayWalkAnimation(0.1f);
		return;
	}
	++mExecutionProgress;
	if (mExecutionProgress >= target->GetIceExecutionRequiredStrikeCount()) {
		target->ResolveIceExecution(mZombieID);
		mExecutionUsed = true;
		mExecutionPhase = ExecutionPhase::SPENT;
		mExecutionTargetPlantID = NULL_PLANT_ID;
		mExecutionProgress = 0;
		PlayWalkAnimation(0.1f);
		return;
	}
	BeginStrike(0.0f);
}

void IceStatueExecutionerZombie::AbortExecution(
	bool consumeAbility, bool restoreWalkAnimation)
{
	if (mExecutionPhase != ExecutionPhase::EXECUTING) {
		if (consumeAbility) {
			mExecutionUsed = true;
			mExecutionPhase = ExecutionPhase::SPENT;
		}
		return;
	}
	if (Plant* target = ResolveExecutionTarget()) {
		target->ReleaseIceSeal(mZombieID);
	}
	mExecutionUsed = mExecutionUsed || consumeAbility;
	mExecutionPhase = mExecutionUsed
		? ExecutionPhase::SPENT : ExecutionPhase::READY;
	mExecutionTargetPlantID = NULL_PLANT_ID;
	mExecutionProgress = 0;
	if (restoreWalkAnimation && mAnimator && !mIsDying && IsActive()) {
		PlayWalkAnimation(0.0f);
	}
}

Plant* IceStatueExecutionerZombie::ResolveExecutionTarget() const
{
	return mBoard && mExecutionTargetPlantID != NULL_PLANT_ID
		? mBoard->mEntityRegistry.GetPlant(mExecutionTargetPlantID) : nullptr;
}

float IceStatueExecutionerZombie::GetInterruptibleSpecialActionRemaining() const
{
	if (mExecutionPhase != ExecutionPhase::EXECUTING || !mAnimator) return -1.0f;
	const auto [beginFrame, endFrame] = mAnimator->GetTrackRange("anim_placeladder");
	if (beginFrame < 0 || endFrame <= beginFrame) return -1.0f;
	const float speed = mAnimator->EffectiveSpeed();
	if (speed <= 0.0001f) return std::numeric_limits<float>::max();
	const float remainingFrames = std::max(0.0f,
		static_cast<float>(endFrame) - mAnimator->GetCurrentFrame());
	return remainingFrames / (kResourceFps * speed);
}

bool IceStatueExecutionerZombie::InterruptUncommittedSpecialAction()
{
	if (mExecutionPhase != ExecutionPhase::EXECUTING || !ResolveExecutionTarget()) {
		return false;
	}
	// 警铃只重置当前尚未提交的前摇；目标、冰封和既有进度全部保留。
	BeginStrike(0.0f);
	return true;
}

void IceStatueExecutionerZombie::Die()
{
	AbortExecution(true, false);
	HideHelmetFollower();
	Zombie::Die();
}

void IceStatueExecutionerZombie::OnMindControlled()
{
	AbortExecution(true, true);
}

void IceStatueExecutionerZombie::HeadDrop()
{
	if (!mHasHead) return;
	AbortExecution(true, true);
	HideHelmetFollower();
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	if (g_particleSystem) {
		// 合成“红帽随头飞”的新粒子等待主人确认；当前只复用既有扶梯裸头终态。
		g_particleSystem->EmitEffect("ZombieLadderHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

void IceStatueExecutionerZombie::ArmDrop()
{
	if (!mHasArm) return;
	AbortExecution(true, true);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_ladder_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_OUTERARM_UPPER2));
	if (g_particleSystem) g_particleSystem->EmitEffect("LadderArmOff", GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

const std::string& IceStatueExecutionerZombie::GetHelmetTextureKey() const
{
	using namespace ResourceKeys::Textures;
	if (mHelmetStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET2;
	}
	if (mHelmetStage == ArmorBrokenState::REALLY_BROKEN) {
		return IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET3;
	}
	return IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET;
}

void IceStatueExecutionerZombie::TakePlantAshDamage(int damage)
{
	if (damage <= 0 || !mBoard) return;

	// 黑盔是处刑者的主要耐久层；灰烬阈值必须先统计它，不得因低本体血量绕甲直消。
	const int scaledDamage =
		ScaleStatusDamage(mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage));
	const int64_t remainingDurability = static_cast<int64_t>(mBodyHealth)
		+ (mHelmType == HelmType::HELMTYPE_FOOTBALL
			? std::max(0, mHelmHealth) : 0);
	if (CanBeCharred() && remainingDurability <= scaledDamage) {
		Charred();
		return;
	}
	TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false,
		PlantDamageOrigin::Ash());
}

bool IceStatueExecutionerZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_FOOTBALL && mHelmHealth > 0;
}

bool IceStatueExecutionerZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = GetHelmetTextureKey();
	const float objectScale = GetTransform() ? GetTransform()->GetScale() : 1.0f;
	item.worldPosition = GetTrackWorldPosition("anim_head1")
		+ Vector(kHelmetFollowerOffsetX, kHelmetFollowerOffsetY) * objectScale;
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = kHelmetDrawScale;
	mHelmHealth = 0;
	Zombie::HelmDrop();
	mHelmetStage = ArmorBrokenState::NONE;
	HideHelmetFollower();
	return true;
}

void IceStatueExecutionerZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	mHelmetStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	SyncHelmetPresentation();
}

void IceStatueExecutionerZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	const float objectScale = GetTransform() ? GetTransform()->GetScale() : 1.0f;
	// 粒子以图片中心定位；先从实际头轨原点换算到当前佩戴头盔中心，再隐藏 follower。
	const Vector dropPosition = GetRenderedTrackWorldPosition("anim_head1")
		+ Vector(kHelmetDropCenterX, kHelmetDropCenterY) * objectScale;
	Zombie::HelmDrop();
	mHelmetStage = ArmorBrokenState::NONE;
	HideHelmetFollower();
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieIceExecutionerHelmetOff", dropPosition);
	}
}

void IceStatueExecutionerZombie::ConfigureHelmetFollower()
{
	if (mHelmetFollowerConfigured || !mAnimator
		|| !mAnimator->HasTrack("anim_head1")) return;
	const Texture* helmet = ResourceManager::GetInstance().GetTexture(
		GetHelmetTextureKey(), false);
	if (!helmet) return;
	// 防具只有贴图状态，不创建第二条时间轴；完整复用头部逐帧插值可避免二次插值抖动和锚点漂移。
	mAnimator->SetTrackFollowerImage("anim_head1", kHelmetFollowerSlot, helmet,
		kHelmetFollowerOffsetX, kHelmetFollowerOffsetY,
		kHelmetFollowerScale, kHelmetFollowerScale,
		/*drawAfterAllTracks=*/true,
		/*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true);
	mAnimator->SetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot, true);
	mHelmetFollowerConfigured = true;
}

void IceStatueExecutionerZombie::SyncHelmetPresentation() const
{
	if (mHelmType == HelmType::HELMTYPE_NONE || mHelmHealth <= 0 || !mHasHead
		|| mHelmetStage == ArmorBrokenState::NONE || mIsDead || mIsDying) {
		HideHelmetFollower();
		return;
	}
	if (!mHelmetFollowerConfigured) {
		const_cast<IceStatueExecutionerZombie*>(this)->ConfigureHelmetFollower();
	}
	if (!mHelmetFollowerConfigured || !mAnimator) return;
	mAnimator->SetTrackFollowerImage("anim_head1", kHelmetFollowerSlot,
		ResourceManager::GetInstance().GetTexture(GetHelmetTextureKey()),
		kHelmetFollowerOffsetX, kHelmetFollowerOffsetY,
		kHelmetFollowerScale, kHelmetFollowerScale,
		/*drawAfterAllTracks=*/true,
		/*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true);
	mAnimator->SetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot, true);
}

void IceStatueExecutionerZombie::HideHelmetFollower() const
{
	if (mAnimator && mHelmetFollowerConfigured) {
		mAnimator->SetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot, false);
	}
}

bool IceStatueExecutionerZombie::HasHelmetFollower() const
{
	return mHelmetFollowerConfigured && mAnimator
		&& mAnimator->GetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot);
}

bool IceStatueExecutionerZombie::DoesHelmetFollowerInheritOverlayEffect() const
{
	return mHelmetFollowerConfigured && mAnimator
		&& mAnimator->GetTrackFollowerInheritsOverlayEffect(
			"anim_head1", kHelmetFollowerSlot);
}

bool IceStatueExecutionerZombie::IsHelmetFollowerGlowing() const
{
	return mHelmetFollowerConfigured && mAnimator
		&& mAnimator->GetTrackFollowerGlowEffectEnabled(
			"anim_head1", kHelmetFollowerSlot);
}

void IceStatueExecutionerZombie::ApplyExecutionerTextures() const
{
	if (!mAnimator) return;
	mAnimator->SetTrackVisible("Zombie_ladder_1", false);
	if (const Texture* maul = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_ICE_EXECUTIONER_MAUL, false)) {
		mAnimator->SetTrackImage("Zombie_ladder_hammer", maul);
	}
	if (const Texture* body = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_BODY, false)) {
		mAnimator->SetTrackImage("Zombie_body", body);
	}
	if (const Texture* tie = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_TOOLSTRAP, false)) {
		mAnimator->SetTrackImage("Zombie_tie", tie);
	}
}

bool IceStatueExecutionerZombie::OwnsIceSealFor(int plantID) const
{
	return mExecutionPhase == ExecutionPhase::EXECUTING
		&& mExecutionTargetPlantID == plantID && !mExecutionUsed;
}

void IceStatueExecutionerZombie::FinalizeIceSealLoad()
{
	if (mExecutionPhase != ExecutionPhase::EXECUTING) return;
	Plant* target = ResolveExecutionTarget();
	if (!CanOwnExecution() || !target
		|| target->GetIceSealOwnerZombieID() != mZombieID
		|| !mBoard->IsPlantFootprintFrozen(
			target->mPlantType, target->mRow, target->mColumn)) {
		AbortExecution(!CanOwnExecution(), true);
		return;
	}
	mExecutionProgress = std::clamp(mExecutionProgress, 0,
		std::max(0, target->GetIceExecutionRequiredStrikeCount() - 1));
	if (GetCurrentTrackName() != "anim_placeladder") BeginStrike(0.0f);
}

int IceStatueExecutionerZombie::GetCurrentRequiredStrikeCount() const
{
	const Plant* target = ResolveExecutionTarget();
	return target ? target->GetIceExecutionRequiredStrikeCount()
		: kDefaultRequiredExecutionProgress;
}

void IceStatueExecutionerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["executionPhase"] = static_cast<int>(mExecutionPhase);
	j["executionTargetPlantID"] = mExecutionTargetPlantID;
	j["executionProgress"] = mExecutionProgress;
	j["executionUsed"] = mExecutionUsed;
	j["walkVelocity"] = mWalkVelocity;
	j["helmetStage"] = static_cast<int>(mHelmetStage);
}

void IceStatueExecutionerZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("executionPhase", 0), 0,
		static_cast<int>(ExecutionPhase::SPENT));
	mExecutionPhase = static_cast<ExecutionPhase>(phase);
	mExecutionTargetPlantID = j.value("executionTargetPlantID", NULL_PLANT_ID);
	mExecutionProgress = std::clamp(j.value("executionProgress", 0),
		0, kMaximumSerializedExecutionProgress);
	mExecutionUsed = j.value("executionUsed",
		mExecutionPhase == ExecutionPhase::SPENT);
	mWalkVelocity = std::clamp(j.value("walkVelocity", 0.30f), 0.23f, 0.37f);
	mHelmetStage = static_cast<ArmorBrokenState>(std::clamp(
		j.value("helmetStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)),
		static_cast<int>(ArmorBrokenState::NONE),
		static_cast<int>(ArmorBrokenState::REALLY_BROKEN)));
	if (mExecutionPhase != ExecutionPhase::EXECUTING) {
		mExecutionTargetPlantID = NULL_PLANT_ID;
		mExecutionProgress = 0;
		if (mExecutionUsed) mExecutionPhase = ExecutionPhase::SPENT;
	}
	else if (!HasHead() || !HasArm() || IsMindControlled()) {
		mExecutionPhase = ExecutionPhase::SPENT;
		mExecutionTargetPlantID = NULL_PLANT_ID;
		mExecutionProgress = 0;
		mExecutionUsed = true;
	}
	ApplyExecutionerTextures();
	SyncHelmetPresentation();
}

void IceStatueExecutionerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	ApplyExecutionerTextures();
	if (!mHasArm && mAnimator) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackImage("Zombie_ladder_outerarm_upper",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_OUTERARM_UPPER2));
	}
	if (!mHasHead && mAnimator) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
	}
	SyncHelmetPresentation();
}

bool IceStatueExecutionerZombie::SetExecutionStateForTesting(
	Plant* target, int progress)
{
	if (!target || !CanOwnExecution()) return false;
	if (mExecutionPhase == ExecutionPhase::EXECUTING) {
		AbortExecution(false, false);
	}
	if (!target->BeginIceSeal(mZombieID)) return false;
	mExecutionPhase = ExecutionPhase::EXECUTING;
	mExecutionTargetPlantID = target->mPlantID;
	mExecutionProgress = std::clamp(progress, 0,
		std::max(0, target->GetIceExecutionRequiredStrikeCount() - 1));
	mExecutionUsed = false;
	BeginStrike(0.0f);
	return true;
}

bool IceStatueExecutionerZombie::AttemptExecutionForTesting(Plant* target)
{
	if (!target || mExecutionPhase != ExecutionPhase::READY) return false;
	return BeginExecution(*target);
}
