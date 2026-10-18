#include "PuffShroom.h"
#include "../ShadowComponent.h"

void PuffShroom::SetupPlant()
{
	auto shadow = GetShadow();
	shadow->SetScale(Vector(0.6f, 0.6f));
	shadow->SetOffset(Vector(3, 30));

	Shroom::SetupPlant();

	if (mIsPreview) return;
	
	mAnimator->AddFrameEvent(28, [this]() {
		if (!mBoard) return;
		AudioSystem::PlaySound("SOUND_PUFF", 0.28f);
		Vector bulletPosition = GetPosition() + Vector(13, 13);
		mBoard->CreatePlantBullet(BulletType::BULLET_PUFF, mRow, bulletPosition, mPlantType);
		}, true);
}

void PuffShroom::PlantUpdate()
{
	// 生存攻速词条 × 雨势行动倍率；吐弹帧与攻击间隔同步缩短。
	float mult = GetAttackSpeedMultiplier();
	this->mShootTimer += (DeltaTime::GetDeltaTime() * mult);
	if (this->mShootTimer >= this->mShootTime)
	{
		if (HasZombieInRow())
		{
			mShootTimer = 0;
			// 动画同比例加快：吐弹的第 28 帧 frame event 跟上更短间隔
			PlayTrackOnce("anim_shooting", "anim_idle", 1.5f * mult, 0.2f);
		}
	}
}

bool PuffShroom::HasZombieInRow()
{
	if (mBoard)
	{
		// 肥沃同步缩短攻击前索敌间隔，避免固定节流抵消高频攻击。
		mCheckZombieTimer += DeltaTime::GetDeltaTime() * GetFertilitySpeedMultiplier();
		if (mCheckZombieTimer >= 0.6f)
		{
			mCheckZombieTimer = 0.0f;
			// 按行索引：只遍历本行僵尸，mRow 过滤已由桶保证。
			const float thisX = GetPosition().x;
			bool found = false;
			mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
				if (found) return;  // 已命中，跳过本行其余
				float dx = zombie->GetPosition().x - thisX;
				if (!zombie->IsMindControlled() && dx >= 0 && dx <= 300.0f
					&& zombie->HasHead()
					&& mBoard->CanPlantAcquireZombie(this, zombie))
					found = true;
			});
			return found;
		}
	}
	return false;
}
