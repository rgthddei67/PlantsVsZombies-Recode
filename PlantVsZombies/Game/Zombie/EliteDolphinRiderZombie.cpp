#include "EliteDolphinRiderZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../Plant/Plant.h"
#include "../../ResourceKeys.h"

namespace {
	constexpr int kEliteDolphinRiderHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER).body;  // 精英海豚骑士本体基础生命
	constexpr int kEliteDolphinJumpCapacity = 2;  // 普通植物最多连续越过次数
	constexpr int kTallNutBlockDamage = 500;       // 被高坚果拦下后的基础碰撞伤害
}

/**
 * @brief 复用普通海豚的完整时间线、声音和状态机，只覆盖精英数值。
 */
void EliteDolphinRiderZombie::SetupZombie()
{
	DolphinRiderZombie::SetupZombie();
	mBodyHealth = kEliteDolphinRiderHealth;
	mBodyMaxHealth = kEliteDolphinRiderHealth;
}

int EliteDolphinRiderZombie::GetDolphinJumpCapacity() const
{
	return kEliteDolphinJumpCapacity;
}

/**
 * @brief 高坚果完成阻拦且精英恢复啃食状态后，结算 500 点僵尸来源伤害。
 */
void EliteDolphinRiderZombie::OnDolphinJumpBlocked(Plant& blockingPlant)
{
	if (mIsPreview || mIsDying) return;
	blockingPlant.TakeDamage(kTallNutBlockDamage, DamageSource::ZOMBIE);
}

const std::string& EliteDolphinRiderZombie::GetLostOuterArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDOLPHINRIDER_OUTERARM_UPPER2;
}

const char* EliteDolphinRiderZombie::GetDolphinHeadOffEffectName() const
{
	return "EliteDolphinRiderHeadOff";
}
