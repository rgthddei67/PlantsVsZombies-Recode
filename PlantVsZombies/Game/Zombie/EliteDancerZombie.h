#pragma once
#ifndef _ELITEDANCERZOMBIE_H_
#define _ELITEDANCERZOMBIE_H_

#include "DancerZombie.h"
#include <vector>

/** 黑夜强台风专属精英舞王：无视植物推进，并持续补充普通伴舞。 */
class EliteDancerZombie : public DancerZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using DancerZombie::DancerZombie;

	void ZombieUpdate(float scaledTime) override;
	void StartEat(ColliderComponent* other) override;
	void EatTarget() override;
	bool ConsumesOtherMowersOnContact() const override { return true; }

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	/** 当前仍存活且与精英舞王同阵营的直属伴舞数量。 */
	int GetActiveBackupCount() const;
	float GetSummonTimer() const { return mSummonTimer; }
	float GetTyphoonAbilitySpeedMultiplier() const;

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	float GetAbilityAnimSpeedMultiplier() const override;

	// 保留80%动画速度
	float GetSlowAnimFactor() const override { return ZombieMovementRules::FastSlowAnimationFactor; }

private:
	void CleanupFollowers();
	bool SummonOneBackupDancer();

	std::vector<int> mFollowerIDs;
	float mSummonTimer = 0.0f;
	int mNextFormationSlot = 0;
	bool mCharmHandled = false;
};

#endif
