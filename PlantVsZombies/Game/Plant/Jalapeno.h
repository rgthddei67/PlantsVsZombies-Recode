#pragma once

#include "Plant.h"

/**
 * 经典火爆辣椒：蓄力动画结束时烧毁本行僵尸，并铺开整行火焰表现。
 */
class Jalapeno final : public Plant {
public:
	using Plant::Plant;
	/** 已进入结算动作时禁止搬运，保留原目标及动作提交位置。 */
	bool CanBeRelocated() const override { return false; }
	/** 读取当前引爆帧的剩余游戏秒；无充能动作时返回 -1，不推进动画。 */
	float GetExplosionTimeRemaining() const;

	/** 蓄力期间免疫啃食伤害，只保留受击闪光。 */
	void TakeDamage(int damage, DamageSource source) override;
	void TakeDeploymentInterceptionDamage(int damage, DamageSource source) override {
		Plant::TakeDamage(damage, source);
	}
	/** 巨人锤击命中引爆中的辣椒时立即点燃整行，不生成压扁残影。 */
	void ResolveGargantuarSmash() override;

protected:
	/** 切到主人裁定的爆炸轨，并在全局第 19 帧结算。 */
	void SetupPlant() override;

private:
	/** 清除本行寒冷效果、结算灰烬伤害并生成原版十二段火焰。 */
	void IgniteRow();
};
