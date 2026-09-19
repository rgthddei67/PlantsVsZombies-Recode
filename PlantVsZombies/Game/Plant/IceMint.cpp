#include "IceMint.h"
#include "Game/Board/Board.h"
#include "DeltaTime.h"
#include "ResourceManager.h"

void IceMint::SetupPlant()
{
	mProductionRemaining = mBoard && mBoard->IsColdStorage()
		? IceProduction::MintInterval : IceProduction::MintSunInterval;
	// 小型冰晶挂在花心之上，叶片继续独立运动，不覆盖整株骨架。
	mAnimator->SetTrackFollowerImage("anim_face", "ice_mint_crystal",
		ResourceManager::GetInstance().GetTexture("IMAGE_ICE_MINT_CRYSTAL", false),
		-8.0f, -36.0f, 0.85f, 0.85f, true);
	mAnimator->SetTrackFollowerVisible("anim_face", "ice_mint_crystal", true);
}

void IceMint::PlantUpdate()
{
	if (mIsPreview || !mBoard || mBoard->mBoardState != BoardState::GAME || mBoard->mTrophySpawned) return;
	mProductionRemaining -= DeltaTime::GetDeltaTime();
	const bool ice = mBoard->IsColdStorage();
	while (mProductionRemaining <= 0.0f) {
		mProductionRemaining += ice ? IceProduction::MintInterval : IceProduction::MintSunInterval;
		if (ice) mBoard->CreditProducedIce(true, IceProduction::MintYield);
		else mBoard->CreateSun(GetVisualAnchorPosition(), true);
		mProductionBatches = std::min(1000000, mProductionBatches + 1);
		SetGlowingTimer(0.5f);
		PlayTrackOnce("anim_block", "anim_idle");
	}
}

void IceMint::SaveExtraData(nlohmann::json& j) const
{
	j["productionRemaining"] = mProductionRemaining;
	j["productionBatches"] = mProductionBatches;
}

void IceMint::LoadExtraData(const nlohmann::json& j)
{
	const float interval = mBoard && mBoard->IsColdStorage() ? IceProduction::MintInterval : IceProduction::MintSunInterval;
	const float remaining = j.value("productionRemaining", interval);
	mProductionRemaining = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, interval) : interval;
	mProductionBatches = std::clamp(j.value("productionBatches", 0), 0, 1000000);
}
