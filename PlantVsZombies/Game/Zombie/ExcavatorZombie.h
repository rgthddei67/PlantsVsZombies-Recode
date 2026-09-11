#pragma once
#include "Zombie.h"

/** 开凿者保留普通僵尸战斗与动画事件，只拥有一次开墙任务的阶段和计时。 */
class ExcavatorZombie final : public Zombie {
public:
	using Zombie::Zombie;
	enum class Phase { READY, APPROACHING, DRILLING, RETRY, SPENT, DISABLED };
	/** 推进重试并在基类早退后清理失效任务，同步行进朝向与装备。 */
	void Update() override;
	void Die() override;
	/** 取消施工，隐藏完整头部，并一次发射带帽头部粒子。 */
	void HeadDrop() override;
	void ZombieItemUpdate() const override;
	/** 保存任务阶段、预留目标与游戏秒余时；地形由 Board 保存。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 校验任务与地形关系、重建预留；失效任务安全退回重试。 */
	void LoadExtraData(const nlohmann::json& j) override;
	/** 节点上选择或继续已锁定任务，到达施工点后原地施工。 */
	int SelectMineNextCell(int cell) override;
	bool IsMovingRight() const override;
	bool UsesMineExitRoute() const override { return IsMindControlled(); }
	bool CanTriggerGameOver() const override { return !IsMindControlled(); }
	bool InterruptUncommittedSpecialAction() override;
	float GetInterruptibleSpecialActionRemaining() const override;
	Phase GetExcavatorPhase() const { return mPhase; }
	int GetWall() const { return mWall; }
	int GetStand() const { return mStand; }
	float GetWorkRemaining() const { return mRemaining; }
	float GetRetryRemaining() const { return mRetry; }
	float GetWorkProgress() const;
protected:
	void SetupZombie() override;
	void ZombieMove(float delta, Transform* transform) override;
	/** 消费已经过基类控制修正的施工时间，并向 Board 提交一次开墙。 */
	void ZombieUpdate(float delta) override;
	void OnStartEating() override;
	void OnStopEating() override;
	void OnMindControlled() override;
private:
	/** 取消未提交任务并释放预留；不会撤销已提交地形或清除承诺行进边。 */
	void Abort(Phase next);
	/** 由当前阶段/头部状态重建挂件，既用于实时换态也用于读档。 */
	void SyncEquipment() const;
	bool HasTask() const { return mPhase == Phase::APPROACHING || mPhase == Phase::DRILLING; }
	Phase mPhase = Phase::READY;
	int mWall = -1;
	int mStand = -1;
	float mRemaining = 0.0f;
	float mRetry = 0.0f;
};
