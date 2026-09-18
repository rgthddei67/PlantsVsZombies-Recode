#pragma once

#include "ScaredyShroom.h"

/**
 * 精英胆小菇：连续射击会同步提升攻速与孢子伤害，受惊后成长全部清零。
 * 白天保持清醒，但每发只获得夜间 60% 的成长进度。
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

protected:
	void SetupPlant() override;
	float GetShootInterval() const override;
	int GetPuffDamage() const override;
	void OnPuffFired() override;
	void OnFearStarted() override;

private:
	float mGrowthProgress = 0.0f;
};
