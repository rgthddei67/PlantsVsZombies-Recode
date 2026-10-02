#include "IceCrackDrillZombie.h"

#include "Game/Board/Board.h"
#include "../GroundRift.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../Reanimation/Animator.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kDrillBodyHealth = 650;              // 钻机碎裂后的步行本体生命
	constexpr int kDrillRigHealth = 900;               // 可由盐晶腐蚀的独立冰制钻机层耐久
	constexpr float kChargeDuration = 3.5f;            // 冻土停步至地裂原子提交的游戏秒数
	constexpr float kChargeParticleInterval = 0.18f;   // 蓄力时钻头碎冰反馈间隔，单位游戏秒
	constexpr float kRigIdleClipSpeed = 0.45f;         // 巡航钻齿相对 reanim 基准的慢转倍率
	constexpr float kRigChargeClipSpeed = 2.2f;        // 蓄力钻齿相对 reanim 基准的高速倍率
	constexpr float kRigAttachOffsetX = -48.0f;        // 从身体稳定锚点把握把对齐画面前侧右手，单位局部 px
	constexpr float kRigAttachOffsetY = 45.0f;         // 钻轴保持腰腹高度，避免继承手轨的大幅上下摆动
	constexpr const char* kRigAttachTrack = "Zombie_body"; // 位于手掌轨之前；右手会自然盖住握把形成握持层级
	constexpr float kNoseAheadOfCollider = 98.0f;       // 钻头前端相对 collider 左缘的前伸距离，单位 px
	constexpr float kNoseGroundOffsetY = 34.0f;         // 钻头碎冰锚点相对僵尸碰撞基线的向下偏移，单位 px
}

void IceCrackDrillZombie::SetupZombie()
{
	// 路障时间线拥有已经验证的死亡与两次啃食帧事件；本品种不注册新帧号。
	ConeZombie::SetupZombie();
	mBodyHealth = kDrillBodyHealth;
	mBodyMaxHealth = kDrillBodyHealth;
	mHelmHealth = kDrillRigHealth;
	mHelmMaxHealth = kDrillRigHealth;
	mHelmType = HelmType::HELMTYPE_TRAFFIC_CONE;
	mHelmStage = ArmorBrokenState::NO_BROKEN;
	mDrillPhase = DrillPhase::MOVING;
	mChargeRemaining = 0.0f;
	mChargeParticleTimer = 0.0f;
	mDrillUsed = false;
	ConfigureDrillRigAnimator();
	CheckHelmImage();
	if (mIsPreview) PlayTrack("anim_idle");
}

void IceCrackDrillZombie::Update()
{
	ConeZombie::Update();
	// 基类在完全定身时不推进 ZombieUpdate；终止条件仍必须当帧收口。
	if (mDrillPhase == DrillPhase::CHARGING) {
		if (HasTerminalChargeAbort()) CancelCharge(true);
		else if (!IsStandingOnFrozenCell()) CancelCharge(false);
	}
	SyncDrillRigPresentation();
}

void IceCrackDrillZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform) return;
	if (mDrillPhase == DrillPhase::CHARGING) return;
	if (!mDrillUsed && CanBeginCharge()) {
		BeginCharge();
		return;
	}
	ConeZombie::ZombieMove(scaledDelta, transform);
}

void IceCrackDrillZombie::ZombieUpdate(float scaledTime)
{
	if (mDrillPhase != DrillPhase::CHARGING) return;
	if (HasTerminalChargeAbort()) {
		CancelCharge(true);
		return;
	}
	if (!IsStandingOnFrozenCell()) {
		CancelCharge(false);
		return;
	}

	const float deltaTime = std::max(0.0f, scaledTime);
	mChargeRemaining = std::max(0.0f, mChargeRemaining - deltaTime);
	mChargeParticleTimer -= deltaTime;
	if (mChargeParticleTimer <= 0.0f) {
		mChargeParticleTimer = kChargeParticleInterval;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("IceCrackDrillCharge", GetDrillNosePosition());
		}
	}
	if (mChargeRemaining <= 0.0f) CommitRift();
}

bool IceCrackDrillZombie::CanBeginCharge() const
{
	if (!mBoard || mIsPreview || !IsActive() || mIsDying || IsMindControlled()
		|| !HasHead() || mDrillUsed || mHelmHealth <= 0
		|| mHelmType == HelmType::HELMTYPE_NONE || !mCollider
		|| !mBoard->SupportsWinterTemperature()) return false;
	const SDL_FRect bounds = mCollider->GetBoundingBox();
	const float battlefieldRightX = CELL_INITALIZE_POS_X
		+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X;
	return bounds.x + bounds.w <= battlefieldRightX && IsStandingOnFrozenCell();
}

bool IceCrackDrillZombie::HasTerminalChargeAbort() const
{
	return !mBoard || !IsActive() || mIsDying || IsMindControlled() || !HasHead()
		|| !HasArm() || mHelmHealth <= 0 || mHelmType == HelmType::HELMTYPE_NONE;
}

bool IceCrackDrillZombie::IsStandingOnFrozenCell() const
{
	if (!mBoard || !mBoard->SupportsWinterTemperature()) return false;
	const int column = GetCurrentColumn();
	return column >= 0 && column < mBoard->mColumns
		&& mBoard->IsCellFrozen(mRow, column);
}

void IceCrackDrillZombie::BeginCharge()
{
	if (!CanBeginCharge()) return;
	mDrillPhase = DrillPhase::CHARGING;
	mChargeRemaining = kChargeDuration;
	mChargeParticleTimer = 0.0f;
	PlayTrack("anim_idle", 1.0f, 0.1f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZAMBONI, 0.32f);
	SyncDrillRigPresentation(true);
}

void IceCrackDrillZombie::CancelCharge(bool consumeAbility)
{
	if (mDrillPhase != DrillPhase::CHARGING) return;
	mDrillUsed = mDrillUsed || consumeAbility;
	mDrillPhase = mDrillUsed ? DrillPhase::SPENT : DrillPhase::MOVING;
	mChargeRemaining = 0.0f;
	mChargeParticleTimer = 0.0f;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.1f);
	SyncDrillRigPresentation(true);
}

void IceCrackDrillZombie::CommitRift()
{
	if (mDrillPhase != DrillPhase::CHARGING || HasTerminalChargeAbort()
		|| !IsStandingOnFrozenCell()) {
		CancelCharge(HasTerminalChargeAbort());
		return;
	}
	const Vector nose = GetDrillNosePosition();
	const int nextColumn = std::clamp(static_cast<int>(std::floor(
		(nose.x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X)),
		0, mBoard->mColumns - 1);
	if (!mBoard->AddGroundRift(mRow, nose.x, nextColumn, 1.0f)) {
		CancelCharge(false);
		return;
	}
	mDrillUsed = true;
	mDrillPhase = DrillPhase::SPENT;
	mChargeRemaining = 0.0f;
	mChargeParticleTimer = 0.0f;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DIRT_RISE, 0.55f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("IceCrackDrillRift", nose);
	}
	PlayWalkAnimation(0.1f);
	SyncDrillRigPresentation(true);
}

void IceCrackDrillZombie::Die()
{
	CancelCharge(true);
	SyncDrillRigPresentation();
	ConeZombie::Die();
}

void IceCrackDrillZombie::OnMindControlled()
{
	CancelCharge(true);
	SyncDrillRigPresentation(true);
}

void IceCrackDrillZombie::ArmDrop()
{
	if (!mHasArm) return;
	// 钻机由画面前侧右手握持；手臂失去时同时脱落装备，避免悬空钻机。
	ConeZombie::ArmDrop();
	HelmDrop();
}

void IceCrackDrillZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	CancelCharge(true);
	ConeZombie::HelmDrop();
	SyncDrillRigPresentation();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.35f);
}

void IceCrackDrillZombie::CheckHelmImage()
{
	ConeZombie::CheckHelmImage();
	SyncDrillRigPresentation();
}

bool IceCrackDrillZombie::ApplyWinterCorrosion(int corrosion)
{
	if (corrosion <= 0 || mHelmType == HelmType::HELMTYPE_NONE
		|| mHelmHealth <= 0) return false;
	const int before = mHelmHealth;
	// TakeHelmDamage 的溢出只作为返回值，不会进入本体，保持盐蚀层独立。
	TakeHelmDamage(corrosion);
	return mHelmHealth < before;
}

float IceCrackDrillZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mDrillPhase == DrillPhase::CHARGING ? mChargeRemaining : -1.0f;
}

bool IceCrackDrillZombie::InterruptUncommittedSpecialAction()
{
	if (mDrillPhase != DrillPhase::CHARGING) return false;
	CancelCharge(false);
	return true;
}

void IceCrackDrillZombie::SaveExtraData(nlohmann::json& j) const
{
	ConeZombie::SaveExtraData(j);
	j["drillPhase"] = static_cast<int>(mDrillPhase);
	j["drillChargeRemaining"] = mChargeRemaining;
	j["drillChargeParticleTimer"] = mChargeParticleTimer;
	j["drillUsed"] = mDrillUsed;
}

void IceCrackDrillZombie::LoadExtraData(const nlohmann::json& j)
{
	ConeZombie::LoadExtraData(j);
	const int phase = std::clamp(j.value("drillPhase", 0), 0,
		static_cast<int>(DrillPhase::SPENT));
	mDrillPhase = static_cast<DrillPhase>(phase);
	mChargeRemaining = std::clamp(j.value("drillChargeRemaining", 0.0f),
		0.0f, kChargeDuration);
	mChargeParticleTimer = std::clamp(
		j.value("drillChargeParticleTimer", 0.0f),
		0.0f, kChargeParticleInterval);
	mDrillUsed = j.value("drillUsed", mDrillPhase == DrillPhase::SPENT);
	if (mDrillPhase == DrillPhase::CHARGING
		&& (!HasHead() || !HasArm() || IsMindControlled() || mHelmHealth <= 0
			|| mHelmType == HelmType::HELMTYPE_NONE
			|| mChargeRemaining <= 0.0f)) {
		// 旧档或异常快照的终止态不能回到可重试状态，否则会凭空恢复一次钻孔能力。
		mDrillUsed = true;
		mDrillPhase = DrillPhase::SPENT;
		mChargeRemaining = 0.0f;
	}
	mPresentedRigStage = ArmorBrokenState::NONE;
	mPresentedRigPhase = DrillPhase::SPENT;
	SyncDrillRigPresentation(true);
	if (mDrillPhase == DrillPhase::CHARGING && mAnimator) {
		PlayTrack("anim_idle", 1.0f, 0.0f);
	}
}

void IceCrackDrillZombie::ZombieItemUpdate() const
{
	ConeZombie::ZombieItemUpdate();
	SyncDrillRigPresentation();
}

void IceCrackDrillZombie::ConfigureDrillRigAnimator()
{
	if (mDrillRigAnimator || !mAnimator || !mAnimator->HasTrack(kRigAttachTrack)) return;
	auto reanimation = ResourceManager::GetInstance().GetReanimation(
		ResourceKeys::Reanimations::REANIM_ICE_CRACK_DRILL_RIG);
	if (!reanimation) return;
	auto rig = std::make_shared<Animator>(reanimation);
	rig->PlayTrack("anim_idle_full", kRigIdleClipSpeed, 0.0f);
	rig->SetLocalPosition(kRigAttachOffsetX, kRigAttachOffsetY);
	if (!mAnimator->AttachAnimator(kRigAttachTrack, rig)) return;
	mDrillRigAnimator = std::move(rig);
	mPresentedRigStage = ArmorBrokenState::NONE;
	mPresentedRigPhase = DrillPhase::SPENT;
}

const char* IceCrackDrillZombie::GetDrillTrackName() const
{
	const bool charging = mDrillPhase == DrillPhase::CHARGING;
	if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return charging ? "anim_charge_cracked1" : "anim_idle_cracked1";
	}
	if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
		return charging ? "anim_charge_cracked2" : "anim_idle_cracked2";
	}
	return charging ? "anim_charge_full" : "anim_idle_full";
}

void IceCrackDrillZombie::SyncDrillRigPresentation(bool restartTrack) const
{
	if (!mDrillRigAnimator) return;
	const bool visible = mHelmType != HelmType::HELMTYPE_NONE && mHelmHealth > 0
		&& mHelmStage != ArmorBrokenState::NONE && !mIsDead;
	if (!visible) {
		// 永久破甲后直接销毁子 Animator；仅设 Alpha=0 仍可能被父级受击加色绘出幽灵轮廓。
		if (mAnimator) {
			mAnimator->DetachAnimator(kRigAttachTrack, mDrillRigAnimator);
		}
		mDrillRigAnimator.reset();
		return;
	}
	mDrillRigAnimator->SetAlpha(1.0f);
	if (!restartTrack && mPresentedRigStage == mHelmStage
		&& mPresentedRigPhase == mDrillPhase) return;
	mDrillRigAnimator->PlayTrack(GetDrillTrackName(),
		mDrillPhase == DrillPhase::CHARGING
			? kRigChargeClipSpeed : kRigIdleClipSpeed,
		0.0f);
	mPresentedRigStage = mHelmStage;
	mPresentedRigPhase = mDrillPhase;
}

Vector IceCrackDrillZombie::GetDrillNosePosition() const
{
	float noseX = GetPosition().x - kNoseAheadOfCollider;
	if (mCollider) noseX = mCollider->GetBoundingBox().x - kNoseAheadOfCollider;
	const float groundY = mBoard
		? mBoard->GetZombieCollisionY(mRow, noseX) + kNoseGroundOffsetY
		: GetPosition().y;
	return Vector(noseX, groundY);
}

int IceCrackDrillZombie::GetCurrentColumn() const
{
	if (!mBoard || mBoard->mColumns <= 0) return -1;
	float centerX = GetPosition().x;
	if (mCollider) {
		const SDL_FRect bounds = mCollider->GetBoundingBox();
		centerX = bounds.x + bounds.w * 0.5f;
	}
	return std::clamp(static_cast<int>(std::floor(
		(centerX - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X)),
		0, mBoard->mColumns - 1);
}

const std::string& IceCrackDrillZombie::GetConeTextureKey(
	ArmorBrokenState stage) const
{
	using namespace ResourceKeys::Textures;
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET2;
	}
	if (stage == ArmorBrokenState::REALLY_BROKEN) {
		return IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET3;
	}
	return IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET1;
}

bool IceCrackDrillZombie::IsDrillRigVisible() const
{
	return mDrillRigAnimator && mDrillRigAnimator->GetAlpha() > 0.01f;
}

void IceCrackDrillZombie::SetDrillStateForTesting(
	DrillPhase phase, float remaining, bool used)
{
	mDrillPhase = phase;
	mChargeRemaining = std::clamp(remaining, 0.0f, kChargeDuration);
	mChargeParticleTimer = 0.0f;
	mDrillUsed = used;
	if (mAnimator) {
		if (phase == DrillPhase::CHARGING) PlayTrack("anim_idle", 1.0f, 0.0f);
		else PlayWalkAnimation(0.0f);
	}
	SyncDrillRigPresentation(true);
}

ZombieMovementRules::BirthProfile IceCrackDrillZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
