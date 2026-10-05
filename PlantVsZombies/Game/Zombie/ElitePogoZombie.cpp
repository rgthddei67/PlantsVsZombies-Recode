#include "ElitePogoZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../Plant/Plant.h"
#include "../../ResourceKeys.h"

namespace {
	constexpr int kElitePogoHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ELITE_POGO).body;               // 精英跳跳本体生命值
	constexpr float kElitePogoSpeedMultiplier = 1.15f;  // 持杆推进与动画的品种能力倍率
	constexpr int kElitePogoImpactDamage = 600;         // 第一次高坚果阻拦时造成的僵尸来源伤害
}

void ElitePogoZombie::SetupZombie()
{
	PogoZombie::SetupZombie();
	mBodyMaxHealth = kElitePogoHealth;
	mBodyHealth = kElitePogoHealth;
}

float ElitePogoZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kElitePogoSpeedMultiplier;
}

bool ElitePogoZombie::HandlePogoJumpBlocked(Plant& plant)
{
	if (!mImpactBufferAvailable) return PogoZombie::HandlePogoJumpBlocked(plant);

	// 缓冲器只抵消一次阻拦；伤害仍走标准僵尸来源链以继承词条倍率。
	mImpactBufferAvailable = false;
	plant.TakeDamage(kElitePogoImpactDamage, DamageSource::ZOMBIE);
	return false;
}

const std::string& ElitePogoZombie::GetDamagedOuterArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_OUTERARM_UPPER2;
}

const std::string& ElitePogoZombie::GetDamagedStickTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_STICKDAMAGE2;
}

const std::string& ElitePogoZombie::GetDamagedStick2TextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_STICK2DAMAGE2;
}

void ElitePogoZombie::SaveExtraData(nlohmann::json& j) const
{
	PogoZombie::SaveExtraData(j);
	j["impactBufferAvailable"] = mImpactBufferAvailable;
}

void ElitePogoZombie::LoadExtraData(const nlohmann::json& j)
{
	PogoZombie::LoadExtraData(j);
	mImpactBufferAvailable = j.value("impactBufferAvailable", true);
}

ZombieMovementRules::BirthProfile ElitePogoZombie::GetBirthMovementProfile()
{
	auto p=PogoZombie::GetBirthMovementProfile();
	p.velocityMinimum*=kElitePogoSpeedMultiplier;
	p.velocityMaximum*=kElitePogoSpeedMultiplier;
	return p;
}
