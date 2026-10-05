#include "RedeyeGargantuarZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../../ResourceKeys.h"

namespace {
	constexpr int kBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_REDEYE_GARGANTUAR).body; // 原版红眼巨人本体生命；其余行为与经典巨人一致
}

/** 复用经典巨人的全部动画事件、武器、砸击、投掷与存档初始化，仅覆盖生命和头部材质。 */
void RedeyeGargantuarZombie::SetupZombie()
{
	GargantuarZombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	ApplyDamagePresentation();
}

const std::string& RedeyeGargantuarZombie::GetHeadTextureKey(int damageStage) const
{
	return damageStage >= 2
		? ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD2_REDEYE
		: ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD_REDEYE;
}
