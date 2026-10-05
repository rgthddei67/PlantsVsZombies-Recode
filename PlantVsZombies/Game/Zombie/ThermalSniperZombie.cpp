#include "ThermalSniperZombie.h"
#include "ZombieBirthVitalsRules.h"
#include "ThermalSniperRules.h"

#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "../../Reanimation/Animator.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../Plant/Plant.h"

#include <algorithm>

namespace {
constexpr int kBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_THERMAL_SNIPER).body; // 热感狙击僵尸本体生命
constexpr int kBiteDamage = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_THERMAL_SNIPER).bite; // 单次啃咬基础伤害
constexpr float kReloadSeconds = ThermalSniperRules::Reload; // 共用独立装填周期，游戏秒
constexpr float kAimSeconds = ThermalSniperRules::Aim; // 共用落种预警时长，游戏秒
constexpr float kPulseSpeed = ThermalSniperRules::PulseSpeed; // 共用热脉冲速度，px/游戏秒
constexpr float kMuzzleOffsetX = ThermalSniperRules::MuzzleOffset; // 共用枪口偏移，px
constexpr float kMuzzleOffsetY = 26.0f; // 枪口相对本体视觉原点的垂直偏移，单位 px
constexpr float kPulseTargetOffsetY = 0.0f; // 热脉冲在原落种位置的目标高度，单位 px
constexpr float kGogglesOffsetX = -13.0f; // 护目镜相对头部轨道的水平偏移，单位动画 px
constexpr float kGogglesOffsetY = -3.0f; // 护目镜相对头部轨道的垂直偏移，单位动画 px
constexpr float kGogglesScale = 0.58f; // 护目镜 follower 相对头部轨道的尺寸倍率
constexpr float kPackOffsetX = 34.0f; // 热能背包相对身体轨道的水平偏移，单位动画 px
constexpr float kPackOffsetY = -15.0f; // 热能背包相对身体轨道的垂直偏移，单位动画 px
constexpr float kPackScale = 0.68f; // 热能背包 follower 相对身体轨道的尺寸倍率
constexpr const char* kGogglesFollowerSlot = "thermal_goggles"; // 头部静态护目镜槽
constexpr const char* kPackFollowerSlot = "thermal_pack"; // 身体热能背包槽
constexpr const char* kLauncherAttachTrack = "Zombie_body"; // 肩炮独立时间轴的身体稳定锚点
}

void ThermalSniperZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mAttackDamage = kBiteDamage;
	mSniperPhase = mIsPreview ? SniperPhase::READY : SniperPhase::RELOADING;
	mReloadRemaining = mIsPreview ? 0.0f : kReloadSeconds;
	mAimRemaining = 0.0f;
	mLockedPlantID = NULL_PLANT_ID;
	mLockedDamage = 0;
	ConfigureFollowers();
	SyncFollowerPresentation();
}

void ThermalSniperZombie::Update()
{
	if (!mIsPreview && mSniperPhase == SniperPhase::RELOADING
		&& !IsImmobilized() && IsActive() && !mIsDying && HasHead()
		&& !IsMindControlled()) {
		const float slowMultiplier = mCooldownTimer > 0.0f ? 0.5f : 1.0f;
		mReloadRemaining = std::max(0.0f, mReloadRemaining
			- DeltaTime::GetDeltaTime() * slowMultiplier);
		if (mReloadRemaining <= 0.0f) mSniperPhase = SniperPhase::READY;
	}
	Zombie::Update();
	if (mSniperPhase != SniperPhase::DISABLED
		&& (!IsActive() || mIsDying || !HasHead() || IsMindControlled())) {
		DisableSniper();
	}
	SyncFollowerPresentation();
}

void ThermalSniperZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mSniperPhase == SniperPhase::AIMING) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void ThermalSniperZombie::ZombieUpdate(float scaledTime)
{
	if (mSniperPhase != SniperPhase::AIMING) return;
	mAimRemaining = std::max(0.0f, mAimRemaining - std::max(0.0f, scaledTime));
	if (mAimRemaining <= 0.0f) FireLockedPulse();
}

bool ThermalSniperZombie::CanReactToDeployment(const Plant& plant) const
{
	return !mIsPreview && IsActive() && !mIsDying && HasHead()
		&& !IsMindControlled() && mSniperPhase == SniperPhase::READY
		&& plant.mRow == mRow;
}

void ThermalSniperZombie::OnPlayerPlantDeployed(
	const Plant& plant, int baseMaxHealth)
{
	if (!CanReactToDeployment(plant)) return;
	CancelEatingForSpecialAction();
	mLockedPlantID = plant.mPlantID;
	mLockedDamage = std::max(1, baseMaxHealth);
	mLockedPosition = plant.GetPosition();
	mAimRemaining = kAimSeconds;
	mSniperPhase = SniperPhase::AIMING;
	SyncFollowerPresentation();
	PlayTrack("anim_idle", 1.0f, 0.08f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.48f);
	if (g_particleSystem) g_particleSystem->EmitEffect(
		"ThermalPulseTarget", mLockedPosition);
}

float ThermalSniperZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mSniperPhase == SniperPhase::AIMING ? mAimRemaining : -1.0f;
}

bool ThermalSniperZombie::InterruptUncommittedSpecialAction()
{
	if (mSniperPhase != SniperPhase::AIMING) return false;
	mLockedPlantID = NULL_PLANT_ID;
	mLockedDamage = 0;
	mAimRemaining = 0.0f;
	mReloadRemaining = kReloadSeconds;
	mSniperPhase = SniperPhase::RELOADING;
	PlayWalkAnimation(0.08f);
	SyncFollowerPresentation();
	return true;
}

void ThermalSniperZombie::FireLockedPulse()
{
	if (!mBoard || mSniperPhase != SniperPhase::AIMING || !HasHead()
		|| mIsDying || IsMindControlled()) {
		DisableSniper();
		return;
	}
	const Vector visualOrigin = GetVisualPosition();
	const Vector origin(
		visualOrigin.x + (IsMovingRight() ? kMuzzleOffsetX : -kMuzzleOffsetX),
		visualOrigin.y + kMuzzleOffsetY);
	const Vector target(mLockedPosition.x,
		mLockedPosition.y + kPulseTargetOffsetY);
	Bullet* pulse = mBoard->CreateBullet(
		BulletType::BULLET_THERMAL_PULSE, mRow, origin);
	if (pulse) {
		pulse->SetBulletDamage(mLockedDamage);
		pulse->SetVelocityX(target.x >= origin.x ? kPulseSpeed : -kPulseSpeed);
		pulse->ConfigureThermalPulse(target, mLockedPlantID);
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.62f);
	if (g_particleSystem) g_particleSystem->EmitEffect("ThermalPulseMuzzle", origin);
	mLockedPlantID = NULL_PLANT_ID;
	mLockedDamage = 0;
	mAimRemaining = 0.0f;
	mReloadRemaining = kReloadSeconds;
	mSniperPhase = SniperPhase::RELOADING;
	PlayWalkAnimation(0.08f);
	SyncFollowerPresentation();
}

void ThermalSniperZombie::DisableSniper()
{
	mSniperPhase = SniperPhase::DISABLED;
	mReloadRemaining = 0.0f;
	mAimRemaining = 0.0f;
	mLockedPlantID = NULL_PLANT_ID;
	mLockedDamage = 0;
	SyncFollowerPresentation();
}

void ThermalSniperZombie::HeadDrop()
{
	if (!mHasHead) return;
	Zombie::HeadDrop();
	DisableSniper();
	SyncFollowerPresentation();
}

void ThermalSniperZombie::OnMindControlled()
{
	DisableSniper();
}

void ThermalSniperZombie::Die()
{
	DisableSniper();
	Zombie::Die();
	SyncFollowerPresentation();
}

void ThermalSniperZombie::Draw(Graphics* g)
{
	if (g && mSniperPhase == SniperPhase::AIMING) {
		const Vector visualOrigin = GetVisualPosition();
		const Vector muzzle(
			visualOrigin.x + (IsMovingRight() ? kMuzzleOffsetX : -kMuzzleOffsetX),
			visualOrigin.y + kMuzzleOffsetY);
		const Vector target(mLockedPosition.x,
			mLockedPosition.y + kPulseTargetOffsetY);
		g->DrawLine(muzzle.x, muzzle.y, target.x, target.y,
			glm::vec4(255.0f, 54.0f, 18.0f, 120.0f));
		g->DrawLine(muzzle.x, muzzle.y, target.x, target.y,
			glm::vec4(255.0f, 226.0f, 92.0f, 205.0f));
		g->DrawCircle(mLockedPosition.x, mLockedPosition.y, 18.0f,
			glm::vec4(255.0f, 68.0f, 24.0f, 235.0f), 24);
		g->DrawLine(mLockedPosition.x - 24.0f, mLockedPosition.y,
			mLockedPosition.x + 24.0f, mLockedPosition.y,
			glm::vec4(255.0f, 190.0f, 74.0f, 205.0f));
	}
	Zombie::Draw(g);
}

void ThermalSniperZombie::ConfigureFollowers()
{
	if (mFollowersConfigured || !mAnimator
		|| !mAnimator->HasTrack("anim_head1")
		|| !mAnimator->HasTrack("Zombie_body")) return;
	ResourceManager& resources = ResourceManager::GetInstance();
	const Texture* goggles = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_THERMAL_GOGGLES, false);
	const Texture* pack = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_THERMAL_PACK, false);
	const auto launcherReanimation = resources.GetReanimation(
		ResourceKeys::Reanimations::REANIM_THERMAL_SNIPER_LAUNCHER);
	if (!goggles || !pack || !launcherReanimation) return;
	mAnimator->SetTrackFollowerImage("anim_head1", kGogglesFollowerSlot,
		goggles, kGogglesOffsetX, kGogglesOffsetY,
		kGogglesScale, kGogglesScale, true, true, true);
	mAnimator->SetTrackFollowerImage("Zombie_body", kPackFollowerSlot,
		pack, kPackOffsetX, kPackOffsetY, kPackScale, kPackScale,
		false, true, true);
	auto launcher = std::make_shared<Animator>(launcherReanimation);
	launcher->PlayTrack("anim_reload", 0.65f, 0.0f);
	if (!mAnimator->AttachAnimator(kLauncherAttachTrack, launcher)) return;
	mLauncherAnimator = std::move(launcher);
	mFollowersConfigured = true;
	mPresentedPhase = SniperPhase::DISABLED;
}

void ThermalSniperZombie::SyncFollowerPresentation() const
{
	if (!mFollowersConfigured || !mAnimator) return;
	const bool alive = !mIsDead && !mIsDying;
	mAnimator->SetTrackFollowerVisible("anim_head1", kGogglesFollowerSlot,
		alive && mHasHead);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kPackFollowerSlot, alive);
	if (!mLauncherAnimator) return;
	mLauncherAnimator->SetTrackVisible("launcher", alive);
	if (!alive || mPresentedPhase == mSniperPhase) return;
	const char* track = "anim_disabled";
	float speed = 0.65f;
	switch (mSniperPhase) {
	case SniperPhase::RELOADING: track = "anim_reload"; break;
	case SniperPhase::READY: track = "anim_ready"; break;
	case SniperPhase::AIMING: track = "anim_aim"; speed = 3.2f; break;
	case SniperPhase::DISABLED: break;
	}
	mLauncherAnimator->PlayTrack(track, speed, 0.0f);
	mPresentedPhase = mSniperPhase;
}

void ThermalSniperZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mFollowersConfigured) {
		const_cast<ThermalSniperZombie*>(this)->ConfigureFollowers();
	}
	SyncFollowerPresentation();
}

void ThermalSniperZombie::SaveExtraData(nlohmann::json& j) const
{
	j["sniperPhase"] = static_cast<int>(mSniperPhase);
	j["reloadRemaining"] = mReloadRemaining;
	j["aimRemaining"] = mAimRemaining;
	j["lockedPlantID"] = mLockedPlantID;
	j["lockedDamage"] = mLockedDamage;
	j["lockedPositionX"] = mLockedPosition.x;
	j["lockedPositionY"] = mLockedPosition.y;
}

void ThermalSniperZombie::LoadExtraData(const nlohmann::json& j)
{
	const int savedPhase = std::clamp(j.value("sniperPhase", 0), 0,
		static_cast<int>(SniperPhase::DISABLED));
	mSniperPhase = static_cast<SniperPhase>(savedPhase);
	mReloadRemaining = std::clamp(j.value("reloadRemaining", kReloadSeconds),
		0.0f, kReloadSeconds);
	mAimRemaining = std::clamp(j.value("aimRemaining", 0.0f), 0.0f, kAimSeconds);
	mLockedPlantID = j.value("lockedPlantID", NULL_PLANT_ID);
	mLockedDamage = std::max(0, j.value("lockedDamage", 0));
	mLockedPosition = Vector(j.value("lockedPositionX", 0.0f),
		j.value("lockedPositionY", 0.0f));
	if (!HasHead() || IsMindControlled() || mIsDying) DisableSniper();
	else if (mSniperPhase == SniperPhase::AIMING
		&& (mAimRemaining <= 0.0f || mLockedDamage <= 0)) {
		mSniperPhase = SniperPhase::RELOADING;
		mReloadRemaining = kReloadSeconds;
	}
	ConfigureFollowers();
	SyncFollowerPresentation();
}

ZombieMovementRules::BirthProfile ThermalSniperZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.abilityMinimum=p.abilityMaximum=BaseAbilitySpeed;
	p.phaseDependent=true;
	return p;
}
