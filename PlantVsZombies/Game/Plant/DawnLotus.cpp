#include "DawnLotus.h"

#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "Game/Board/Board.h"

#include <algorithm>

namespace {
constexpr int kHealth = 500; // 曙光莲本体生命
constexpr float kMaxEnergy = 60.0f; // 一次组合黎明所需能量
constexpr float kBaseEnergyPerSecond = 2.5f; // 每游戏秒固定恢复能量，平稳天气也能预先蓄能
constexpr float kDangerEnergyPerSecond = 1.0f; // 每项红色危险仪表每游戏秒额外提供的能量
constexpr const char* kFollowerTrack = "anim_idle"; // 睡莲单轨稳定浮动时间轴
constexpr const char* kCrownSlot = "dawn_lotus_crown"; // 黎明花冠命名槽
constexpr float kFollowerOffsetX = 2.0f; // 花冠相对睡莲局部水平偏移，动画 px
constexpr float kFollowerOffsetY = -22.0f; // 花冠相对睡莲局部垂直偏移，动画 px
}

void DawnLotus::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = kHealth;
	ConfigureRig();
	PlayTrack("anim_idle", 1.0f);
	RefreshPresentation();
}

void DawnLotus::PlantUpdate()
{
	if (mIsPreview || !mBoard || IsShutdown() || mEnergy >= kMaxEnergy) return;
	int dangerousGauges = 0;
	if (mBoard->IsPolarTemperatureDangerous()) ++dangerousGauges;
	if (mBoard->IsPolarHumidityDangerous()) ++dangerousGauges;
	if (mBoard->IsPolarWindDangerous()) ++dangerousGauges;
	mEnergy = std::min(kMaxEnergy,
		mEnergy + DeltaTime::GetDeltaTime()
			* (kBaseEnergyPerSecond + kDangerEnergyPerSecond * dangerousGauges));
	if (mEnergy >= kMaxEnergy && g_particleSystem) {
		g_particleSystem->EmitEffect("DawnLotusReady", GetVisualPosition());
	}
	RefreshPresentation();
}

bool DawnLotus::TryActivate()
{
	if (!mBoard || IsShutdown() || mEnergy < kMaxEnergy) return false;
	const int dangerMask = (mBoard->IsPolarTemperatureDangerous() ? 1 : 0)
		| (mBoard->IsPolarHumidityDangerous() ? 2 : 0)
		| (mBoard->IsPolarWindDangerous() ? 4 : 0);
	if (dangerMask == 0 || !mBoard->ActivateDawnLotus(mPlantID, dangerMask)) return false;
	mEnergy = 0.0f;
	RefreshPresentation();
	return true;
}

void DawnLotus::SaveExtraData(nlohmann::json& j) const
{
	j["energy"] = mEnergy;
}

void DawnLotus::LoadExtraData(const nlohmann::json& j)
{
	mEnergy = std::clamp(j.value("energy", 0.0f), 0.0f, kMaxEnergy);
	ConfigureRig();
	RefreshPresentation();
}

void DawnLotus::ConfigureRig()
{
	if (mRigConfigured || !mAnimator || !mAnimator->HasTrack(kFollowerTrack)) return;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_REANIM_DAWNLOTUS_CROWN, false);
	if (!texture) return;
	mAnimator->SetTrackFollowerImage(kFollowerTrack, kCrownSlot, texture,
		kFollowerOffsetX, kFollowerOffsetY, 0.86f, 0.86f, false, true, true);
	mRigConfigured = true;
}

void DawnLotus::RefreshPresentation() const
{
	if (!mRigConfigured || !mAnimator) return;
	mAnimator->SetTrackFollowerVisible(kFollowerTrack, kCrownSlot, IsActive());
}
