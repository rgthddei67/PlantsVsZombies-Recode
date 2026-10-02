#pragma once

#include "Zombie.h"

class Plant;

/**
 * @brief 经典跳跳僵尸：周期弹跳越过普通植物，被高坚果阻拦后弃杆步行。
 */
class PogoZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		BOUNCING,
		HIGH_BOUNCE,
		FORWARD_BOUNCE,
		WALKING,
	};

	Phase GetPhase() const { return mPhase; }
	float GetBounceRemaining() const { return mBounceRemaining; }
	float GetBounceProgress() const;
	float GetPogoAltitude() const { return mAltitude; }
	bool HasPogo() const { return mHasPogo; }
	/** 预览大图是否正在运行无位移、无音效的原地弹跳循环。 */
	bool IsPreviewBounceActive() const {
		return mIsPreview && !mIsUI && mHasPogo && mPhase != Phase::WALKING
			&& mBounceRemaining > 0.0f;
	}
	bool HasCheckedJumpBlocker() const { return mJumpBlockChecked; }
	float GetForwardDistanceTotal() const { return mForwardDistanceTotal; }
	float GetForwardDistanceApplied() const { return mForwardDistanceApplied; }
	/** AutoTest 专用：把当前持杆弹跳推进到确定性的剩余时间。 */
	void SetBounceRemainingForTesting(float seconds);

	void StartEat(ColliderComponent* other) override;
	void Update() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void OnTemporalCoreStateRestored() override;
	void PlaySpawnSound() override;
	Vector GetVisualPosition() const override;
	bool CanBeCharmed() const override { return !mHasPogo; }
	bool CanBeFrozen() const override { return !mHasPogo; }
	bool CanBeGrabbedByTangleKelp() const override { return false; }
	bool CanTriggerPotatoMine() const override { return !mHasPogo; }
	bool TakePlantInstantKill() override;
	/** 持杆时位于大嘴花咬合范围之外，因此拒吞后不承受咬击伤害。 */
	int AdjustRejectedChomperBiteDamage(int damage) const override;
	bool HasMagneticItem() const override { return mHasPogo; }
	bool ExtractMagneticItem(MagneticItem& item) override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	/** 持杆弹跳动画不受寒冰降速；弃杆步行恢复普通僵尸的减速动画倍率。 */
	float GetSlowAnimFactor() const override {
		return mHasPogo ? 1.0f : Zombie::GetSlowAnimFactor();
	}
	bool CanUseGroundPoolState() const override { return !mHasPogo; }
	/** 处理高坚果等植物的跳跃阻拦；返回 true 表示本次阻拦已终止前跳。 */
	virtual bool HandlePogoJumpBlocked(Plant& plant);
	virtual const std::string& GetDamagedOuterArmTextureKey() const;
	virtual const std::string& GetDamagedStickTextureKey() const;
	virtual const std::string& GetDamagedStick2TextureKey() const;
	virtual const char* GetPogoBreakEffectName() const { return "ZombiePogo"; }

private:
	void BeginBounce(Phase phase);
	void BeginForwardBounce(Plant& plant);
	void ResolveBounceLanding();
	void RestartLandingAnimation();
	void UpdateBounceAltitude();
	void UpdatePreviewBounce();
	void ApplyArmDamagePresentation() const;
	void BreakPogo(bool emitParticle = true);
	void MovePogoDistance(float distance, Transform* transform);
	Plant* ResolveContactPlant() const;
	Plant* ResolveForwardTarget() const;

	Phase mPhase = Phase::BOUNCING;
	float mBounceRemaining = 0.0f;
	float mAltitude = 9.0f;
	bool mHasPogo = true;
	int mContactPlantID = NULL_PLANT_ID;
	int mForwardTargetPlantID = NULL_PLANT_ID;
	bool mJumpBlockChecked = false;
	bool mLandingAnimationStarted = false;
	bool mLandingSoundPlayed = false;
	float mForwardDistanceTotal = 0.0f;
	float mForwardDistanceApplied = 0.0f;
};
