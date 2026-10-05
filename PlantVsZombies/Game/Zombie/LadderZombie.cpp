#include "LadderZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

namespace {
	constexpr float kResourceFps = 12.0f; // Zombie_ladder.reanim 的资源帧率
	constexpr float kCSharpTicksPerSecond = 47.0f; // C# UpdateAnimSpeed 使用的逻辑 tick 频率
	constexpr float kWalkFrameCount = 47.0f; // anim_ladderwalk/anim_walk 的帧数
	constexpr float kWalkGroundDistance = 66.0f; // 两套行走轨道的 _ground 总位移，单位 px
	constexpr float kGroundRootMotionScale = 12.0f; // 本项目根轨逐帧位移换算为每秒位移的倍率
	constexpr float kCarryingVelocityMin = 0.79f; // C# 携梯 mVelX 随机下界，单位 px/tick
	constexpr float kCarryingVelocityMax = 0.81f; // C# 携梯 mVelX 随机上界，单位 px/tick
	constexpr float kNormalVelocityMin = 0.23f; // C# 卸梯普通 mVelX 随机下界，单位 px/tick
	constexpr float kNormalVelocityMax = 0.37f; // C# 卸梯普通 mVelX 随机上界，单位 px/tick
	constexpr float kEatClipSpeed = 3.0f; // C# 扶梯僵尸啃食 36FPS 相对资源 12FPS 的倍率
	constexpr float kPlacementClipSpeed = 2.0f; // C# 24FPS 相对资源 12FPS 的轨道倍率
	constexpr float kMagnetDestinationX = 30.0f; // 携带扶梯吸附到磁力菇的局部 X
	constexpr float kMagnetDestinationY = 0.0f; // 携带扶梯吸附到磁力菇的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f; // 离体扶梯落点随机扰动，单位 px
	constexpr float kLimbVolume = 0.25f; // 扶梯僵尸断肢与掉头音量
}

float LadderZombie::WalkClipFromVelocity(float velocity)
{
	// C#：animRate = mVelX * frameCount / groundDistance * 47；再除以资源 FPS 得 clip。
	return velocity * kWalkFrameCount * kCSharpTicksPerSecond
		/ (kWalkGroundDistance * kResourceFps);
}

const char* LadderZombie::GetPhaseName() const
{
	switch (mPhase) {
	case Phase::CARRYING: return "CARRYING";
	case Phase::PLACING: return "PLACING";
	case Phase::NORMAL: return "NORMAL";
	}
	return "CARRYING";
}

void LadderZombie::SetupZombie()
{
	mBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_LADDER).body;
	mBodyMaxHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_LADDER).body;
	mShieldType = ShieldType::SHIELDTYPE_LADDER;
	mShieldHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_LADDER).shield;
	mShieldMaxHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_LADDER).shield;
	mShieldStage = ArmorBrokenState::NO_BROKEN;
	mPhase = Phase::CARRYING;
	mWalkVelocity = GameRandom::Range(kCarryingVelocityMin, kCarryingVelocityMax);
	mSpeed = kGroundRootMotionScale;

	if (mIsPreview) {
		PlayTrack("anim_idle");
		ApplyShieldImage();
		return;
	}

	RegisterFrameEvents();
	PlayWalkAnimation(0.0f);
	ApplyShieldImage();
}

/** 注册主人确认的携梯/卸梯啃食帧和死亡终点；帧号不做减一换算。 */
void LadderZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(85, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(194, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(131, [this]() { Die(); });
}

bool LadderZombie::IsLadderTarget(const Plant* plant) const
{
	if (!plant || !plant->IsActive() || plant->mRow != mRow || !mBoard
		|| mBoard->HasLadderAt(plant->mRow, plant->mColumn)) {
		return false;
	}
	return plant->SupportsLadderPlacement();
}

Plant* LadderZombie::ResolvePlacementTarget() const
{
	if (!mBoard || mPlacementRow < 0 || mPlacementColumn < 0) return nullptr;
	Plant* pumpkin = mBoard->GetPumpkinAt(mPlacementRow, mPlacementColumn);
	if (IsLadderTarget(pumpkin)) return pumpkin;
	Plant* normal = mBoard->GetNormalPlantAt(mPlacementRow, mPlacementColumn);
	return IsLadderTarget(normal) ? normal : nullptr;
}

void LadderZombie::BeginPlacement(Plant& plant)
{
	if (mPhase != Phase::CARRYING || !mHasHead || mIsDying || mIsDead
		|| mIsMindControlled || !IsActive() || !IsLadderTarget(&plant)) return;
	if (mIsEating && mEatPlantID != NULL_PLANT_ID) {
		StopEatingInvalidPlantTarget(0.0f);
	}
	mPhase = Phase::PLACING;
	mPlacementRow = plant.mRow;
	mPlacementColumn = plant.mColumn;
	PlayTrack("anim_placeladder", kPlacementClipSpeed, 0.0f);
	mAnimator->Play(PlayState::PLAY_ONCE);
}

void LadderZombie::AbortPlacement(bool restoreWalkAnimation)
{
	if (mPhase != Phase::PLACING) return;

	// 断头、死亡或旧存档可在一次性动作完成前使其失效；移动锁不能只由完成分支释放。
	mPhase = mShieldType == ShieldType::SHIELDTYPE_NONE ? Phase::NORMAL : Phase::CARRYING;
	mPlacementRow = -1;
	mPlacementColumn = -1;
	if (restoreWalkAnimation && mAnimator && !mIsDying && !mIsDead) {
		PlayWalkAnimation(0.0f);
	}
}

void LadderZombie::StartEat(ColliderComponent* other)
{
	if (!other || mIsPreview || !mHasHead || mIsDying || mIsDead
		|| mPhase == Phase::PLACING) return;
	if (other->GetGameObject()->GetObjectType() == ObjectType::OBJECT_PLANT
		&& mPhase == Phase::CARRYING) {
		if (auto* plant = dynamic_cast<Plant*>(other->GetGameObject())) {
			if (mBoard) {
				if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
					plant = top;
				}
			}
			if (IsLadderTarget(plant)) {
				BeginPlacement(*plant);
				return;
			}
		}
	}

	const bool wasEating = mIsEating;
	Zombie::StartEat(other);
	if (!wasEating && mIsEating) {
		PlayTrack(mPhase == Phase::CARRYING ? "anim_laddereat" : "anim_eat",
			kEatClipSpeed, 0.2f);
	}
}

void LadderZombie::ZombieUpdate(float)
{
	if (mPhase == Phase::PLACING
		&& (mIsMindControlled || !mHasHead || mIsDying || mIsDead)) {
		AbortPlacement(!mIsDying && !mIsDead);
		return;
	}
	if (mPhase != Phase::PLACING || !mAnimator || mAnimator->IsPlaying()) {
		return;
	}

	Plant* target = ResolvePlacementTarget();
	if (!target) {
		mPhase = Phase::CARRYING;
		mPlacementRow = -1;
		mPlacementColumn = -1;
		PlayWalkAnimation(0.0f);
		return;
	}

	mBoard->AddLadder(target->mRow, target->mColumn, GetPlacedLadderStyle());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LADDER_ZOMBIE);
	BeginLadderClimb(target->mColumn);
	if (RetainsLadderAfterPlacement()) {
		// 无限搭梯只免除“放置即消耗”；梯子被伤害打碎或被磁力菇吸走仍会失去能力载体。
		mPhase = Phase::CARRYING;
		mPlacementRow = -1;
		mPlacementColumn = -1;
		PlayWalkAnimation(0.0f);
	}
	else {
		DetachLadder(false);
	}
}

void LadderZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase == Phase::PLACING) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void LadderZombie::PlayWalkAnimation(float blendTime)
{
	PlayTrack(mPhase == Phase::CARRYING ? "anim_ladderwalk" : "anim_walk",
		WalkClipFromVelocity(mWalkVelocity), blendTime);
}

void LadderZombie::ApplyShieldImage() const
{
	if (!mAnimator || mShieldType != ShieldType::SHIELDTYPE_LADDER) return;
	mAnimator->SetTrackImage("Zombie_ladder_1",
		ResourceManager::GetInstance().GetTexture(GetShieldTextureKey(mShieldStage)));
}

const std::string& LadderZombie::GetShieldTextureKey(ArmorBrokenState stage) const
{
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1_DAMAGE1;
	}
	if (stage == ArmorBrokenState::REALLY_BROKEN) {
		return ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1_DAMAGE2;
	}
	return ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1;
}

const std::string& LadderZombie::GetBrokenArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_OUTERARM_UPPER2;
}

void LadderZombie::CheckShieldImage()
{
	if (mShieldType != ShieldType::SHIELDTYPE_LADDER) return;
	mShieldStage = mShieldHealth >= static_cast<int64_t>(mShieldMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mShieldHealth >= mShieldMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	ApplyShieldImage();
}

void LadderZombie::DetachLadder(bool emitParticle)
{
	if (mShieldType != ShieldType::SHIELDTYPE_LADDER) return;
	const Vector particlePosition = GetTrackWorldPosition("Zombie_ladder_1");
	Zombie::ShieldDrop();
	mShieldHealth = 0;
	mShieldStage = ArmorBrokenState::NONE;
	mPhase = Phase::NORMAL;
	mPlacementRow = -1;
	mPlacementColumn = -1;
	mWalkVelocity = GameRandom::Range(kNormalVelocityMin, kNormalVelocityMax);
	mSpeed = kGroundRootMotionScale;
	if (mIsEating) PlayTrack("anim_eat", kEatClipSpeed, 0.2f);
	else PlayWalkAnimation(0.0f);
	if (emitParticle && g_particleSystem) {
		g_particleSystem->EmitEffect(GetLadderDropEffectName(), particlePosition);
	}
}

void LadderZombie::ShieldDrop()
{
	DetachLadder(true);
}

bool LadderZombie::HasMagneticItem() const
{
	return mShieldType == ShieldType::SHIELDTYPE_LADDER;
}

bool LadderZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = GetShieldTextureKey(mShieldStage);
	item.worldPosition = GetTrackWorldPosition("Zombie_ladder_1");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = 0.8f;
	DetachLadder(false);
	return true;
}

void LadderZombie::ApplyBrokenArmPresentation() const
{
	if (!mAnimator) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_ladder_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			GetBrokenArmTextureKey()));
}

void LadderZombie::ArmDrop()
{
	if (!mHasArm) return;
	ApplyBrokenArmPresentation();
	if (g_particleSystem) g_particleSystem->EmitEffect("LadderArmOff", GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

void LadderZombie::HeadDrop()
{
	if (!mHasHead) return;
	// 如果受伤恰好发生在放梯期，立即恢复行走，避免无头分支永远不结算动画。
	AbortPlacement(true);
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieLadderHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

void LadderZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	ApplyShieldImage();
	if (!mHasArm) ApplyBrokenArmPresentation();
	if (!mHasHead && mAnimator) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
	}
}

void LadderZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (mAnimator && mHasArm) {
		mAnimator->SetTrackImage("Zombie_ladder_outerarm_upper", nullptr);
	}
}

void LadderZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["shieldStage"] = static_cast<int>(mShieldStage);
	j["placementRow"] = mPlacementRow;
	j["placementColumn"] = mPlacementColumn;
	j["walkVelocity"] = mWalkVelocity;
}

void LadderZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::NORMAL)));
	mShieldStage = static_cast<ArmorBrokenState>(std::clamp(
		j.value("shieldStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)),
		static_cast<int>(ArmorBrokenState::NONE),
		static_cast<int>(ArmorBrokenState::REALLY_BROKEN)));
	mPlacementRow = j.value("placementRow", -1);
	mPlacementColumn = j.value("placementColumn", -1);
	const bool carrying = mPhase != Phase::NORMAL;
	const float velocityMin = carrying ? kCarryingVelocityMin : kNormalVelocityMin;
	const float velocityMax = carrying ? kCarryingVelocityMax : kNormalVelocityMax;
	mWalkVelocity = std::clamp(j.value("walkVelocity",
		(velocityMin + velocityMax) * 0.5f), velocityMin, velocityMax);
	// 旧档的 mSpeed 是合并后的根运动倍率；新模型固定根轨换算，只让 clip 随 C# mVelX 改变。
	mSpeed = kGroundRootMotionScale;
	// 修复旧档可保留的“无头/垂死 + PLACING”坏状态；RestoreAnimState 已在此前执行。
	if (mPhase == Phase::PLACING
		&& (mIsMindControlled || !mHasHead || mIsDying || mIsDead)) {
		AbortPlacement(!mIsDying && !mIsDead);
	}
	ApplyShieldImage();
}

ZombieMovementRules::BirthProfile LadderZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.clip="anim_ladderwalk";
	p.alternative=nullptr;
	p.rootMinimum=p.rootMaximum=kGroundRootMotionScale;
	p.animationMinimum=WalkClipFromVelocity(kCarryingVelocityMin);
	p.animationMaximum=WalkClipFromVelocity(kCarryingVelocityMax);
	p.phaseDependent=true;
	return p;
}

float LadderZombie::GetMineSimulationMoveSpeed() const
{
	return GetSimulationRootMoveSpeed(mShieldType==ShieldType::SHIELDTYPE_LADDER ? "anim_ladderwalk" : "anim_walk",WalkClipFromVelocity(mWalkVelocity));
}
