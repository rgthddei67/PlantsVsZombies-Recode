#pragma once

#include "Plant.h"
#include "../WeatherTypes.h"

/**
 * @brief 三叶草：第 44 帧按卡槽方向吹风，第 61 帧演出结束后消失。
 */
class Blover final : public Plant {
public:
	using Plant::Plant;
	/** 已进入结算动作时禁止搬运，保留原目标及动作提交位置。 */
	bool CanBeRelocated() const override { return false; }

	void SetBlowDirection(WindDirection direction);
	WindDirection GetBlowDirection() const { return mBlowDirection; }
	bool HasTriggeredBlow() const { return mBlowTriggered; }
	/** 新生三叶草到既有吹风结算帧的动画秒，尚未应用雨势或植物行动倍率。 */
	static float GetForecastBlowDelay();
	/** 活体到既有吹风帧的剩余动画秒；已提交时返回零，调用方须先检查 HasTriggeredBlow。 */
	float GetForecastBlowRemaining() const;

protected:
	void SetupPlant() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

private:
	void TriggerBlow();
	void ApplyDirectionPresentation();

	WindDirection mBlowDirection = WindDirection::TOWARD_FRONT;
	bool mBlowTriggered = false;
};
