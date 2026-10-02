#pragma once

#include "LadderZombie.h"
#include <cstdint>

/**
 * @brief 精英扶梯僵尸：出场五秒后一次性扫描本行，并按血量与远程植物构成获得能力。
 */
class EliteLadderZombie final : public LadderZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using LadderZombie::LadderZombie;

	void Update() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	bool IsRowScanComplete() const { return mRowScanComplete; }
	bool HasInfiniteLadderAbility() const { return mInfiniteLadderAbility; }
	bool HasDoubledAnimationSpeed() const { return mDoubledAnimationSpeed; }
	bool HasBodyHealthBonus() const { return mBodyHealthBonusApplied; }
	bool HasDoubledShieldHealth() const { return mShieldHealthDoubled; }
	float GetRowScanTimeRemaining() const { return mRowScanTimeRemaining; }
	int64_t GetScannedPlantHealth() const { return mScannedPlantHealth; }
	int GetScannedPultCount() const { return mScannedPultCount; }
	int GetScannedShooterCount() const { return mScannedShooterCount; }
	/** AutoTest 专用：只调整一次性扫描倒计时，不直接结算能力。 */
	void SetRowScanTimeRemainingForTesting(float seconds);

protected:
	void SetupZombie() override;
	float GetAbilityAnimSpeedMultiplier() const override;
	bool RetainsLadderAfterPlacement() const override;
	LadderStyle GetPlacedLadderStyle() const override;
	const std::string& GetShieldTextureKey(ArmorBrokenState stage) const override;
	const std::string& GetBrokenArmTextureKey() const override;
	const char* GetLadderDropEffectName() const override;

private:
	/** 采样同排活动植物的当前生命与战斗类别，并一次性应用四个严格不等分支。 */
	void ScanRowAndApplyAbilities();

	float mRowScanTimeRemaining = 5.0f;
	bool mRowScanComplete = false;
	bool mInfiniteLadderAbility = false;
	bool mDoubledAnimationSpeed = false;
	bool mBodyHealthBonusApplied = false;
	bool mShieldHealthDoubled = false;
	int64_t mScannedPlantHealth = 0;
	int mScannedPultCount = 0;
	int mScannedShooterCount = 0;
};
