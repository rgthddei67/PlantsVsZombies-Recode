#include "PinkFootballZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"

namespace {
	// 自定义夜晚变体；普通橄榄球僵尸当前为本体 270、头盔 1100、速度层 1.7/1.8。
	constexpr int kBodyHealth = 220;
	constexpr int kHelmetHealth = 900;
	constexpr int kNormalBiteDamage = 40;
	constexpr int kFirstPlantStrikeDamage = 400;
	constexpr int kHelmetBreakDamage = 50;
	constexpr float kFootballMoveSpeedMultiplier = 1.7f;
	constexpr float kMoveSpeedMultiplier = 1.85f;
	constexpr float kAnimationSpeedMultiplier = 1.95f;
	constexpr float kHelmetBreakRadius = 120.0f;
}

/**
 * @brief 初始化粉色橄榄球僵尸的数值，并复用橄榄球动画已有的啃食/死亡帧时序。
 */
void PinkFootballZombie::SetupZombie()
{
	// 基类负责注册原橄榄球动画已有的啃食/死亡事件；本变体不增加新帧事件。
	FootballZombie::SetupZombie();

	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mHelmHealth = kHelmetHealth;
	mHelmMaxHealth = kHelmetHealth;
	mHelmType = HelmType::HELMTYPE_FOOTBALL;
	mHelmStage = ArmorBrokenState::NO_BROKEN;
	mAttackDamage = kNormalBiteDamage;
	mFirstPlantStrikeUsed = false;

	if (mIsPreview) return;

	// 基类已经应用 1.7 倍移速，只补足到本变体的 1.85 倍。
	mSpeed *= kMoveSpeedMultiplier / kFootballMoveSpeedMultiplier;
}

float PinkFootballZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kAnimationSpeedMultiplier;
}

const char* PinkFootballZombie::GetMagneticHelmetImageKey() const
{
	switch (mHelmStage) {
	case ArmorBrokenState::A_LITTLE_BROKEN: return "IMAGE_ZOMBIE_PINKFOOTBALL_HELMET2";
	case ArmorBrokenState::REALLY_BROKEN: return "IMAGE_ZOMBIE_PINKFOOTBALL_HELMET3";
	default: return "IMAGE_ZOMBIE_PINKFOOTBALL_HELMET";
	}
}

/**
 * @brief 第一次真正啃到植物时临时把本次伤害提升为 400，之后恢复 40。
 */
void PinkFootballZombie::EatTarget()
{
	if (!mFirstPlantStrikeUsed && mEatPlantID != NULL_PLANT_ID && mHasHead && mBoard
		&& IsCurrentPlantEatingTargetValid()) {
		const int normalDamage = mAttackDamage;
		mAttackDamage = kFirstPlantStrikeDamage;
		FootballZombie::EatTarget();
		mAttackDamage = normalDamage;
		mFirstPlantStrikeUsed = true;
		return;
	}

	FootballZombie::EatTarget();
}

/**
 * @brief 根据粉色头盔剩余血量切换两档破损贴图。
 */
void PinkFootballZombie::CheckHelmImage()
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

/**
 * @brief 头盔破碎后发射粉色掉落物，并对半径 120 像素内植物造成一次范围伤害。
 */
void PinkFootballZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;

	Zombie::HelmDrop();
	mHelmStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("zombie_football_helmet", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombiePinkFootballOff", GetPosition());
	}
	DamagePlantsNearBrokenHelmet();
}

/**
 * @brief 复用橄榄球断臂轨道，但换成粉色残臂与粉色手臂掉落物。
 */
void PinkFootballZombie::ArmDrop()
{
	if (!mHasArm) return;

	mAnimator->SetTrackVisible("zombie_football_leftarm_hand", false);
	mAnimator->SetTrackVisible("zombie_football_leftarm_lower", false);
	mAnimator->SetTrackImage("zombie_football_leftarm_upper", ResourceManager::GetInstance().
		GetTexture("IMAGE_ZOMBIE_PINKFOOTBALL_LEFTARM_UPPER2"));
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PinkFootballZombieArmOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void PinkFootballZombie::DamagePlantsNearBrokenHelmet()
{
	if (!mBoard) return;

	const Vector center = GetPosition();
	mBoard->ApplyPumpkinProtectedZombieAreaDamage(kHelmetBreakDamage,
		[center](const Plant& plant) {
			const Vector plantPosition = plant.GetPosition();
			const float deltaX = plantPosition.x - center.x;
			const float deltaY = plantPosition.y - center.y;
			return deltaX * deltaX + deltaY * deltaY
				<= kHelmetBreakRadius * kHelmetBreakRadius;
		});
}

/**
 * @brief 读档后重建粉色防具与残臂轨道，保持与实时掉落路径一致。
 */
void PinkFootballZombie::ZombieItemUpdate() const
{
	if (!mHasArm) {
		mAnimator->SetTrackVisible("zombie_football_leftarm_hand", false);
		mAnimator->SetTrackVisible("zombie_football_leftarm_lower", false);
		mAnimator->SetTrackImage("zombie_football_leftarm_upper", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_PINKFOOTBALL_LEFTARM_UPPER2"));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}

	if (mHelmStage == ArmorBrokenState::NONE || mHelmType == HelmType::HELMTYPE_NONE) {
		mAnimator->SetTrackVisible("zombie_football_helmet", false);
	}
	else if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
		mAnimator->SetTrackImage("zombie_football_helmet", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_PINKFOOTBALL_HELMET2"));
	}
	else if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
		mAnimator->SetTrackImage("zombie_football_helmet", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_PINKFOOTBALL_HELMET3"));
	}
}

/** @brief 保存头盔破损阶段与首次植物重击是否已经消耗。 */
void PinkFootballZombie::SaveExtraData(nlohmann::json& j) const
{
	FootballZombie::SaveExtraData(j);
	j["firstPlantStrikeUsed"] = mFirstPlantStrikeUsed;
}

/** @brief 恢复头盔破损阶段与首次植物重击状态，避免读档后重复触发 400 伤害。 */
void PinkFootballZombie::LoadExtraData(const nlohmann::json& j)
{
	FootballZombie::LoadExtraData(j);
	mFirstPlantStrikeUsed = j.value("firstPlantStrikeUsed", false);
}
