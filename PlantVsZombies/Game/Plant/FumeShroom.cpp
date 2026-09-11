#include "FumeShroom.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "Game/Board/Board.h"
#include <algorithm>
#include <vector>

void FumeShroom::SetupPlant()
{
	Shroom::SetupPlant();

	if (mIsPreview) return;

	mAnimator->AddFrameEvent(27, [this]() {
		FireFume();
		}, true);
}

void FumeShroom::FireFume()
{
	if (!mBoard) return;
	AudioSystem::PlaySound("SOUND_FUME", 0.28f);
	// 先确定阻断点，再裁剪粒子，保持经典大喷菇的命中与表现一致。
	const float clipRightX = FumeAttack();
	g_particleSystem->EmitEffect(FumeParticleName(), GetPosition(), LAYER_EFFECTS_WORLD, -1.0f, clipRightX);
}

void FumeShroom::PlantUpdate()
{
	// 生存攻速词条 × 雨势行动倍率；喷雾结算帧和攻击间隔必须同比例提前。
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

bool FumeShroom::HasZombieInRow()
{
	if (mBoard)
	{
		mCheckZombieTimer += DeltaTime::GetDeltaTime();
		if (mCheckZombieTimer >= 0.6f)
		{
			mCheckZombieTimer = 0.0f;
			// 按行索引：只遍历本行僵尸，mRow 过滤已由桶保证。
			const float thisX = GetPosition().x;
			bool found = false;
			mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
				if (found) return;  // 已命中，跳过本行其余
				float dx = zombie->GetPosition().x - thisX;
				// 跳过魅惑僵尸：全行只剩魅惑时不触发喷射动画（与 Chomper/PotatoMine 索敌跳过魅惑同一惯例）
				if (!zombie->IsMindControlled() && dx >= 0 && dx <= mFumeReach
					&& zombie->HasHead()
					&& mBoard->CanPlantAcquireZombie(this, zombie))
					found = true;
				});
			return found;
		}
	}
	return false;
}

float FumeShroom::FumeAttack()
{
	if (!mBoard) return -1.0f;

	const float thisX = GetPosition().x;
	std::vector<Zombie*> targets;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (mBoard->MineBlocksSegment(GetPosition(), zombie->GetPosition())) return;
		const float dx = zombie->GetPosition().x - thisX;
		// 豁免魅惑僵尸：原版 DoRowAreaDamage(20, 2U) 的 damageRangeFlags 不含 bit7（不炸魅惑目标）
		if (dx >= 0 && dx <= mFumeReach && zombie->HasHead()
			&& !zombie->IsMindControlled()
			&& zombie->CanBeTargetedByProjectile(false))
			targets.push_back(zombie);
		});

	// 行桶不保证世界位置顺序；阻断语义必须按孢子从植物向右传播的顺序结算。
	std::sort(targets.begin(), targets.end(), [](Zombie* lhs, Zombie* rhs) {
		return lhs->GetPosition().x < rhs->GetPosition().x;
		});

	for (Zombie* zombie : targets) {
		// 命中开始时取阻断状态：即使这一击恰好打掉门，本次喷雾仍应在门前截止。
		const bool blocksFume = zombie->BlocksFumePiercing();
		const int damage = zombie->ModifyFumeDamage(mFumeDamage);
		// 阻断门只承受门伤且吸收破门溢出；普通目标保持原版喷雾穿透二类护盾的语义。
		zombie->TakeDamage(damage, DamageSource::PLANT,
			/*penetrateShield=*/!blocksFume, /*discardShieldOverflow=*/blocksFume,
			/*bypassShield=*/false, PlantDamageOrigin::FromPlant(mPlantType));
		OnFumeHit(zombie);

		if (blocksFume) {
			if (auto* collider = zombie->GetColliderComponent()) {
				return collider->GetBoundingBox().x; // 门的迎击面，视觉不穿过判定矩形前沿
			}
			return zombie->GetPosition().x;
		}
	}
	return -1.0f;
}
