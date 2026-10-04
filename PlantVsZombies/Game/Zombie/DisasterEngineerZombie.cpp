#include "DisasterEngineerZombie.h"
#include "DisasterEngineerRules.h"
#include "Game/Board/Board.h"
#include "ResourceManager.h"
#include "DeltaTime.h"
#include "Graphics.h"
#include <cmath>

void DisasterEngineerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = DisasterEngineerRules::Health;
	SyncCanister();
}

void DisasterEngineerZombie::SyncCanister()
{
	const int stage = mFull ? 4 : mReloadPaid
		? std::clamp(static_cast<int>(4 * (1 - mReloadRemaining / DisasterEngineerRules::ReloadSeconds)), 0, 3) : 0;
	if (mVisualStage != stage) {
		mVisualStage = stage;
		mAnimator->SetTrackFollowerImage("Zombie_body", "disaster_canister",
			ResourceManager::GetInstance().GetTexture("IMAGE_DISASTER_CANISTER_" + std::to_string(stage), false),
			-25, 9, .5f, .5f, true);
	}
	mAnimator->SetTrackFollowerVisible("Zombie_body", "disaster_canister", !IsDying() && HasHead());
}

std::vector<int> DisasterEngineerZombie::GetProtectedWorkerIDs() const
{
	std::vector<std::pair<float,int>> nearby;
	if (!mBoard || !IsActive() || IsDying() || !HasHead() || !mFull) return {};
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* z) {
		if (!z || z->mZombieType!=ZombieType::ZOMBIE_ICE_WORKER || !z->IsActive()
			|| z->IsDying() || !z->HasHead() || z->IsMindControlled()!=IsMindControlled()) return;
		const float distance=std::abs(z->GetPosition().x-GetPosition().x);
		if (distance<=DisasterEngineerRules::RadiusCells*CELL_COLLIDER_SIZE_X) nearby.emplace_back(distance,z->mZombieID);
	});
	std::sort(nearby.begin(),nearby.end());
	std::vector<int> ids;
	for (size_t i=0;i<nearby.size() && i<DisasterEngineerRules::Capacity;++i) ids.push_back(nearby[i].second);
	return ids;
}

/** 在实际受保护工人身旁绘制小冷却罐标志，满罐消耗后同帧消失。 */
void DisasterEngineerZombie::Draw(Graphics* g)
{
	Zombie::Draw(g);
	for (const int id:GetProtectedWorkerIDs()) if (const auto* z=mBoard->mEntityRegistry.GetZombie(id)) {
		const auto p=z->GetPosition();
		g->DrawTexture(ResourceManager::GetInstance().GetTexture("IMAGE_DISASTER_WORKER_BADGE",false),p.x+18,p.y-55,13,18);
	}
}

void DisasterEngineerZombie::ConsumeCanister()
{
	if (!mFull) return;
	mFull = false;
	mReloadPaid = false;
	mReloadRemaining = DisasterEngineerRules::ReloadSeconds;
	++mProtectionUses;
	SyncCanister();
}

/** 装填只消耗有效行动时间；麻痹不重置进度，也不关闭已有满罐保护。 */
void DisasterEngineerZombie::Update()
{
	const bool controlled = IsImmobilized();
	Zombie::Update();
	if (IsDying() || !HasHead()) SyncCanister();
	if (mIsPreview || !mBoard || mBoard->mBoardState != BoardState::GAME
		|| mBoard->mTrophySpawned || DeltaTime::IsPaused() || !IsActive() || IsDying() || !HasHead()) return;
	if (!mFull && !controlled && !IsImmobilized() && !IsTangleKelpTarget()) {
		if (!mReloadPaid) mReloadPaid = IsMindControlled()
			? mBoard->TrySpendPlantAbilityResource(DisasterEngineerRules::ReloadCost, 0)
			: mBoard->TrySpendZombieAbilityIce(DisasterEngineerRules::ReloadCost);
		else {
			mReloadRemaining = std::max(0.0f, mReloadRemaining - DeltaTime::GetDeltaTime());
			if (mReloadRemaining <= 0) { mFull = true; mReloadPaid = false; }
		}
	}
	SyncCanister();
}

void DisasterEngineerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["canisterFull"] = mFull; j["reloadPaid"] = mReloadPaid;
	j["reloadRemaining"] = mReloadRemaining; j["protectionUses"] = mProtectionUses;
}

void DisasterEngineerZombie::LoadExtraData(const nlohmann::json& j)
{
	mFull = j.value("canisterFull", true); mReloadPaid = !mFull && j.value("reloadPaid", false);
	const float remaining = j.value("reloadRemaining", DisasterEngineerRules::ReloadSeconds);
	mReloadRemaining = mFull ? 0 : std::isfinite(remaining)
		? std::clamp(remaining, 0.0f, DisasterEngineerRules::ReloadSeconds) : DisasterEngineerRules::ReloadSeconds;
	mProtectionUses = std::clamp(j.value("protectionUses", 0), 0, 1000000);
	mVisualStage = -1;
	SyncCanister();
}
