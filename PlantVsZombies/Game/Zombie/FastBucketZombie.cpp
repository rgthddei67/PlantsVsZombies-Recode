#include "FastBucketZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

namespace
{
	constexpr float kFastBucketMoveSpeedMin = 1.15f; // 初始化时随机移动速度倍率下限
	constexpr float kFastBucketMoveSpeedMax = 1.22f; // 初始化时随机移动速度倍率上限
	constexpr float kFastBucketAnimSpeedMin = 1.55f; // 初始化时随机能力动画倍率下限
	constexpr float kFastBucketAnimSpeedMax = 1.60f; // 初始化时随机能力动画倍率上限
	constexpr float kMagnetDestinationX = 25.0f; // 铁桶吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 20.0f; // 铁桶吸附到磁力菇头部附近的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f; // 离体装备落点随机扰动，单位 px

	const char* FastBucketImageKey(ArmorBrokenState stage)
	{
		switch (stage) {
		case ArmorBrokenState::A_LITTLE_BROKEN: return "IMAGE_FASTZOMBIE_BUCKET2";
		case ArmorBrokenState::REALLY_BROKEN: return "IMAGE_FASTZOMBIE_BUCKET3";
		default: return "IMAGE_FASTZOMBIE_BUCKET1";
		}
	}
}

void FastBucketZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
		GetTexture("IMAGE_FASTZOMBIE_BUCKET1"));

	if (mIsPreview) return;
	this->mHelmHealth = 600;
	this->mHelmMaxHealth = 600;
	this->mHelmType = HelmType::HELMTYPE_BUCKET;
	this->mSpeed *= GameRandom::Range(kFastBucketMoveSpeedMin, kFastBucketMoveSpeedMax);
	int damage = static_cast<int>(this->mAttackDamage * 1.5f);
	this->mAttackDamage = damage;
	mAbilityAnimSpeedMultiplier =
		GameRandom::Range(kFastBucketAnimSpeedMin, kFastBucketAnimSpeedMax);
}

float FastBucketZombie::GetAbilityAnimSpeedMultiplier() const
{
	return mAbilityAnimSpeedMultiplier;
}

void FastBucketZombie::RestoreLegacyAbilityAnimSpeedMultiplier(float multiplier)
{
	mAbilityAnimSpeedMultiplier = multiplier;
}

void FastBucketZombie::SaveExtraData(nlohmann::json& j) const
{
	j["helmStage"] = static_cast<int>(mHelmStage);
	j["abilityAnimSpeedMultiplier"] = mAbilityAnimSpeedMultiplier;
}

void FastBucketZombie::LoadExtraData(const nlohmann::json& j)
{
	mHelmStage = static_cast<ArmorBrokenState>(
		j.value("helmStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)));
	// 新档从派生数据恢复；旧档缺字段时保留 LoadProtectedData 已迁移的根字段 extraSpeed。
	mAbilityAnimSpeedMultiplier =
		j.value("abilityAnimSpeedMultiplier", mAbilityAnimSpeedMultiplier);
	UpdateAnimSpeed();
}

void FastBucketZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_bucket", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieFastBucketOff",
			GetPosition());
	}
}

void FastBucketZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("anim_bucket", true);
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
		GetTexture(FastBucketImageKey(mHelmStage)));
}

bool FastBucketZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_BUCKET;
}

bool FastBucketZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = FastBucketImageKey(mHelmStage);
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
