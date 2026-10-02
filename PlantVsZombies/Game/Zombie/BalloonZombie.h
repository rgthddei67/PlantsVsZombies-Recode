#pragma once

#include "Zombie.h"
#include "../WeatherTypes.h"

/**
 * @brief 经典气球僵尸：先以 20 点气球生命飞行，破裂演出结束后落地并恢复普通啃食。
 */
class BalloonZombie final : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		FLYING,
		POPPING,
		WALKING,
	};

	Phase GetPhase() const { return mPhase; }
	int GetBalloonHealth() const { return mBalloonHealth; }
	int GetBalloonMaxHealth() const { return mBalloonMaxHealth; }
	float GetFlightVelocity() const { return mFlightVelocity; }
	float GetPropellerFrame() const;
	bool IsPropellerPlaying() const;
	bool IsBloverBlowing() const { return mBloverBlowing; }
	WindDirection GetBloverBlowDirection() const { return mBloverBlowDirection; }
	float GetBloverBlowRemaining() const { return mBloverBlowRemaining; }
	/** AutoTest 静止靶与存档恢复共用的明确速度入口，单位：像素/秒。 */
	void SetFlightVelocity(float velocity);
	/**
	 * 启动三叶草连续吹飞：向屋后累计滑行 400px，向前线滑出屏幕后再 Die。
	 */
	void BlowAway(WindDirection direction);

	void Update() override;
	void StartEat(ColliderComponent* other) override;
	void PlaySpawnSound() override;
	/** 灰烬伤害足以击杀时直接移除，不生成烧焦残影或转入普通死亡轨。 */
	void TakePlantAshDamage(int damage) override;
	/** 所有进入化灰表现的兼容调用都收敛为直接移除。 */
	void Charred() override { Die(); }
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	bool IsFlying() const override { return mPhase != Phase::WALKING; }
	bool CanBeAffectedByCobCannonExplosion() const override { return true; }
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanBeFrozen() const override { return mPhase == Phase::WALKING; }
	bool CanBeCharred() const override {
		return mPhase == Phase::WALKING && Zombie::CanBeCharred();
	}

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	int TakeExtraProtectionDamage(int damage, DamageSource source) override;
	void ApplyExtraHealthMultiplier(double multiplier) override;
	bool CanUseGroundPoolState() const override { return mPhase == Phase::WALKING; }

private:
	/** 从同一 reanim 创建只循环 propeller 轨道的帽子附件。 */
	void CreatePropellerAnimator();
	/** 清除父轨切换传播来的 clip 覆盖，同时保留冻结和减速的 extra 层。 */
	void KeepPropellerIndependent() const;
	/** 消耗完气球生命后播放爆裂演出；水路行直接移除。 */
	void PopBalloon();
	/** 爆裂轨结束后提交地面阶段、碰撞框和走路轨道。 */
	void FinishLanding();
	/** 按当前阶段同步碰撞高度、入水状态和断肢许可。 */
	void ApplyPhasePresentation();
	/** 落地时补结算气球阶段积累的本体断肢断头阈值。 */
	void ResolveDeferredBodyParts();

	Phase mPhase = Phase::FLYING;
	int mBalloonHealth = 20;
	int mBalloonMaxHealth = 20;
	float mFlightVelocity = 30.0f;
	float mGroundColliderOffsetY = -65.0f;
	std::shared_ptr<Animator> mPropellerAnimator;
	bool mBloverBlowing = false;
	WindDirection mBloverBlowDirection = WindDirection::NONE;
	float mBloverBlowRemaining = 0.0f;
};
