#include "CherryBomb.h"
#include "Game/Board/Board.h"
#include "Reanimation/Animator.h"
#include <algorithm>

namespace {
constexpr int kExplosionFrame = 13; // 原有引爆事件全局帧，不新增或改变事件
constexpr float kAnimationFps = 30; // CherryBomb.reanim 的基础帧率
constexpr float kMinimumSpeed = .34f; // 原有出生充能速度随机下限
constexpr float kMaximumSpeed = .45f; // 原有出生充能速度随机上限，预测取最快引爆
}

void CherryBomb::SetupPlant()
{
	if (mIsPreview) return;
	this->PlayTrack("anim_explode", GameRandom::Range(kMinimumSpeed, kMaximumSpeed), 0);
	mAnimator->AddFrameEvent(kExplosionFrame, [this]() {
		Explode();
		});
}

float CherryBomb::GetMinimumChargeDuration() { return kExplosionFrame/(kAnimationFps*kMaximumSpeed); }

float CherryBomb::GetExplosionTimeRemaining() const {
	if (!mAnimator || GetCurrentTrackName() != "anim_explode") return -1;
	return std::max(0.0f,kExplosionFrame-mAnimator->GetCurrentFrame())/std::max(.001f,kAnimationFps*mAnimator->EffectiveSpeed());
}

void CherryBomb::TakeDamage(int /*damage*/, DamageSource /*source*/)
{
	this->SetGlowingTimer(0.1f);
	return;
}

void CherryBomb::ResolveGargantuarSmash()
{
	if (GetCurrentTrackName() == "anim_explode") {
		Explode();
		return;
	}
	Plant::ResolveGargantuarSmash();
}

void CherryBomb::Explode()
{
	if (!IsActive()) return;
	if (mBoard) mBoard->CreateBoom(GetPosition(), mRow);
	Die();
}
