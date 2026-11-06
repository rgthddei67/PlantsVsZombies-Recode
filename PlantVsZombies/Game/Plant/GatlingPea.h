#pragma once

#include "Shooter.h"

/** 经典机枪射手紫卡；一次攻击动画按四个独立帧事件依次发射普通豌豆。 */
class GatlingPea final : public Shooter
{
public:
	using Shooter::Shooter;

	/** 按经典约 1.5 秒节奏启动一轮四连发动画。 */
	void PlantUpdate() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	/** 工坊原位改造继承已付费等级；不改变经典机枪射手。 */
	void InheritWorkshopLevel(int level);

protected:
	/** 建立机枪射手独立头部，并注册主人给定的四个发射帧。 */
	void SetupPlant() override;
	/** 从当前炮口发射一颗保留机枪谱系的普通豌豆。 */
	void ShootBullet() override;

private:
	int mWorkshopReservedShots = 0; // 本轮已付费但尚未发射的普通弹，入档后不重复付款
	void PlayShotSound() const;
};
