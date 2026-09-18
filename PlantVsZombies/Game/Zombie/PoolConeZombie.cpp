#include "PoolConeZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

/** 初始化路障耐久，同时保留泳池僵尸的事件与稳态轨道设置。 */
void PoolConeZombie::SetupZombie()
{
	PoolNormalZombie::SetupZombie();
	mHelmHealth = 370;
	mHelmMaxHealth = 370;
	mHelmType = HelmType::HELMTYPE_TRAFFIC_CONE;
}

/** 路障完全掉落时隐藏轨道，并沿用现有掉落特效。 */
void PoolConeZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_cone", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieConeOff", GetPosition());
	}
}

/** 按剩余耐久切换路障的两级破损贴图。 */
void PoolConeZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("anim_cone", true);
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	const char* imageKey = mHelmStage == ArmorBrokenState::NO_BROKEN
		? "IMAGE_ZOMBIE_CONE1"
		: (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN
			? "IMAGE_ZOMBIE_CONE2" : "IMAGE_ZOMBIE_CONE3");
	mAnimator->SetTrackImage("anim_cone",
		ResourceManager::GetInstance().GetTexture(imageKey));
}

/** 组合保存泳池状态与头盔破损阶段。 */
void PoolConeZombie::SaveExtraData(nlohmann::json& j) const
{
	PoolNormalZombie::SaveExtraData(j);
	j["helmStage"] = static_cast<int>(mHelmStage);
}

/** 组合恢复泳池状态与头盔破损阶段，并兼容缺字段旧档。 */
void PoolConeZombie::LoadExtraData(const nlohmann::json& j)
{
	PoolNormalZombie::LoadExtraData(j);
	mHelmStage = static_cast<ArmorBrokenState>(
		j.value("helmStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)));
}

/** 读档后重建路障显隐与破损贴图。 */
void PoolConeZombie::ZombieItemUpdate() const
{
	PoolNormalZombie::ZombieItemUpdate();
	if (mHelmStage == ArmorBrokenState::NONE || mHelmType == HelmType::HELMTYPE_NONE) {
		mAnimator->SetTrackVisible("anim_cone", false);
	}
	else if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		mAnimator->SetTrackImage("anim_cone", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_CONE2"));
	}
	else if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
		mAnimator->SetTrackImage("anim_cone", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_CONE3"));
	}
}
