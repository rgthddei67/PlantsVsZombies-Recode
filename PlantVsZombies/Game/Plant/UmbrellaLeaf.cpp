#include "UmbrellaLeaf.h"

#include "../AudioSystem.h"
#include "../ShadowComponent.h"
#include "../../DeltaTime.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kUmbrellaHealth = 300;                    // 经典叶子保护伞基础生命值
	constexpr float kActivationSeconds = 0.05f;             // 原版 5 厘秒展开倒计时，换算为秒
	constexpr float kBlockClipSpeed = 22.0f / 12.0f;        // 原版 22 FPS 相对资源 12 FPS 的倍率
	constexpr float kIdleClipSpeed = 1.0f;                  // 待机轨按资源原生 12 FPS 播放
	constexpr float kUmbrellaSoundVolume = 0.35f;           // 展开伞叶时的 throw2 Foley 音量
	constexpr float kShadowOffsetX = 0.0f;                  // 半尺寸阴影相对逻辑格中心的水平偏移，单位 px
	constexpr float kShadowOffsetY = 27.0f;                 // 半尺寸阴影相对逻辑格中心的垂直偏移，单位 px
	constexpr float kShadowScale = 0.5f;                    // 原版叶子保护伞阴影等比缩放
}

void UmbrellaLeaf::SetupPlant()
{
	Plant::SetupPlant();
	mPlantHealth = kUmbrellaHealth;
	mPlantMaxHealth = kUmbrellaHealth;
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(kShadowOffsetX, kShadowOffsetY));
		shadow->SetScale(Vector(kShadowScale, kShadowScale));
	}
}

void UmbrellaLeaf::PlantUpdate()
{
	if (mDefenseState == AirborneDefenseState::ACTIVATING) {
		mActivationTimer = std::max(0.0f,
			mActivationTimer - DeltaTime::GetDeltaTime());
		if (mActivationTimer <= 0.0f) {
			mDefenseState = AirborneDefenseState::REFLECTING;
		}
	}
	else if (mDefenseState == AirborneDefenseState::REFLECTING
		&& GetCurrentTrackName() == "anim_idle") {
		mDefenseState = AirborneDefenseState::INACTIVE;
	}
}

bool UmbrellaLeaf::ProtectsCellFromAirborneThreat(int row, int column) const
{
	return IsActive() && !mIsPreview && !IsSquished() && !IsBungeeTargeted()
		&& mPlantHealth > 0
		&& std::abs(row - mRow) <= ProtectionCells
		&& std::abs(column - mColumn) <= ProtectionCells;
}

AirborneDefenseState UmbrellaLeaf::ActivateAirborneDefense()
{
	if (!IsActive() || mIsPreview || IsSquished() || IsBungeeTargeted()
		|| mPlantHealth <= 0) {
		return AirborneDefenseState::INACTIVE;
	}
	if (mDefenseState != AirborneDefenseState::INACTIVE) return mDefenseState;

	mDefenseState = AirborneDefenseState::ACTIVATING;
	mActivationTimer = kActivationSeconds;
	// anim_block 的 15..29 包装窗完整播放后自动回到 idle；结算只看时间/轨道，不使用帧事件。
	PlayTrackOnce("anim_block", "anim_idle", kBlockClipSpeed,
		0.0f, kIdleClipSpeed, 0.0f);
	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, kUmbrellaSoundVolume);
	return mDefenseState;
}

void UmbrellaLeaf::SaveExtraData(nlohmann::json& j) const
{
	j["defenseState"] = static_cast<int>(mDefenseState);
	j["activationTimer"] = mActivationTimer;
}

void UmbrellaLeaf::LoadExtraData(const nlohmann::json& j)
{
	const int state = std::clamp(
		j.value("defenseState", static_cast<int>(AirborneDefenseState::INACTIVE)),
		static_cast<int>(AirborneDefenseState::INACTIVE),
		static_cast<int>(AirborneDefenseState::REFLECTING));
	mDefenseState = static_cast<AirborneDefenseState>(state);
	mActivationTimer = std::max(0.0f, j.value("activationTimer", 0.0f));
	NormalizeLoadedDefenseState();
}

void UmbrellaLeaf::NormalizeLoadedDefenseState()
{
	// Animator 已由通用存档先恢复；状态与轨道不一致时以可见终态为准，且绝不重播声音。
	if (mDefenseState == AirborneDefenseState::ACTIVATING
		&& mActivationTimer <= 0.0f) {
		mDefenseState = AirborneDefenseState::REFLECTING;
	}
	if (mDefenseState != AirborneDefenseState::INACTIVE
		&& GetCurrentTrackName() == "anim_idle") {
		mDefenseState = AirborneDefenseState::INACTIVE;
		mActivationTimer = 0.0f;
	}
}
