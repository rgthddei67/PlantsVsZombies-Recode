#pragma once
#include "Shooter.h"
#include <algorithm>

/** 雷鸣花：复用豌豆射手发射事件，雷种平射命中后造成三行局部伤害与有限麻痹。 */
class ThunderFlower final : public Shooter {
public:
	using Shooter::Shooter;
	/** 下一次正式发射事件的有效行动余秒，包含尚未提交的头部发射轨。 */
	float GetAttackRemaining() const;
protected:
	void SetupPlant() override;
	void ShootBullet() override;
};
