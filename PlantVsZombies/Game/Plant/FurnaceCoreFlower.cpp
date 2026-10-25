#include "FurnaceCoreFlower.h"

#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaximumStoredCores = 2; // 单株炉芯花同时保存的炉芯上限
constexpr float kCoreChargeDuration = 10.0f; // 温度高于 0°C 时每枚炉芯所需游戏秒数
constexpr float kCoreReadyGlowDuration = 0.32f; // 完成一枚炉芯时的本体发光游戏秒数
constexpr float kCoreConsumeGlowDuration = 0.35f; // 拒绝冰像封存时的本体发光游戏秒数
constexpr float kCoreConsumeSoundVolume = 0.32f; // 消耗炉芯时已有燃烧音效的音量
constexpr const char* kHeadTrack = "anim_idle"; // 向日葵面部轨承载木质炉芯及状态火焰
constexpr const char* kLeftCoreFollower = "furnace_core_left"; // 第一枚炉芯的稳定 follower 槽
constexpr const char* kRightCoreFollower = "furnace_core_right"; // 第二枚炉芯的稳定 follower 槽
constexpr float kCoreFlameScale = 0.22f; // 原版火炬火焰缩到炉芯指示灯的倍率
constexpr float kLeftCoreOffsetX = 11.0f; // 左侧炉芯相对面部轨的局部 X 偏移
constexpr float kRightCoreOffsetX = 31.0f; // 右侧炉芯相对面部轨的局部 X 偏移
constexpr float kCoreOffsetY = 4.0f; // 两枚炉芯相对面部轨的局部 Y 偏移

constexpr const char* kPetalTracks[] = {
	"SunFlower_leftpetal8", "SunFlower_leftpetal7",
	"SunFlower_leftpetal6", "SunFlower_leftpetal5",
	"SunFlower_leftpetal4", "SunFlower_leftpetal3",
	"SunFlower_leftpetal2", "SunFlower_leftpetal1",
	"SunFlower_bottompetals", "SunFlower_rightpetal9",
	"SunFlower_rightpetal8", "SunFlower_rightpetal7",
	"SunFlower_rightpetal6", "SunFlower_rightpetal5",
	"SunFlower_rightpetal4", "SunFlower_rightpetal3",
	"SunFlower_rightpetal2", "SunFlower_rightpetal1",
	"SunFlower_toppetals",
};
constexpr SDL_Color kWarmPetalColor{255, 112, 60, 255}; // 原版黄色花瓣的橙红乘色
} // namespace

void FurnaceCoreFlower::SetupPlant()
{
	ConfigureRig();
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(0.0f, 27.0f));
		shadow->SetScale(Vector(0.9f, 0.75f));
	}
	RefreshPresentation();
}

/** 温度条件满足时积累炉芯，肥沃加快充能但不改变储量。 */
void FurnaceCoreFlower::PlantUpdate()
{
	if (mIsPreview || !mBoard) return;
	if (mStoredCores >= kMaximumStoredCores) {
		mChargeProgress = 0.0f;
		return;
	}
	if (mBoard->GetAmbientTemperatureC()
		<= mBoard->GetWinterFreezingTemperatureC()) return;

	mChargeProgress += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
	while (mChargeProgress >= kCoreChargeDuration
		&& mStoredCores < kMaximumStoredCores) {
		mChargeProgress -= kCoreChargeDuration;
		++mStoredCores;
		SetGlowingTimer(kCoreReadyGlowDuration);
		if (mStoredCores >= kMaximumStoredCores) mChargeProgress = 0.0f;
	}
	RefreshPresentation();
}

bool FurnaceCoreFlower::TryPreventIceExecutionSealFor(Plant* target)
{
	if (!target || target == this || !target->IsActive()
		|| mStoredCores <= 0 || mIsPreview || !mBoard) return false;
	if (std::abs(mRow - target->mRow) > 1
		|| std::abs(mColumn - target->mColumn) > 1) return false;

	--mStoredCores;
	SetGlowingTimer(kCoreConsumeGlowDuration);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_IGNITE,
		kCoreConsumeSoundVolume);
	RefreshPresentation();
	return true;
}

void FurnaceCoreFlower::SaveExtraData(nlohmann::json& j) const
{
	j["storedCores"] = mStoredCores;
	j["chargeProgress"] = mChargeProgress;
}

void FurnaceCoreFlower::LoadExtraData(const nlohmann::json& j)
{
	mStoredCores = std::clamp(j.value("storedCores", 0), 0,
		kMaximumStoredCores);
	mChargeProgress = std::clamp(j.value("chargeProgress", 0.0f), 0.0f,
		kCoreChargeDuration);
	if (mStoredCores >= kMaximumStoredCores) mChargeProgress = 0.0f;
	RefreshPresentation();
}

bool FurnaceCoreFlower::IsCharging() const
{
	return !mIsPreview && mBoard && mStoredCores < kMaximumStoredCores
		&& !IsActionPaused()
		&& mBoard->GetAmbientTemperatureC()
			> mBoard->GetWinterFreezingTemperatureC();
}

void FurnaceCoreFlower::SetCoreStateForTesting(int storedCores,
	float chargeProgress)
{
	mStoredCores = std::clamp(storedCores, 0, kMaximumStoredCores);
	mChargeProgress = std::clamp(chargeProgress, 0.0f, kCoreChargeDuration);
	if (mStoredCores >= kMaximumStoredCores) mChargeProgress = 0.0f;
	RefreshPresentation();
}

void FurnaceCoreFlower::ConfigureRig()
{
	if (!mAnimator) return;
	auto& resources = ResourceManager::GetInstance();
	mAnimator->SetTrackImage(kHeadTrack,
		resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_FURNACECOREFLOWER_CORE));
	mAnimator->SetTrackFollowerImage(kHeadTrack, kLeftCoreFollower,
		resources.GetTexture(ResourceKeys::Textures::IMAGE_REANIM_TORCHWOOD_FIRE1A),
		kLeftCoreOffsetX, kCoreOffsetY, kCoreFlameScale, kCoreFlameScale, false);
	mAnimator->SetTrackFollowerImage(kHeadTrack, kRightCoreFollower,
		resources.GetTexture(ResourceKeys::Textures::IMAGE_REANIM_TORCHWOOD_FIRE1A),
		kRightCoreOffsetX, kCoreOffsetY, kCoreFlameScale, kCoreFlameScale, false);
	for (const char* track : kPetalTracks) {
		mAnimator->SetTrackColor(track, kWarmPetalColor);
	}
}

void FurnaceCoreFlower::RefreshPresentation()
{
	if (!mAnimator) return;
	const int visibleCores = mIsPreview ? kMaximumStoredCores : mStoredCores;
	mAnimator->SetTrackFollowerVisible(kHeadTrack, kLeftCoreFollower,
		visibleCores >= 1);
	mAnimator->SetTrackFollowerVisible(kHeadTrack, kRightCoreFollower,
		visibleCores >= 2);
}
