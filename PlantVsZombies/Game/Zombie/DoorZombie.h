#pragma once
#ifndef _DOOR_ZOMBIE_H
#define _DOOR_ZOMBIE_H

#include "Zombie.h"

class DoorZombie : public Zombie {
protected:
	ArmorBrokenState mShieldStage = ArmorBrokenState::NO_BROKEN;

	void SetupZombie() override;
	/** 返回当前破损阶段使用的门贴图键；换色派生类只需覆写此映射。 */
	virtual const char* GetDoorImageKey(ArmorBrokenState stage) const;
	/** 返回掉门粒子名；保证换色门掉落后仍使用同色贴图。 */
	virtual const char* GetDoorDropParticleName() const { return "ZombieDoorOff"; }
	/** 把当前破损阶段贴图应用到门轨道；无门/无动画时安全跳过。 */
	void ApplyDoorImage() const;

	void ShieldDrop() override;

	void CheckShieldImage() override;

	// 啃食视觉：开吃露常规手臂、停吃藏回门后（门还在才动）。一对对称钩子由基类模板方法在
	// 所有开吃/停吃路径统一触发，修复历史门臂 bug（c0cb799 多臂 / 52bd86d 啃僵尸无臂）。
	void OnStartEating() override;
	void OnStopEating()  override;

	// 显示/隐藏全部手臂相关轨道（无 animator 时安全跳过）
	void ShowArm(bool show) const;

	// 断臂造型：内臂 + 外上臂(断裂贴图)可见，外手掌/外下臂隐藏。
	// 掉头/死亡姿势复用——避免 ShowArm(true) 把 hand/lower 设可见后又被立刻覆盖隐藏。
	void ShowBrokenArm() const;

public:
	static constexpr int InitialBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_DOOR).body; // 普通门本体生命，供出生和候选投影共用
	static constexpr int InitialShieldHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_DOOR).shield; // 普通铁门生命，不能替本体抵挡穿透伤害
	using Zombie::Zombie;
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;

	void HeadDrop() override;
	void OnTemporalCoreStateRestored() override;

	void ZombieItemUpdate() const override {
		const bool shieldGone = (mShieldStage == ArmorBrokenState::NONE
			|| mShieldType == ShieldType::SHIELDTYPE_NONE);

		// —— 二类护盾（铁门）贴图/可见性 ——
		if (shieldGone) {
			mAnimator->SetTrackVisible("anim_screendoor", false);
			mAnimator->SetTrackVisible("Zombie_innerarm_screendoor", false);
			mAnimator->SetTrackVisible("Zombie_outerarm_screendoor", false);
			mAnimator->SetTrackVisible("Zombie_innerarm_screendoor_hand", false);
		}
		else {
			ApplyDoorImage();
		}

		// —— 手臂造型一次定状态（避免先 ShowArm 全开、再被逐条覆盖）——
		// !mHasHead：穿透可在门未掉时掉头（HeadDrop→ShowBrokenArm，mHasArm 仍 true），
		// 判据须与 OnStart/OnStopEating 的残端优先守卫一致，否则读档后残端被藏回门后。
		if (!mHasArm || !mHasHead || mIsDying) {
			this->ShowBrokenArm();           // 断臂残端（掉手 / 掉头 / 死亡姿势）
		}
		else if (shieldGone || mIsEating) {
			this->ShowArm(true);             // 门没了 / 啃食时露出整条手臂
		}
		// 否则：门还在、手还在、未啃食/死亡 → 手臂藏在门后（SetupZombie 已 ShowArm(false)）

		if (!mHasHead) {
			mAnimator->SetTrackVisible("anim_head1", false);
			mAnimator->SetTrackVisible("anim_head2", false);
			mAnimator->SetTrackVisible("anim_hair", false);
		}
	}

	void SaveExtraData(nlohmann::json& j) const override {
		j["shieldStage"] = static_cast<int>(mShieldStage);
	}

	void LoadExtraData(const nlohmann::json& j) override {
		mShieldStage = static_cast<ArmorBrokenState>(
			j.value("shieldStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)));
	}

	void TakeBodyDamage(int damage) override;
};

#endif
