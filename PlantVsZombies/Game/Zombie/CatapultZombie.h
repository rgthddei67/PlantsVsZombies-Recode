#pragma once

#include "Zombie.h"
#include "CatapultRules.h"
#include <utility>

class Caltrop;
class Plant;

/**
 * @brief 经典投篮车僵尸：十二发篮球循环、车辆碾压、损坏与专属爆炸/化灰表现。
 */
class CatapultZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
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
	static constexpr bool SupportsParalysis = false; // 投篮车可寒冰减速，但不接受麻痹
	bool CanBeParalyzed() const override { return SupportsParalysis; }
	bool CanBeGrabbedByTangleKelp() const override { return false; }

	Phase GetPhase() const { return mPhase; }
	int GetBasketballCount() const { return mBasketballCount; }
	float GetPhaseTimer() const { return mPhaseTimer; }
	float GetDriveSpeed() const { return mDriveSpeed; }
	int GetDamageStage() const;
	bool IsCaltropPunctured() const { return mPhase == Phase::CALTROP_DYING; }
	/** 只读正式 anim_shoot 的离膛/片段完成行动秒，不生成播放实例；强制资源异常时拒绝画像。 */
	static std::pair<float,float> GetForecastShotTiming();
	/** 当前射击片段余秒，未乘状态/雨势倍率；已离膛标志另读，避免重复制造篮球。 */
	std::pair<float,float> GetForecastShotRemaining() const;
	bool HasLaunchedBasketball() const { return mShotFiredThisCycle; }
	Vector GetShotTargetPosition() const { return mShotTarget; }
	float GetForecastAnimationBase() const { return GetAbilityAnimSpeedMultiplier(); }
	/** 碰撞中心相对稳定视觉原点的 X，供未出生画像换算回车身原点。 */
	static float GetForecastColliderCenterFromVisualX();
	/** 与正式选靶共用跳过地刺的资格，不包含格位/生命和层次选择。 */
	static bool CanLobAtPlantType(PlantType type);
	/** 与正式碾压共用植物类型/睡眠资格；位置和护体响应由当前场景另行判断。 */
	static bool CanCrushPlantType(PlantType type,bool asleep);

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
	int mBasketballCount = CatapultRules::kInitialBasketballs;
	float mDriveSpeed = 30.0f;
	Vector mShotTarget;
	bool mShotFiredThisCycle = false;
	float mSelfBrokenTimer = 0.0f;
	float mSmokeTimer = 0.0f;
	Vector mDamageShakeOffset;
	bool mSuppressDeathEffects = false;
	bool mDeathEffectsEmitted = false;
};
