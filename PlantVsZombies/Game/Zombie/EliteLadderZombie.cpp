#include "EliteLadderZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "Game/Board/Board.h"
#include "../GameObjectManager.h"
#include "../Plant/Plant.h"
#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"

namespace {
	constexpr int kEliteLadderBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ELITE_LADDER).body; // 精英扶梯本体初始生命；保留普通扶梯的 500 点扶梯防具
	constexpr float kRowScanDelaySeconds = 5.0f; // 出场后一次性整行扫描的游戏时间，单位：秒
	constexpr int64_t kInfiniteLadderHealthThreshold = 6000; // 严格高于此当前总血量时保留无限搭梯
	constexpr int kBodyHealthBonus = 500; // 投手数量较多时增加的本体当前/最大生命
	constexpr int kShieldHealthMultiplier = 2; // 射手数量较多时扶梯当前/最大生命倍率
	constexpr float kAnimationSpeedMultiplier = 2.0f; // 本行总血量严格低于阈值时的整体动画倍率

	bool IsPultPlant(PlantType type)
	{
		switch (type) {
		case PlantType::PLANT_CABBAGEPULT:
		case PlantType::PLANT_KERNELPULT:
		case PlantType::PLANT_MELONPULT:
		case PlantType::PLANT_WINTERMELON:
			return true;
		default:
			return false;
		}
	}

	bool IsShooterPlant(PlantType type)
	{
		// “射手”包含直线、扇形和对空远程植物；投手由上方独立列表排除。
		switch (type) {
		case PlantType::PLANT_PEASHOOTER:
		case PlantType::PLANT_SNOWPEA:
		case PlantType::PLANT_REPEATER:
		case PlantType::PLANT_PUFFSHROOM:
		case PlantType::PLANT_FUMESHROOM:
		case PlantType::PLANT_SCAREDYSHROOM:
		case PlantType::PLANT_ICEFUMESHROOM:
		case PlantType::PLANT_THREEPEATER:
		case PlantType::PLANT_SEASHROOM:
		case PlantType::PLANT_CACTUS:
		case PlantType::PLANT_SPLITPEA:
		case PlantType::PLANT_STARFRUIT:
		case PlantType::PLANT_GATLINGPEA:
		case PlantType::PLANT_GLOOMSHROOM:
		case PlantType::PLANT_CATTAIL:
		case PlantType::PLANT_LEFTPEATER:
		case PlantType::PLANT_ELITE_SCAREDYSHROOM:
		case PlantType::PLANT_TOXICPEASHOOTER:
			return true;
		default:
			return false;
		}
	}
}

void EliteLadderZombie::SetupZombie()
{
	// 与经典扶梯共用 reanim 时间线和主人确认的全部帧事件，只扩展资源与五秒能力。
	LadderZombie::SetupZombie();
	mBodyHealth = kEliteLadderBodyHealth;
	mBodyMaxHealth = kEliteLadderBodyHealth;
	mRowScanTimeRemaining = kRowScanDelaySeconds;
}

void EliteLadderZombie::Update()
{
	LadderZombie::Update();
	if (mRowScanComplete || mIsPreview || mIsDying || !IsActive() || !mBoard
		|| IsParalyzed() || mBoard->mBoardState != BoardState::GAME) {
		return;
	}

	// 出场计时独立于旧有走路/啃食/冰冻控制；通用麻痹明确冻结尚未释放的技能。
	mRowScanTimeRemaining = std::max(0.0f,
		mRowScanTimeRemaining - DeltaTime::GetDeltaTime());
	if (mRowScanTimeRemaining <= 0.0f) ScanRowAndApplyAbilities();
}

void EliteLadderZombie::ScanRowAndApplyAbilities()
{
	if (mRowScanComplete || !mBoard) return;
	mRowScanComplete = true;
	mRowScanTimeRemaining = 0.0f;
	mScannedPlantHealth = 0;
	mScannedPultCount = 0;
	mScannedShooterCount = 0;

	for (const auto& object : GameObjectManager::GetInstance().GetAllGameObjects()) {
		auto* plant = object ? dynamic_cast<Plant*>(object.get()) : nullptr;
		if (!plant || !plant->IsActive() || plant->IsPreview() || plant->IsSquished()
			|| plant->mRow != mRow) {
			continue;
		}
		mScannedPlantHealth += std::max(0, plant->mPlantHealth);
		if (IsPultPlant(plant->mPlantType)) ++mScannedPultCount;
		else if (IsShooterPlant(plant->mPlantType)) ++mScannedShooterCount;
	}

	if (mScannedPlantHealth > kInfiniteLadderHealthThreshold) {
		mInfiniteLadderAbility = true;
		// 黄色扶梯是无限能力的状态提示；命中分支后立即同步当前损坏阶段的携梯贴图。
		CheckShieldImage();
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("EliteLadderInfiniteBuff",
				GetTrackWorldPosition("Zombie_ladder_1"));
		}
	}
	else if (mScannedPlantHealth < kInfiniteLadderHealthThreshold) {
		mDoubledAnimationSpeed = true;
		UpdateAnimSpeed();
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("EliteLadderHasteBuff",
				GetTrackWorldPosition("Zombie_ladder_body"));
		}
	}

	if (mScannedPultCount > mScannedShooterCount) {
		mBodyHealth += kBodyHealthBonus;
		mBodyMaxHealth += kBodyHealthBonus;
		mBodyHealthBonusApplied = true;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("EliteLadderBodyBuff",
				GetTrackWorldPosition("Zombie_ladder_body"));
		}
	}
	else if (mScannedPultCount < mScannedShooterCount) {
		mShieldHealth *= kShieldHealthMultiplier;
		mShieldMaxHealth *= kShieldHealthMultiplier;
		mShieldHealthDoubled = true;
		CheckShieldImage();
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("EliteLadderShieldBuff",
				GetTrackWorldPosition("Zombie_ladder_1"));
		}
	}
}

float EliteLadderZombie::GetAbilityAnimSpeedMultiplier() const
{
	return mDoubledAnimationSpeed ? kAnimationSpeedMultiplier : 1.0f;
}

bool EliteLadderZombie::RetainsLadderAfterPlacement() const
{
	return mInfiniteLadderAbility;
}

LadderStyle EliteLadderZombie::GetPlacedLadderStyle() const
{
	return mInfiniteLadderAbility ? LadderStyle::ELITE : LadderStyle::CLASSIC;
}

const std::string& EliteLadderZombie::GetShieldTextureKey(ArmorBrokenState stage) const
{
	if (!mInfiniteLadderAbility) {
		return LadderZombie::GetShieldTextureKey(stage);
	}
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1_DAMAGE1;
	}
	if (stage == ArmorBrokenState::REALLY_BROKEN) {
		return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1_DAMAGE2;
	}
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1;
}

const std::string& EliteLadderZombie::GetBrokenArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_OUTERARM_UPPER2;
}

const char* EliteLadderZombie::GetLadderDropEffectName() const
{
	return mInfiniteLadderAbility ? "ZombieEliteLadder" : "ZombieLadder";
}

void EliteLadderZombie::SetRowScanTimeRemainingForTesting(float seconds)
{
	if (mRowScanComplete) return;
	mRowScanTimeRemaining = std::clamp(seconds, 0.0f, kRowScanDelaySeconds);
}

void EliteLadderZombie::SaveExtraData(nlohmann::json& j) const
{
	LadderZombie::SaveExtraData(j);
	j["rowScanTimeRemaining"] = mRowScanTimeRemaining;
	j["rowScanComplete"] = mRowScanComplete;
	j["infiniteLadderAbility"] = mInfiniteLadderAbility;
	j["doubledAnimationSpeed"] = mDoubledAnimationSpeed;
	j["bodyHealthBonusApplied"] = mBodyHealthBonusApplied;
	j["shieldHealthDoubled"] = mShieldHealthDoubled;
	j["scannedPlantHealth"] = mScannedPlantHealth;
	j["scannedPultCount"] = mScannedPultCount;
	j["scannedShooterCount"] = mScannedShooterCount;
}

void EliteLadderZombie::LoadExtraData(const nlohmann::json& j)
{
	LadderZombie::LoadExtraData(j);
	mRowScanComplete = j.value("rowScanComplete", false);
	mRowScanTimeRemaining = mRowScanComplete ? 0.0f : std::clamp(
		j.value("rowScanTimeRemaining", kRowScanDelaySeconds),
		0.0f, kRowScanDelaySeconds);
	mInfiniteLadderAbility = j.value("infiniteLadderAbility", false);
	mDoubledAnimationSpeed = j.value("doubledAnimationSpeed", false);
	mBodyHealthBonusApplied = j.value("bodyHealthBonusApplied", false);
	mShieldHealthDoubled = j.value("shieldHealthDoubled", false);
	mScannedPlantHealth = std::max<int64_t>(0, j.value("scannedPlantHealth", int64_t{ 0 }));
	mScannedPultCount = std::max(0, j.value("scannedPultCount", 0));
	mScannedShooterCount = std::max(0, j.value("scannedShooterCount", 0));
	CheckShieldImage();
	UpdateAnimSpeed();
}

ZombieMovementRules::BirthProfile EliteLadderZombie::GetBirthMovementProfile()
{
	auto p=LadderZombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
