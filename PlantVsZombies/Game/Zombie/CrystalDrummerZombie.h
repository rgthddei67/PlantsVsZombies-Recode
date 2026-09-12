#pragma once
#include "Zombie.h"

/** 震晶鼓手：逻辑倒计时提交鼓舞；已发出的效果归受益目标，装备跟随普通骨架。 */
class CrystalDrummerZombie final : public Zombie {
public:
	using Zombie::Zombie;
	void Update() override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void Die() override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	/** 保存等待/前摇/禁用阶段与剩余游戏秒；已提交鼓舞仍归受益目标。 */
	bool CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const override;
	/** 恢复本地敲鼓进度并同步停步与装备表现，不补发鼓舞或重放敲击反馈。 */
	void RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state) override;
	void OnTemporalCoreStateRestored() override;
	bool IsDrumWindingUp() const { return mWindingUp; }
	float GetDrumRemaining() const { return mRemaining; }
	int GetDrumBeatCount() const { return mBeatCount; }
protected:
	void SetupZombie() override;
	void ZombieMove(float delta, Transform* transform) override;
	void OnMindControlled() override;
private:
	/** 清除停步前摇，恢复步行并进入剩余周期；不撤销目标的鼓舞。 */
	void FinishBeat(bool disabled);
	/** 沿连通格查询并一次提交，以稳定来源 ID 刷新各目标的独立层。 */
	void EmitInspiration();
	/** 鼓体跟随躯干、鼓槌跟随内前臂；读档与断肢共用派生表现。 */
	void SyncEquipment() const;
	bool mWindingUp=false;
	bool mDisabled=false;
	float mRemaining=3.5f;
	float mPulseRemaining=0.0f;
	int mBeatCount=0;
};
