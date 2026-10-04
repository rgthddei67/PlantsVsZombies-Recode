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
	float remaining=std::max(0.0f,mShootTime-mShootTimer)+ThunderFlowerRules::Windup;
	// 复用既有第64帧事件；头部正在发射但未出膛时，先兑现这一次待提交攻击。
	if (mHeadAnim && mHeadAnim->GetCurrentTrackName()=="anim_shooting" && mHeadAnim->GetCurrentFrame()<=64)
		remaining=std::min(remaining,std::max(0.0f,64-mHeadAnim->GetCurrentFrame())/18.0f);
	return remaining;
}
