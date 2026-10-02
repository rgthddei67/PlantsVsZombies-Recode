#pragma once

#include "FootballZombie.h"

/**
 * @brief 黑夜专属的粉色橄榄球僵尸：首次啃食重击，头盔破碎时伤害近邻植物。
 */
class PinkFootballZombie : public FootballZombie {
protected:
	void SetupZombie() override;
	void CheckHelmImage() override;
	float GetAbilityAnimSpeedMultiplier() const override;
	const char* GetMagneticHelmetImageKey() const override;
	// 与 FastBucketZombie 使用同一减速速度层；减速时保留 75% 动画速度。
	float GetSlowAnimFactor() const override { return 0.75f; }

private:
	bool mFirstPlantStrikeUsed = false;

	/** @brief 对以本体为圆心、半径 120 像素内的植物结算头盔破碎伤害。 */
	void DamagePlantsNearBrokenHelmet();

public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using FootballZombie::FootballZombie;

	void EatTarget() override;
	void HelmDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
};
