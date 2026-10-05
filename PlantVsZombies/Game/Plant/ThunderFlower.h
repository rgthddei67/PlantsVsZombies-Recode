#pragma once
#include "Shooter.h"
#include "ThunderFlowerRules.h"
#include <algorithm>

/** 雷鸣花：复用豌豆射手发射事件，雷种平射命中后造成三行局部伤害与有限麻痹。 */
class ThunderFlower final : public Shooter {
public:
	using Shooter::Shooter;
	/** 1倍攻击速度下下一次发射的名义余秒；完整计时请读取 GetAttackForecast。 */
	float GetAttackRemaining() const;
	/** 主线程冻结冷却、索敌及已有头部前摇，不创建动画或消耗正式随机数。 */
	ThunderFlowerRules::AttackForecast GetAttackForecast() const;
protected:
	void SetupPlant() override;
	void ShootBullet() override;
};
