#include "EliteScaredyShroom.h"

#include "../../GameApp.h"
#include "../../Reanimation/Animator.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kEliteHealth = 500;                        // 初始与最大生命值
	constexpr int kBasePuffDamage = 8;                      // a：初始孢子伤害
	constexpr int kMaxPuffDamage = 28;                       // 孢子伤害硬上限
	constexpr int kDamageGrowthShots = 5;                   // c：每累计多少成长发数提高伤害
	constexpr int kDamagePerStage = 1;                       // d：每个伤害阶段增加的伤害
	constexpr float kBaseShootIntervalSeconds = 1.5f;        // 初始射击间隔（秒）
	constexpr float kMinimumShootIntervalSeconds = 0.2f;     // 最快射击间隔（秒）
	constexpr int kSpeedGrowthShots = 3;                     // n：每累计多少成长发数提高攻速
	constexpr float kSpeedFactorPerStage = 0.85f;            // b：每阶段发射间隔缩短 15%
	constexpr int kMaximumSpeedStages = 13;                  // 第 13 阶开始触及 0.2 秒硬上限
	constexpr float kDayGrowthFactor = 0.90f;                 // e：白天成长慢 10%
	constexpr float kNightGrowthFactor = 1.0f;               // 夜间每发获得完整成长
	constexpr SDL_Color kElitePurpleOverlay{165, 70, 255, 176}; // 精英紫覆盖色，保留原贴图明暗

	constexpr int kMaximumDamageGrowthShots =
		(kMaxPuffDamage - kBasePuffDamage) / kDamagePerStage * kDamageGrowthShots;
	constexpr int kMaximumGrowthShots =
		kMaximumSpeedStages * kSpeedGrowthShots > kMaximumDamageGrowthShots
		? kMaximumSpeedStages * kSpeedGrowthShots
		: kMaximumDamageGrowthShots;
}

void EliteScaredyShroom::SetupPlant()
{
	mPlantHealth = kEliteHealth;
	mPlantMaxHealth = kEliteHealth;
	ScaredyShroom::SetupPlant();

	// 精英身份在卡片预览、图鉴和实战中保持一致。
	if (mAnimator) {
		mAnimator->EnableOverlayEffect(true);
		mAnimator->SetOverlayColor(kElitePurpleOverlay.r, kElitePurpleOverlay.g,
			kElitePurpleOverlay.b, kElitePurpleOverlay.a);
	}

	// 复用 Shroom 的夜间判断，但精英变体在白天也必须清醒并回到正常待机轨。
	if (!mIsPreview && mBoard &&
		!GameAPP::GetInstance().GetBackgroundIsNight(mBoard->mBackGround)) {
		SetSleepState(false);
		PlayTrack("anim_idle");
	}
}

void EliteScaredyShroom::SaveExtraData(nlohmann::json& j) const
{
	ScaredyShroom::SaveExtraData(j);
	j["growthProgress"] = mGrowthProgress;
}

void EliteScaredyShroom::LoadExtraData(const nlohmann::json& j)
{
	ScaredyShroom::LoadExtraData(j);
	mGrowthProgress = std::clamp(j.value("growthProgress", 0.0f),
		0.0f, static_cast<float>(kMaximumGrowthShots));
}

float EliteScaredyShroom::GetShootInterval() const
{
	return GetSimulationAttackGrowth().Interval();
}

int EliteScaredyShroom::GetPuffDamage() const
{
	return static_cast<int>(GetSimulationAttackGrowth().Damage());
}

AttackGrowth EliteScaredyShroom::GetSimulationAttackGrowth() const
{
	auto growth = InitialSimulationAttackGrowth(GetGrowthRatePercent() == 100);
	growth.progress = mGrowthProgress;
	return growth;
}

AttackGrowth EliteScaredyShroom::InitialSimulationAttackGrowth(bool night)
{
	AttackGrowth growth;
	growth.perShot = night ? kNightGrowthFactor : kDayGrowthFactor;
	growth.maximum = kMaximumGrowthShots;
	growth.baseInterval = kBaseShootIntervalSeconds; growth.minimumInterval = kMinimumShootIntervalSeconds;
	growth.intervalFactor = kSpeedFactorPerStage; growth.speedShots = kSpeedGrowthShots;
	growth.damageShots = kDamageGrowthShots; growth.baseDamage = kBasePuffDamage;
	growth.damageStep = kDamagePerStage; growth.maximumDamage = kMaxPuffDamage;
	return growth;
}

void EliteScaredyShroom::OnPuffFired()
{
	const float growth = GetGrowthRatePercent() == 100
		? kNightGrowthFactor
		: kDayGrowthFactor;
	mGrowthProgress = std::min(static_cast<float>(kMaximumGrowthShots),
		mGrowthProgress + growth);
}

void EliteScaredyShroom::OnFearStarted()
{
	// “一切从 0 开始”包含攻速与伤害成长；射击计时由胆小状态机统一压回 0。
	mGrowthProgress = 0.0f;
}

int EliteScaredyShroom::GetGrowthShotCount() const
{
	return static_cast<int>(std::floor(mGrowthProgress));
}

int EliteScaredyShroom::GetAttackSpeedStage() const
{
	return GetGrowthShotCount() / kSpeedGrowthShots;
}

int EliteScaredyShroom::GetCurrentPuffDamage() const
{
	return GetPuffDamage();
}

int EliteScaredyShroom::GetShootIntervalMilliseconds() const
{
	return static_cast<int>(std::lround(GetShootInterval() * 1000.0f));
}

int EliteScaredyShroom::GetGrowthRatePercent() const
{
	if (!mBoard) return 100;
	return GameAPP::GetInstance().GetBackgroundIsNight(mBoard->mBackGround)
		? 100
		: static_cast<int>(std::lround(kDayGrowthFactor * 100.0f));
}

int EliteScaredyShroom::GetGrowthProgressTenths() const
{
	return static_cast<int>(std::lround(mGrowthProgress * 10.0f));
}
