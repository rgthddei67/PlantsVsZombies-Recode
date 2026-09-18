#include "ConeZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

#include "../../ResourceKeys.h"

void ConeZombie::SetupZombie()
{
	Zombie::SetupZombie();
	this->mHelmHealth = 370;
	this->mHelmMaxHealth = 370;
	this->mHelmType = HelmType::HELMTYPE_TRAFFIC_CONE;
}

void ConeZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_cone", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(GetConeDropEffectName(),
			GetPosition());
	}
}

const std::string& ConeZombie::GetConeTextureKey(ArmorBrokenState stage) const
{
	static const std::string kCone1 = "IMAGE_ZOMBIE_CONE1";
	static const std::string kCone2 = "IMAGE_ZOMBIE_CONE2";
	static const std::string kCone3 = "IMAGE_ZOMBIE_CONE3";
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) return kCone2;
	if (stage == ArmorBrokenState::REALLY_BROKEN) return kCone3;
	return kCone1;
}

void ConeZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (mHelmStage == ArmorBrokenState::NONE
		|| mHelmType == HelmType::HELMTYPE_NONE) {
		mAnimator->SetTrackVisible("anim_cone", false);
		return;
	}
	mAnimator->SetTrackImage("anim_cone",
		ResourceManager::GetInstance().GetTexture(GetConeTextureKey(mHelmStage)));
}

void ConeZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("anim_cone", true);
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	mAnimator->SetTrackImage("anim_cone",
		ResourceManager::GetInstance().GetTexture(GetConeTextureKey(mHelmStage)));
}
