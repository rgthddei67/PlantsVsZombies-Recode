#pragma once

#include "Zombie.h"

/**
 * @brief 极光祭司僵尸：在逻辑计时仪式后向玩家阵线提交独立极光裂隙。
 *
 * 本体完整复用普通僵尸时间线；极光仪器和胸前光谱片使用命名 follower。
 */
class AuroraPriestZombie final : public Zombie {
public:
	using Zombie::Zombie;

	enum class RitualPhase {
		PREPARING,
		WINDUP,
		RETRY_WAIT,
		COMMITTED,
		DISABLED,
		COOLDOWN,
	};

	/** 推进可中断仪式，只在成功释放裂隙时计次，达到上限后停止施法。 */
	void Update() override;
	void Die() override;
	void HeadDrop() override;
	void HelmDrop() override;
	void ZombieItemUpdate() const override;
	void OnTemporalCoreStateRestored() override;
	/** 保存仪式阶段、余时和累计释放次数。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复仪式与次数，并收敛已耗尽或失去资格的状态。 */
	void LoadExtraData(const nlohmann::json& j) override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;
	bool HasCommittedIrreversibleSpecialAction() const override {
		return mRitualPhase == RitualPhase::COMMITTED;
	}
	void RestoreCommittedIrreversibleSpecialAction(bool submitted) override;
	/** 钟匠记录阶段、余时与释放次数，回溯时可返还额度。 */
	bool CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const override;
	/** 按时间锚恢复局部仪式及次数，保留已经提交的 Board 裂隙。 */
	void RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state) override;

	RitualPhase GetRitualPhase() const { return mRitualPhase; }
	int GetRitualReleaseCount() const { return mRitualReleaseCount; }
	float GetRitualRemaining() const { return mRitualRemaining; }
	bool IsOverloaded() const { return mOverloaded; }
	bool HasFinaleFollowersConfigured() const { return mFollowersConfigured; }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void OnMindControlled() override;
	/** 右侧场外行走时同步加快步频与根运动，其他动作保持原倍率。 */
	float GetAbilityAnimSpeedMultiplier() const override;

private:
	/** 按逻辑 X 与 Board 最右列右缘判断是否仍在入场途中。 */
	bool IsOutsideEntryBoundary() const;
	/** 配置不新增时间轴的极光仪器与光谱片 follower。 */
	void ConfigureFollowers();
	/** 入场后，从首次准备、循环冷却或打断重试边沿抢占啃食并进入完整前摇。 */
	void BeginWindup();
	/** 永久取消尚未提交的仪式，同时保留已经提交的 Board 裂隙。 */
	void DisableUncommittedRitual();
	/** 按当前防具、断头和死亡状态同步附加分件。 */
	void SyncFollowerPresentation() const;

	RitualPhase mRitualPhase = RitualPhase::PREPARING;
	int mRitualReleaseCount = 0; // 已释放次数；存档和时间锚共同恢复
	float mRitualRemaining = 6.0f;
	float mRitualVisualPulseTimer = 0.0f;
	bool mOverloaded = false;
	bool mFollowersConfigured = false;
};
