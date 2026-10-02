#pragma once

#include "Polevaulter.h"

/**
 * @brief 绿色精英撑杆僵尸：双倍跳距，落地或被挡时生成普通撑杆，被挡还会撞伤植物。
 */
class ElitePolevaulterZombie : public Polevaulter {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Polevaulter::Polevaulter;

	void HeadDrop() override;

protected:
	void SetupZombie() override;
	float GetVaultDistance() const override;
	float GetAbilityAnimSpeedMultiplier() const override;
	void OnVaultLanded() override;
	void OnVaultBlocked(Plant& blockingPlant) override;
};
