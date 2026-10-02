#pragma once

#include "Zombie.h"

/**
 * @brief 经典矿工僵尸：地下穿行至房屋前出土，眩晕后向前线折返；丢镐时走独立出土分支。
 */
class DiggerZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;
	~DiggerZombie() override;

	enum class Phase {
		TUNNELING,
		RISING,
		STUNNED,
		WALKING_WITH_PICKAXE,
		TUNNELING_PAUSE_WITHOUT_PICKAXE,
		RISING_WITHOUT_PICKAXE,
		WALKING_WITHOUT_PICKAXE,
	};

	Phase GetPhase() const { return mPhase; }
	float GetPhaseRemaining() const { return mPhaseRemaining; }
	float GetAltitude() const { return mAltitude; }
	bool HasPickaxe() const { return mHasPickaxe; }
	bool HasShownSurprise() const { return mSurpriseShown; }
	float GetPickaxeWalkVelocityValue() const { return GetPickaxeWalkVelocity(); }
	/** 磁力菇与 AutoTest 共用的原版丢镐入口。 */
	void LosePickaxe();
	bool HasMagneticItem() const override;
	MagneticSimulationLayer GetMagneticSimulationLayer() const override {
		return HasMagneticItem()
			? MagneticSimulationLayer::TOOL : MagneticSimulationLayer::NONE;
	}
	bool ExtractMagneticItem(MagneticItem& item) override;

	void PlaySpawnSound() override;
	void StartEat(ColliderComponent* other) override;
	void HelmDrop() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void TakePlantAshDamage(int damage) override;
	void Charred() override;
	void Die() override;
	bool IsMovingRight() const override;
	bool CanTriggerGameOver() const override;
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanBeCharmed() const override;
	bool CanBeChilled() const override;
	bool CanBeFrozen() const override;
	bool CanBeCharred() const override;
	bool CanBeAffectedByGroundHazards() const override { return IsInteractivePhase(); }
	bool CanBeGrabbedByTangleKelp() const override { return false; }

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void CheckHelmImage() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	bool CanUseGroundPoolState() const override { return false; }
	bool TryGetDrawClipBottom(float& clipBottom) const override;
	/** 持镐眩晕结束入口；精英变体在这里先结算一次性能力。 */
	virtual void OnPickaxeStunFinished();
	/** 丢镐后的变体入口；previousPhase 是丢镐发生前的状态。 */
	virtual void OnPickaxeLost(Phase previousPhase);
	virtual float GetPickaxeWalkVelocity() const;
	virtual const std::string& GetFullHardhatTexture() const;
	virtual const std::string& GetDamagedHardhatTexture(bool heavilyDamaged) const;
	virtual const std::string& GetBrokenOuterArmTexture() const;
	/** 返回当前矿工变体被磁力吸走的镐子贴图。 */
	virtual const std::string& GetMagneticPickaxeImageKey() const;
	virtual const char* GetHelmDropEffectName() const;
	virtual const char* GetArmDropEffectName() const;
	void BeginStableWalk(bool withPickaxe);

private:
	void BeginRising(bool withPickaxe);
	void UpdateRising(float scaledTime, bool withPickaxe);
	void ApplyPhasePresentation();
	void ApplyAltitude();
	void UpdateFacing();
	void EmitTunnelDust();
	void EmitRiseEffects(bool playWakeup);
	void StopEatingForTransition();
	void ClaimLoopSound();
	void ReleaseLoopSound();
	float GetWalkClipSpeed(float velocity) const;
	bool IsInteractivePhase() const;
	Vector GetStableVisualOrigin() const;

	Phase mPhase = Phase::TUNNELING;
	float mPhaseRemaining = 0.0f;
	float mAltitude = 0.0f;
	float mBaseVisualOffsetY = 0.0f;
	float mTunnelVelocity = 0.67f;
	float mWalkVelocity = 0.30f;
	float mTunnelDustTimer = 0.0f;
	bool mHasPickaxe = true;
	bool mSurpriseShown = false;
	bool mLandingStarted = false;
	bool mLoopSoundClaimed = false;
	ArmorBrokenState mHelmStage = ArmorBrokenState::NO_BROKEN;

	static int sLoopSoundUsers;
};
