#include "ThunderFlower.h"
#include "ThunderFlowerRules.h"
#include "Game/Board/Board.h"

void ThunderFlower::SetupPlant()
{
	Shooter::SetupPlant();
	mShootTime = ThunderFlowerRules::Interval;
	mShootTimer = 0;
}

/** 沿豌豆射手原有发射帧平射雷种，命中后的电弧另由 Board 结算。 */
void ThunderFlower::ShootBullet()
{
	if (!mBoard || mIsPreview) return;
	auto* bullet=mBoard->CreatePlantBullet(BulletType::BULLET_THUNDER_SEED,mRow,
		GetVisualAnchorPosition()+Vector(30,-30),mPlantType);
	if (bullet) bullet->SetVelocityX(ThunderFlowerRules::ProjectileSpeed);
}

float ThunderFlower::GetAttackRemaining() const
{
	return GetAttackForecast().NominalRemaining();
}

ThunderFlowerRules::AttackForecast ThunderFlower::GetAttackForecast() const
{
	ThunderFlowerRules::AttackForecast forecast;
	forecast.cooldownRemaining = std::max(0.0f,mShootTime-mShootTimer);
	forecast.checkRemaining = std::max(0.0f,ShooterRules::TargetCheckSeconds-GetTargetCheckElapsed());
	forecast.sampledRate = GetAttackSpeedMultiplier();
	// 已过吐弹帧不再补发；正在前摇的雷种即使场上目标离开，也会按正式帧事件兑现。
	if (mHeadAnim && mHeadAnim->GetCurrentTrackName() == "anim_shooting" && mHeadAnim->GetCurrentFrame() < 64)
		forecast.pendingRemaining = std::max(0.0f,64-mHeadAnim->GetCurrentFrame())/18.0f;
	return forecast;
}
