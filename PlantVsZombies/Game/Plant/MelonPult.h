#pragma once

#include "Plant.h"

enum class BulletType;

/**
 * 经典西瓜投手：优先攻击同行冰墙，否则预判僵尸落点并溅射相邻行。
 */
class MelonPult : public Plant
{
public:
	using Plant::Plant;

    int GetWorkshopUpgradeLevel() const override { return mWorkshopUpgradeLevel; }
    bool TryUpgradeWorkshop() override;
    /** 原位升级冰瓜时继承已付费的等级；只用于工坊创建事务。 */
    void InheritWorkshopLevel(int level);
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
	/** 设置待机、随机首轮相位和主人确认的第 44 帧发射事件。 */
	void SetupPlant() override;
	/** 返回该西瓜投手品种创建的投射物类型。 */
	virtual BulletType GetMelonBulletType() const;

private:
    int mWorkshopUpgradeLevel = 1;
    float WorkshopSpeed() const;
	float mShootTimer = 0.0f;
	float mShootInterval = 3.0f;

	/** 返回同行最近的合法地面目标；未找到时返回 nullptr。 */
	class Zombie* FindTarget() const;
	/** 在动画发射帧重新预测落点并创建西瓜。 */
	void FireMelon();
};
