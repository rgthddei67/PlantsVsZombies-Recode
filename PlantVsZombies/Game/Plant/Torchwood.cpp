#include "Torchwood.h"

#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"

#include <algorithm>

namespace {
	constexpr float kTorchwoodAttackOffsetX = 10.0f; // 原版 mX+50 换算到本项目格心逻辑坐标后的起点
	constexpr float kTorchwoodAttackWidth = 30.0f;   // 原版火炬树桩横向转换判定宽度，单位：像素
	constexpr float kMinimumOverlap = 1.0f;          // 原版 GetRectOverlap 至少重叠 1px 才触发
}

void Torchwood::PlantUpdate()
{
	if (!mBoard) return;
	if (mBoard->IsWorkshop() && mAnimator) mAnimator->SetOverlayColor(255, 176, 65, 65);

	const float attackLeft = GetPosition().x + kTorchwoodAttackOffsetX;
	const float attackRight = attackLeft + kTorchwoodAttackWidth;
	for (int bulletID : mBoard->mEntityRegistry.GetAllBulletIDs()) {
		Bullet* bullet = mBoard->mEntityRegistry.GetBullet(bulletID);
		if (!bullet || !bullet->IsActive() || bullet->mRow != mRow) continue;
		if (bullet->mBulletType != BulletType::BULLET_PEA
			&& bullet->mBulletType != BulletType::BULLET_TOXICPEA
			&& bullet->mBulletType != BulletType::BULLET_SNOWPEA
			&& bullet->mBulletType != BulletType::BULLET_AURORA_PEA) {
			continue;
		}

		const ColliderComponent* collider = bullet->GetColliderComponent();
		if (!collider) continue;
		const SDL_FRect bounds = collider->GetBoundingBox();
		const float overlap = std::min(attackRight, bounds.x + bounds.w)
			- std::max(attackLeft, bounds.x);
		if (overlap < kMinimumOverlap) continue;

		if (bullet->mBulletType == BulletType::BULLET_PEA
			|| bullet->mBulletType == BulletType::BULLET_TOXICPEA
			|| bullet->mBulletType == BulletType::BULLET_AURORA_PEA) {
			bullet->ConvertToFireball(mColumn);
		}
		else {
			bullet->ConvertSnowPeaToPea(mColumn);
		}
	}
}
