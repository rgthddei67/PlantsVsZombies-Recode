#pragma once

#include "Zombie.h"

/**
 * @brief 极夜钟匠僵尸：提交由 Board 独立拥有的六秒时间锚。
 *
 * 星盘是非磁性一类防具；本体继续使用普通僵尸的经典完整时间线。
 */
class PolarClockmakerZombie final : public Zombie {
public:
	static constexpr float BaseAbilitySpeed = 0.7f; // 本品种常驻能力速度倍率，与出生预测共用
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class ClockPhase {
		PREPARING,
		WINDUP,
		RETRY_WAIT,
		COMMITTED,
		DISABLED,
		COOLDOWN,
	};

	void Update() override;
	void Die() override;
	void HeadDrop() override;
	void HelmDrop() override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	bool HasCommittedIrreversibleSpecialAction() const override {
		return mClockPhase == ClockPhase::COMMITTED;
	}
	void RestoreCommittedIrreversibleSpecialAction(bool submitted) override;
	bool CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const override;
	void RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state) override;

	ClockPhase GetClockPhase() const { return mClockPhase; }
	float GetClockRemaining() const { return mClockRemaining; }
	bool HasFinaleFollowersConfigured() const { return mFollowersConfigured; }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void OnMindControlled() override;
	float GetAbilityAnimSpeedMultiplier() const override { return BaseAbilitySpeed; }

private:
	/** 配置星盘与悬摆命名 follower，保持普通僵尸时间线不变。 */
	void ConfigureFollowers();
	/** 从首次准备、循环冷却或重试等待抢占啃食，进入完整 3.2 秒前摇。 */
	void BeginWindup();
	/** 永久取消尚未提交的时间锚。 */
	void DisableUncommittedClock();
	/** 由当前防具与动作阶段同步星盘/悬摆可见性。 */
	void SyncFollowerPresentation() const;

	ClockPhase mClockPhase = ClockPhase::PREPARING;
	float mClockRemaining = 2.0f;
	float mClockVisualPulseTimer = 0.0f;
	bool mFollowersConfigured = false;
};
