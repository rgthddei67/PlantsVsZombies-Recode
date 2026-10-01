#pragma once
#ifndef _H_DOOMSHROOM_H
#define _H_DOOMSHROOM_H

#include "Shroom.h"
#include "Game/Board/Board.h"

// 毁灭菇：夜晚种下立即充能（anim_explode 全局 19..51 帧、按原版 23fps 播放），
// 第 51 帧（主人指定）引爆——半径 250 圆形全场结算、清除同格其他植物并原地留弹坑 180s，
// 随即本体消失。
// 白天种下睡觉（Shroom 基类处理，anim_sleep 活跃区间 52..76，扫不到第 51 帧不会误爆）。
// 充能期间无敌；睡眠时仍按普通蘑菇承伤。
class DoomShroom : public Shroom
{
public:
	using Shroom::Shroom;
	/** 清醒后到正式爆炸帧的完整时长，游戏秒；预测与现有动画事件共用参数。 */
	static float GetChargeDuration();
	/** 已放下咖啡/正在唤醒/已经充能时返回剩余爆炸秒数，未提交的睡眠态返回 -1。 */
	float GetExplosionTimeRemaining() const;
	/** 动作提交期间不搬运，防止原目标和结算位置失配。 */
	bool CanBeRelocated() const override { return Plant::CanBeRelocated() && GetSleepState() && !IsWakingUp(); }

protected:
	void SetupPlant() override;
	void OnWakeUp() override;

public:
	// 充能（引爆倒计时）期间无敌，参考樱桃炸弹；白天睡觉时仍正常掉血
	void TakeDamage(int damage, DamageSource source) override;
	void TakeDeploymentInterceptionDamage(int damage, DamageSource source) override {
		Plant::TakeDamage(damage, source);
	}
	/** 清醒充能中的毁灭菇被巨人锤击时立即引爆；睡眠态仍走普通压扁。 */
	void ResolveGargantuarSmash() override;

private:
	/** 进入毁灭菇原版充能轨并播放吸气声；夜种与咖啡豆唤醒共用。 */
	void StartCharging();
	/** 引爆前清除当前格除自身外的全部植物，避免任意承载/保护层留在弹坑里。 */
	void KillOtherPlantsInCell();
	void Explode();
};

#endif
