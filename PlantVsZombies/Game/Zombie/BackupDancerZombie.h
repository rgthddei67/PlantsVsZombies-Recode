#pragma once
#ifndef _BACKUPDANCERZOMBIE_H_
#define _BACKUPDANCERZOMBIE_H_

#include "Zombie.h"
#include "DancerRules.h"

// 伴舞僵尸：只能被舞王召唤（gamedata weight=0）。出土升起 → 随 Board 全局节拍齐舞。
// 领队关系：mLeaderID 指向舞王；领队死亡/自己被魅惑后即为无主（照常跳舞前进，原版行为）。
class BackupDancerZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	/** 升起后恢复的稳态步态，与出生阶段的零速度分别投影。 */
	static ZombieMovementRules::BirthProfile GetDancingMovementProfile();
	/** 采样升起余时和独立行走速度，不改变正式阶段。 */
	DancerRules::Forecast GetDanceForecast() const;
	using Zombie::Zombie;

	enum class BackupPhase {
		RISING,		// 出土升起中（无粒子无音效——主人指定；不移动不啃食）
		DANCING		// 随全局节拍跳舞前进
	};

	int mLeaderID = NULL_ZOMBIE_ID;	// 舞王 ID（舞王召唤后写入）

	void ZombieUpdate(float scaledTime) override;
	void StartEat(ColliderComponent* other) override;

	// 出土升起中免疫寒冰（原版 CanBeChilled 排除 DancerRising）：冻结会阻断升起计时，令伴舞卡在土里
	bool CanBeChilled() const override {
		return mPhase != BackupPhase::RISING && Zombie::CanBeChilled();
	}
	// 出土中的伴舞尚不在地表命中层；升起后可按普通泳池僵尸被水草拖走。
	bool CanBeGrabbedByTangleKelp() const override {
		return mPhase != BackupPhase::RISING;
	}
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	// Zombie_dancer.reanim 无 anim_walk2：稳态“走路”=按全局节拍选 anim_walk/anim_armraise
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void OnStopEating() override;

	void UpdateDanceTrack(float blendTime);
	// 按 C# 的舞步阶段与阵营重算模型朝向；升起结束后才随全局节拍翻面。
	void UpdateDanceFacing();

	BackupPhase mPhase = BackupPhase::RISING;
	float mRiseTimer = 0.0f;		// 已升起时长，达 kRiseDuration 落地
	int mLastBeatBucket = -1;		// 上次应用的节拍段（0=walk 1=armraise），-1=强制刷新
	bool mCharmHandled = false;		// 魅惑边沿标志：置位时清 mLeaderID 脱队
	float mBaseOffsetY = 0.0f;		// gamedata offset 原始 y（升起位移叠加其上）
};

#endif
