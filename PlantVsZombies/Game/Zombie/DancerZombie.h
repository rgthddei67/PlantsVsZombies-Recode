#pragma once
#ifndef _DANCERZOMBIE_H_
#define _DANCERZOMBIE_H_

#include "Zombie.h"
#include "DancerRules.h"

// 舞王僵尸(MJ版)：月球漫步入场 → 打响指(anim_point)召唤十字 4 伴舞 → 定身跳舞 2s →
// 随全局节拍齐舞前进；伴舞阵亡后在节拍==12 时重新打响指补位（有头且未越过 kDanceLimitX 才补）。
class DancerZombie : public Zombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	/** 读取已注册响指轨道时长；主线程调用，不新增动画事件。 */
	static float GetForecastSnapSeconds();
	/** 采样真实阶段、余时及伴舞身份；返回值不借用实体或动画器。 */
	DancerRules::Forecast GetDanceForecast() const;
	using Zombie::Zombie;

	enum class DancerPhase {
		DANCING_IN,	// 入场月球漫步（计时；碰到植物啃一口后转 SNAPPING 召唤）
		SNAPPING,	// 播 anim_point，第 36 帧事件触发召唤
		HOLD,		// 召唤后定身跳舞 2s（不移动）
		DANCING		// 节拍齐舞 + 前进 + 缺位补召
	};

	void ZombieUpdate(float scaledTime) override;
	void StartEat(ColliderComponent* other) override;
	void EatTarget() override;	// 月球漫步首口后中断→召唤
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	// Zombie_Jackson.reanim 无 anim_walk2：稳态“走路”按阶段选 moonwalk/point/节拍舞
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void OnStopEating() override;
	// 按舞步阶段与魅惑阵营刷新镜像；精英舞王复用月球漫步朝向。
	void UpdateDanceFacing();

private:
	void SummonBackupDancers();
	bool NeedsMoreBackupDancers() const;
	void UpdateDanceTrack(float blendTime);
	DancerPhase mPhase = DancerPhase::DANCING_IN;
	float mPhaseTimer = 0.0f;
	int mFollowerID[4] = { NULL_ZOMBIE_ID, NULL_ZOMBIE_ID, NULL_ZOMBIE_ID, NULL_ZOMBIE_ID };
	int mLastBeatBucket = -1;	// 0=walk 1=armraise，-1=强制刷新
	bool mCharmHandled = false;	// 魅惑边沿标志：置位时清空 mFollowerID（放弃旧伴舞，原版行为）
};

#endif
