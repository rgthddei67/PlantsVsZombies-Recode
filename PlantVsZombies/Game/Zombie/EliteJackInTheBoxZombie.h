#pragma once

#include "JackInTheBoxZombie.h"

class Graphics;

/**
 * @brief 精英小丑：不会自爆，持续把手中盒子投向自身及相邻有效行并造成小范围伤害。
 */
class EliteJackInTheBoxZombie final : public JackInTheBoxZombie {
public:
	/** 固定精英手摇步速，复用普通小丑的资源换算。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	enum class PlantTargetingMode {
		NONE,
		FORCED,
		GREEDY,
		MONTE_CARLO,
		CHARMED_RANDOM
	};

	using JackInTheBoxZombie::JackInTheBoxZombie;

	void Update() override;
	void Draw(Graphics* g) override;
	void ZombieItemUpdate() const override;
	bool HasMagneticItem() const override;

	float GetThrowCountdown() const { return mThrowCountdown; }
	bool IsBoxInFlight() const { return mBoxInFlight; }
	float GetBoxFlightProgress() const;
	Vector GetThrownBoxPosition() const;
	int GetThrowTargetRow() const { return mThrowTargetRow; }
	Vector GetThrowTargetPosition() const { return mBoxTargetPosition; }
	bool WasThrownByMindControlledZombie() const {
		return mThrowWasMindControlled;
	}
	PlantTargetingMode GetLastPlantTargetingMode() const {
		return mLastPlantTargetingMode;
	}
	int GetLastMonteCarloRolloutCount() const {
		return mLastMonteCarloRolloutCount;
	}
	int GetLastMonteCarloCandidateCount() const {
		return mLastMonteCarloCandidateCount;
	}
	int GetLastMonteCarloZombieCount() const {
		return mLastMonteCarloZombieCount;
	}
	int GetLastMonteCarloCardCount() const {
		return mLastMonteCarloCardCount;
	}
	float GetLastMonteCarloCoordinationLoss() const {
		return mLastMonteCarloCoordinationLoss;
	}

	/** AutoTest 确定性入口：覆盖下一投倒计时，并可固定合法目标格。 */
	void SetThrowCountdownForTesting(
		float seconds, int targetRow = -1, int targetColumn = -1);

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	const std::string& GetBrokenArmTextureKey() const override;
	const char* GetArmDropEffectName() const override;

private:
	float RollThrowInterval() const;
	bool BeginThrow();
	void UpdateThrownBox(float deltaTime);
	void ResolveThrownBox();
	void DamagePlantsAtImpact() const;
	void DamageEnemyZombiesAtImpact() const;
	Vector GetHeldBoxWorldPosition() const;
	bool PickThrowTarget(int& targetRow, Vector& targetPosition);
	bool PickMonteCarloPlantTarget(int& targetRow, Vector& targetPosition);
	bool PickGreedyPlantTarget(int& targetRow, Vector& targetPosition) const;
	bool PickRandomEnemyZombieTarget(
		int& targetRow, Vector& targetPosition) const;
	float ScorePlantBlastAt(const Vector& targetPosition) const;
	void SetHeldBoxVisible(bool visible) const;

	float mThrowCountdown = 0.0f;
	float mBoxFlightElapsed = 0.0f;
	Vector mBoxStartPosition;
	Vector mBoxTargetPosition;
	int mThrowTargetRow = -1;
	bool mBoxInFlight = false;
	bool mThrowWasMindControlled = false;
	PlantTargetingMode mLastPlantTargetingMode = PlantTargetingMode::NONE;
	int mLastMonteCarloRolloutCount = 0;
	int mLastMonteCarloCandidateCount = 0;
	int mLastMonteCarloZombieCount = 0;
	int mLastMonteCarloCardCount = 0;
	float mLastMonteCarloCoordinationLoss = 0.0f;

	// 仅由 AutoTest 下一次投掷消费，不属于正式玩法状态，无需入档。
	int mForcedTargetRow = -1;
	int mForcedTargetColumn = -1;
};
