#pragma once
#ifndef SHOOTER_H
#define SHOOTER_H

#include "Plant.h"
#include "ShooterRules.h"
#include "../../DeltaTime.h"

class Shooter : public Plant {
protected:
	std::shared_ptr<Animator> mHeadAnim = nullptr;
	float mCheckZombieTimer = 0.0f;
	float mShootTime = 1.5f;     // 射击间隔时间
	float mShootTimer = 1.0f;    // 射击计时器

	bool HasZombieInRow();		// 检测本行是否有僵尸
	virtual void ShootBullet() = 0;	// 射击子弹 必须写
	void SetupPlant() override;
	/** 保存任意附加头部 Animator 的完整播放状态，prefix 决定 JSON 字段前缀。 */
	static void SaveHeadAnimatorState(
		nlohmann::json& j, const char* prefix, const Animator* animator);
	/**
	 * 恢复任意附加头部 Animator；可选 legacy 轨道用于兼容缺少播放状态的旧 Shooter 存档。
	 */
	static void LoadHeadAnimatorState(
		const nlohmann::json& j, const char* prefix, Animator* animator,
		const char* legacyShootingTrack = nullptr,
		const char* legacyIdleTrack = nullptr);

public:
	using Plant::Plant;
	const Animator* GetHeadAnimator() const { return mHeadAnim.get(); }
	/** 已累计的冷却就绪后索敌游戏秒，供主线程冻结当前攻击阶段。 */
	float GetTargetCheckElapsed() const { return mCheckZombieTimer; }

	/** 保存射击计时器及头部 Animator 的完整播放状态机。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复射击计时器及头部 Animator，并兼容只保存轨道/帧的旧存档。 */
	void LoadExtraData(const nlohmann::json& j) override;

	void PlantUpdate() override;
};

#endif
