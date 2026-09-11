#pragma once
#include "Zombie.h"

/** 盗晶僵尸只拥有动作阶段；累计盗款和携款归 Board 的不可回溯账本。 */
class SunThiefZombie final : public Zombie {
public:
	using Zombie::Zombie;
	enum class Phase { READY, WINDUP, COOLDOWN, RETREAT, DISABLED };
	/** 主线程推进抽取事务；硬控暂停局部计时，撤退位移复用矿道出口图。 */
	void Update() override;
	void Draw(Graphics* g) override;
	/** 实际清除只结算一次返款，时间锚替换外壳保留账本。 */
	void Die() override;
	void HeadDrop() override;
	void StartEat(ColliderComponent* other) override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool IsMovingRight() const override { return IsMindControlled() || mPhase == Phase::RETREAT; }
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	void OnTemporalRecreated() override;
	void OnTemporalCoreStateRestored() override;
	Phase GetTheftPhase() const { return mPhase; }
	float GetTheftRemaining() const { return mRemaining; }
	int GetCarriedSun() const;
protected:
	void SetupZombie() override;
	void ZombieMove(float delta, Transform* transform) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	void OnMindControlled() override;
private:
	/** 原子离开停步前摇，保留承诺中的矿道边和已经提交的经济记录。 */
	void FinishWindup(Phase phase);
	/** 由携款和动作阶段派生静态装备槽，读档/复活不另存视觉状态。 */
	void SyncEquipment() const;
	Phase mPhase = Phase::READY;
	float mRemaining = 0.0f;
	float mFullNotice = 0.0f;
};
