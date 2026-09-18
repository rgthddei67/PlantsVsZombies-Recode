#include "FootballZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../AudioSystem.h"

namespace {
	constexpr float kFootballMoveSpeedMultiplier = 1.7f;	// 橄榄球僵尸相对基础僵尸的水平位移倍率
	constexpr float kFootballAnimSpeedMultiplier = 1.8f;	// 橄榄球僵尸自身的整体动画能力倍率
	constexpr float kMagnetDestinationX = 20.0f; // 头盔吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 20.0f; // 头盔吸附到磁力菇头部附近的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f; // 离体装备落点随机扰动，单位 px
}

void FootballZombie::SetupZombie()
{
	this->mHelmHealth = 1400;
	this->mHelmMaxHealth = 1400;
	this->mHelmType = HelmType::HELMTYPE_FOOTBALL;

	if (!mIsPreview) {
		this->mSpeed *= kFootballMoveSpeedMultiplier;

		PlayTrack("anim_walk");

		mAnimator->AddFrameEvent(63, [this]() {
			this->EatTarget();
			}, true);
		mAnimator->AddFrameEvent(80, [this]() {
			this->EatTarget();
			}, true);
		mAnimator->AddFrameEvent(104, [this]() {
			this->Die();
			});
	}
}

float FootballZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kFootballAnimSpeedMultiplier;
}

const char* FootballZombie::GetMagneticHelmetImageKey() const
{
	switch (mHelmStage) {
	case ArmorBrokenState::A_LITTLE_BROKEN: return "IMAGE_ZOMBIE_FOOTBALL_HELMET2";
	case ArmorBrokenState::REALLY_BROKEN: return "IMAGE_ZOMBIE_FOOTBALL_HELMET3";
	default: return "IMAGE_ZOMBIE_FOOTBALL_HELMET";
	}
}

bool FootballZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_FOOTBALL;
}

bool FootballZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = GetMagneticHelmetImageKey();
	item.worldPosition = GetTrackWorldPosition("zombie_football_helmet");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = 0.8f;
	mHelmHealth = 0;
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("zombie_football_helmet", false);
	return true;
}

void FootballZombie::CheckHelmImage()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("zombie_football_helmet", true);
	mHelmStage = mHelmHealth > static_cast<int64_t>(mHelmMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > mHelmMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	mAnimator->SetTrackImage("zombie_football_helmet", ResourceManager::GetInstance().
		GetTexture(GetMagneticHelmetImageKey()));
}

void FootballZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	g_particleSystem->EmitEffect("FootballZombieHeadOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void FootballZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("zombie_football_helmet", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieFootballOff",
			GetPosition());
	}
}

void FootballZombie::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("zombie_football_leftarm_hand", false);
	mAnimator->SetTrackVisible("zombie_football_leftarm_lower", false);
	mAnimator->SetTrackImage("zombie_football_leftarm_upper", ResourceManager::GetInstance().
		GetTexture("IMAGE_ZOMBIE_FOOTBALL_LEFTARM_UPPER2"));
	g_particleSystem->EmitEffect("FootballZombieArmOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void FootballZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (!mAnimator) return;
	if (mHasArm) {
		mAnimator->SetTrackVisible("zombie_football_leftarm_hand", true);
		mAnimator->SetTrackVisible("zombie_football_leftarm_lower", true);
		mAnimator->SetTrackImage("zombie_football_leftarm_upper", nullptr);
	}
}
