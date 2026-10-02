#pragma once
#ifndef _FASTPAPER_ZOMBIE_H
#define _FASTPAPER_ZOMBIE_H

#include "PaperZombie.h"

// 加强版读报僵尸：完整复用 PaperZombie 的狂暴/破碎/啃食逻辑与 PaperZombie.reanim，
// 仅把报纸三段贴图换成 FastZombie_paper_paper1/2/3，并上调数值（更厚报纸 + 更肉本体 + 更快 + 概率免伤）。
class FastPaperZombie : public PaperZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using PaperZombie::PaperZombie;

	// 读档贴图恢复：先让基类处理断手/掉头/报纸可见性，再把报纸贴图覆盖成 FastZombie 版本。
	// 仅读档时调用一次（GameInfoSaver），重复 SetTrackImage 无性能顾虑。
	void ZombieItemUpdate() const override {
		PaperZombie::ZombieItemUpdate();

		if (mShieldStage == ArmorBrokenState::A_LITTLE_BROKEN) {
			mAnimator->SetTrackImage("Zombie_paper_paper", ResourceManager::GetInstance().
				GetTexture("IMAGE_FASTZOMBIE_PAPER_PAPER2"));
		}
		else if (mShieldStage == ArmorBrokenState::REALLY_BROKEN) {
			mAnimator->SetTrackImage("Zombie_paper_paper", ResourceManager::GetInstance().
				GetTexture("IMAGE_FASTZOMBIE_PAPER_PAPER3"));
		}
	}

	// 加强：被攻击有概率完全免伤（更耐打），其余伤害分配（护盾/本体）沿用基类。
	// penetrateShield 透传：大喷菇穿透命中同样先过这层免伤判定。
	void TakeDamage(int damage, DamageSource source, bool penetrateShield,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {}) override {
		if (GameRandom::Range(1, 10) <= 1) return;   // 10% 免伤
		PaperZombie::TakeDamage(damage, source, penetrateShield,
			discardShieldOverflow, bypassShield, plantOrigin);
	}

protected:
	void SetupZombie() override;
	void CheckShieldImage() override;
	float GetAbilityAnimSpeedMultiplier() const override;
};

#endif
