#pragma once
#include "WallNut.h"
#include "IceStorageNutRules.h"

/** 冰仓坚果：把压扁转为有限承伤，短暂无敌并可付费修复自身。 */
class IceStorageNut final : public WallNut {
public:
	using WallNut::WallNut;
	/** 按游戏时间推进无敌和修复冷却；暂停不推进。 */
	void Update() override;
	/** 保留经典受啃与裂纹表现，只在缺少完整恢复量时自动修复。 */
	void PlantUpdate() override;
	/** 绘制当前模式、护体状态和游戏时间倒计时。 */
	void Draw(Graphics* g) override;
	bool IsDamageImmune() const override { return mInvulnerableRemaining > 0.0f; }
	void ResolveGargantuarSmash() override;
	VehicleCrushResponse ResolveVehicleCrush() override;
	/** 原子校验存活、行动、血量、冷却和余额，成功才付款。 */
	bool TryActivate();
	bool IsReadyToActivate() const;
	bool CanAffordActivation() const;
	bool IsAutomatic() const { return mAutomatic; }
	void SetAutomatic(bool value) { mAutomatic = value; }
	float GetCooldownRemaining() const { return mCooldownRemaining; }
	float GetInvulnerableRemaining() const { return mInvulnerableRemaining; }
	bool HasManualAbility() const override { return true; }
	bool TryActivateManualAbility() override { return TryActivate(); }
	bool IsAbilityAutomatic() const override { return IsAutomatic(); }
	void SetAbilityAutomatic(bool value) override { SetAutomatic(value); }
	std::string GetManualAbilityDescription() const override;
	/** 保存每株模式、无敌和冷却余时，不重放已付款修复。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 夹紧并还原模式与余时，不重新付款、补血或触发护体。 */
	void LoadExtraData(const nlohmann::json& j) override;
protected:
	/** 初始化坚果的生命与独立损伤材质，保留原版动画。 */
	void SetupPlant() override;
	const std::string& GetBodyTextureKey() const override;
	const std::string& GetCrackedTextureKey(int stage) const override;
private:
	/** 巨人和车辆共用一次承伤边沿；无敌期间不刷新资格。 */
	bool TakeCrushImpact();
	float mInvulnerableRemaining = 0.0f;
	float mCooldownRemaining = 0.0f;
	bool mAutomatic = false;
};
