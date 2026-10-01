#include "DoomShroom.h"
#include "../Crater.h"
#include "../AudioSystem.h"
#include "../ShadowComponent.h"
#include "CoffeeBean.h"
#include "../../Reanimation/Animator.h"
#include <vector>

namespace {
	constexpr int kChargeFirstFrame = 19; // 已有 anim_explode 充能轨起始全局帧
	constexpr int kExplosionFrame = 51; // 主人已确认的正式引爆全局帧，不新增动画事件
	constexpr float kChargeFps = 23.0f; // 充能轨实际播放帧率，帧/游戏秒
	constexpr float kReanimationFps = 12.0f; // reanim 基础播放帧率，帧/游戏秒
}

void DoomShroom::SetupPlant()
{
	auto shadow = GetShadow();
	shadow->SetScale(Vector(1.0f, 1.0f));   // 宽扁大伞盖，比小蘑菇系(0.6)大一档（主人校对）
	shadow->SetOffset(Vector(2, 30));       // 较其他蘑菇右移 6px（主人校对：本体略偏右）

	Shroom::SetupPlant();

	// 预览/图鉴绝不结算（图鉴也会走到这里）
	if (mIsPreview) return;

	// 全局第 51 帧 = anim_explode(19..51) 末帧引爆（主人指定，帧号已按代码口径-1）。
	// 读档时 SetupPlant 重新注册：RestoreAnimState 恢复的帧在 51 之前，穿过时照常触发。
	mAnimator->AddFrameEvent(kExplosionFrame, [this]() {
		Explode();
		});

	// 夜晚种下立即充能：镜像原版 UpdateDoomShroom（23fps 播 anim_explode + 吸气充能声）；
	// 白天睡觉由 Shroom 基类切 anim_sleep，咖啡豆倒计时归零后从 OnWakeUp 进入同一路径。
	if (!mIsSleeping) {
		StartCharging();
	}
}

void DoomShroom::OnWakeUp()
{
	StartCharging();
}

void DoomShroom::StartCharging()
{
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_REVERSE_EXPLOSION, 0.5f);
	PlayTrack("anim_explode", kChargeFps / kReanimationFps, 0.0f);
}

float DoomShroom::GetChargeDuration()
{
	return (kExplosionFrame - kChargeFirstFrame) / kChargeFps;
}

float DoomShroom::GetExplosionTimeRemaining() const
{
	if (GetSleepState()) {
		if (IsWakingUp()) return GetWakeUpTimeRemaining() + GetChargeDuration();
		const auto* coffee = mBoard ? dynamic_cast<const CoffeeBean*>(mBoard->GetOverlayPlantAt(mRow,mColumn)) : nullptr;
		const float pendingWake = coffee && coffee->IsActive() ? coffee->GetPendingWakeDelay() : 0.0f;
		return pendingWake > 0 ? pendingWake + GetChargeDuration() : -1.0f;
	}
	if (!mAnimator || GetCurrentTrackName() != "anim_explode") return -1.0f;
	return std::max(0.0f,kExplosionFrame-mAnimator->GetCurrentFrame())
		/ std::max(0.001f,kReanimationFps*mAnimator->EffectiveSpeed());
}

void DoomShroom::TakeDamage(int damage, DamageSource source)
{
	// 充能期间无敌（参考樱桃炸弹：只闪光不掉血）；白天睡觉=普通蘑菇，照常被啃
	if (!mIsSleeping) {
		this->SetGlowingTimer(0.1f);
		return;
	}
	Plant::TakeDamage(damage, source);
}

void DoomShroom::ResolveGargantuarSmash()
{
	if (!mIsSleeping && GetCurrentTrackName() == "anim_explode") {
		Explode();
		return;
	}
	Plant::ResolveGargantuarSmash();
}

void DoomShroom::Explode()
{
	if (!mBoard) return;
	// 音效、Doom 粒子、半径 250 圆形僵尸结算和 7x7 扶梯清除都在 CreateDoomBoom 内统一处理。
	mBoard->CreateDoomBoom(GetPosition(), mRow);
	KillOtherPlantsInCell();
	mBoard->AddCrater(mRow, mColumn, Crater::CRATER_DURATION);
	Die();
}

void DoomShroom::KillOtherPlantsInCell()
{
	if (!mBoard) return;
	// 原版按全部植物的逻辑 row/column 过滤；保留这一口径可覆盖以后新增的南瓜等额外层。
	// 先复制 ID 列表，避免 Plant::Die 释放格位及延迟销毁时改变遍历来源。
	const std::vector<int> plantIDs = mBoard->mEntityRegistry.GetAllPlantIDs();
	for (const int plantID : plantIDs) {
		if (plantID == mPlantID) continue;
		Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
		if (plant && plant->IsActive()
			&& plant->mRow == mRow && plant->mColumn == mColumn) {
			plant->Die();
		}
	}
}
