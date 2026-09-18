#pragma once
#include "Zombie.h"

/** 晶角头盔驱动的直线冲撞；动作提交后不拐弯、不重新锁定目标。 */
class CrystalHornMinerZombie final : public Zombie {
public:
	using Zombie::Zombie;
	enum class Phase { READY, WINDUP, CHARGING, COOLDOWN };
	void Update() override;
	void StartEat(ColliderComponent* other) override;
	void HelmDrop() override;
	void TakePlantAshDamage(int damage) override;
	void HeadDrop() override;
	void Die() override;
	void ZombieItemUpdate() const override;
	void OnTemporalCoreStateRestored() override;
	void OnTemporalRecreated() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool InterruptUncommittedSpecialAction() override;
	float GetInterruptibleSpecialActionRemaining() const override;
	Phase GetChargePhase() const { return mPhase; }
	float GetChargeRemaining() const { return mRemaining; }
	float GetChargeTravelled() const { return mTravelled; }
protected:
	void SetupZombie() override;
	void ZombieMove(float delta, Transform* transform) override;
	void OnMindControlled() override;
private:
	/** 取消本地动作并收敛移动/啃食，不退款已结算伤害。 */
	void AbortCharge();
	/** 有前方触发范围内目标时锁定当前直线，裁剪六格终点并进入蓄力。 */
	void TryBeginCharge();
	/** 返回锁定方向扫掠范围内最近的可食战斗顶层；reach 单位像素。 */
	Plant* FindChargePlant(float reach) const;
	/** 静默同步头盔破损、可见性和蓄力姿态，不修改逻辑坐标。 */
	void SyncEquipment() const;
	Phase mPhase = Phase::READY;
	float mRemaining = 0.0f;
	float mTravelled = 0.0f;
	Vector mDirection{-1,0};
	Vector mEnd{};
	float mChargeSpeed = 0.0f;
};
