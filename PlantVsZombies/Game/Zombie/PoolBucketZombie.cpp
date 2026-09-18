#include "PoolBucketZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

namespace {
	constexpr float kMagnetDestinationX = 25.0f; // 铁桶吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 20.0f; // 铁桶吸附到磁力菇头部附近的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f; // 离体装备落点随机扰动，单位 px

	const char* BucketImageKey(ArmorBrokenState stage)
	{
		switch (stage) {
		case ArmorBrokenState::A_LITTLE_BROKEN: return "IMAGE_ZOMBIE_BUCKET2";
		case ArmorBrokenState::REALLY_BROKEN: return "IMAGE_ZOMBIE_BUCKET3";
		default: return "IMAGE_ZOMBIE_BUCKET1";
		}
	}
}

/** 初始化铁桶耐久，同时保留泳池僵尸的事件与稳态轨道设置。 */
void PoolBucketZombie::SetupZombie()
{
	PoolNormalZombie::SetupZombie();
	mHelmHealth = 1100;
	mHelmMaxHealth = 1100;
	mHelmType = HelmType::HELMTYPE_BUCKET;
}

/** 铁桶完全掉落时隐藏轨道，并沿用现有掉落特效。 */
void PoolBucketZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_bucket", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBucketOff", GetPosition());
	}
}

bool PoolBucketZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_BUCKET;
}

bool PoolBucketZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = BucketImageKey(mHelmStage);
	item.worldPosition = GetTrackWorldPosition("anim_bucket");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = 0.8f;
	mHelmHealth = 0;
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_bucket", false);
	return true;
}

/** 按剩余耐久切换铁桶的两级破损贴图。 */
void PoolBucketZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("anim_bucket", true);
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
		GetTexture(BucketImageKey(mHelmStage)));
}

/** 组合保存泳池状态与头盔破损阶段。 */
void PoolBucketZombie::SaveExtraData(nlohmann::json& j) const
{
	PoolNormalZombie::SaveExtraData(j);
	j["helmStage"] = static_cast<int>(mHelmStage);
}

/** 组合恢复泳池状态与头盔破损阶段，并兼容缺字段旧档。 */
void PoolBucketZombie::LoadExtraData(const nlohmann::json& j)
{
	PoolNormalZombie::LoadExtraData(j);
	mHelmStage = static_cast<ArmorBrokenState>(
		j.value("helmStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)));
}

/** 读档后重建铁桶显隐与破损贴图。 */
void PoolBucketZombie::ZombieItemUpdate() const
{
	PoolNormalZombie::ZombieItemUpdate();
	if (mHelmStage == ArmorBrokenState::NONE || mHelmType == HelmType::HELMTYPE_NONE) {
		mAnimator->SetTrackVisible("anim_bucket", false);
	}
	else if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_BUCKET2"));
	}
	else if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
		mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_BUCKET3"));
	}
}
