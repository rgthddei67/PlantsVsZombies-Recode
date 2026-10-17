#pragma once

#include "Plant.h"

/**
 * 杨桃：按五向星弹的真实碰撞轨迹索敌，并在一次射击动画中同时发弹。
 */
class StarFruit final : public Plant
{
public:
	using Plant::Plant;

	/** 保存当前射击周期，保证读档不会提前或延后下一轮。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复当前射击周期；旧档缺字段时采用原版中性初值。 */
	void LoadExtraData(const nlohmann::json& j) override;

	void PlantUpdate() override;

	float GetShootTimer() const { return mShootTimer; }
	float GetShootInterval() const { return mShootInterval; }
	/** AutoTest 专用：布置即将到期的正式射击周期，不直接触发动画或发弹。 */
	void SetShootCycleForTesting(float elapsedSeconds, float intervalSeconds);

protected:
	/** 注册主人指定的第 27 帧发射事件，并设置原版随机待机速度。 */
	void SetupPlant() override;

private:
	float mShootTimer = 0.0f;
	float mShootInterval = 1.5f;

	/** 按发射前摇、目标当前移动和星弹碰撞/行/地形规则预测是否能命中。 */
	bool HasStarFruitTarget() const;
	/** 在同一逻辑帧创建左、上、下、右上、右下五颗星弹。 */
	void FireStarVolley();
};
