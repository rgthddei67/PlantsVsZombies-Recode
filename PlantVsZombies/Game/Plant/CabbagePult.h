#pragma once

#include "Plant.h"

/**
 * 经典卷心菜投手：优先攻击同行冰墙，否则按僵尸预计落点投掷卷心菜。
 */
class CabbagePult : public Plant
{
public:
	using Plant::Plant;

	void PlantUpdate() override;
	/** 保存当前攻击周期，保证读档后发射相位连续。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复攻击周期；旧档采用中性首轮状态。 */
	void LoadExtraData(const nlohmann::json& j) override;

	float GetShootTimer() const { return mShootTimer; }
	float GetShootInterval() const { return mShootInterval; }
	/** AutoTest 专用：布置一个将到期的正式攻击周期。 */
	void SetShootCycleForTesting(float elapsedSeconds, float intervalSeconds);

protected:
	/** 设置待机、随机首轮相位和主人确认的第 43 帧发射事件。 */
	void SetupPlant() override;
	/** 同时间轴变体复用已确认的发射帧，只覆写实际弹丸提交。 */
	virtual void FireCabbage();
	virtual float NextShootInterval() const;
	class Zombie* FindTarget() const;

private:
	float mShootTimer = 0.0f;
	float mShootInterval = 3.0f;

};
