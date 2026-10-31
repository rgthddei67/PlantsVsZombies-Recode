#include "MelonPult.h"
#include "Game/Workshop/WorkshopRoster.h"
#include <stdexcept>
#include "../../GameApp.h"

#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../IceWall.h"
#include "../Zombie/Zombie.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <limits>

namespace {
	constexpr int kFireFrame = 44;                         // 主人确认的 AddFrameEvent 真实发射帧
	constexpr float kReanimFramesPerSecond = 12.0f;        // Melonpult.reanim 的基础帧率
	constexpr float kShootFramesPerSecond = 35.0f;         // C# anim_shooting 的播放帧率
	constexpr float kIdleFramesPerSecondMin = 10.0f;       // 经典普通植物待机随机帧率下限
	constexpr float kIdleFramesPerSecondMax = 15.0f;       // 经典普通植物待机随机帧率上限
	constexpr float kShootBlendSeconds = 0.0f;             // 射击动画进入与返回的混合时间
	constexpr float kInitialShootInterval = 3.0f;          // C# launch rate 300 厘秒
	constexpr float kRepeatShootIntervalMin = 2.86f;       // 300-NextNumber(15) 的最短后续周期
	constexpr float kRepeatShootIntervalMax = 3.0f;        // 300-NextNumber(15) 的最长后续周期
	const Vector kLaunchOffset(-1.0f, -79.0f);              // C# 左上坐标换算到本项目稳定视觉锚点的弹心偏移
	constexpr float kFlightDuration = 1.2f;                // 经典投掷物约 120 厘秒的飞行时间
	constexpr float kArcApexHeight = 210.0f;               // 相对起终点线性插值轨迹的最高拱高，单位 px
	constexpr float kTargetHeightRatio = 0.35f;            // 瞄准碰撞箱上部身体，避开脚底与头顶空白
	constexpr float kFallbackLandingOffsetY = -20.0f;      // 目标消失时相对同行地形基线的安全落点高度
	constexpr float kShootSoundVolume = 0.3f;              // Throw/Throw2 发射 Foley 音量
}

void MelonPult::SetupPlant()
{
	Plant::SetupPlant();
	if (!mAnimator) return;

	mAnimator->PlayTrack("anim_idle");
	mAnimator->SetSpeed(GameRandom::Range(
		kIdleFramesPerSecondMin / kReanimFramesPerSecond,
		kIdleFramesPerSecondMax / kReanimFramesPerSecond));
	mShootInterval = kInitialShootInterval;
	mShootTimer = GameRandom::Range(0.0f, kInitialShootInterval);

	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(2.0f, 24.0f));
	}

	mAnimator->AddFrameEvent(kFireFrame, [this]() {
		if (GetCurrentTrackName() == "anim_shooting") FireMelon();
	}, true);
}

void MelonPult::PlantUpdate()
{
	const float attackSpeed = GetAttackSpeedMultiplier() * WorkshopSpeed();
	mShootTimer += DeltaTime::GetDeltaTime() * attackSpeed;
	if (mShootTimer < mShootInterval) return;

    const bool hasTarget = mBoard && mAnimator && (mBoard->GetIceWallInRow(mRow) || FindTarget());
    // 工坊断供时保持攻击就绪，不重抽周期或推进随机序列；非工坊沿用到期索敌节奏。
    if (hasTarget && !mBoard->ReserveWorkshopAmmo(mRow, mPlantType)) {
        mShootTimer = mShootInterval;
        return;
    }
    mShootTimer = 0.0f;
    mShootInterval = GameRandom::Range(kRepeatShootIntervalMin, kRepeatShootIntervalMax);
    if (!hasTarget) return;

	const float shootSpeed =
		(kShootFramesPerSecond / kReanimFramesPerSecond) * attackSpeed;
	mAnimator->PlayTrackOnce(
		"anim_shooting", "anim_idle", shootSpeed,
		kShootBlendSeconds, 0.0f, kShootBlendSeconds);
}

Zombie* MelonPult::FindTarget() const
{
	if (!mBoard) return nullptr;

	Zombie* closest = nullptr;
	float closestX = std::numeric_limits<float>::max();
	const float plantX = GetPosition().x;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| zombie->IsMindControlled() || !zombie->HasHead()
			|| !mBoard->CanPlantAcquireZombie(this, zombie)) {
			return;
		}
		const ColliderComponent* collider = zombie->GetColliderComponent();
		if (!collider) return;
		const SDL_FRect bounds = collider->GetBoundingBox();
		const float centerX = bounds.x + bounds.w * 0.5f;
		if (bounds.x + bounds.w < plantX || centerX > static_cast<float>(SCENE_WIDTH)) {
			return;
		}
		if (centerX < closestX) {
			closestX = centerX;
			closest = zombie;
		}
	});
	return closest;
}

void MelonPult::FireMelon()
{
	if (!mBoard || mIsPreview) return;

	const Vector launchPosition = GetVisualAnchorPosition() + kLaunchOffset;
	Vector landingPosition(
		static_cast<float>(SCENE_WIDTH + 20),
		mBoard->GetRowCenterYAtX(mRow, static_cast<float>(SCENE_WIDTH))
			+ kFallbackLandingOffsetY);
	bool targetsIceWall = false;
	if (IceWall* wall = mBoard->GetIceWallInRow(mRow)) {
		landingPosition = wall->GetProjectileAimPosition();
		targetsIceWall = true;
	}
	else if (Zombie* target = FindTarget()) {
		if (const ColliderComponent* collider = target->GetColliderComponent()) {
			const SDL_FRect bounds = collider->GetBoundingBox();
			const float currentTargetX = bounds.x + bounds.w * 0.5f;
			landingPosition.x = target->GetTargetLeadX(kFlightDuration);
			// 屋顶水平移动会同步改变目标高度；沿 Board 连续坡面补偿预测后的 Y。
			const float terrainDeltaY =
				mBoard->GetRowCenterYAtX(mRow, landingPosition.x)
					- mBoard->GetRowCenterYAtX(mRow, currentTargetX);
			landingPosition.y = bounds.y + bounds.h * kTargetHeightRatio
				+ terrainDeltaY;
		}
	}

	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT
		: ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, kShootSoundVolume);
	const BulletType bulletType = GetMelonBulletType();
	if (bulletType == BulletType::BULLET_WINTERMELON) {
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES, kShootSoundVolume);
	}
	Bullet* melon = mBoard->CreatePlantBullet(
		bulletType, mRow, launchPosition, mPlantType);
	if (!melon) return;
    mBoard->RecordWorkshopPlantShot(mPlantType);
	const bool polarGuided = mBoard->IsPolarWindDangerous()
		&& mBoard->PreparePolarLobbedNavigation(this);
	melon->ConfigureLobbedMotion(landingPosition, kFlightDuration,
		kArcApexHeight, targetsIceWall, polarGuided);
}

BulletType MelonPult::GetMelonBulletType() const
{
	return BulletType::BULLET_MELON;
}

void MelonPult::SaveExtraData(nlohmann::json& j) const
{
	j["shootTimer"] = mShootTimer;
    if (mBoard && mBoard->IsWorkshop()) j["workshopUpgradeLevel"] = mWorkshopUpgradeLevel;
	j["shootInterval"] = mShootInterval;
}

void MelonPult::LoadExtraData(const nlohmann::json& j)
{
    mWorkshopUpgradeLevel = 1;
    if (mBoard && mBoard->IsWorkshop() && j.contains("workshopUpgradeLevel")) {
        const auto& level = j.at("workshopUpgradeLevel");
        if (!level.is_number_integer() || level < 1 || level > Workshop::MaxUpgradeLevel)
            throw std::runtime_error("无效的工坊投手等级");
        mWorkshopUpgradeLevel = level.get<int>();
        if (mBoard->GetWorkshop().CompletedWaves() < Workshop::UpgradeWaves[mWorkshopUpgradeLevel - 1])
            throw std::runtime_error("工坊投手等级尚未解锁");
    }
	mShootInterval = std::clamp(
		j.value("shootInterval", kInitialShootInterval),
		kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	mShootTimer = std::clamp(
		j.value("shootTimer", 0.0f), 0.0f, mShootInterval);
}

void MelonPult::SetShootCycleForTesting(
	float elapsedSeconds, float intervalSeconds)
{
	mShootInterval = std::clamp(
		intervalSeconds, kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	mShootTimer = std::clamp(elapsedSeconds, 0.0f, mShootInterval);
}

float MelonPult::WorkshopSpeed() const {
    return mBoard && mBoard->IsWorkshop() ? Workshop::ShooterCooldowns[0] / Workshop::ShooterCooldowns[mWorkshopUpgradeLevel - 1] : 1;
}
void MelonPult::InheritWorkshopLevel(int level) {
    if (mBoard && mBoard->IsWorkshop()) mWorkshopUpgradeLevel = std::clamp(level, 1, Workshop::MaxUpgradeLevel);
}
bool MelonPult::TryUpgradeWorkshop() {
    const auto* info = Workshop::FindPlant(mPlantType);
    if (!mBoard || !mBoard->IsWorkshop() || !info || !IsActive() || mPlantHealth <= 0
        || !mBoard->GetWorkshop().BuyUpgrade(info->cost, mWorkshopUpgradeLevel)) return false;
    const float oldSpeed = WorkshopSpeed(); ++mWorkshopUpgradeLevel;
    if (mAnimator && GetCurrentTrackName() == "anim_shooting") mAnimator->SetClipSpeed(mAnimator->GetClipSpeed() * WorkshopSpeed() / oldSpeed);
    return true;
}
