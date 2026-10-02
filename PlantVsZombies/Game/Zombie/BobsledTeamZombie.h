#pragma once

#include "Zombie.h"

#include <array>

/**
 * @brief 原版四人雪橇车队：冻土上共乘滑行，碰撞后按植物响应决定散开落点。
 */
class BobsledTeamZombie final : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	enum class Role {
		LEADER,
		FOLLOWER,
	};

	enum class Phase {
		RIDING,
		LANDING,
		WALKING,
	};

	using Zombie::Zombie;

	void ZombieUpdate(float scaledTime) override;
	void Draw(Graphics* g) override;
	void StartEat(ColliderComponent* other) override;
	void SetCooldown(float timer, bool bypassShield = false) override;
	void TakeBodyDamage(int damage) override;
	void HelmDrop() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void Die() override;
	void Charred() override;

	bool CanBeCharmed() const override;
	bool CanBeChilled() const override;
	bool CanBeFrozen() const override;
	bool CanBeParalyzed() const override;
	bool CanBeGrabbedByTangleKelp() const override;
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanBeAffectedByGroundHazards() const override;
	float GetCurrentHorizontalMoveSpeed() const override;
	Vector GetVisualPosition() const override;
	bool ShouldPlayDeathAnimation() const override;

	Role GetBobsledRole() const { return mRole; }
	Phase GetBobsledPhase() const { return mPhase; }
	int GetBobsledSlot() const { return mSlot; }
	int GetBobsledLeaderID() const { return mLeaderID; }
	const std::array<int, 4>& GetBobsledMemberIDs() const { return mMemberIDs; }
	int GetLiveTeamMemberCount() const;
	bool WasScatterContained() const { return mScatterContained; }
	float GetLandingTimeRemaining() const;
	/** 将选卡展示实体配置为指定骑乘槽位；不注册正式队伍 ID 或生成玩法实体。 */
	void ConfigurePreviewTeamMember(int slot);
	/** 返回选卡展示中指定骑乘槽位相对队长的水平偏移，单位 px。 */
	static float GetPreviewMemberOffsetX(int slot);

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime = 0.0f) override;
	void OnStartEating() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool CanUseGroundPoolState() const override { return mPhase == Phase::WALKING; }
	bool CanBeMovedByTyphoonGust() const override { return mPhase == Phase::WALKING; }
	bool IsOutsideWorldCleanupBounds(const Vector& position) const override;

private:
	/** 队长首更新创建三名跟随者，并把稳定 ID 数组复制给整队。 */
	void SpawnFollowers();
	/** 将一个刚完成构造的同类型实体改为跟随者，阻止其下一帧递归生队。 */
	void ConfigureFollower(int leaderID, int slot);
	/** 取得当前仍活动的队长；损坏交叉引用返回 nullptr。 */
	BobsledTeamZombie* ResolveLeader() const;
	/** 取得指定槽位成员；槽位或类型不匹配时返回 nullptr。 */
	BobsledTeamZombie* ResolveMember(int slot) const;
	/** 按雪锚约束语义或普通跨行语义，为整队原子提交下车落点。 */
	void BeginTeamLanding(bool contained, float baseX);
	/** 为单名成员保存确定落点并进入计时落地阶段。 */
	void BeginMemberLanding(bool contained, int targetRow, const Vector& targetPosition);
	/** 队伍引用损坏时让当前成员原地安全落地，禁止补生新成员。 */
	void BeginOrphanLanding();
	/** 冻土左边界或冻土消失时触发普通散开。 */
	void CheckFrozenFrontier();
	/** 车上任一成员死亡时，由队长一次性回收自身和所有已登记的存活成员。 */
	void KillAttachedTeam();
	/** 恢复雪橇阶段的大碰撞框或普通地面碰撞框。 */
	void ConfigureColliderForPhase();
	/** 隐藏走路与啃食两套断落手臂轨道，并应用上臂残端材质。 */
	void ApplyDetachedArmVisuals() const;
	/** 绘制原版雪橇内层或当前耐久/坠毁前层。 */
	void DrawSledLayer(Graphics* g, bool drawInside, bool drawFront) const;
	/** 返回当前雪橇前层贴图。 */
	const Texture* GetSledFrontTexture() const;

	Role mRole = Role::LEADER;
	Phase mPhase = Phase::RIDING;
	int mSlot = 0;
	int mLeaderID = NULL_ZOMBIE_ID;
	std::array<int, 4> mMemberIDs{
		NULL_ZOMBIE_ID, NULL_ZOMBIE_ID, NULL_ZOMBIE_ID, NULL_ZOMBIE_ID };
	bool mTeamSpawned = false;
	bool mTeamTeardown = false;
	bool mScatterContained = false;
	float mLandingElapsed = 0.0f;
	Vector mLandingStart;
	Vector mLandingTarget;
};
