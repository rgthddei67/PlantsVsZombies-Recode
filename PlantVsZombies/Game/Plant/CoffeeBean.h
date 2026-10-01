#pragma once

#include "Plant.h"

/**
 * @brief 叠种在睡眠蘑菇上方的一次性咖啡豆；等待 1 秒后播放碎裂动画并启动目标唤醒。
 *
 * 咖啡豆占 Cell 的短时 overlay 层，不参与啃食目标与普通/南瓜顶层选择。
 */
class CoffeeBean : public Plant
{
public:
	using Plant::Plant;
	/** 已进入结算动作时禁止搬运，保留原目标及动作提交位置。 */
	bool CanBeRelocated() const override { return false; }

	bool CanBeEaten() const override { return false; }
	/** 原版 flying 咖啡豆不参与地面植物伤害结算；等待和碎裂阶段均忽略伤害。 */
	void TakeDamage(int damage, DamageSource source) override;
	/** 地裂明确命中整个植物格，因此咖啡豆对此类别例外地走基类正式承伤链。 */
	void TakeWinterGroundImpactDamage(WinterGroundImpactKind kind,
		int damage, DamageSource source) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

	const char* GetPhaseName() const;
	int GetWaitTimeRemainingMs() const;
	/** 从新种咖啡到目标清醒的完整游戏秒数，不含目标自己的后续动作。 */
	static float GetFullWakeDelay();
	/** 已放下咖啡尚未启动的唤醒等待；已进入碎裂时由目标自己的唤醒计时接管。 */
	float GetPendingWakeDelay() const;

protected:
	void SetupPlant() override;
	void PlantUpdate() override;

private:
	enum class Phase {
		WAITING,
		CRUMBLING,
	};

	Phase mPhase = Phase::WAITING;
	float mWaitTimer = 1.0f;

	/** 查询同格普通层并启动正式唤醒，然后切入原版 anim_crumble 一次性动画。 */
	void StartCrumbling();
};
