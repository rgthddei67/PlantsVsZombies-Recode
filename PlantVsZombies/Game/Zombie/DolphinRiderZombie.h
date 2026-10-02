#pragma once

#include "Zombie.h"

class Plant;

/**
 * @brief 经典海豚僵尸：只在泳池水路生成，骑豚越过第一株植物后弃豚游泳。
 */
class DolphinRiderZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		APPROACHING,
		ENTERING_POOL,
		RIDING,
		JUMPING,
		SWIMMING,
		WALKING_WITHOUT_DOLPHIN,
	};

	Phase GetPhase() const { return mPhase; }
	bool HasDolphin() const;
	bool HasCheckedJumpBlocker() const { return mJumpBlockChecked; }
	bool HasPlayedEntrySplash() const { return mEntrySplashPlayed; }
	int GetSuccessfulDolphinJumps() const { return mSuccessfulJumpCount; }
	/** 返回本品种在弃豚前最多允许完成的普通植物越障次数。 */
	virtual int GetDolphinJumpCapacity() const;
	/** 返回当前入水动画归一化进度；非入水阶段为 0。 */
	float GetEntryProgress() const;
	float GetJumpProgress() const;
	/** 返回为消除海豚各阶段 reanim 基准差而叠加的连续视觉补偿。 */
	Vector GetDolphinVisualCompensation() const;

	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void PlaySpawnSound() override;
	void ZombieItemUpdate() const override;
	Vector GetVisualPosition() const override;
	bool CanBeCharmed() const override;
	bool CanBeFrozen() const override;
	bool CanBeGrabbedByTangleKelp() const override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool ShouldPlayDeathAnimation() const override;
	bool TryGetDrawClipBottom(float& clipBottom) const override;
	/** 阻拦状态和啃食恢复后结算品种效果；普通海豚无额外行为。 */
	virtual void OnDolphinJumpBlocked(Plant&) {}
	/** 返回断臂后上臂残肢使用的品种材质键。 */
	virtual const std::string& GetLostOuterArmTextureKey() const;
	/** 返回陆地断头使用的品种粒子名。 */
	virtual const char* GetDolphinHeadOffEffectName() const;

private:
	void BeginEnteringPool();
	void FinishEnteringPool();
	void BeginJump(Plant* target);
	void FinishJump(bool blocked, Plant* blockingPlant = nullptr);
	void ApplyPhasePresentation() const;
	void CheckDeferredArmDrop();
	void MoveManually(float speed, float scaledDelta, Transform* transform);

	Phase mPhase = Phase::APPROACHING;
	int mJumpTargetPlantID = NULL_PLANT_ID;
	bool mEntrySplashPlayed = false;
	bool mJumpSplashPlayed = false;
	bool mJumpBlockChecked = false;
	bool mJumpRetainsDolphinOnLanding = false;
	int mSuccessfulJumpCount = 0;
	float mBaseColliderOffsetX = -25.0f;
};
