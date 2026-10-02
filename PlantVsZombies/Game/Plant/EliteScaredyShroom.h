#pragma once

#include "ScaredyShroom.h"
#include "AttackGrowth.h"

/**
 * 精英胆小菇：连续射击会同步提升攻速与孢子伤害，受惊后成长全部清零。
 * 白天保持清醒，但每发只获得夜间 90% 的成长进度。
 */
class EliteScaredyShroom final : public ScaredyShroom
{
public:
	using ScaredyShroom::ScaredyShroom;

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	int GetGrowthShotCount() const;
	int GetAttackSpeedStage() const;
	int GetCurrentPuffDamage() const;
	int GetShootIntervalMilliseconds() const;
	int GetGrowthRatePercent() const;
	int GetGrowthProgressTenths() const;
	float GetSimulationAttackDps(float) const override { return GetPuffDamage() / GetShootInterval(); }
	/** 导出当前成长及正式阶段参数；预测只修改返回副本，不改变实体或存档。 */
	AttackGrowth GetSimulationAttackGrowth() const;
	/** 新种画像与实体共用成长参数；不继承旧株成长进度。 */
	static AttackGrowth InitialSimulationAttackGrowth(bool night);

protected:
	void SetupPlant() override;
	float GetShootInterval() const override;
	int GetPuffDamage() const override;
	void OnPuffFired() override;
	void OnFearStarted() override;

private:
	float mGrowthProgress = 0.0f;
};
