#pragma once

#include <algorithm>
#include <cmath>

/** 连续射击成长的纯数值画像；仅有实际射击时间才推进，供实体规则与防线预测共用。 */
struct AttackGrowth {
	float progress = 0, perShot = 0, maximum = 0;
	float baseInterval = 1, minimumInterval = 1, intervalFactor = 1;
	int speedShots = 1, damageShots = 1;
	float baseDamage = 0, damageStep = 0, maximumDamage = 0;

	float Damage() const {
		return (std::min)(maximumDamage,baseDamage+static_cast<int>(progress)/damageShots*damageStep);
	}
	float Interval() const {
		return (std::max)(minimumInterval,baseInterval*std::pow(intervalFactor,static_cast<float>(static_cast<int>(progress)/speedShots)));
	}
	/** 在一小步内积分成长；细分阶段跃迁，避免低速起点按整步旧射速漏算菠萝协同。 */
	float Advance(float seconds) {
		float damage = 0;
		while (seconds > 0) {
			const float step = (std::min)(seconds,0.05f);
			const float shots = step/Interval();
			damage += shots*Damage();
			progress = (std::min)(maximum,progress+shots*perShot);
			seconds -= step;
		}
		return damage;
	}
};
