#include "CabbagePult.h"
#include "../../GameApp.h"

#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../IceWall.h"
#include "../Zombie/Zombie.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <limits>

namespace {
	constexpr int kFireFrame = 43;                         // 主人确认的 AddFrameEvent 真实发射帧
	constexpr float kReanimFramesPerSecond = 12.0f;        // Cabbagepult.reanim 的基础帧率
	constexpr float kShootFramesPerSecond = 35.0f;         // C# anim_shooting 的播放帧率
	constexpr float kIdleFramesPerSecondMin = 10.0f;       // 经典普通植物待机随机帧率下限
	constexpr float kIdleFramesPerSecondMax = 15.0f;       // 经典普通植物待机随机帧率上限
	constexpr float kShootBlendSeconds = 0.0f;             // 射击动画进入与返回的混合时间
	constexpr float kInitialShootInterval = 3.0f;          // C# launch rate 300 厘秒
	constexpr float kRepeatShootIntervalMin = 2.86f;       // 300-NextNumber(15) 的最短后续周期
	constexpr float kRepeatShootIntervalMax = 3.0f;        // 300-NextNumber(15) 的最长后续周期
	const Vector kLaunchOffset(-21.0f, -45.0f);             // 由 C# 左上坐标换算到本项目稳定视觉锚点的弹心偏移
	constexpr float kFlightDuration = 1.2f;                // 经典投掷物约 120 厘秒的飞行时间
	constexpr float kArcApexHeight = 210.0f;               // 相对起终点线性插值轨迹的最高拱高，单位 px
	constexpr float kTargetHeightRatio = 0.35f;            // 瞄准碰撞箱上部身体，避开脚底与头顶空白
	constexpr float kFallbackLandingOffsetY = -20.0f;      // 目标消失时相对同行地形基线的安全落点高度
	constexpr int kCabbageDamage = 40;                     // C# ProjectileType.Cabbage 直击伤害
}

void CabbagePult::SetupPlant()
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
		if (GetCurrentTrackName() == "anim_shooting") FireCabbage();
	}, true);
}

void CabbagePult::PlantUpdate()
{
	const float attackSpeed = GetAttackSpeedMultiplier();
	mShootTimer += DeltaTime::GetDeltaTime() * attackSpeed;
	if (mShootTimer < mShootInterval) return;

	// 到期即开始下一周期；无目标时不会每个逻辑帧重复遍历行桶。
	mShootTimer = 0.0f;
	mShootInterval = NextShootInterval();
	if ((!mBoard || (!mBoard->GetIceWallInRow(mRow) && !FindTarget()))
		|| !mAnimator) return;

	const float shootSpeed =
		(kShootFramesPerSecond / kReanimFramesPerSecond) * attackSpeed;
	mAnimator->PlayTrackOnce(
		"anim_shooting", "anim_idle", shootSpeed,
		kShootBlendSeconds, 0.0f, kShootBlendSeconds);
}

float CabbagePult::NextShootInterval() const
{
	return GameRandom::Range(kRepeatShootIntervalMin, kRepeatShootIntervalMax);
}

Zombie* CabbagePult::FindTarget() const
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

void CabbagePult::FireCabbage()
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
			// 屋顶目标水平移动时会沿连续坡面改变 Y；用 Board 地形差预测，
			// 不把屋顶或僵尸类型特判塞进通用抛物线。
			const float terrainDeltaY =
				mBoard->GetRowCenterYAtX(mRow, landingPosition.x)
				- mBoard->GetRowCenterYAtX(mRow, currentTargetX);
			landingPosition.y = bounds.y + bounds.h * kTargetHeightRatio
				+ terrainDeltaY;
		}
	}

	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT
		: ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);
	Bullet* cabbage = mBoard->CreatePlantBullet(
		BulletType::BULLET_CABBAGE, mRow, launchPosition, mPlantType);
	if (!cabbage) return;
	cabbage->SetBulletDamage(kCabbageDamage);
	const bool polarGuided = mBoard->IsPolarWindDangerous()
		&& mBoard->PreparePolarLobbedNavigation(this);
	cabbage->ConfigureLobbedMotion(landingPosition, kFlightDuration,
		kArcApexHeight, targetsIceWall, polarGuided);
}

void CabbagePult::SaveExtraData(nlohmann::json& j) const
{
	j["shootTimer"] = mShootTimer;
	j["shootInterval"] = mShootInterval;
}

void CabbagePult::LoadExtraData(const nlohmann::json& j)
{
	mShootInterval = std::clamp(
		j.value("shootInterval", kInitialShootInterval),
		kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	mShootTimer = std::clamp(
		j.value("shootTimer", 0.0f), 0.0f, mShootInterval);
}

void CabbagePult::SetShootCycleForTesting(
	float elapsedSeconds, float intervalSeconds)
{
	mShootInterval = std::clamp(
		intervalSeconds, kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	mShootTimer = std::clamp(elapsedSeconds, 0.0f, mShootInterval);
}
