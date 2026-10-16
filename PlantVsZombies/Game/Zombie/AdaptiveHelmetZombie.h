#pragma once

#include "Zombie.h"

/**
 * 第八大关适应头盔僵尸：首次击穿头盔的植物谱系或灰烬类别被永久记为数值免疫。
 * 普通行走、啃食、断肢断头与死亡动画全部复用普通僵尸。
 */
class AdaptiveHelmetZombie final : public Zombie {
public:
	using Zombie::Zombie;

	bool CanBeCharmed() const override { return false; }
	bool BlocksPlantDamage(PlantDamageOrigin origin) const override;
	bool TryAdaptHelmetToPlantDamage(int damage, PlantDamageOrigin origin) override;
	void ResolvePlantAshDamage(int damage) override;
	void HeadDrop() override;
	void HelmDrop() override;
	void Die() override;
	void ZombieItemUpdate() const override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	/** 记录“未适应/植物谱系/灰烬”完整来源；未适应也必须作为有效快照参与回溯。 */
	bool CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const override;
	/** 原子恢复适应来源、头盔数值与 follower 表现。 */
	void RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state) override;

	bool HasAdapted() const { return mAdaptedOrigin.IsValid(); }
	PlantDamageOrigin GetAdaptedOrigin() const { return mAdaptedOrigin; }
	bool IsAdaptiveHelmetVisible() const;
	bool IsAdaptedBadgeVisible() const;

protected:
	void SetupZombie() override;

private:
	/** 配置静态头盔与胸章 follower；两者均只复用父轨道时间线。 */
	void ConfigureFollowers();
	/** 按头盔、适应和死亡状态同步两个 follower 的可见性。 */
	void SyncFollowerPresentation() const;
	/** 应用适应来源并修复“头盔存在”和“已适应”互斥的不变量。 */
	void ApplyAdaptedOriginState(PlantDamageOrigin origin);

	PlantDamageOrigin mAdaptedOrigin{};
	bool mFollowersConfigured = false;
};
