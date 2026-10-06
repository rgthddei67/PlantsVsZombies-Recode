#pragma once

#include "Plant.h"

/**
 * 经典玉米加农炮：由两株相邻玉米投手升级为一个双格实体，装填完成后由玩家指定爆心。
 */
class CobCannon final : public Plant {
public:
	using Plant::Plant;
	/** 动作提交期间不搬运，防止原目标和结算位置失配。 */
	bool CanBeRelocated() const override { return Plant::CanBeRelocated() && mPhase != Phase::FIRING; }

	enum class Phase {
		ARMING,
		CHARGING,
		READY,
		FIRING,
	};

	bool IsReady() const { return mPhase == Phase::READY; }
	Phase GetPhase() const { return mPhase; }
	float GetArmingTimeRemaining() const { return mArmingTime; }
	bool HasLaunchedCurrentShot() const { return mShotLaunched; }
	const Vector& GetPendingTarget() const { return mPendingTarget; }
	int GetPendingTargetRow() const { return mPendingTargetRow; }
	/** READY 状态冻结玩家落点并进入射击轨；其他阶段返回 false。 */
	bool FireAt(const Vector& target, int targetRow);
	bool CanBeTargetedByBungee() const override { return false; }
	/** 双轮炮体需七次已提交锤击才会被冰像处刑者移除。 */
	int GetIceExecutionRequiredStrikeCount() const override { return 7; }
	/** 返回下一次由高操作玩家命中默认最佳爆区的预计剩余秒数。 */
	float GetSimulationAbilityCooldownRemaining() const override;
	/** 已进入射击轨但尚未离膛时，返回当前已提交炮击到爆炸的剩余秒数。 */
	float GetPendingSimulationBlastDelay() const;
	/** 从 READY 下令到爆炸的游戏秒数，与正式射击前摇和弹丸飞行共用参数。 */
	static float GetSimulationShotWindup();
	/** 离膛后独立飞行的游戏秒数；来源死亡不得撤销这段已经提交的炮击。 */
	static float GetSimulationFlightSeconds();
	void Die() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	/** AutoTest 专用：缩短装填倒计时，不跳过正式状态边沿。 */
	void SetArmingTimeForTesting(float seconds) { mArmingTime = std::max(0.0f, seconds); }

protected:
	void SetupPlant() override;
	void PlantUpdate() override;

private:
	/** 开始一次 12fps 充能轨，完成后由轨道切换进入 READY。 */
	void BeginCharge();
	/** 第 78 帧创建唯一玉米棒并播放发射声。 */
	void LaunchCob();
	/** 按当前阶段同步炮弹轨道乘色；只有 READY 使用原版三角波闪烁。 */
	void UpdateCobTrackColor();
	/** 按当前播放头估算指定全局帧尚需的游戏秒数；已越过时返回零。 */
	float GetSecondsUntilFrame(int frame) const;

	Phase mPhase = Phase::ARMING;
	float mArmingTime = 0.0f;
	Vector mPendingTarget = Vector::zero();
	int mPendingTargetRow = -1;
	bool mShotLaunched = false;
};
