#pragma once
#include "Plant.h"
#include "ColdPineappleRules.h"

/** 蓄冷菠萝：付费开启九格攻速领域；实例拥有模式、持续时间和冷却。 */
class ColdPineapple final : public Plant {
public:
	using Plant::Plant;
	/** 按游戏时间推进持续/冷却，控制状态不延长已付款领域。 */
	void Update() override;
	/** 自动挡仅在就绪且付得起费用时提交技能。 */
	void PlantUpdate() override;
	/** 绘制就绪晶片、持续/冷却条和模式提示。 */
	void Draw(Graphics* g) override;
	/** 原子检查行动资格、余额及冷却，成功才扣费并开始领域。 */
	bool TryActivate();
	bool IsReadyToActivate() const;
	bool CanAffordActivation() const;
	bool IsAutomatic() const { return mAutomatic; }
	void SetAutomatic(bool automatic) { mAutomatic = automatic; }
	bool HasManualAbility() const override { return true; }
	bool TryActivateManualAbility() override { return TryActivate(); }
	bool IsAbilityAutomatic() const override { return IsAutomatic(); }
	void SetAbilityAutomatic(bool value) override { SetAutomatic(value); }
	std::string GetManualAbilityDescription() const override;
	/** 从持续/冷却、暂停和真实余额分别生成提示，不把不能行动当成缺资源。 */
	std::string GetAbilityStatusText() const;
	float GetActiveRemaining() const { return mActiveRemaining; }
	float GetCooldownRemaining() const { return mCooldownRemaining; }
	float GetAreaAttackSpeedBonus() const override;
	/** 保存领域剩余时间、冷却和每株模式；不保存派生目标列表。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 还原权威状态，不重复付款或播放发动反馈。 */
	void LoadExtraData(const nlohmann::json& j) override;
protected:
	void SetupPlant() override;
private:
	float mActiveRemaining = 0.0f;
	float mCooldownRemaining = 0.0f;
	float mFeedbackRemaining = 0.0f;
	bool mAutomatic = false;
};
