#include "BucketZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

namespace {
	constexpr float kMagnetDestinationX = 25.0f; // C# 铁桶吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 20.0f; // C# 铁桶吸附到磁力菇头部附近的局部 Y
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

void BucketZombie::SetupZombie()
{
	Zombie::SetupZombie();
	this->mHelmHealth = 1100;
	this->mHelmMaxHealth = 1100;
	this->mHelmType = HelmType::HELMTYPE_BUCKET;
}

void BucketZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_bucket", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBucketOff",
			GetPosition());
	}
}

void BucketZombie::CheckHelmImage()
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

bool BucketZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_BUCKET;
}

bool BucketZombie::ExtractMagneticItem(MagneticItem& item)
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
