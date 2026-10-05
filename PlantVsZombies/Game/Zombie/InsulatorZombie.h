#pragma once

#include "Zombie.h"

/**
 * 第六大关绝缘僵尸：用一层陶瓷胸甲改写轻型弹丸、径流湿润和黑夜屋顶放电。
 * 普通走路、啃食、断肢断头与死亡帧事件全部继承 `Zombie`。
 */
class InsulatorZombie final : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	void Update() override;
	void Die() override;
	void HelmDrop() override;
	void CheckHelmImage() override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	static constexpr int MagneticBacklashDamage=150; // 卸下绝缘胸甲对磁力菇本体的反噬生命点
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;
	bool CanBeCharred() const override;
	int ModifyProjectileDamage(int damage, BulletType bulletType) const override;
	float ModifySpikeFrameDamage(float damage, bool bypassShield = false) const override;
	int TakeHelmDamageFromSource(int damage, DamageSource source) override;
	bool CanProtectFromNightRoofCharge(const Zombie* target) const override;
	bool AbsorbNightRoofChargeFor(Zombie* target, int damage) override;
	void TakeNightRoofChargeImpact(int damage, float paralysisDuration,
		bool onWetSlope) override;
	float GetAbilityAnimSpeedMultiplier() const override;
	float GetAbilityBiteDamageMultiplier() const override;

	bool IsWet() const;
	bool IsOverloaded() const;
	float GetWetTimeRemaining() const { return mWetTimer; }
	float GetOverloadTimeRemaining() const { return mOverloadTimer; }
	ArmorBrokenState GetArmorStage() const { return mArmorStage; }
	bool HasArmorFollower() const { return mArmorFollowerConfigured; }
	bool IsArmorVisible() const;

protected:
	void SetupZombie() override;

private:
	/** 当前是否正站在活动径流所覆盖的本行坡段。 */
	bool IsOnWetRunoffSlope() const;
	/** 把胸甲配置为身体轨道的前景 follower，完整跟随变换并在全部本体轨道后绘制。 */
	void ConfigureArmorFollower();
	/** 按当前一类防具生命派生裂纹阶段与附件贴图，不产生破甲反馈。 */
	void RefreshArmorPresentation();
	/** 仅扣胸甲且截断溢出；屋顶放电与掩护统一走该入口。 */
	bool TakeArmorDamageNoOverflow(int damage);
	/** 成功承接干燥放电后开始或刷新 15 秒过载。 */
	void BeginOverload();

	ArmorBrokenState mArmorStage = ArmorBrokenState::NO_BROKEN;
	float mWetTimer = 0.0f;
	float mOverloadTimer = 0.0f;
	float mWetArmorDamageRemainder = 0.0f;
	bool mArmorFollowerConfigured = false;
};
