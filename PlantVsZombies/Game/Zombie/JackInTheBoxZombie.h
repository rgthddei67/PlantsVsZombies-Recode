#pragma once

#include "Zombie.h"
#include "JackBoxRules.h"

#include <string>

/**
 * @brief 经典小丑僵尸：伴随手摇盒循环声快速前进，随机倒计时后开盒并范围爆炸。
 */
class JackInTheBoxZombie : public Zombie {
public:
	/** 按当前装备/运动阶段读取实际稳态步速，吃饭或施法时不误用动作 clip。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;
	~JackInTheBoxZombie() override;

	enum class Phase {
		RUNNING,
		POPPING,
		DISARMED,
	};

	/** 主线程只读已注册开盒片段，返回有效动画秒；不注册新帧事件。 */
	static float GetForecastExplosionSeconds();
	/** 活体动作/倒计时副本，动画基准剥离天气与控制倍率。 */
	JackBoxRules::Forecast GetBoxForecast() const;
	/** 失盒后的普通行走画像，不消费正式随机数。 */
	static ZombieMovementRules::BirthProfile GetDisarmedMovementProfile();
	Phase GetPhase() const { return mPhase; }
	float GetPopCountdown() const { return mPopCountdown; }
	bool HasPlayedSurprise() const { return mSurprisePlayed; }
	bool HasResolvedExplosion() const { return mExplosionResolved; }
	float GetRunVelocity() const { return mRunVelocity; }
	/** AutoTest 用确定性入口；仅在 RUNNING 阶段覆盖剩余开盒秒数。 */
	void SetPopCountdownForTesting(float seconds);
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;

	void Update() override;
	void TakeDamage(int damage, DamageSource source, bool penetrateShield = false,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {}) override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void PlaySpawnSound() override;
	void Die() override;
	bool CanBeFrozen() const override;

protected:
	/** 普通/精英实际手摇速度共用的出生画像换算。 */
	static ZombieMovementRules::BirthProfile GetRunMovementProfile(float minimum, float maximum);
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	/** 注册同一小丑时间线共用的啃食命中与死亡回收帧。 */
	void RegisterSharedFrameEvents();
	/** 同时间线变体通过该入口调整根运动对应的 C# 速度并立即同步步频。 */
	void SetRunVelocityForVariant(float velocity);
	void ClaimLoopSound();
	void ReleaseLoopSound();
	/** 返回当前变体断臂后仍留在本体上的前臂贴图。 */
	virtual const std::string& GetBrokenArmTextureKey() const;
	/** 返回当前小丑变体被磁力吸走的盒子贴图。 */
	virtual const std::string& GetMagneticBoxImageKey() const;
	/** 返回当前变体抛出断臂所用的粒子效果名。 */
	virtual const char* GetArmDropEffectName() const;

private:
	void BeginPop();
	void PlaySurprise();
	void Explode();
	void StopEatingForPop();
	Vector GetExplosionCenter() const;

	Phase mPhase = Phase::RUNNING;
	float mPopCountdown = 0.0f;
	float mSurpriseCountdown = 0.0f;
	float mRunVelocity = 0.67f;
	bool mSurprisePlayed = false;
	bool mExplosionResolved = false;
	bool mLoopSoundClaimed = false;

	static int sLoopSoundUsers;
};
