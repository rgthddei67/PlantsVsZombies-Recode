#pragma once
#include "Zombie.h"

/** 冷链护卫：持非磁性一类冰盾，消耗己方公共冰块周期修复；破盾永久停止修复。 */
class ColdChainGuardZombie final : public Zombie {
public:
	using Zombie::Zombie;
	/** 正式更新后按游戏时间修复，硬控暂停周期，掉头/死亡不再结算。 */
	void Update() override;
	bool HasMagneticItem() const override { return false; }
	float GetRepairRemaining() const { return mRepairRemaining; }
	bool HasIceShield() const;
	/** 灰烬按冰盾加本体总耐久判断致死，非致死伤害仍走一类防具。 */
	void TakePlantAshDamage(int damage) override;
	int GetShieldDamageStage() const { return mShieldStage; }
	void ZombieItemUpdate() const override;
	/** 破盾后隐藏挂件并停止修复，不能重新取得冰盾。 */
	void HelmDrop() override;
	/** 保存修复周期；盾值和破盾终态由基类防具存档负责。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 还原余时和挂件，不套出生血量或重放修复事务。 */
	void LoadExtraData(const nlohmann::json& j) override;
	/** 记录修盾余时；盾类型与耐久由时间锚核心快照持有。 */
	bool CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const override;
	/** 按已恢复盾值重建挂件和修复周期，不重新扣费。 */
	void RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state) override;
protected:
	/** 初始化普通僵尸身体、一类冰盾与完整修复周期。 */
	void SetupZombie() override;
	/** 从当前盾值刷新损伤阶段与随手臂运动的挂件。 */
	void CheckHelmImage() override;
private:
	/** 按当前防具生命同步冰盾材质与命名手臂 follower。 */
	void SyncShieldPresentation() const;
	float mRepairRemaining = 0.0f;
	int mShieldStage = 0;
};
