#pragma once

#include "Zombie.h"

class Caltrop;

/**
 * @brief 经典冰车僵尸：碾压植物、铺设冰道，并使用车辆专属损坏与死亡表现。
 */
class ZamboniZombie : public Zombie {
public:
	/** 实际碰撞与 AI 共用的冰车压扁资格；状态/行/碰撞过滤仍由各自调用者负责。 */
	static bool CanCrushPlantType(PlantType type, bool asleep);
	/** 铺路前缘相对逻辑原点的水平偏移，供预测生成黄色冰道。 */
	float GetSimulationIceTrailFrontOffset() const;
	/** 待出生品种只读配置偏移，不试生成实体。 */
	static float GetSimulationIceTrailFrontOffset(ZombieType type);
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	void ZombieUpdate(float scaledTime) override;
	void TakeBodyDamage(int damage) override;
	void ZombieItemUpdate() const override;
	void Die() override;
	void Charred() override;
	void StartEat(ColliderComponent* other) override;
	void SetCooldown(float /*timer*/, bool /*bypassShield*/ = false) override {}

	bool CanBeCharmed() const override { return false; }
	bool CanBeChilled() const override { return false; }
	bool CanBeFrozen() const override { return false; }
	bool CanBeParalyzed() const override { return false; }
	bool CanBeGrabbedByTangleKelp() const override { return false; }

	/**
	 * @brief 处理地刺扎车事件；普通冰车进入延迟爆胎死亡，精英冰车可覆写生存规则。
	 */
	bool HandleCaltropHit(Caltrop& caltrop) override;
	float GetCurrentHorizontalMoveSpeed() const override;

	int GetDamageStage() const;
	float GetDriveSpeed() const { return mDriveSpeed; }
	/** 返回当前地图供冰车速度曲线使用的水平坐标基准。 */
	float GetDriveCoordinateBaseX() const;
	/** 实际移动与主线程预测共用当前位置的基础车速曲线。 */
	float GetDriveSpeedAtX(float x) const;
	Vector GetDamageShakeOffset() const { return mDamageShakeOffset; }
	Vector GetVisualPosition() const override;
	const char* GetButterSplatTrackName() const override { return "Zombie_head"; }
	bool IsPuncturedByCaltrop() const { return mPuncturedByCaltrop; }
	float GetCaltropDeathTimer() const { return mCaltropDeathTimer; }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	/** @brief 铺设本品种对应的冰道；普通冰车只处理当前行。 */
	virtual void LayIceTrails(const Vector& stableVisualOrigin);
	/** @brief 返回车辆稳定视觉原点对应的冰道前缘世界 X。 */
	float GetIceTrailFrontX(const Vector& stableVisualOrigin) const;
	/** @brief 按当前血量恢复两段车辆损坏贴图。 */
	virtual void ApplyDamageVisuals() const;
	/** @brief 返回两段损坏贴图使用的资源键前缀。 */
	virtual const char* GetDamageTexturePrefix() const {
		return "IMAGE_ZOMBIE_ZAMBONI_";
	}
	/** @brief 返回车辆速度曲线的品种基础倍率。 */
	virtual float GetBaseDriveSpeedMultiplier() const { return 1.0f; }
	/** @brief 返回本品种死亡时触发的车辆爆炸粒子名。 */
	virtual const char* GetDeathParticleEffectName() const {
		return "ZamboniExplosion";
	}
	/** @brief 判断指定行是否属于本车辆的碾压范围。 */
	virtual bool CanCrushRow(int row) const { return row == mRow; }
	/** @brief 判断植物是否属于原版不可被冰车直接碾压的例外。 */
	virtual bool CanCrushPlant(const Plant* plant) const;
	/** @brief 从迎敌面结算植物碾压响应；存活阻挡者可要求车辆后退。 */
	void CrushPlants();

	float mDriveSpeed = 25.0f;

private:
	/** @brief 恢复地刺爆胎后的扁胎与碰撞终态。 */
	void ApplyCaltropPuncturePresentation() const;

	float mSelfBrokenTimer = 0.0f;	// 自损时间判定计时器
	float mSmokeTimer = 0.0f;
	Vector mDamageShakeOffset;
	bool mSuppressDeathEffects = false;
	bool mDeathEffectsEmitted = false;
	bool mPuncturedByCaltrop = false;
	float mCaltropDeathTimer = 0.0f;
};
