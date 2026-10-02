#pragma once
#ifndef _POLEVAULTER_H
#define _POLEVAULTER_H

#include "Zombie.h"

class Plant;

class Polevaulter : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class VaultState {
		RUNNING,	// 手持撑杆跑步
		JUMPING,	// 跳跃中
		WALKING		// 跳跃后走路
	};

	VaultState mVaultState = VaultState::RUNNING;
	bool mHasVaulted = false;

	/** @brief 仅落地后的 WALKING 状态允许啃食，避免起跳碰撞批次覆盖跳跃动画。 */
	void StartEat(ColliderComponent* other) override;

	/** @brief 锁定当前植物并开始撑杆跳跃，阻拦判定延后到原版动画进度节点。 */
	void StartJump(Plant* target);
	/** @brief 按品种跳距完成落地，恢复走路状态并调用落地扩展钩子。 */
	void EndJump();
	void JumpMove(float distance);  // 跳跃期间手动移动，传入本次移动距离
	float GetLastVaultDistance() const { return mLastVaultDistance; }
	float GetVaultExtraDistanceApplied() const { return mVaultExtraDistanceApplied; }
	float GetVaultProgress() const;
	bool HasCheckedVaultBlock() const { return mVaultBlockChecked; }
	bool HasVaultTarget() const { return mVaultTargetPlantID != NULL_PLANT_ID; }

	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;

	// 撑杆跑/跳阶段不可魅惑（原版：跳过魅惑菇根本不吃它）；落地 WALKING 后可
	bool CanBeCharmed() const override { return mVaultState == VaultState::WALKING; }

	// 跳跃中不可定身（原版 CanBeFrozen 排除 PolevaulterInVault）；减速尾巴照吃（CanBeChilled 不拦）
	bool CanBeFrozen() const override { return mVaultState != VaultState::JUMPING; }
	// 水草沿用原版 DamageRangeFlags：仅真正腾空的 JUMPING 阶段不可抓，起跑和落地后都可抓。
	bool CanBeGrabbedByTangleKelp() const override {
		return mVaultState != VaultState::JUMPING;
	}

protected:
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void SetupZombie() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;

	/** @brief 恢复稳态 anim_walk；跳跃中拒绝介质切换抢占 anim_jump。 */
	void PlayWalkAnimation(float blendTime) override;

	/** @brief 返回本品种一次落地应推进的逻辑距离，单位为像素。 */
	virtual float GetVaultDistance() const;
	/** @brief 落地位移与基础状态恢复完成后的派生行为入口。 */
	virtual void OnVaultLanded() {}
	/** @brief 撑杆在动画阻拦节点被挡、弃杆状态恢复后的派生行为入口。 */
	virtual void OnVaultBlocked(Plant&) {}

private:
	/** @brief 在原版 60% 跳跃节点撤回额外位移、恢复碰撞并开始啃食阻拦植物。 */
	void FinishBlockedVault(Plant& blockingPlant);
	/** @brief 在本次完整跳跃扫过的路径中选择最先遇到的阻拦植物。 */
	Plant* FindVaultBlockingPlant() const;

	float mLastVaultDistance = 0.0f;  // 最近一次实际落地位移；仅供稳定测试取证
	float mVaultExtraDistanceApplied = 0.0f;  // 本次跳跃已随动画进度补偿的额外逻辑位移，单位 px
	float mVaultStartX = 0.0f;  // 本次起跳逻辑原点 X；用于长跳路径阻拦与快照恢复
	int mVaultTargetPlantID = NULL_PLANT_ID;  // 起跳时锁定的顶层植物 ID，供状态取证与快照恢复
	bool mVaultBlockChecked = false;  // 本次跳跃是否已越过唯一阻拦检查节点
};

#endif
