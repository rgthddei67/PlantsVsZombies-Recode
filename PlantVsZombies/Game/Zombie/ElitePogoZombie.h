#pragma once

#include "PogoZombie.h"

/**
 * @brief 碳纤维精英跳跳僵尸：跳杆免疫磁力菇，并可缓冲第一次高坚果阻拦。
 */
class ElitePogoZombie final : public PogoZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using PogoZombie::PogoZombie;

	bool HasImpactBuffer() const { return mImpactBufferAvailable; }
	bool HasMagneticItem() const override { return false; }
	bool ExtractMagneticItem(MagneticItem&) override { return false; }

protected:
	void SetupZombie() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	bool HandlePogoJumpBlocked(Plant& plant) override;
	const std::string& GetDamagedOuterArmTextureKey() const override;
	const std::string& GetDamagedStickTextureKey() const override;
	const std::string& GetDamagedStick2TextureKey() const override;
	const char* GetPogoBreakEffectName() const override { return "ZombieElitePogo"; }

private:
	bool mImpactBufferAvailable = true;
};
