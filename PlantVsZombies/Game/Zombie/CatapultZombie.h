#pragma once

#include "Zombie.h"

class Caltrop;
class Plant;

/**
 * @brief 经典投篮车僵尸：十二发篮球循环、车辆碾压、损坏与专属爆炸/化灰表现。
 */
class CatapultZombie : public Zombie {
public:
	using Zombie::Zombie;

	enum class Phase {
		WALKING,
		SHOOTING,
		RELOADING,
		CALTROP_DYING,
	};

	void ZombieUpdate(float scaledTime) override;
	void TakeBodyDamage(int damage) override;
	void ZombieItemUpdate() const override;
	void Die() override;
	void Charred() override;
	void StartEat(ColliderComponent* other) override;
	bool HandleCaltropHit(Caltrop& caltrop) override;
	float GetCurrentHorizontalMoveSpeed() const override;
	Vector GetVisualPosition() const override;
	const char* GetButterSplatTrackName() const override {
		return "Zombie_catapult_driver_head";
	}
	Vector GetIceTrapBottomAnchor() const override;

	bool CanBeCharmed() const override { return false; }
	bool CanBeParalyzed() const override { return false; }
	bool CanBeGrabbedByTangleKelp() const override { return false; }

	Phase GetPhase() const { return mPhase; }
	int GetBasketballCount() const { return mBasketballCount; }
	float GetPhaseTimer() const { return mPhaseTimer; }
	float GetDriveSpeed() const { return mDriveSpeed; }
	int GetDamageStage() const;
	bool IsCaltropPunctured() const { return mPhase == Phase::CALTROP_DYING; }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	/** 按损坏阶段选择侧板材质，使精英子类复用完整车辆状态机。 */
	virtual const std::string& GetCatapultSidingTextureKey(bool damaged) const;
	/** 按损坏与持球状态选择投臂材质。 */
	virtual const std::string& GetCatapultPoleTextureKey(bool damaged, bool hasBall) const;
	/** 返回普通死亡时发射的粒子效果名。 */
	virtual const char* GetCatapultExplosionEffectName() const;

private:
	/** @brief 从步行态进入一次射击，并冻结本发篮球的目标弹心。 */
	void BeginShooting(Plant& target);
	/** @brief 主人指定的第 46 帧回调：生成并配置一颗篮球。 */
	void LaunchBasketball();
	/** @brief 射击片段完成后扣除库存并进入装填或永久步行。 */
	void FinishShooting();
	/** @brief 返回同排最靠房屋且与车辆保持原版最小间距的植物。 */
	Plant* FindBasketballTarget() const;
	/** @brief 从迎敌面结算同排植物碾压响应；存活阻挡者可要求车辆后退。 */
	void CrushPlants();
	bool CanCrushPlant(const Plant* plant) const;
	/** @brief 按库存重建四个篮筐篮球轨道和投臂带球材质。 */
	void ApplyBasketballPresentation() const;
	/** @brief 按当前生命重建侧板、投臂与烟雾阶段材质。 */
	void ApplyDamageVisuals() const;
	void PlayWalking();

	Phase mPhase = Phase::WALKING;
	float mPhaseTimer = 0.0f;
	int mBasketballCount = 12;
	float mDriveSpeed = 30.0f;
	Vector mShotTarget;
	bool mShotFiredThisCycle = false;
	float mSelfBrokenTimer = 0.0f;
	float mSmokeTimer = 0.0f;
	Vector mDamageShakeOffset;
	bool mSuppressDeathEffects = false;
	bool mDeathEffectsEmitted = false;
};
