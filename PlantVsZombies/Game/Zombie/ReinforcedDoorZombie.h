#pragma once

#include "DoorZombie.h"

/**
 * 加固铁门僵尸：持门时限制植物单次伤害并阻断大喷穿透，灰烬与植物直杀走专属耐性。
 */
class ReinforcedDoorZombie final : public DoorZombie {
protected:
	void SetupZombie() override;
	const char* GetDoorImageKey(ArmorBrokenState stage) const override;
	const char* GetDoorDropParticleName() const override { return "ZombieReinforcedDoorOff"; }
	int AdjustIncomingDamage(int damage, DamageSource source, bool penetrateShield,
		bool bypassShield = false) const override;

public:
	static constexpr int InitialShieldHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_REINFORCED_DOOR).shield; // 加固门生命，出生与候选投影共用
	static constexpr int ShieldedHitCap = 10; // 持门时植物普通伤害的每击上限
	static constexpr int ShieldedAshCap = 320; // 持门时灰烬伤害的每击上限
	static constexpr int FumeMultiplier = 2; // 大喷家族对本变体的伤害倍率
	using DoorZombie::DoorZombie;

	bool CanBeCharred() const override;
	/** 加固门是本变体的核心能力，主人指定磁力菇不能将其卸除。 */
	bool HasMagneticItem() const override { return false; }
	bool CanBeCharmed() const override { return false; }
	bool ResistsTangleKelpDrowning() const override {
		return mShieldType != ShieldType::SHIELDTYPE_NONE;
	}
	bool BlocksFumePiercing() const override {
		return mShieldType != ShieldType::SHIELDTYPE_NONE;
	}
	/** 持有加固门时拒绝特殊弹丸主动绕盾；从背后命中仍沿用通用方向判定。 */
	bool BlocksProjectileShieldBypass() const override {
		return mShieldType != ShieldType::SHIELDTYPE_NONE;
	}
	int ModifyFumeDamage(int damage) const override;
	float ModifySpikeFrameDamage(float damage, bool bypassShield = false) const override;
	bool TakePlantInstantKill() override;
	/** 持门拒吞时保留 10 点特殊基础伤害；破门后会被正常吞下而不调用此入口。 */
	int AdjustRejectedChomperBiteDamage(int damage) const override;
};
