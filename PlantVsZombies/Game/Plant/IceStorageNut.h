#pragma once
#include "WallNut.h"
#include "IceStorageNutRules.h"

/** 冰仓坚果：抗碾压、付费修复，并在短时大量承伤后开启护体。 */
class IceStorageNut final : public WallNut {
public:
	using WallNut::WallNut;
	bool SupportsLadderPlacement() const override { return true; }
	/** 按游戏时间推进修复与护体计时；暂停不推进。 */
	void Update() override;
	/** 正式结算实际承伤；存活且护体就绪时记录滚动窗口。 */
	void TakeDamage(int damage, DamageSource source) override;
	bool IsDamageImmune() const override { return mProtection.invulnerable > 0.0f; }
	/** 保留经典受啃与裂纹表现，只在缺少完整恢复量时自动修复。 */
	void PlantUpdate() override;
	/** 绘制当前模式和修复冷却倒计时。 */
	void Draw(Graphics* g) override;
	void ResolveGargantuarSmash() override;
	VehicleCrushResponse ResolveVehicleCrush() override;
	/** 原子校验存活、行动、血量、冷却和余额，成功才付款。 */
	bool TryActivate();
	bool IsReadyToActivate() const;
	bool CanAffordActivation() const;
	bool IsAutomatic() const { return mAutomatic; }
	void SetAutomatic(bool value) { mAutomatic = value; }
	float GetCooldownRemaining() const { return mCooldownRemaining; }
	float GetInvulnerableRemaining() const { return mProtection.invulnerable; }
	const IceStorageNutRules::Protection& GetProtectionState() const { return mProtection; }
	bool HasManualAbility() const override { return true; }
	bool TryActivateManualAbility() override { return TryActivate(); }
	bool IsAbilityAutomatic() const override { return IsAutomatic(); }
	void SetAbilityAutomatic(bool value) override { SetAutomatic(value); }
	std::string GetManualAbilityDescription() const override;
	/** 保存修复模式与冷却、护体阶段和滚动窗口，不重放已提交效果。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 夹紧并还原模式、计时与窗口；旧档通过 schema 迁移到中性护体状态。 */
	void LoadExtraData(const nlohmann::json& j) override;
protected:
	/** 初始化坚果的生命与独立损伤材质，保留原版动画。 */
	void SetupPlant() override;
	const std::string& GetBodyTextureKey() const override;
	const std::string& GetCrackedTextureKey(int stage) const override;
private:
	/** 巨人和车辆共用正式承伤边沿；护体期间免伤且不重复击退车辆。 */
	bool TakeCrushImpact();
	/** 按当前护体阶段同步冰蓝染色；读档只恢复外观。 */
	void UpdateProtectionAppearance();
	IceStorageNutRules::Protection mProtection;
	float mCooldownRemaining = 0.0f;
	bool mAutomatic = false;
};
