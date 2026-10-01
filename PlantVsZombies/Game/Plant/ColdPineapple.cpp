#include "ColdPineapple.h"
#include "Game/Board/Board.h"
#include "Game/AudioSystem.h"
#include "DeltaTime.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

using namespace ColdPineappleRules;

void ColdPineapple::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = 300;
	PlayTrack("anim_idle");
}

void ColdPineapple::Update()
{
	// 领域与冷却按游戏时间推进；自身停机不能把已付款的强化无限延长。
	if (!mIsPreview && mBoard && mBoard->mBoardState == BoardState::GAME) {
		float delta = DeltaTime::GetDeltaTime();
		mFeedbackRemaining = std::max(0.0f, mFeedbackRemaining - delta);
		if (mActiveRemaining > 0.0f) {
			const float consumed = std::min(delta, mActiveRemaining);
			mActiveRemaining -= consumed;
			delta -= consumed;
			if (mActiveRemaining <= 0.0f) mCooldownRemaining = kCooldown;
		}
		mCooldownRemaining = std::max(0.0f, mCooldownRemaining - delta);
	}
	Plant::Update();
}

void ColdPineapple::PlantUpdate()
{
	if (mAutomatic && IsReadyToActivate() && CanAffordActivation()) TryActivate();
}

bool ColdPineapple::IsReadyToActivate() const
{
	return mBoard && mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned && !DeltaTime::IsPaused()
		&& !mIsPreview && IsActive() && !IsSquished() && !IsBungeeTargeted()
		&& !IsActionPaused() && mActiveRemaining <= 0.0f && mCooldownRemaining <= 0.0f;
}

bool ColdPineapple::CanAffordActivation() const
{
	return mBoard && (mBoard->IsColdStorage()
		? mBoard->mColdStorage.playerIce >= mBoard->GetPlantAbilityIceCost(kIceCost) : mBoard->GetSun() >= kSunCost);
}

bool ColdPineapple::TryActivate()
{
	if (!IsReadyToActivate()) return false;
	if (!mBoard->TrySpendPlantAbilityResource(kIceCost, kSunCost)) {
		mFeedbackRemaining = 1.5f;
		return false;
	}
	mActiveRemaining = kDuration;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.35f);
	return true;
}

float ColdPineapple::GetAreaAttackSpeedBonus() const
{
	return IsActive() && !mIsPreview && !IsSquished() && !IsBungeeTargeted()
		&& mActiveRemaining > 0.0f ? 1.0f : 0.0f;
}

std::string ColdPineapple::GetManualAbilityDescription() const
{
	return mBoard && mBoard->IsColdStorage()
		? u8"每次" + std::to_string(mBoard->GetPlantAbilityIceCost(kIceCost)) + u8"冰块 · 攻速+100%"
		: u8"每次" + std::to_string(kSunCost) + u8"阳光 · 攻速+100%";
}

std::string ColdPineapple::GetAbilityStatusText() const
{
	std::string label;
	if (mActiveRemaining > 0.0f) label = u8"强化中";
	else if (mCooldownRemaining > 0.0f) label = u8"冷却";
	else if (!CanAffordActivation()) label = u8"缺资源";
	else if (DeltaTime::IsPaused()) label = u8"暂停";
	else if (IsActionPaused()) label = u8"停机";
	else label = u8"就绪";
	if (mAutomatic) label += u8" ↻";
	// 失败提示也读取当前费用与余额，补足资源后不保留旧的缺资源文案。
	if (mFeedbackRemaining > 0 && mActiveRemaining <= 0 && mCooldownRemaining <= 0 && !CanAffordActivation())
		label = mBoard && mBoard->IsColdStorage()
			? u8"需要" + std::to_string(mBoard->GetPlantAbilityIceCost(kIceCost)) + u8"冰块" : u8"需要" + std::to_string(kSunCost) + u8"阳光";
	return label;
}

void ColdPineapple::Draw(Graphics* g)
{
	Plant::Draw(g);
	if (!g || mIsPreview || !mBoard || !IsActive() || IsSquished()) return;
	const auto p = GetVisualAnchorPosition();
	const bool active = mActiveRemaining > 0.0f;
	const bool ready = IsReadyToActivate() && CanAffordActivation();
	const float pulse = 0.75f + 0.25f * std::sin(static_cast<float>(DeltaTime::GetTotalTime()) * 4.0f);
	const glm::vec4 color = active ? glm::vec4(120, 245, 255, 255)
		: ready ? glm::vec4(160, 255, 210, 180 + 75 * pulse) : glm::vec4(155, 165, 175, 230);
	// 分面冰晶是常驻状态灯，缩小后仍能与叶冠分辨；冷却条与文字只读实例状态。
	for (int i = -1; i <= 1; ++i) {
		const float x = p.x + i * 9.0f, y = p.y - 65.0f + std::abs(i) * 4.0f;
		g->DrawLine(x, y - 7, x - 4, y, color);
		g->DrawLine(x - 4, y, x, y + 6, color);
		g->DrawLine(x, y + 6, x + 4, y, color);
		g->DrawLine(x + 4, y, x, y - 7, color);
		g->DrawLine(x, y - 5, x, y + 4, color);
	}
	if (active || mCooldownRemaining > 0.0f) {
		const float fraction = active ? mActiveRemaining / kDuration : 1.0f - mCooldownRemaining / kCooldown;
		g->FillRect(p.x - 23, p.y + 27, 46, 5, glm::vec4(22, 42, 51, 220));
		g->FillRect(p.x - 22, p.y + 28, 44 * fraction, 3, color);
	}
	const std::string label = GetAbilityStatusText();
	const auto font = ResourceKeys::Fonts::FONT_FZCQ;
	const auto size = g->MeasureTextSize(label, font, 12);
	g->FillRect(p.x - size.x / 2 - 3, p.y + 33, size.x + 6, 17, glm::vec4(22, 42, 51, 210));
	g->DrawText(label, font, 12, color, p.x - size.x / 2, p.y + 33);
}

void ColdPineapple::SaveExtraData(nlohmann::json& j) const
{
	j["activeRemaining"] = mActiveRemaining;
	j["cooldownRemaining"] = mCooldownRemaining;
	j["automatic"] = mAutomatic;
}

void ColdPineapple::LoadExtraData(const nlohmann::json& j)
{
	const float active = j.value("activeRemaining", 0.0f), cooldown = j.value("cooldownRemaining", 0.0f);
	mActiveRemaining = std::isfinite(active) ? std::clamp(active, 0.0f, kDuration) : 0.0f;
	mCooldownRemaining = mActiveRemaining > 0 ? 0.0f
		: std::isfinite(cooldown) ? std::clamp(cooldown, 0.0f, kCooldown) : 0.0f;
	mAutomatic = j.value("automatic", false);
	mFeedbackRemaining = 0.0f; // 读档只还原权威状态，不重放付费、音效或失败提示。
}
