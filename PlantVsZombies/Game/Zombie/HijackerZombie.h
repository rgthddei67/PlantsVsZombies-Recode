#pragma once

#include "Zombie.h"

/**
 * 黑夜屋顶劫持者：Board 在 75% 雷荷锁定，在最后一秒驱动独立充能动画并于放电边沿处决。
 */
class HijackerZombie : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;
	~HijackerZombie() override;

	enum class Phase {
		NORMAL,
		LOCKED,
		FINALIZING,
		RESOLVED,
	};

	Phase GetPhase() const { return mPhase; }
	bool HasLockHealthBoost() const { return mLockHealthBoostApplied; }
	bool CanBeNightRoofHijackerCandidate() const;
	/** 75% 选择边沿调用；首次锁定增加本体耐久，但不打断移动或当前啃食动作。 */
	void BeginNightRoofLock();
	/** 满电进入延长预警时提高循环反馈，但仍允许普通行动。 */
	void BeginNightRoofWarning();
	/** 最后一秒原子结束啃食、停走并播放 anim_hijack。 */
	void BeginNightRoofFinalization();
	/** 死亡/掉头取消或一轮结束时清理能力展示和声音。 */
	void ClearNightRoofLock();
	/** 读档实体全部恢复后，按 Board 权威状态重建能力展示。 */
	void RestoreNightRoofPhase(bool locked, bool finalizing, bool warning);

	void Update() override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void Die() override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetForcedAnimSpeedMultiplier() const override {
		return mPhase == Phase::FINALIZING ? 1.0f : -1.0f;
	}

private:
	void StopEatingForFinalization();
	void ClaimLockSound(float volume);
	void ReleaseLockSound();

	Phase mPhase = Phase::NORMAL;
	float mAlarmPulseTimer = 0.0f;
	bool mWarningActive = false;
	bool mLoopSoundClaimed = false;
	bool mLockHealthBoostApplied = false;
};
