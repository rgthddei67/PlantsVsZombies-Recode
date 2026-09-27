#pragma once
#include "Zombie.h"
#include "BoilerRules.h"

/** 锅炉僵尸：四格内预热，付费提交一生一次的超频；冻结不延长已提交阶段。 */
class BoilerZombie final : public Zombie {
public:
	using Zombie::Zombie;
	enum class Phase { READY, PREHEATING, OVERDRIVE, VENTING, SPENT };
	/** 推进预热与独立于硬控的已提交阶段，然后走普通僵尸生命周期。 */
	void Update() override;
	/** 普通骨架之外显示当前锅炉阶段及剩余压力条。 */
	void Draw(Graphics* g) override;
	void Die() override;
	void HeadDrop() override;
	void ZombieItemUpdate() const override;
	/** 保存唯一发动资格与剩余时长，已付费机会不得读档补发。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 修复失头、死亡、魅惑和无效阶段组合，同时重建设备。 */
	void LoadExtraData(const nlohmann::json& j) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	Phase GetBoilerPhase() const { return mPhase; }
	float GetPhaseRemaining() const { return mRemaining; }
	bool HasSpentOverdrive() const { return mSpent; }
	float GetRetryRemaining() const { return mRetryRemaining; }
	/** 去除当前临时阶段倍率，供预测重新按阶段施加移动/每口伤害，避免把超频当成永久状态。 */
	float GetForecastBaseMoveSpeed() const { return GetMineSimulationMoveSpeed() / GetAmplifiedAbilitySpeedMultiplier(); }
	float GetForecastBaseBiteDps() const { return GetMineSimulationAttackDps() / GetAbilityBiteDamageMultiplier(); }
protected:
	void SetupZombie() override;
	void ZombieMove(float delta, Transform* transform) override;
	void StartEat(ColliderComponent* other) override;
	void OnMindControlled() override;
	float GetAbilityAnimSpeedMultiplier() const override;
	float GetAbilityBiteDamageMultiplier() const override;
private:
	/** 停止前摇/爆发并修复移动与啃食；已扣费用不回滚。 */
	void ChangePhase(Phase phase, float duration);
	bool HasPlantInRange() const;
	void ConfigureBoiler();
	Phase mPhase = Phase::READY;
	float mRemaining = 0.0f;
	float mRetryRemaining = 0.0f;
	bool mSpent = false;
};
