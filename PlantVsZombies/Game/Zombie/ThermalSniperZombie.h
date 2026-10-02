#pragma once

#include "Zombie.h"

#include <memory>

class Animator;

/**
 * @brief 热感狙击僵尸：独立装填，并在玩家同行落种时向原位置发射热脉冲。
 */
class ThermalSniperZombie final : public Zombie {
public:
	static constexpr float BaseAbilitySpeed = 0.8f; // 本品种常驻能力速度倍率，与出生预测共用
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class SniperPhase {
		RELOADING,
		READY,
		AIMING,
		DISABLED,
	};

	void Update() override;
	void Draw(Graphics* g) override;
	void Die() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;
	void OnPlayerPlantDeployed(const Plant& plant, int baseMaxHealth) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;

	SniperPhase GetSniperPhase() const { return mSniperPhase; }
	float GetReloadRemaining() const { return mReloadRemaining; }
	float GetAimRemaining() const { return mAimRemaining; }
	int GetLockedPlantID() const { return mLockedPlantID; }
	int GetLockedDamage() const { return mLockedDamage; }
	Vector GetLockedPosition() const { return mLockedPosition; }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void OnMindControlled() override;
	void HeadDrop() override;
	float GetAbilityAnimSpeedMultiplier() const override { return BaseAbilitySpeed; }

private:
	void ConfigureFollowers();
	void SyncFollowerPresentation() const;
	void FireLockedPulse();
	void DisableSniper();
	bool CanReactToDeployment(const Plant& plant) const;

	SniperPhase mSniperPhase = SniperPhase::RELOADING;
	float mReloadRemaining = 3.0f;
	float mAimRemaining = 0.0f;
	int mLockedPlantID = NULL_PLANT_ID;
	int mLockedDamage = 0;
	Vector mLockedPosition = Vector::zero();
	bool mFollowersConfigured = false;
	mutable std::shared_ptr<Animator> mLauncherAnimator;
	mutable SniperPhase mPresentedPhase = SniperPhase::DISABLED;
};
