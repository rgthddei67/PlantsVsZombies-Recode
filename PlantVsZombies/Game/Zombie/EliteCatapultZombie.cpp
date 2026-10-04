#include "EliteCatapultZombie.h"

#include "../../ResourceKeys.h"

namespace {
	constexpr float kEliteRoofRunoffDriftMultiplier = 5.0f / 3.0f; // 把 Board 的 -60 px/s 径流放大为自身 -100 px/s
}

void EliteCatapultZombie::SetupZombie()
{
	// 父类保留十二发篮球、46 帧发射、装填、碾压、爆胎与存档状态机。
	CatapultZombie::SetupZombie();
	mBodyHealth = CatapultRules::kEliteBodyHealth;
	mBodyMaxHealth = CatapultRules::kEliteBodyHealth;
}

bool EliteCatapultZombie::CanGuideRoofRunoff() const
{
	return !mIsPreview && !mIsDying && !mIsDead && IsActive()
		&& !IsCaltropPunctured();
}

float EliteCatapultZombie::GetRoofRunoffDriftMultiplier() const
{
	// 爆胎只撤销精英加成；基类仍会继续应用普通目标行径流，直至 2.8 秒死亡流程结束。
	return IsCaltropPunctured() ? 1.0f : kEliteRoofRunoffDriftMultiplier;
}

const std::string& EliteCatapultZombie::GetCatapultSidingTextureKey(bool damaged) const
{
	return damaged
		? ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_CATAPULT_SIDING_DAMAGE
		: ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ELITE_CATAPULT_SIDING;
}

const char* EliteCatapultZombie::GetCatapultExplosionEffectName() const
{
	return "EliteCatapultExplosion";
}
