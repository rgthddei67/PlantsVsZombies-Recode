#pragma once

#include "Zombie.h"
#include "Game/Board/Board.h"

#include <algorithm>

class Plant;

/**
 * @brief 经典蹦极僵尸：从目标格上方下降，短暂停留后抓走一株植物并升空离场。
 */
class BungeeZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		DIVING,
		AT_BOTTOM,
		GRABBING,
		RISING,
	};

	enum class TargetMode {
		RANDOM,
		MONTE_CARLO,
	};

	Phase GetPhase() const { return mPhase; }
	TargetMode GetTargetMode() const { return mTargetMode; }
	float GetAltitude() const { return mAltitude; }
	float GetPhaseTimer() const { return mPhaseTimer; }
	int GetTargetRow() const { return mTargetRow; }
	int GetTargetColumn() const { return mTargetColumn; }
	int GetTargetPlantID() const { return mTargetPlantID; }
	bool HasSelectedTarget() const { return mTargetInitialized; }
	const MonteCarloTargetStats& GetMonteCarloStats() const { return mMonteCarloStats; }
	/** AutoTest 专用：把下降高度压到确定值，避免按墙钟等待完整下落。 */
	void SetAltitudeForTesting(float altitude) {
		if (mPhase == Phase::DIVING || mPhase == Phase::RISING) {
			mAltitude = std::max(0.0f, altitude);
		}
	}
	/** AutoTest 专用：把落地等待计时压到确定值。 */
	void SetBottomWaitForTesting(float seconds) {
		if (mPhase == Phase::AT_BOTTOM) mPhaseTimer = std::max(0.0f, seconds);
	}

	void Draw(Graphics* g) override;
	void StartEat(ColliderComponent*) override {}
	void TakeDamage(int damage, DamageSource source, bool penetrateShield = false,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {}) override;
	void TakePlantAshDamage(int damage) override;
	bool TakePlantInstantKill() override;
	void Die() override;
	void PlaySpawnSound() override;
	void ZombieItemUpdate() const override;
	Vector GetVisualPosition() const override;
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanBeCharmed() const override { return false; }
	bool CanBeChilled() const override;
	bool CanBeFrozen() const override { return CanBeChilled(); }
	bool CanTriggerPotatoMine() const override { return false; }
	bool CanBeGrabbedByTangleKelp() const override { return false; }
	bool CanTriggerGameOver() const override { return false; }
	bool CanBeCharred() const override { return IsVulnerable(); }
	bool CanBeAffectedByGroundHazards() const override { return IsVulnerable(); }

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override {}
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float, Transform*) override {}
	void TakeBodyDamage(int damage) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool CanUseGroundPoolState() const override { return false; }
	bool CanBeMovedByTyphoonGust() const override { return false; }

private:
	struct CellCandidate {
		int row = 0;
		int column = 0;
		int plantID = NULL_PLANT_ID;
	};

	/** 按全局 AI 开关选择蒙特卡洛单株移除或原版网格加权随机。 */
	bool SelectTarget();
	/** 为每个未预订植物格建立精确实体候选，并选取僵尸方收益最大的单株移除。 */
	bool SelectMonteCarloTarget();
	/** 复刻原版有植物格 10000、空格 1 的随机选择，并保护最后一株向日葵。 */
	bool SelectOriginalRandomTarget();
	void ApplySelectedCell(const CellCandidate& candidate);
	/** 返回该格会被实际抱走的一层，优先 normal，其次 pumpkin、under。 */
	Plant* ResolveBungeePlantAt(int row, int column) const;
	bool IsCellReserved(int row, int column) const;
	void LandAtTarget();
	void BeginGrab();
	void BeginRise();
	void UpdateCargoOffset();
	/** 把前侧下臂拆到独立 Animator，以便在身体与手臂之间夹绘植物。 */
	void ConfigureFrontArmLayers();
	void DrawCordAndTarget(Graphics* g) const;
	bool IsVulnerable() const {
		return mPhase == Phase::AT_BOTTOM || mPhase == Phase::GRABBING;
	}

	Phase mPhase = Phase::DIVING;
	TargetMode mTargetMode = TargetMode::RANDOM;
	float mAltitude = 0.0f;
	float mPhaseTimer = 0.0f;
	int mTargetRow = -1;
	int mTargetColumn = -1;
	int mTargetPlantID = NULL_PLANT_ID;
	bool mTargetInitialized = false;
	bool mScreamPlayed = false;
	MonteCarloTargetStats mMonteCarloStats;
	std::shared_ptr<Animator> mFrontArmAnimator;
};
