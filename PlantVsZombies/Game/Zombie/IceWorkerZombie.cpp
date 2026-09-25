#include "IceWorkerZombie.h"
#include "Game/Board/Board.h"
#include "DeltaTime.h"
#include "ResourceManager.h"

namespace {
constexpr const char* kMachineSlot = "ice_worker_machine";
}

void IceWorkerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = IceProduction::WorkerHealth;
	ConfigureMachine();
}

void IceWorkerZombie::ConfigureMachine()
{
	mAnimator->SetTrackFollowerImage("Zombie_body", kMachineSlot,
		ResourceManager::GetInstance().GetTexture("IMAGE_ICE_WORKER_MACHINE", false),
		30.0f, -24.0f, 0.85f, 0.85f, true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kMachineSlot, !IsDying() && IsActive());
}

bool IceWorkerZombie::HasIceMachine() const
{
	return mAnimator && mAnimator->GetTrackFollowerVisible("Zombie_body", kMachineSlot);
}

void IceWorkerZombie::Update()
{
	const bool producing = !mIsPreview && mBoard && mBoard->IsColdStorage()
		&& mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned
		&& IsActive() && !IsDying() && HasHead() && !IsImmobilized()
		&& !IsDraggedUnderByTangleKelp();
	Zombie::Update();
	if (mAnimator && (IsDying() || !HasHead()))
		mAnimator->SetTrackFollowerVisible("Zombie_body", kMachineSlot, false);
	if (!producing || !IsActive() || IsDying() || !HasHead()) return;
	mIceRemaining -= DeltaTime::GetDeltaTime();
	while (mIceRemaining <= 0.0f) {
		mIceRemaining += IceProduction::Interval;
		mBoard->CreditProducedIce(IsMindControlled(), static_cast<int>(mNextIceYield), mSpawnWave);
		mNextIceYield = std::min(IceProduction::MaximumYield,
			mNextIceYield * IceProduction::YieldGrowth);
		mIceBatches = std::min(1000000, mIceBatches + 1);
		SetGlowingTimer(0.3f);
	}
}

void IceWorkerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["iceRemaining"] = mIceRemaining;
	j["nextIceYield"] = mNextIceYield;
	j["iceBatches"] = mIceBatches;
}

void IceWorkerZombie::LoadExtraData(const nlohmann::json& j)
{
	const float remaining = j.value("iceRemaining", IceProduction::Interval);
	const float amount = j.value("nextIceYield", IceProduction::InitialYield);
	mIceRemaining = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, IceProduction::Interval) : IceProduction::Interval;
	mNextIceYield = std::isfinite(amount) ? std::clamp(amount, IceProduction::InitialYield, IceProduction::MaximumYield) : IceProduction::InitialYield;
	mIceBatches = std::clamp(j.value("iceBatches", 0), 0, 1000000);
	ConfigureMachine();
}
