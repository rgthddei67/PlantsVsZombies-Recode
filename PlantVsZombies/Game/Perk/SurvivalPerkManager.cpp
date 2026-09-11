#include "SurvivalPerkManager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include "../../GameRandom.h"

namespace {
	// 顺序必须与 PerkType 枚举一一对应（static_assert 强制）
	const PerkInfo kPerks[] = {
		{ "PLANT_DAMAGE_UP", u8"全体植物伤害", u8"每层使全体植物伤害 +12%（不限层）", 0.12f, 9999, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_HEALTH_UP", u8"僵尸血量", u8"每层使僵尸血量 +12%（不限层）", 0.12f, 9999, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_DAMAGE_RESIST", u8"僵尸免伤", u8"每层使僵尸受到伤害 -3%（最多 15 层）", 0.03f, 15, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_DAMAGE_UP", u8"僵尸伤害", u8"每层使僵尸对植物伤害 +5%（不限层）", 0.05f, 9999, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_INVULN_HITS", u8"僵尸前N次免伤", u8"每层使僵尸出生后前 4 次受击免伤（最多 2 层）", 4.0f, 2, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_REGEN", u8"植物回血", u8"每层使植物 5 秒回 65 HP，5 层解锁 3 倍过量治疗（最多 8 层）", 65.0f, 8, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_ATTACK_SPEED", u8"植物攻速", u8"每层使全体植物开火速度 +15%（最多 8 层）", 0.15f, 8, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_DAMAGE_REDUCTION", u8"植物韧性", u8"每层使全体植物受到伤害 -3%（最多 15 层）", 0.03f, 15, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_SUN_BONUS", u8"阳光增产", u8"每层使收集到的阳光 +15%（最多 10 层）", 0.15f, 10, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_CARD_RECHARGE", u8"卡片加速", u8"每层使植物卡片冷却速度 +12%（最多 10 层）", 0.12f, 10, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "PLANT_MIST_REFINING", u8"雾灯精炼", u8"雾火价值与本波并发接收上限 +50%（最多 2 层）", 0.50f, 2, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::PLANTERN_MECHANICS },
		{ "PLANT_CORROSIVE_TOXIN", u8"腐蚀毒液", u8"毒液按目标最大生命造成百分比伤害", 0.01f, 1, PerkCategory::PLANT_BUFF, PerkRarity::RARE, PerkCondition::NONE },
		{ "PLANT_UNYIELDING_ROOTS", u8"不屈根系", u8"每株植物每轮首次致命伤保留 1 HP，并免伤 3 秒", 3.0f, 1, PerkCategory::PLANT_BUFF, PerkRarity::RARE, PerkCondition::NONE },
		{ "PLANT_DAMAGE_ECHO", u8"火力回响", u8"每 10 次实际植物伤害命中，额外回响一次同额伤害", 10.0f, 1, PerkCategory::PLANT_BUFF, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_FOG_BREAKOUT", u8"雾幕突围", u8"首次走出浓雾时解控，并获得 3 秒控制免疫", 3.0f, 1, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::PLANTERN_MECHANICS },
		{ "ZOMBIE_DEATH_RELAY", u8"亡者接力", u8"死亡时给同排前锋传递 1 次受击免伤", 1.0f, 1, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_DEVOUR_REPAIR", u8"吞噬修复", u8"亲口吃掉植物时修满仍存在的本体与防具", 1.0f, 1, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
		{ "ZOMBIE_ARMOR_BREAK_RUSH", u8"破甲狂潮", u8"首次破甲时解控免控 5 秒，并获得 60% 行动加速", 1.60f, 1, PerkCategory::ZOMBIE_CURSE, PerkRarity::COMMON, PerkCondition::NONE },
	};
	static_assert(sizeof(kPerks) / sizeof(kPerks[0]) == static_cast<size_t>(PerkType::COUNT),
		"kPerks 必须与 PerkType 一一对应");

	constexpr float kPlantRegenIntervalSec = 5.0f;     // 回血词条脉冲间隔
	constexpr int   kPlantRegenOverhealUnlockStacks = 5;
	constexpr int   kPlantRegenOverhealMult = 3;       // 5 层起的过量治疗上限倍率
	constexpr double kMinDamageTakenMultiplier = 0.55; // 植物韧性与僵尸免伤都最多减伤 45%
	constexpr float kRarePanelChance = 0.015f;          // 每块三选面板出现一个稀有植物词条的总概率

	bool MeetsCondition(const PerkInfo& info, const PerkOfferContext& context)
	{
		return info.condition == PerkCondition::NONE
			|| (info.condition == PerkCondition::PLANTERN_MECHANICS
				&& context.supportsPlanternMechanics);
	}

	int RoundScale(int base, double mult) {
		if (base <= 0) return base;
		// 秒杀哨兵（小推车 LawnMower::TakeDamage(INT32_MAX) 等）不参与缩放：base*mult 会溢出 int
		// → UB（x86 上变 INT_MIN）→ 被下面 r<1?1 钳成 1，导致小推车只打 1 点伤害 → 推车失效 → 输。
		// 任何真实伤害都远小于此阈值，故只拦截哨兵，不影响正常缩放。
		if (base >= INT32_MAX / 2) return base;
		int r = static_cast<int>(static_cast<double>(base) * mult + 0.5);
		return r < 1 ? 1 : r;   // 防 50% 免伤把 1 点伤害抹成 0
	}
}

const PerkInfo& SurvivalPerkManager::GetInfo(PerkType type) {
	return kPerks[static_cast<size_t>(type)];
}

bool SurvivalPerkManager::AddPerk(PerkType type) {
	int& s = mStacks[static_cast<size_t>(type)];
	if (s >= GetInfo(type).maxStacks) return false;
	++s;
	return true;
}

int SurvivalPerkManager::GetStacks(PerkType type) const {
	return mStacks[static_cast<size_t>(type)];
}

void SurvivalPerkManager::Clear() {
	mStacks.fill(0);
}

std::vector<PerkType> SurvivalPerkManager::AvailablePerks(PerkCategory cat,
	const PerkOfferContext& context, PerkRarity rarity) const {
	std::vector<PerkType> out;
	for (int i = 0; i < static_cast<int>(PerkType::COUNT); ++i) {
		PerkType t = static_cast<PerkType>(i);
		const PerkInfo& info = GetInfo(t);
		if (info.category == cat && info.rarity == rarity
			&& MeetsCondition(info, context) && GetStacks(t) < info.maxStacks)
			out.push_back(t);
	}
	return out;
}

double SurvivalPerkManager::GetPlantDamageMultiplier() const {
	return 1.0 + GetInfo(PerkType::PLANT_DAMAGE_UP).perStack
		* GetStacks(PerkType::PLANT_DAMAGE_UP);
}

double SurvivalPerkManager::GetPlantAttackSpeedMultiplier() const {
	return 1.0 + GetInfo(PerkType::PLANT_ATTACK_SPEED).perStack
		* GetStacks(PerkType::PLANT_ATTACK_SPEED);
}

double SurvivalPerkManager::GetPlantDamageTakenMultiplier() const {
	double reduction = GetInfo(PerkType::PLANT_DAMAGE_REDUCTION).perStack
		* GetStacks(PerkType::PLANT_DAMAGE_REDUCTION);
	return std::max(kMinDamageTakenMultiplier, 1.0 - reduction);
}

double SurvivalPerkManager::GetSunIncomeMultiplier() const {
	return 1.0 + GetInfo(PerkType::PLANT_SUN_BONUS).perStack
		* GetStacks(PerkType::PLANT_SUN_BONUS);
}

double SurvivalPerkManager::GetPlantCardRechargeMultiplier() const {
	return 1.0 + GetInfo(PerkType::PLANT_CARD_RECHARGE).perStack
		* GetStacks(PerkType::PLANT_CARD_RECHARGE);
}

double SurvivalPerkManager::GetZombieHealthMultiplier() const {
	return 1.0 + GetInfo(PerkType::ZOMBIE_HEALTH_UP).perStack
		* GetStacks(PerkType::ZOMBIE_HEALTH_UP);
}

double SurvivalPerkManager::GetZombieDamageTakenMultiplier() const {
	double reduction = GetInfo(PerkType::ZOMBIE_DAMAGE_RESIST).perStack
		* GetStacks(PerkType::ZOMBIE_DAMAGE_RESIST);
	return std::max(kMinDamageTakenMultiplier, 1.0 - reduction);
}

int SurvivalPerkManager::ScalePlantDamage(int base) const {
	return RoundScale(base, GetPlantDamageMultiplier());
}

int SurvivalPerkManager::ScaleDamageToZombie(int base) const {
	return RoundScale(base, GetZombieDamageTakenMultiplier());
}

int SurvivalPerkManager::ScaleTotalDamageToZombie(int base) const {
	return ScaleDamageToZombie(ScalePlantDamage(base));
}

int SurvivalPerkManager::ScaleNumericDamage(int base, double multiplier) {
	return RoundScale(base, multiplier);
}

int SurvivalPerkManager::ScaleDamageToPlant(int base) const {
	return RoundScale(base, GetPlantDamageTakenMultiplier());
}

int SurvivalPerkManager::ScaleSunIncome(int base) const {
	return RoundScale(base, GetSunIncomeMultiplier());
}

void SurvivalPerkManager::Save(nlohmann::json& j) const {
	for (int i = 0; i < static_cast<int>(PerkType::COUNT); ++i) {
		if (mStacks[i] > 0) j[kPerks[i].key] = mStacks[i];
	}
}

void SurvivalPerkManager::Load(const nlohmann::json& j) {
	if (!j.is_object()) return;   // 容错：旧档可能写成 "perks": null（零词条时被 operator[] 物化）→ value() 会抛 type_error.306
	for (int i = 0; i < static_cast<int>(PerkType::COUNT); ++i) {
		int v = j.value(kPerks[i].key, 0);
		if (v < 0) v = 0;
		if (v > kPerks[i].maxStacks) v = kPerks[i].maxStacks;
		mStacks[i] = v;
	}
}

double SurvivalPerkManager::GetZombieDamageMultiplier() const {
	return 1.0 + GetInfo(PerkType::ZOMBIE_DAMAGE_UP).perStack
		* GetStacks(PerkType::ZOMBIE_DAMAGE_UP);
}

int SurvivalPerkManager::ScaleZombieDamage(int base) const {
	return RoundScale(base, GetZombieDamageMultiplier());
}

int SurvivalPerkManager::GetZombieInvulnHits() const {
	// perStack=4 是精确整数，直接整数乘（无小数需取整）
	return static_cast<int>(GetInfo(PerkType::ZOMBIE_INVULN_HITS).perStack)
		* GetStacks(PerkType::ZOMBIE_INVULN_HITS);
}

float SurvivalPerkManager::GetPlantRegenInterval() const {
	return kPlantRegenIntervalSec;
}

int SurvivalPerkManager::GetPlantRegenPerPulse() const {
	// perStack=65 是精确整数，直接整数乘
	return static_cast<int>(GetInfo(PerkType::PLANT_REGEN).perStack)
		* GetStacks(PerkType::PLANT_REGEN);
}

int SurvivalPerkManager::GetPlantRegenHpCap(int maxHealth) const {
	// 过量治疗在第 5 层提前解锁，与词条最高 8 层是两个独立平衡参数。
	if (GetStacks(PerkType::PLANT_REGEN) >= kPlantRegenOverhealUnlockStacks)
		return maxHealth * kPlantRegenOverhealMult;
	return maxHealth;
}

double SurvivalPerkManager::GetMistFuelMultiplier() const {
	return 1.0 + GetInfo(PerkType::PLANT_MIST_REFINING).perStack
		* GetStacks(PerkType::PLANT_MIST_REFINING);
}

bool SurvivalPerkManager::HasCorrosiveToxin() const { return GetStacks(PerkType::PLANT_CORROSIVE_TOXIN) > 0; }
bool SurvivalPerkManager::HasUnyieldingRoots() const { return GetStacks(PerkType::PLANT_UNYIELDING_ROOTS) > 0; }
bool SurvivalPerkManager::HasPlantDamageEcho() const { return GetStacks(PerkType::PLANT_DAMAGE_ECHO) > 0; }
bool SurvivalPerkManager::HasFogBreakout() const { return GetStacks(PerkType::ZOMBIE_FOG_BREAKOUT) > 0; }
bool SurvivalPerkManager::HasZombieDeathRelay() const { return GetStacks(PerkType::ZOMBIE_DEATH_RELAY) > 0; }
bool SurvivalPerkManager::HasZombieDevourRepair() const { return GetStacks(PerkType::ZOMBIE_DEVOUR_REPAIR) > 0; }
bool SurvivalPerkManager::HasZombieArmorBreakRush() const { return GetStacks(PerkType::ZOMBIE_ARMOR_BREAK_RUSH) > 0; }

std::vector<PerkPairing> RollPerkPairings(const SurvivalPerkManager& mgr, int count,
	const PerkOfferContext& context) {
	if (count <= 0) return {};
	std::vector<PerkType> plants = mgr.AvailablePerks(
		PerkCategory::PLANT_BUFF, context, PerkRarity::COMMON);
	std::vector<PerkType> rarePlants = mgr.AvailablePerks(
		PerkCategory::PLANT_BUFF, context, PerkRarity::RARE);
	std::vector<PerkType> zombies = mgr.AvailablePerks(
		PerkCategory::ZOMBIE_CURSE, context, PerkRarity::COMMON);
	if (zombies.empty() || (plants.empty() && rarePlants.empty())) return {};

	std::vector<PerkPairing> all;
	for (PerkType p : plants)
		for (PerkType z : zombies)
			all.push_back(PerkPairing{ p, z });

	GameRandom::Shuffle(all);
	std::vector<PerkPairing> selected;
	selected.reserve(count);
	// 普通植物词条已满时必须退化到稀有池，否则仍有合法组合却会弹出空面板。
	const bool rarePanel = !rarePlants.empty() && (plants.empty()
		|| (context.rarePanelRollOverride >= 0
			? context.rarePanelRollOverride != 0 : GameRandom::Chance(kRarePanelChance)));
	if (rarePanel) {
		selected.push_back({
			rarePlants[GameRandom::Range(0, static_cast<int>(rarePlants.size()) - 1)],
			zombies[GameRandom::Range(0, static_cast<int>(zombies.size()) - 1)] });
	}
	for (const PerkPairing& pairing : all) {
		if (static_cast<int>(selected.size()) >= count) break;
		selected.push_back(pairing);
	}
	GameRandom::Shuffle(selected);
	return selected;
}
