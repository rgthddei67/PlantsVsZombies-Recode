#include "EliteDiggerZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"

#include <algorithm>

namespace {
	constexpr int kEliteBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ELITE_DIGGER).body;              // 爆破工头本体生命值
	constexpr int kEliteHardhatHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ELITE_DIGGER).helm;           // 爆破工头安全帽生命值
	constexpr int kBlastDamage = 150;                  // 爆破对每个植物层造成的固定伤害
	constexpr int kPumpkinBlastDamageMultiplier = 4;   // 爆破被九宫格南瓜保护拦截时的基础伤害倍率
	constexpr int kBlastMinColumn = 0;                 // 爆区从房屋侧第 0 列开始
	constexpr int kBlastMaxColumn = 2;                 // 爆区覆盖到第 2 列，共 240px
	constexpr int kBlastAdjacentRows = 1;              // 爆区向上下各扩一行
	constexpr float kElitePickaxeWalkVelocity = 0.15f; // 持镐折返速度，单位 px/tick，为普通矿工的 125%
	constexpr float kExplosionVolume = 0.55f;          // 爆破音效音量
}

void EliteDiggerZombie::SetupZombie()
{
	DiggerZombie::SetupZombie();
	mBodyHealth = kEliteBodyHealth;
	mBodyMaxHealth = kEliteBodyHealth;
	mHelmHealth = kEliteHardhatHealth;
	mHelmMaxHealth = kEliteHardhatHealth;
	mBlastResolved = false;
}

/** 只有仍持镐的正常预警结束才爆破；随后进入父类稳定折返。 */
void EliteDiggerZombie::OnPickaxeStunFinished()
{
	if (HasPickaxe() && !mBlastResolved) ResolveBlast();
	DiggerZombie::OnPickaxeStunFinished();
}

/** 预警期间若通过调试或其他卸装路径丢镐，会立即取消爆破并改为向房屋推进。 */
void EliteDiggerZombie::OnPickaxeLost(Phase previousPhase)
{
	if (previousPhase == Phase::STUNNED) {
		BeginStableWalk(false);
	}
}

float EliteDiggerZombie::GetPickaxeWalkVelocity() const
{
	return kElitePickaxeWalkVelocity;
}

const std::string& EliteDiggerZombie::GetFullHardhatTexture() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_HARDHAT;
}

const std::string& EliteDiggerZombie::GetDamagedHardhatTexture(bool heavilyDamaged) const
{
	return heavilyDamaged
		? ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_HARDHAT3
		: ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_HARDHAT2;
}

const std::string& EliteDiggerZombie::GetBrokenOuterArmTexture() const
{
	return ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_OUTERARM_UPPER2;
}

const char* EliteDiggerZombie::GetHelmDropEffectName() const
{
	return "ZombieEliteDiggerHeadLight";
}

const char* EliteDiggerZombie::GetArmDropEffectName() const
{
	return "ZombieEliteDiggerArmOff";
}

/** 逻辑伤害按格判定，视觉偏移和泳池水面偏移均不改变覆盖范围。 */
void EliteDiggerZombie::ResolveBlast()
{
	if (mBlastResolved) return;
	mBlastResolved = true;
	if (!mBoard) return;

	const int minRow = std::max(0, mRow - kBlastAdjacentRows);
	const int maxRow = std::min(mBoard->mRows - 1,
		mRow + kBlastAdjacentRows);
	mBoard->ApplyPumpkinProtectedZombieAreaDamage(kBlastDamage,
		kPumpkinBlastDamageMultiplier,
		[minRow, maxRow](const Plant& plant) {
			return plant.mRow >= minRow && plant.mRow <= maxRow
				&& plant.mColumn >= kBlastMinColumn
				&& plant.mColumn <= kBlastMaxColumn;
		});

	const Vector blastCenter = mBoard->GetCellCenterPosition(mRow, 1);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_EXPLOSION, kExplosionVolume);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("EliteDiggerBlast", blastCenter);
	}
	mBoard->ShakeBoard(4.0f, -6.0f);
}

void EliteDiggerZombie::SaveExtraData(nlohmann::json& j) const
{
	DiggerZombie::SaveExtraData(j);
	j["eliteBlastResolved"] = mBlastResolved;
}

void EliteDiggerZombie::LoadExtraData(const nlohmann::json& j)
{
	DiggerZombie::LoadExtraData(j);
	mBlastResolved = j.value("eliteBlastResolved", false);
}
