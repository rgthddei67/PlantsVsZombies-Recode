#include "LilyPad.h"
#include "../ShadowComponent.h"
#include <algorithm>

void LilyPad::SetupPlant()
{
	// 睡莲贴在水面上，原版 DrawShadow 对该类型直接返回。
	RemoveShadow();
}

void LilyPad::PlantUpdate()
{
	// 保护时间按真实游戏时间递减，不受雨势植物行动倍率影响。
	if (mBiteProtectionTimer > 0.0f) {
		mBiteProtectionTimer = std::max(0.0f,
			mBiteProtectionTimer - DeltaTime::GetDeltaTime());
	}
}

void LilyPad::Draw(Graphics* g)
{
	AnimatedObject::Draw(g);
    DrawSludgeStatus(g);
}

bool LilyPad::CanBeEaten() const
{
	return Plant::CanBeEaten() && mBiteProtectionTimer <= 0.0f;
}

void LilyPad::SaveExtraData(nlohmann::json& j) const
{
	j["biteProtectionTimer"] = mBiteProtectionTimer;
}

void LilyPad::LoadExtraData(const nlohmann::json& j)
{
	// 缺字段视为保护已结束，避免旧档读入时凭空重新获得一秒免咬。
	mBiteProtectionTimer = std::max(0.0f,
		j.value("biteProtectionTimer", 0.0f));
}
