#include "DiggerZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Cell.h"
#include "../GameObjectManager.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
	constexpr int kBodyHealth = 270;                         // 原版矿工本体生命值
	constexpr int kHardhatHealth = 100;                      // 原版矿工安全帽生命值
	constexpr float kResourceFps = 16.0f;                    // Zombie_digger.reanim 资源帧率
	constexpr float kCSharpTicksPerSecond = 100.0f;          // 原版 mVelX 每厘秒位移换算基准
	constexpr float kTunnelVelocityMin = 0.66f;              // 原版地下速度随机下界，单位 px/tick
	constexpr float kTunnelVelocityMax = 0.68f;              // 原版地下速度随机上界，单位 px/tick
	constexpr float kBackwardWalkVelocity = 0.12f;           // 原版持镐折返速度，单位 px/tick
	constexpr float kNormalWalkVelocityMin = 0.23f;          // 原版无镐步行速度随机下界，单位 px/tick
	constexpr float kNormalWalkVelocityMax = 0.37f;          // 原版无镐步行速度随机上界，单位 px/tick
	constexpr float kGroundTrackDistance = 72.5f;            // anim_walk 的 _ground 总水平位移，单位 px
	constexpr float kGroundTrackIntervals = 36.0f;           // anim_walk 从 18 到 54 的根运动间隔数
	constexpr float kDeathBaseClip = 1.3f;                   // 基类播放死亡轨道的 clip 速度
	constexpr float kDeathFps = 18.0f;                       // 原版矿工死亡动画帧率
	constexpr float kAbilityAnimMultiplier =
		kDeathFps / (kResourceFps * kDeathBaseClip);          // 令基类死亡轨精确落在 18 FPS
	constexpr float kDigClip =
		(12.0f / kResourceFps) / kAbilityAnimMultiplier;      // 原版 anim_dig 12 FPS
	constexpr float kDrillClip =
		(20.0f / kResourceFps) / kAbilityAnimMultiplier;      // 原版 anim_drill 20 FPS
	constexpr float kLandingClip =
		(12.0f / kResourceFps) / kAbilityAnimMultiplier;      // 原版 anim_landing/anim_dizzy 12 FPS
	constexpr float kEatClip =
		(20.0f / kResourceFps) / kAbilityAnimMultiplier;      // 原版 anim_eat 20 FPS
	constexpr float kRiseDuration = 1.30f;                  // 原版出土阶段 130cs
	constexpr float kRiseCurveSwitch = 0.40f;               // 原版剩余 40cs 时切换回落曲线
	constexpr float kLandingStartRemaining = 0.30f;         // 原版剩余 30cs 时开始落地动画
	constexpr float kRiseDepth = -120.0f;                   // 原版初始 altitude，负值表示地下
	constexpr float kRiseOvershoot = 20.0f;                 // 原版出土后向上越过地面高度
	constexpr float kPauseWithoutPickaxeDuration = 2.0f;    // 原版丢镐地下停顿 200cs
	constexpr float kSurpriseDelay = 0.5f;                  // 原版停顿 50cs 后显示问号
	constexpr float kDizzyDuration = 3.5f;                  // anim_dizzy 12 FPS 播放两轮
	constexpr float kEmergenceOffsetFromFirstCell = 30.0f;  // 原版出土线位于首格起点右侧约 30px
	constexpr float kTunnelDustInterval = 0.12f;            // 移动尘土短爆发间隔，单位秒
	constexpr float kDiggerFlipPivotX = 45.0f;              // 原版反向绘制约 90px 补偿的镜像轴
	constexpr float kGroundClipMargin = 38.0f;              // 地下出土裁剪底边相对逻辑行 Y，单位 px
	constexpr float kLoopVolume = 0.42f;                    // 地下挖掘循环声音量
	constexpr float kRiseVolume = 0.55f;                    // 出土一次性音效音量
	constexpr float kLimbVolume = 0.25f;                    // 断肢、掉头与掉帽音量
	constexpr int kCharredRemovalFrameWithPickaxe = 36;     // 主人确认的 anim_crumble 灰烬回收帧
	constexpr int kCharredRemovalFrameWithoutPickaxe = 73;  // 主人确认的 anim_crumble_noaxe 灰烬回收帧
	constexpr float kMagnetDestinationX = 45.0f;             // 镐子吸到磁力菇旁的局部 X 偏移，单位 px
	constexpr float kMagnetDestinationY = 15.0f;             // 镐子吸到磁力菇旁的局部 Y 偏移，单位 px
	constexpr float kMagnetDestinationJitter = 10.0f;        // 镐子落点随机扰动半径，单位 px

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

	class DiggerOneShotVisual final : public AnimatedObject {
	public:
		DiggerOneShotVisual(Board* board, const Vector& position, AnimationType animType,
			std::string track, float clipSpeed, bool flip = false, int removalFrame = -1,
			int row = -1)
			: AnimatedObject(ObjectType::OBJECT_PARTICLE, board, position, animType,
				ColliderType::BOX, Vector::zero(), Vector::zero(), 1.0f,
				"DiggerOneShotVisual", true)
			, mTrack(std::move(track))
			, mClipSpeed(clipSpeed)
			, mFlip(flip)
			, mRemovalFrame(removalFrame)
		{
			SetSortingKey(row);
		}

		/** 播放指定分层轨道，依靠 AnimatedObject 的非循环结束自动回收。 */
		void Start() override
		{
			AnimatedObject::Start();
			// AnimatedObject 只在自身循环类型为一次性时执行自动回收；轨道状态本身不够。
			SetLoopType(PlayState::PLAY_ONCE);
			if (mRemovalFrame >= 0 && mAnimator) {
				mAnimator->AddFrameEvent(mRemovalFrame, [this]() {
					GameObjectManager::GetInstance().DestroyGameObject(this);
					});
			}
			PlayTrackOnce(mTrack, "", mClipSpeed);
			if (mAnimator) mAnimator->SetFlipX(mFlip, kDiggerFlipPivotX);
		}

	private:
		std::string mTrack;
		float mClipSpeed = 1.0f;
		bool mFlip = false;
		int mRemovalFrame = -1;
	};
}

int DiggerZombie::sLoopSoundUsers = 0;

DiggerZombie::~DiggerZombie()
{
	ReleaseLoopSound();
}

void DiggerZombie::SetupZombie()
{
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_DIGGER;
	mHelmHealth = kHardhatHealth;
	mHelmMaxHealth = kHardhatHealth;
	mHelmStage = ArmorBrokenState::NO_BROKEN;
	mSpeed = kResourceFps;
	mBaseVisualOffsetY = mVisualOffset.y;
	mTunnelVelocity = mIsPreview
		? (kTunnelVelocityMin + kTunnelVelocityMax) * 0.5f
		: GameRandom::Range(kTunnelVelocityMin, kTunnelVelocityMax);
	mWalkVelocity = mIsPreview
		? (kNormalWalkVelocityMin + kNormalWalkVelocityMax) * 0.5f
		: GameRandom::Range(kNormalWalkVelocityMin, kNormalWalkVelocityMax);
	mHasPickaxe = true;
	mPhase = Phase::TUNNELING;
	mAltitude = 0.0f;

	if (mIsPreview) {
		PlayTrack("anim_idle");
		return;
	}

	RegisterFrameEvents();
	PlayTrack("anim_dig", kDigClip);
	ApplyPhasePresentation();
}

/** 注册主人给出的矿工专属全局帧号；不得套用普通僵尸时间线。 */
void DiggerZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(66, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(81, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(127, [this]() { Die(); });
}

void DiggerZombie::ZombieUpdate(float scaledTime)
{
	if (mIsDying || mIsDead || scaledTime <= 0.0f) {
		UpdateFacing();
		return;
	}

	switch (mPhase) {
	case Phase::TUNNELING:
		mTunnelDustTimer -= scaledTime;
		while (mTunnelDustTimer <= 0.0f) {
			EmitTunnelDust();
			mTunnelDustTimer += kTunnelDustInterval;
		}
		if (GetPosition().x < CELL_INITALIZE_POS_X + kEmergenceOffsetFromFirstCell) {
			BeginRising(true);
		}
		break;

	case Phase::RISING:
		UpdateRising(scaledTime, true);
		break;

	case Phase::STUNNED:
		mPhaseRemaining = std::max(0.0f, mPhaseRemaining - scaledTime);
		if (mPhaseRemaining <= 0.0f) OnPickaxeStunFinished();
		break;

	case Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE: {
		const float oldRemaining = mPhaseRemaining;
		mPhaseRemaining = std::max(0.0f, mPhaseRemaining - scaledTime);
		const float surpriseRemaining =
			kPauseWithoutPickaxeDuration - kSurpriseDelay;
		if (!mSurpriseShown && oldRemaining > surpriseRemaining
			&& mPhaseRemaining <= surpriseRemaining) {
			mSurpriseShown = true;
			GameObjectManager::GetInstance().CreateGameObjectImmediate<DiggerOneShotVisual>(
				LAYER_GAME_BULLET, mBoard,
				GetStableVisualOrigin() + Vector(23.0f, 93.0f),
				AnimationType::ANIM_ZOMBIE_SURPRISE, "Layer 1", 1.0f);
		}
		if (mPhaseRemaining <= 0.0f) BeginRising(false);
		break;
	}

	case Phase::RISING_WITHOUT_PICKAXE:
		UpdateRising(scaledTime, false);
		break;

	case Phase::WALKING_WITH_PICKAXE:
	case Phase::WALKING_WITHOUT_PICKAXE:
		break;
	}
	// 通用介质状态或读档恢复不得让出土中的矿工提前出现地面投影。
	if (auto* shadow = GetShadow()) shadow->SetVisible(IsInteractivePhase());
	UpdateFacing();
}

void DiggerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || scaledDelta <= 0.0f) return;
	if (mPhase == Phase::TUNNELING) {
		transform->Translate(-mTunnelVelocity * kCSharpTicksPerSecond * scaledDelta, 0.0f);
		return;
	}
	if (mPhase == Phase::WALKING_WITH_PICKAXE
		|| mPhase == Phase::WALKING_WITHOUT_PICKAXE) {
		Zombie::ZombieMove(scaledDelta, transform);
	}
}

void DiggerZombie::BeginRising(bool withPickaxe)
{
	StopEatingForTransition();
	ReleaseLoopSound();
	mPhase = withPickaxe ? Phase::RISING : Phase::RISING_WITHOUT_PICKAXE;
	mPhaseRemaining = kRiseDuration;
	mAltitude = kRiseDepth;
	mLandingStarted = false;
	ApplyAltitude();
	if (withPickaxe) {
		PlayTrack("anim_drill", kDrillClip);
	}
	else {
		PlayTrack("anim_landing", kLandingClip);
		mAnimator->Pause();
	}
	EmitRiseEffects(withPickaxe);
	ApplyPhasePresentation();
}

void DiggerZombie::UpdateRising(float scaledTime, bool withPickaxe)
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
		PlayTrack("anim_landing", kLandingClip, 0.2f);
	}
	if (mPhaseRemaining > 0.0f) return;

	mAltitude = 0.0f;
	ApplyAltitude();
	if (withPickaxe && mHasPickaxe) {
		mPhase = Phase::STUNNED;
		mPhaseRemaining = kDizzyDuration;
		PlayTrack("anim_dizzy", kLandingClip, 0.1f);
		ApplyPhasePresentation();
	}
	else {
		BeginStableWalk(false);
	}
}

void DiggerZombie::BeginStableWalk(bool withPickaxe)
{
	mPhase = withPickaxe
		? Phase::WALKING_WITH_PICKAXE
		: Phase::WALKING_WITHOUT_PICKAXE;
	mPhaseRemaining = 0.0f;
	mAltitude = 0.0f;
	ApplyAltitude();
	PlayWalkAnimation(0.2f);
	ApplyPhasePresentation();
}

void DiggerZombie::LosePickaxe()
{
	if (!mHasPickaxe || mIsDead) return;
	const Phase previousPhase = mPhase;
	if (mPhase == Phase::TUNNELING) {
		mPhase = Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE;
		mPhaseRemaining = kPauseWithoutPickaxeDuration;
		mSurpriseShown = false;
		mAnimator->Pause();
		ReleaseLoopSound();
	}
	mHasPickaxe = false;
	mAnimator->SetTrackVisible("Zombie_digger_pickaxe", false);
	mAnimator->SetTrackVisible("Zombie_digger_dirt", false);
	OnPickaxeLost(previousPhase);
	UpdateFacing();
}

bool DiggerZombie::HasMagneticItem() const
{
	return mHasPickaxe && (mPhase == Phase::TUNNELING
		|| mPhase == Phase::STUNNED
		|| mPhase == Phase::WALKING_WITH_PICKAXE);
}

/** 先记录镐子的视觉状态，再复用正式丢镐状态机处理地下停顿或爆破取消。 */
bool DiggerZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = GetMagneticPickaxeImageKey();
	item.worldPosition = GetTrackWorldPosition("Zombie_digger_pickaxe");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter,
			kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter,
			kMagnetDestinationJitter));
	LosePickaxe();
	return true;
}

void DiggerZombie::PlayWalkAnimation(float blendTime)
{
	const float velocity = mPhase == Phase::WALKING_WITH_PICKAXE
		? GetPickaxeWalkVelocity() : mWalkVelocity;
	PlayTrack("anim_walk", GetWalkClipSpeed(velocity), blendTime);
}

void DiggerZombie::OnPickaxeStunFinished()
{
	BeginStableWalk(mHasPickaxe);
}

void DiggerZombie::OnPickaxeLost(Phase)
{
}

float DiggerZombie::GetPickaxeWalkVelocity() const
{
	return kBackwardWalkVelocity;
}

const std::string& DiggerZombie::GetFullHardhatTexture() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_HARDHAT;
}

const std::string& DiggerZombie::GetDamagedHardhatTexture(bool heavilyDamaged) const
{
	return heavilyDamaged
		? ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_HARDHAT3
		: ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_HARDHAT2;
}

const std::string& DiggerZombie::GetBrokenOuterArmTexture() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_OUTERARM_UPPER2;
}

const std::string& DiggerZombie::GetMagneticPickaxeImageKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_PICKAXE;
}

const char* DiggerZombie::GetHelmDropEffectName() const
{
	return "ZombieHeadLight";
}

const char* DiggerZombie::GetArmDropEffectName() const
{
	return "ZombieDiggerArmOff";
}

void DiggerZombie::OnStartEating()
{
	PlayTrack("anim_eat", kEatClip, 0.2f);
	UpdateFacing();
}

void DiggerZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::WALKING_WITH_PICKAXE
		&& mPhase != Phase::WALKING_WITHOUT_PICKAXE) {
		return;
	}
	Zombie::StartEat(other);
}

void DiggerZombie::StopEatingForTransition()
{
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

void DiggerZombie::ApplyPhasePresentation()
{
	ApplyAltitude();
	const bool interactive = IsInteractivePhase() && !mIsDying && !mIsDead;
	if (mCollider) mCollider->mEnabled = interactive;
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(interactive);
	}
	// reanim 自带 _ground 轨道不参与绘制，统一使用 ShadowComponent 管理出现时机。
	mAnimator->SetTrackVisible("_ground", false);
	if (!mHasPickaxe) {
		mAnimator->SetTrackVisible("Zombie_digger_pickaxe", false);
		mAnimator->SetTrackVisible("Zombie_digger_dirt", false);
	}
	UpdateFacing();
}

void DiggerZombie::ApplyAltitude()
{
	mVisualOffset.y = mBaseVisualOffsetY - mAltitude;
}

void DiggerZombie::UpdateFacing()
{
	if (mAnimator && !mIsPreview) {
		mAnimator->SetFlipX(IsMovingRight(), kDiggerFlipPivotX);
	}
}

void DiggerZombie::EmitTunnelDust()
{
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("DiggerTunnel",
			GetStableVisualOrigin() + Vector(60.0f, 100.0f));
	}
}

void DiggerZombie::EmitRiseEffects(bool playWakeup)
{
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DIRT_RISE, kRiseVolume);
	if (playWakeup) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_WAKEUP, kRiseVolume);
	}
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("DiggerRise",
			GetStableVisualOrigin() + Vector(60.0f, 118.0f));
	}
	GameObjectManager::GetInstance().CreateGameObjectImmediate<DiggerOneShotVisual>(
		LAYER_GAME_BULLET, mBoard,
		GetStableVisualOrigin() + Vector(13.0f, 97.0f),
		AnimationType::ANIM_DIGGER_RISING_DIRT,
		"Digger_rising_dirt", 24.0f / 12.0f);
}

Vector DiggerZombie::GetStableVisualOrigin() const
{
	return GetPosition() + Vector(mVisualOffset.x, mBaseVisualOffsetY);
}

float DiggerZombie::GetWalkClipSpeed(float velocity) const
{
	const float pixelsPerSecond = velocity * kCSharpTicksPerSecond;
	const float groundPixelsPerFrame = kGroundTrackDistance / kGroundTrackIntervals;
	return pixelsPerSecond
		/ (groundPixelsPerFrame * kResourceFps * kAbilityAnimMultiplier);
}

bool DiggerZombie::IsInteractivePhase() const
{
	return mPhase == Phase::STUNNED
		|| mPhase == Phase::WALKING_WITH_PICKAXE
		|| mPhase == Phase::WALKING_WITHOUT_PICKAXE;
}

bool DiggerZombie::IsMovingRight() const
{
	if (mIsMindControlled) return true;
	if (mIsDying) return mHasPickaxe;
	return mPhase == Phase::RISING
		|| mPhase == Phase::STUNNED
		|| mPhase == Phase::WALKING_WITH_PICKAXE;
}

bool DiggerZombie::CanTriggerGameOver() const
{
	return !mIsMindControlled
		&& mPhase == Phase::WALKING_WITHOUT_PICKAXE;
}

bool DiggerZombie::CanBeTargetedByProjectile(bool targetsFlying) const
{
	return !targetsFlying && IsInteractivePhase();
}

bool DiggerZombie::CanBeCharmed() const
{
	return mPhase == Phase::WALKING_WITH_PICKAXE
		|| mPhase == Phase::WALKING_WITHOUT_PICKAXE;
}

bool DiggerZombie::CanBeChilled() const
{
	return IsInteractivePhase() && Zombie::CanBeChilled();
}

bool DiggerZombie::CanBeFrozen() const
{
	return IsInteractivePhase();
}

bool DiggerZombie::CanBeCharred() const
{
	return IsInteractivePhase() && Zombie::CanBeCharred();
}

bool DiggerZombie::TryGetDrawClipBottom(float& clipBottom) const
{
	if (!mIsPreview && mAltitude < 0.0f) {
		const float groundY = mBoard
			? mBoard->GetZombieSpawnY(mRow, GetPosition().x)
			: GetPosition().y;
		clipBottom = static_cast<float>(static_cast<int>(
			std::lround(groundY + kGroundClipMargin)));
		return true;
	}
	return Zombie::TryGetDrawClipBottom(clipBottom);
}

void DiggerZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("Zombie_digger_hardhat", true);
	// 治疗允许伤势阶段向上恢复；外观必须完全由当前生命派生，不能只单向破损。
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	const std::string& textureKey = mHelmStage == ArmorBrokenState::NO_BROKEN
		? GetFullHardhatTexture()
		: GetDamagedHardhatTexture(mHelmStage == ArmorBrokenState::REALLY_BROKEN);
	mAnimator->SetTrackImage("Zombie_digger_hardhat",
		ResourceManager::GetInstance().GetTexture(textureKey));
}

void DiggerZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("Zombie_digger_hardhat", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(GetHelmDropEffectName(), GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

void DiggerZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("Zombie_digger_head_eye", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieDiggerHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
	ReleaseLoopSound();
}

void DiggerZombie::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackImage("Zombie_digger_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			GetBrokenOuterArmTexture()));
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(GetArmDropEffectName(), GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kLimbVolume);
}

void DiggerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	mAnimator->SetTrackVisible("_ground", false);
	if (!mHasPickaxe) {
		mAnimator->SetTrackVisible("Zombie_digger_pickaxe", false);
		mAnimator->SetTrackVisible("Zombie_digger_dirt", false);
	}
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackImage("Zombie_digger_outerarm_upper",
			ResourceManager::GetInstance().GetTexture(
				GetBrokenOuterArmTexture()));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("Zombie_digger_head_eye", false);
	}
	if (mHelmType == HelmType::HELMTYPE_NONE) {
		mAnimator->SetTrackVisible("Zombie_digger_hardhat", false);
	}
	else if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		mAnimator->SetTrackImage("Zombie_digger_hardhat",
			ResourceManager::GetInstance().GetTexture(
				GetDamagedHardhatTexture(false)));
	}
	else if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
		mAnimator->SetTrackImage("Zombie_digger_hardhat",
			ResourceManager::GetInstance().GetTexture(
				GetDamagedHardhatTexture(true)));
	}
}

/** 地下与出土阶段对齐原版 BurnZombie：灰烬直接回收，不播放地上焦尸。 */
void DiggerZombie::TakePlantAshDamage(int damage)
{
	if (damage <= 0) return;
	if (!IsInteractivePhase()) {
		Die();
		return;
	}
	Zombie::TakePlantAshDamage(damage);
}

void DiggerZombie::Charred()
{
	if (!CanBeCharred()) {
		Die();
		return;
	}
	const bool noAxeTrack = mPhase == Phase::WALKING_WITHOUT_PICKAXE;
	GameObjectManager::GetInstance().CreateGameObjectImmediate<DiggerOneShotVisual>(
		LAYER_GAME_ZOMBIE, mBoard,
		GetVisualPosition() + Vector(IsMovingRight() ? 36.0f : 22.0f, -10.0f),
		AnimationType::ANIM_DIGGER_CHARRED,
		noAxeTrack ? "anim_crumble_noaxe" : "anim_crumble",
		GameRandom::Range(0.9f, 1.1f), IsMovingRight(),
		noAxeTrack ? kCharredRemovalFrameWithoutPickaxe : kCharredRemovalFrameWithPickaxe,
		mRow);
	Die();
}

void DiggerZombie::PlaySpawnSound()
{
	ClaimLoopSound();
}

void DiggerZombie::ClaimLoopSound()
{
	if (mLoopSoundClaimed || mPhase != Phase::TUNNELING || mIsPreview
		|| mIsDying || mIsDead) {
		return;
	}
	mLoopSoundClaimed = true;
	++sLoopSoundUsers;
	AudioSystem::PlayLoopingSound(ResourceKeys::Sounds::SOUND_DIGGER_ZOMBIE,
		kLoopVolume);
}

void DiggerZombie::ReleaseLoopSound()
{
	if (!mLoopSoundClaimed) return;
	mLoopSoundClaimed = false;
	sLoopSoundUsers = std::max(0, sLoopSoundUsers - 1);
	if (sLoopSoundUsers == 0) {
		AudioSystem::StopLoopingSound(ResourceKeys::Sounds::SOUND_DIGGER_ZOMBIE);
	}
}

void DiggerZombie::Die()
{
	ReleaseLoopSound();
	Zombie::Die();
}

float DiggerZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kAbilityAnimMultiplier;
}

void DiggerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["phaseRemaining"] = mPhaseRemaining;
	j["altitude"] = mAltitude;
	j["tunnelVelocity"] = mTunnelVelocity;
	j["walkVelocity"] = mWalkVelocity;
	j["tunnelDustTimer"] = mTunnelDustTimer;
	j["hasPickaxe"] = mHasPickaxe;
	j["surpriseShown"] = mSurpriseShown;
	j["landingStarted"] = mLandingStarted;
	j["helmStage"] = static_cast<int>(mHelmStage);
}

void DiggerZombie::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::WALKING_WITHOUT_PICKAXE));
	mPhase = static_cast<Phase>(phase);
	mPhaseRemaining = std::max(0.0f, j.value("phaseRemaining", 0.0f));
	mAltitude = std::clamp(j.value("altitude", 0.0f), kRiseDepth, kRiseOvershoot);
	mTunnelVelocity = std::clamp(j.value("tunnelVelocity", 0.67f),
		kTunnelVelocityMin, kTunnelVelocityMax);
	mWalkVelocity = std::clamp(j.value("walkVelocity", 0.30f),
		kNormalWalkVelocityMin, kNormalWalkVelocityMax);
	mTunnelDustTimer = std::clamp(j.value("tunnelDustTimer", 0.0f),
		0.0f, kTunnelDustInterval);
	mHasPickaxe = j.value("hasPickaxe", true);
	mSurpriseShown = j.value("surpriseShown", false);
	mLandingStarted = j.value("landingStarted", false);
	mHelmStage = static_cast<ArmorBrokenState>(std::clamp(
		j.value("helmStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)),
		static_cast<int>(ArmorBrokenState::NONE),
		static_cast<int>(ArmorBrokenState::REALLY_BROKEN)));

	ApplyPhasePresentation();
	if (mPhase == Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE
		|| (mPhase == Phase::RISING_WITHOUT_PICKAXE && !mLandingStarted)) {
		mAnimator->Pause();
	}
	if (mPhase == Phase::TUNNELING) ClaimLoopSound();
}
