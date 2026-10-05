#pragma once
#ifndef _FASTBUCKET_ZOMBIE_H
#define _FASTBUCKET_ZOMBIE_H

#include "Zombie.h"

class FastBucketZombie : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	ArmorBrokenState mHelmStage = ArmorBrokenState::NO_BROKEN;
	void HelmDrop() override;
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	void ZombieItemUpdate() const override {
		Zombie::ZombieItemUpdate();

		if (mHelmStage == ArmorBrokenState::NONE || mHelmType == HelmType::HELMTYPE_NONE) {
			mAnimator->SetTrackVisible("anim_bucket", false);
		}
		else if (mHelmStage == ArmorBrokenState::A_LITTLE_BROKEN) {
			mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
				GetTexture("IMAGE_FASTZOMBIE_BUCKET2"));
		}
		else if (mHelmStage == ArmorBrokenState::REALLY_BROKEN) {
			mAnimator->SetTrackImage("anim_bucket", ResourceManager::GetInstance().
				GetTexture("IMAGE_FASTZOMBIE_BUCKET3"));
		}
	}

	void TakeDamage(int damage, DamageSource source, bool penetrateShield,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {}) override {
		if (GameRandom::Range(1, 10) <= 2) return;
		Zombie::TakeDamage(damage, source, penetrateShield,
			discardShieldOverflow, bypassShield, plantOrigin);
	}

protected:
	// 减速动画只降到 0.8x（快速僵尸减速后仍偏快的手感）；其余逻辑沿用基类 SetCooldown/UpdateAnimSpeed
	float GetSlowAnimFactor() const override { return ZombieMovementRules::FastSlowAnimationFactor; }
	float GetAbilityAnimSpeedMultiplier() const override;
	void RestoreLegacyAbilityAnimSpeedMultiplier(float multiplier) override;

	void SetupZombie() override;
	void CheckHelmImage() override;

private:
	float mAbilityAnimSpeedMultiplier = 1.0f;	// 每只快速铁桶出生时独立抽取并随关卡存档的动画能力倍率
};

#endif
