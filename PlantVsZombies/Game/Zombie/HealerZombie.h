#pragma once

#include "Zombie.h"

#include <vector>

/**
 * 第六大关急救员僵尸：每五秒用可切换的推演/确定性策略选择无上限群疗或高额单疗。
 * 普通移动、啃食、断肢断头与死亡帧事件全部复用 `Zombie`。
 */
class HealerZombie final : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class TreatmentState {
		IDLE,
		AREA,
		FOCUSED,
		DISABLED,
	};
	enum class DecisionMode {
		DETERMINISTIC,
		MONTE_CARLO,
	};
	enum class DecisionAction {
		NONE,
		AREA,
		FOCUSED,
		WAIT,
	};

	void Update() override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void Die() override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	TreatmentState GetTreatmentState() const { return mTreatmentState; }
	float GetHealCooldownRemaining() const { return mHealCooldown; }
	float GetRetryRemaining() const { return mRetryTimer; }
	float GetCastRemaining() const { return mCastRemaining; }
	int GetFocusedTargetID() const { return mFocusedTargetID; }
	int GetLastHealTargetCount() const { return mLastHealTargetCount; }
	int GetLastHealTotalAmount() const { return mLastHealTotalAmount; }
	float GetStrategicWaitElapsed() const { return mStrategicWaitElapsed; }
	DecisionMode GetLastDecisionMode() const { return mLastDecisionMode; }
	DecisionAction GetLastDecisionAction() const { return mLastDecisionAction; }
	int GetLastMonteCarloRolloutCount() const { return mLastMonteCarloRolloutCount; }
	int GetLastMonteCarloCandidateCount() const { return mLastMonteCarloCandidateCount; }
	int GetLastMonteCarloZombieCount() const { return mLastMonteCarloZombieCount; }
	int GetLastMonteCarloCardCount() const { return mLastMonteCarloCardCount; }
	float GetLastMonteCarloBestScore() const { return mLastMonteCarloBestScore; }
	bool IsHealingPermanentlyDisabled() const { return mHealingPermanentlyDisabled; }
	bool HasTreatmentGearFollower() const { return mGearFollowerConfigured; }
	bool IsTreatmentGearVisible() const;
	const std::string& GetTreatmentGearTextureKey() const {
		return TreatmentGearTextureKey();
	}
	/** EntityRegistry 的 ID 顺序门禁只询问稀有急救员索引，不扫描全体僵尸。 */
	bool IsReadyForTreatmentChoice() const;
	bool IsFocusedOnTarget(int zombieID) const;
	/** AutoTest 只压缩等待时间，不越过正常决策、前摇和结算路径。 */
	void MakeTreatmentReadyForTesting();

protected:
	void SetupZombie() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void OnMindControlled() override;

private:
	/** 返回目标碰撞中心；没有碰撞体时退回稳定位置。 */
	static Vector TreatmentCenter(const Zombie& zombie);
	/** 目标是否仍有可修复的本体、头盔或盾牌生命。 */
	static bool IsWounded(const Zombie& zombie);
	/** 返回现存可修复生命层的最低剩余比例，用于单疗排序。 */
	static float LowestRepairableRatio(const Zombie& zombie);
	bool IsValidTreatmentTarget(const Zombie& zombie, float radius,
		bool allowSelf) const;
	std::vector<int> CollectAreaTargets(float radius) const;
	/** 收集全部未被其他急救员预留的单疗候选，并按实体 ID 稳定排序。 */
	std::vector<int> CollectFocusedTargets() const;
	int SelectFocusedTarget() const;
	/** 在群疗、全部单疗目标和有界等待间执行一次纯数值推演决策。 */
	bool SelectMonteCarloTreatment(const std::vector<int>& areaTargets,
		const std::vector<int>& focusedTargets);
	void BeginTreatment(TreatmentState state, int focusedTargetID);
	void ResolveTreatment();
	int ApplyTreatment(Zombie& target, int amount, const char* effectName) const;
	void CancelTreatment(bool permanent, bool resumeEating);
	void StopEatingForTreatment();
	void ResumeEatingAfterTreatment();
	bool IsResumeTargetInBiteRange(const AnimatedObject& target) const;
	void ConfigureTreatmentPresentation();
	void ApplyTreatmentPresentation() const;
	const std::string& TreatmentGearTextureKey() const;

	TreatmentState mTreatmentState = TreatmentState::IDLE;
	float mHealCooldown = 0.0f;
	float mRetryTimer = 0.0f;
	float mCastRemaining = 0.0f;
	int mFocusedTargetID = NULL_ZOMBIE_ID;
	int mResumePlantID = NULL_PLANT_ID;
	int mResumeZombieID = NULL_ZOMBIE_ID;
	int mLastHealTargetCount = 0;
	int mLastHealTotalAmount = 0;
	float mStrategicWaitElapsed = 0.0f;
	DecisionMode mLastDecisionMode = DecisionMode::DETERMINISTIC;
	DecisionAction mLastDecisionAction = DecisionAction::NONE;
	int mLastMonteCarloRolloutCount = 0;
	int mLastMonteCarloCandidateCount = 0;
	int mLastMonteCarloZombieCount = 0;
	int mLastMonteCarloCardCount = 0;
	float mLastMonteCarloBestScore = 0.0f;
	bool mHealingPermanentlyDisabled = false;
	bool mGearFollowerConfigured = false;
};
