#include "BoundaryFlowerRules.h"
#include "BoundaryFlower.h"

#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kHealth = 450; // 界碑花本体生命
constexpr const char* kFollowerTrack = "stalk_top"; // 金盏花稳定上茎轨道
constexpr const char* kMonumentSlot = "boundary_monument"; // 界碑身份件命名槽
constexpr float kFollowerOffsetX = 0.0f; // 界碑身份件局部水平偏移，动画 px
constexpr float kFollowerOffsetY = -9.0f; // 界碑身份件局部垂直偏移，动画 px
}

void BoundaryFlower::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = kHealth;
	ConfigureRig();
	PlayTrack("anim_idle", 1.0f);
	RefreshPresentation();
}

void BoundaryFlower::PlantUpdate()
{
	if (mIsPreview || IsShutdown() || mShardCount >= BoundaryFlowerRules::MaxShards) return;
	mShardCharge += DeltaTime::GetDeltaTime();
	while (mShardCharge >= BoundaryFlowerRules::ShardSeconds && mShardCount < BoundaryFlowerRules::MaxShards) {
		mShardCharge -= BoundaryFlowerRules::ShardSeconds;
		++mShardCount;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("BoundaryShardReady", GetVisualPosition());
		}
	}
	if (mShardCount >= BoundaryFlowerRules::MaxShards) mShardCharge = 0.0f;
	RefreshPresentation();
}

bool BoundaryFlower::CoversBoundaryEntryCell(int row, int column) const
{
	return IsActive() && !IsShutdown() && mShardCount > 0
		&& std::abs(row - mRow) <= 1 && std::abs(column - mColumn) <= 1;
}

bool BoundaryFlower::TryConsumeBoundaryShard()
{
	if (!IsActive() || IsShutdown() || mShardCount <= 0) return false;
	--mShardCount;
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("BoundaryEntryReject", GetVisualPosition());
	}
	RefreshPresentation();
	return true;
}

void BoundaryFlower::SaveExtraData(nlohmann::json& j) const
{
	j["shardCount"] = mShardCount;
	j["shardCharge"] = mShardCharge;
}

void BoundaryFlower::LoadExtraData(const nlohmann::json& j)
{
	mShardCount = std::clamp(j.value("shardCount", 0), 0, BoundaryFlowerRules::MaxShards);
	mShardCharge = mShardCount >= BoundaryFlowerRules::MaxShards ? 0.0f
		: std::clamp(j.value("shardCharge", 0.0f), 0.0f, BoundaryFlowerRules::ShardSeconds);
	ConfigureRig();
	RefreshPresentation();
}

void BoundaryFlower::ConfigureRig()
{
	if (mRigConfigured || !mAnimator || !mAnimator->HasTrack(kFollowerTrack)) return;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_REANIM_BOUNDARYFLOWER_MONUMENT, false);
	if (!texture) return;
	mAnimator->SetTrackFollowerImage(kFollowerTrack, kMonumentSlot, texture,
		kFollowerOffsetX, kFollowerOffsetY, 0.82f, 0.82f, false, true, true);
	mRigConfigured = true;
}

void BoundaryFlower::RefreshPresentation() const
{
	if (!mRigConfigured || !mAnimator) return;
	mAnimator->SetTrackFollowerVisible(kFollowerTrack, kMonumentSlot, IsActive());
}
