#pragma once

#include "Zombie.h"

class Plant;

/**
 * 冰像处刑者：在冻土上封存一株推演判定的关键植物，以植物专属次数的锤击完成处决。
 * 黑色橄榄球头盔是 anim_head1 的跟随贴图，耐久与掉落状态独立；扶梯轨本身始终隐藏。
 */
class IceStatueExecutionerZombie final : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class ExecutionPhase {
		READY,
		EXECUTING,
		SPENT,
	};
	enum class TargetingMode {
		NONE,
		MONTE_CARLO,
		STRATEGIC_FALLBACK,
	};

	void StartEat(ColliderComponent* other) override;
	void Die() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void HelmDrop() override;
	/** 头盔存在时以本体+防具总耐久决定灰烬直消，否则让爆炸走正常扣血链。 */
	void TakePlantAshDamage(int damage) override;
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;

	ExecutionPhase GetExecutionPhase() const { return mExecutionPhase; }
	int GetExecutionTargetPlantID() const { return mExecutionTargetPlantID; }
	int GetExecutionProgress() const { return mExecutionProgress; }
	bool HasUsedExecution() const { return mExecutionUsed; }
	TargetingMode GetTargetingMode() const { return mTargetingMode; }
	int GetTargetingRolloutCount() const { return mTargetingRolloutCount; }
	int GetTargetingCandidateCount() const { return mTargetingCandidateCount; }
	int GetTargetingZombieCount() const { return mTargetingZombieCount; }
	float GetTargetingBestScore() const { return mTargetingBestScore; }
	int GetCurrentRequiredStrikeCount() const;
	ArmorBrokenState GetHelmetStage() const { return mHelmetStage; }
	bool HasHelmetFollower() const;
	bool DoesHelmetFollowerInheritOverlayEffect() const;
	/** 黑盔可见且父头轨正在提交 additive glow 时返回 true。 */
	bool IsHelmetFollowerGlowing() const;
	bool OwnsIceSealFor(int plantID) const;
	/** 全实体恢复后校验植物侧拥有者、冻土和终止态，不重放音画。 */
	void FinalizeIceSealLoad();
	/** AutoTest 专用：在保持双向关系的前提下进入指定锤击状态。 */
	bool SetExecutionStateForTesting(Plant* target, int progress);
	/** AutoTest 专用：从正式入口尝试造冰，保留炉芯花拒绝语义。 */
	bool AttemptExecutionForTesting(Plant* target);

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void OnMindControlled() override;
	void CheckHelmImage() override;

private:
	bool IsFullyOnBattlefield() const;
	bool CanOwnExecution() const;
	bool BeginExecution(Plant& target);
	void BeginStrike(float blendTime);
	void CommitStrike();
	void AbortExecution(bool consumeAbility, bool restoreWalkAnimation);
	Plant* ResolveExecutionTarget() const;
	void ConfigureHelmetFollower();
	void SyncHelmetPresentation() const;
	void HideHelmetFollower() const;
	void ApplyExecutionerTextures() const;
	const std::string& GetHelmetTextureKey() const;
	static float WalkClipFromVelocity(float velocity);

	ExecutionPhase mExecutionPhase = ExecutionPhase::READY;
	int mExecutionTargetPlantID = NULL_PLANT_ID;
	int mExecutionProgress = 0;
	bool mExecutionUsed = false;
	TargetingMode mTargetingMode = TargetingMode::NONE;
	int mTargetingRolloutCount = 0;
	int mTargetingCandidateCount = 0;
	int mTargetingZombieCount = 0;
	float mTargetingBestScore = 0.0f;
	float mWalkVelocity = 0.30f;
	ArmorBrokenState mHelmetStage = ArmorBrokenState::NO_BROKEN;
	mutable bool mHelmetFollowerConfigured = false;
};
