#pragma once

#include "ConeZombie.h"

#include <memory>

class Animator;

/**
 * @brief 冰裂钻机：携带可盐蚀钻机层，在冻土上完成可中断蓄力后提交独立同行地裂。
 * @details 本体继续复用路障的死亡和啃食帧事件；钻机动作完全由保存的逻辑计时驱动。
 */
class IceCrackDrillZombie final : public ConeZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using ConeZombie::ConeZombie;

	enum class DrillPhase {
		MOVING,
		CHARGING,
		SPENT,
	};

	void Update() override;
	void Die() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;
	bool ApplyWinterCorrosion(int corrosion) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;

	DrillPhase GetDrillPhase() const { return mDrillPhase; }
	float GetChargeRemaining() const { return mChargeRemaining; }
	bool HasUsedDrill() const { return mDrillUsed; }
	bool HasDrillRigAnimator() const { return static_cast<bool>(mDrillRigAnimator); }
	bool IsDrillRigVisible() const;
	/** AutoTest 专用：直接还原钻孔状态，不生成地裂或播放音效。 */
	void SetDrillStateForTesting(
		DrillPhase phase, float remaining, bool used);

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void OnMindControlled() override;
	void ArmDrop() override;
	void HelmDrop() override;
	void CheckHelmImage() override;
	const std::string& GetConeTextureKey(ArmorBrokenState stage) const override;
	const char* GetConeDropEffectName() const override { return "IceCrackDrillRigOff"; }

private:
	bool CanBeginCharge() const;
	bool HasTerminalChargeAbort() const;
	bool IsStandingOnFrozenCell() const;
	void BeginCharge();
	void CancelCharge(bool consumeAbility);
	void CommitRift();
	void ConfigureDrillRigAnimator();
	void SyncDrillRigPresentation(bool restartTrack = false) const;
	Vector GetDrillNosePosition() const;
	int GetCurrentColumn() const;
	const char* GetDrillTrackName() const;

	DrillPhase mDrillPhase = DrillPhase::MOVING;
	float mChargeRemaining = 0.0f;
	float mChargeParticleTimer = 0.0f;
	bool mDrillUsed = false;
	mutable std::shared_ptr<Animator> mDrillRigAnimator;
	mutable ArmorBrokenState mPresentedRigStage = ArmorBrokenState::NONE;
	mutable DrillPhase mPresentedRigPhase = DrillPhase::SPENT;
};
