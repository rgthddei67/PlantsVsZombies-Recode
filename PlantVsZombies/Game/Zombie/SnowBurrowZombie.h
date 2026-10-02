#pragma once

#include "Zombie.h"

/**
 * @brief 潜雪僵尸：出生短距潜雪，并在半血时以可中断前摇提交第二次短潜雪。
 */
class SnowBurrowZombie final : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		FIRST_BURROW,
		SURFACING,
		WALKING,
		REBURROW_WINDUP,
		SECOND_BURROW,
	};

	Phase GetPhase() const { return mPhase; }
	const char* GetPhaseName() const;
	float GetPhaseRemaining() const { return mPhaseRemaining; }
	float GetBurrowOriginX() const { return mBurrowOriginX; }
	float GetBurrowLimitX() const { return mBurrowLimitX; }
	float GetBurrowTargetX() const { return mBurrowTargetX; }
	int GetEmergenceColumn() const { return mEmergenceColumn; }
	int GetBurrowsCommitted() const { return mBurrowsCommitted; }
	float GetReburrowRetryRemaining() const { return mReburrowRetryRemaining; }
	bool IsNaturalImpactPending() const { return mNaturalImpactPending; }

	void Update() override;
	void TakeDamage(int damage, DamageSource source, bool penetrateShield = false,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {}) override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void OnTemporalCoreStateRestored() override;
	bool IsMovingRight() const override { return mIsMindControlled; }
	bool CanTriggerGameOver() const override;
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanBeCharmed() const override;
	bool CanBeAffectedByGroundHazards() const override { return !mIsDying && !mIsDead; }
	bool CanBeGrabbedByTangleKelp() const override { return IsSurfaceInteractive(); }
	bool ForceSurfaceFromGroundHazard() override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void OnMindControlled() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	bool CanUseGroundPoolState() const override { return false; }
	bool TryGetDrawClipBottom(float& clipBottom) const override;
	/** 地下移动可直接回收；开始出土后即使尚不可交互，也必须播放可见死亡动画。 */
	bool ShouldPlayDeathAnimation() const override { return !IsUnderground(); }

private:
	void BeginBurrow(Phase phase, float maxCells);
	void RefreshBurrowTarget();
	void BeginSurfacing(bool naturalImpact);
	void UpdateSurfacing(float scaledTime);
	void FinishSurfacing();
	void ApplyNaturalEmergenceImpact();
	void TryBeginReburrowWindup();
	void CommitSecondBurrow();
	void BeginWalking(float blendTime);
	void ApplyPhasePresentation();
	void ApplyAltitude();
	void UpdateFacing();
	void EmitBurrowTrail();
	Vector GetStableVisualOrigin() const;
	float GetWalkClipSpeed() const;
	bool IsUnderground() const;
	bool IsSurfaceInteractive() const;

	Phase mPhase = Phase::FIRST_BURROW;
	float mPhaseRemaining = 0.0f;
	float mAltitude = 0.0f;
	float mBaseVisualOffsetY = 0.0f;
	float mWalkVelocity = 0.30f;
	float mBurrowOriginX = 0.0f;
	float mBurrowLimitX = 0.0f;
	float mBurrowTargetX = 0.0f;
	float mBurrowTrailTimer = 0.0f;
	float mReburrowRetryRemaining = 0.0f;
	int mEmergenceColumn = -1;
	int mBurrowsCommitted = 1;
	bool mSecondBurrowSpent = false;
	bool mNaturalImpactPending = false;
	bool mLandingStarted = false;
};
