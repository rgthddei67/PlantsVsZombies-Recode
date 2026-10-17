#include "WinterClimate.h"
#include "GameRandom.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace {
	constexpr float kWinterWarmTemperatureC = 6.0f;      // 寒潮外冬日花园的稳定环境温度（摄氏度）
	constexpr float kWinterFreezeTemperatureC = 0.0f;    // 降水转雪并开始冻结最右列的冰点（摄氏度）
	constexpr float kWinterColdTemperatureC = -12.0f;    // 寒潮最冷阶段的稳定环境温度（摄氏度）
	constexpr float kColdWaveCalmDurationMin = 35.0f;    // 两次寒潮之间温暖间隔的最短游戏秒数
	constexpr float kColdWaveCalmDurationMax = 60.0f;    // 两次寒潮之间温暖间隔的最长游戏秒数
	constexpr float kColdWaveForecastLeadTime = 20.0f;   // 寒潮降温前开始显示准确预报的游戏秒数
	constexpr float kOpeningColdWaveDelay = 12.0f;       // 7-8/7-9 开战后到固定强寒潮开始降温的游戏秒数
	constexpr float kOpeningColdWaveCoolingDuration = 15.0f; // 7-8/7-9 固定强寒潮的降温时长（游戏秒）
	constexpr float kOpeningColdWaveHoldDuration = 50.0f; // 7-8/7-9 固定强寒潮在 -12°C 的维持时长（游戏秒）
	constexpr float kOpeningColdWaveThawDuration = 30.0f; // 7-8/7-9 固定强寒潮的回暖时长（游戏秒）
	constexpr int kOpeningColdWaveFirstFrostVariant = 1; // 7-8 开幕寒潮固定使用的霜线轮廓
	constexpr int kOpeningColdWaveFinalFrostVariant = 2; // 7-9 开幕寒潮固定使用的霜线轮廓
	constexpr int kColdWaveWeakWeight = 35;              // 弱寒潮抽取权重（百分比）
	constexpr int kColdWaveNormalWeight = 45;            // 普通寒潮抽取权重（百分比）
	constexpr float kWeakColdWaveTargetC = -5.0f;        // 弱寒潮最低温基准（摄氏度）
	constexpr float kNormalColdWaveTargetC = -8.0f;      // 普通寒潮最低温基准（摄氏度）
	constexpr float kStrongColdWaveTargetC = -11.0f;     // 强寒潮最低温基准（摄氏度）
	constexpr int kColdWaveTargetJitterMin = -1;         // 同强度最低温随机偏移下限（摄氏度）
	constexpr int kColdWaveTargetJitterMax = 1;          // 同强度最低温随机偏移上限（摄氏度）
	constexpr float kWeakColdWaveCoolingMin = 21.0f;     // 弱寒潮最短降温时长（游戏秒）
	constexpr float kWeakColdWaveCoolingMax = 25.0f;     // 弱寒潮最长降温时长（游戏秒）
	constexpr float kWeakColdWaveHoldMin = 35.0f;        // 弱寒潮最短低温维持时长（游戏秒）
	constexpr float kWeakColdWaveHoldMax = 50.0f;        // 弱寒潮最长低温维持时长（游戏秒）
	constexpr float kWeakColdWaveThawMin = 24.0f;        // 弱寒潮最短回暖时长（游戏秒）
	constexpr float kWeakColdWaveThawMax = 30.0f;        // 弱寒潮最长回暖时长（游戏秒）
	constexpr float kNormalColdWaveCoolingMin = 18.0f;   // 普通寒潮最短降温时长（游戏秒）
	constexpr float kNormalColdWaveCoolingMax = 22.0f;   // 普通寒潮最长降温时长（游戏秒）
	constexpr float kNormalColdWaveHoldMin = 45.0f;      // 普通寒潮最短低温维持时长（游戏秒）
	constexpr float kNormalColdWaveHoldMax = 65.0f;      // 普通寒潮最长低温维持时长（游戏秒）
	constexpr float kNormalColdWaveThawMin = 28.0f;      // 普通寒潮最短回暖时长（游戏秒）
	constexpr float kNormalColdWaveThawMax = 36.0f;      // 普通寒潮最长回暖时长（游戏秒）
	constexpr float kStrongColdWaveCoolingMin = 15.0f;   // 强寒潮最短降温时长（游戏秒）
	constexpr float kStrongColdWaveCoolingMax = 19.0f;   // 强寒潮最长降温时长（游戏秒）
	constexpr float kStrongColdWaveHoldMin = 55.0f;      // 强寒潮最短低温维持时长（游戏秒）
	constexpr float kStrongColdWaveHoldMax = 75.0f;      // 强寒潮最长低温维持时长（游戏秒）
	constexpr float kStrongColdWaveThawMin = 34.0f;      // 强寒潮最短回暖时长（游戏秒）
	constexpr float kStrongColdWaveThawMax = 44.0f;      // 强寒潮最长回暖时长（游戏秒）
	constexpr int kWinterFrostVariantCount = 3;          // 基础冻融线与两张霜枝贴图组成的稳定轮廓数量
}

void WinterClimate::Initialize(bool supported, bool openingScript, bool finalOpening)
{
	if (!supported) {
		mWinterTemperatureInitialized = false;
		mOpeningColdWavePlanInitialized = false;
		mColdWavePhase = ColdWavePhase::CALM;
		mColdWaveTimer = 0.0f;
		mAmbientTemperatureC = kWinterWarmTemperatureC;
		mColdWaveStrength = ColdWaveStrength::STRONG;
		mColdWaveTargetTemperatureC = kWinterColdTemperatureC;
		mColdWaveCoolingDuration = 20.0f;
		mColdWaveHoldDuration = 57.5f;
		mColdWaveThawDuration = 32.0f;
		mColdWaveForecastDisrupted = false;
		mWinterFrostVariant = 0;
		return;
	}
	const bool shouldStartOpeningColdWave =
		openingScript
		&& !mOpeningColdWavePlanInitialized;
	if (mWinterTemperatureInitialized && !shouldStartOpeningColdWave) return;
	mWinterTemperatureInitialized = true;
	mColdWavePhase = ColdWavePhase::CALM;
	mColdWaveForecastDisrupted = false;
	mAmbientTemperatureC = kWinterWarmTemperatureC;
	if (shouldStartOpeningColdWave) {
		// 选卡与戴夫闲聊尚未进入 GAME，不会调用这里；旧档恢复也从本次 StartGame 开始计时。
		InitializeOpeningPlan(finalOpening);
		mOpeningColdWavePlanInitialized = true;
		mColdWaveTimer = kOpeningColdWaveDelay;
		return;
	}
	mColdWaveTimer = GameRandom::Range(
		kColdWaveCalmDurationMin, kColdWaveCalmDurationMax);
	RollNext();
}

void WinterClimate::InitializeOpeningPlan(bool finalOpening)
{
	mColdWaveForecastDisrupted = false;
	mColdWaveStrength = ColdWaveStrength::STRONG;
	mColdWaveTargetTemperatureC = kWinterColdTemperatureC;
	mColdWaveCoolingDuration = kOpeningColdWaveCoolingDuration;
	mColdWaveHoldDuration = kOpeningColdWaveHoldDuration;
	mColdWaveThawDuration = kOpeningColdWaveThawDuration;
	mWinterFrostVariant = finalOpening
		? kOpeningColdWaveFinalFrostVariant : kOpeningColdWaveFirstFrostVariant;
}

void WinterClimate::RollNext()
{
	mColdWaveForecastDisrupted = false;
	const int strengthRoll = GameRandom::Range(1, 100);
	if (strengthRoll <= kColdWaveWeakWeight) {
		mColdWaveStrength = ColdWaveStrength::WEAK;
	} else if (strengthRoll <= kColdWaveWeakWeight + kColdWaveNormalWeight) {
		mColdWaveStrength = ColdWaveStrength::NORMAL;
	} else {
		mColdWaveStrength = ColdWaveStrength::STRONG;
	}

	float targetBase = kNormalColdWaveTargetC;
	float coolingMin = kNormalColdWaveCoolingMin;
	float coolingMax = kNormalColdWaveCoolingMax;
	float holdMin = kNormalColdWaveHoldMin;
	float holdMax = kNormalColdWaveHoldMax;
	float thawMin = kNormalColdWaveThawMin;
	float thawMax = kNormalColdWaveThawMax;
	switch (mColdWaveStrength) {
	case ColdWaveStrength::WEAK:
		targetBase = kWeakColdWaveTargetC;
		coolingMin = kWeakColdWaveCoolingMin;
		coolingMax = kWeakColdWaveCoolingMax;
		holdMin = kWeakColdWaveHoldMin;
		holdMax = kWeakColdWaveHoldMax;
		thawMin = kWeakColdWaveThawMin;
		thawMax = kWeakColdWaveThawMax;
		break;
	case ColdWaveStrength::NORMAL:
		break;
	case ColdWaveStrength::STRONG:
		targetBase = kStrongColdWaveTargetC;
		coolingMin = kStrongColdWaveCoolingMin;
		coolingMax = kStrongColdWaveCoolingMax;
		holdMin = kStrongColdWaveHoldMin;
		holdMax = kStrongColdWaveHoldMax;
		thawMin = kStrongColdWaveThawMin;
		thawMax = kStrongColdWaveThawMax;
		break;
	}

	mColdWaveTargetTemperatureC = std::clamp(targetBase + static_cast<float>(
		GameRandom::Range(kColdWaveTargetJitterMin, kColdWaveTargetJitterMax)),
		kWinterColdTemperatureC, kWinterFreezeTemperatureC);
	mColdWaveCoolingDuration = GameRandom::Range(coolingMin, coolingMax);
	mColdWaveHoldDuration = GameRandom::Range(holdMin, holdMax);
	mColdWaveThawDuration = GameRandom::Range(thawMin, thawMax);
	mWinterFrostVariant = GameRandom::Range(0, kWinterFrostVariantCount - 1);
}

void WinterClimate::Advance(float deltaTime)
{
    if (!mWinterTemperatureInitialized || deltaTime <= 0.0f) return;
    // 每步最多切换一阶段；跨阶段剩余时间沿用旧规则不继续消费。
	mColdWaveTimer = std::max(0.0f, mColdWaveTimer - deltaTime);
	switch (mColdWavePhase) {
	case ColdWavePhase::CALM:
		mAmbientTemperatureC = kWinterWarmTemperatureC;
		if (mColdWaveTimer <= 0.0f) {
			mColdWavePhase = ColdWavePhase::COOLING;
			mColdWaveTimer = mColdWaveCoolingDuration;
		}
		break;
	case ColdWavePhase::COOLING:
	{
		const float progress = std::clamp(1.0f
			- mColdWaveTimer / mColdWaveCoolingDuration, 0.0f, 1.0f);
		mAmbientTemperatureC = kWinterWarmTemperatureC
			+ (mColdWaveTargetTemperatureC - kWinterWarmTemperatureC) * progress;
		if (mColdWaveTimer <= 0.0f) {
			mColdWavePhase = ColdWavePhase::COLD;
			mColdWaveTimer = mColdWaveHoldDuration;
			mAmbientTemperatureC = mColdWaveTargetTemperatureC;
		}
		break;
	}
	case ColdWavePhase::COLD:
		mAmbientTemperatureC = mColdWaveTargetTemperatureC;
		if (mColdWaveTimer <= 0.0f) {
			mColdWavePhase = ColdWavePhase::THAWING;
			mColdWaveTimer = mColdWaveThawDuration;
		}
		break;
	case ColdWavePhase::THAWING:
	{
		const float progress = std::clamp(1.0f
			- mColdWaveTimer / mColdWaveThawDuration, 0.0f, 1.0f);
		mAmbientTemperatureC = mColdWaveTargetTemperatureC
			+ (kWinterWarmTemperatureC - mColdWaveTargetTemperatureC) * progress;
		if (mColdWaveTimer <= 0.0f) {
			mColdWavePhase = ColdWavePhase::CALM;
			mColdWaveTimer = GameRandom::Range(
				kColdWaveCalmDurationMin, kColdWaveCalmDurationMax);
			mAmbientTemperatureC = kWinterWarmTemperatureC;
			RollNext();
		}
		break;
	}
	}
}

bool WinterClimate::HasForecast() const
{
	return mWinterTemperatureInitialized
		&& !mColdWaveForecastDisrupted
		&& mColdWavePhase == ColdWavePhase::CALM
		&& mColdWaveTimer > 0.0f && mColdWaveTimer <= kColdWaveForecastLeadTime;
}

bool WinterClimate::IsForecastDisruptionVisible() const
{
	return mWinterTemperatureInitialized
		&& mColdWaveForecastDisrupted
		&& mColdWavePhase == ColdWavePhase::CALM
		&& mColdWaveTimer > 0.0f && mColdWaveTimer <= kColdWaveForecastLeadTime;
}

bool WinterClimate::IsActive() const
{
	return mWinterTemperatureInitialized
		&& mColdWavePhase != ColdWavePhase::CALM;
}

bool WinterClimate::DisruptForecast()
{
    if (!HasForecast()) return false;
    mColdWaveForecastDisrupted = true;
    return true;
}

bool WinterClimate::SetForTesting(float temperatureC,
	ColdWavePhase phase, float remaining, ColdWaveStrength strength,
	float targetTemperatureC, float coolingDuration, float holdDuration,
	float thawDuration, int frostVariant)
{
	if (!std::isfinite(temperatureC)
		|| !std::isfinite(remaining) || !std::isfinite(targetTemperatureC)
		|| !std::isfinite(coolingDuration) || coolingDuration <= 0.0f
		|| !std::isfinite(holdDuration) || holdDuration <= 0.0f
		|| !std::isfinite(thawDuration) || thawDuration <= 0.0f
		|| phase < ColdWavePhase::CALM || phase > ColdWavePhase::THAWING) {
		return false;
	}
	mWinterTemperatureInitialized = true;
	mColdWaveForecastDisrupted = false;
	mColdWavePhase = phase;
	mColdWaveTimer = std::max(0.0f, remaining);
	mColdWaveStrength = std::clamp(strength,
		ColdWaveStrength::WEAK, ColdWaveStrength::STRONG);
	mColdWaveTargetTemperatureC = std::clamp(targetTemperatureC,
		kWinterColdTemperatureC, kWinterFreezeTemperatureC);
	mColdWaveCoolingDuration = coolingDuration;
	mColdWaveHoldDuration = holdDuration;
	mColdWaveThawDuration = thawDuration;
	mWinterFrostVariant = std::clamp(frostVariant, 0, kWinterFrostVariantCount - 1);
	mAmbientTemperatureC = std::clamp(temperatureC,
		kWinterColdTemperatureC, kWinterWarmTemperatureC);
    return true;
}

void WinterClimate::Save(nlohmann::json& j) const
{
	j["winterTemperatureInitialized"] = mWinterTemperatureInitialized;
	j["openingColdWavePlanInitialized"] = mOpeningColdWavePlanInitialized;
	j["coldWavePhase"] = static_cast<int>(mColdWavePhase);
	j["coldWaveStrength"] = static_cast<int>(mColdWaveStrength);
	j["coldWaveTimer"] = mColdWaveTimer;
	j["ambientTemperatureC"] = mAmbientTemperatureC;
	j["coldWaveTargetTemperatureC"] = mColdWaveTargetTemperatureC;
	j["coldWaveCoolingDuration"] = mColdWaveCoolingDuration;
	j["coldWaveHoldDuration"] = mColdWaveHoldDuration;
	j["coldWaveThawDuration"] = mColdWaveThawDuration;
	j["coldWaveForecastDisrupted"] = mColdWaveForecastDisrupted;
	j["winterFrostVariant"] = mWinterFrostVariant;
}

void WinterClimate::Load(const nlohmann::json& j, bool supported)
{
	const int coldWavePhaseValue = j.value("coldWavePhase",
		static_cast<int>(ColdWavePhase::CALM));
	const bool validColdWavePhase = coldWavePhaseValue >= static_cast<int>(ColdWavePhase::CALM)
		&& coldWavePhaseValue <= static_cast<int>(ColdWavePhase::THAWING);
	const int coldWaveStrengthValue = j.value("coldWaveStrength",
		static_cast<int>(ColdWaveStrength::STRONG));
	const bool validColdWaveStrength = coldWaveStrengthValue
		>= static_cast<int>(ColdWaveStrength::WEAK)
		&& coldWaveStrengthValue <= static_cast<int>(ColdWaveStrength::STRONG);
	mWinterTemperatureInitialized = supported
		&& validColdWavePhase && validColdWaveStrength
		&& j.value("winterTemperatureInitialized", false);
	// v7 以前的关卡档把开幕寒潮视为尚未消费；恢复后的 StartGame 会从 12 秒倒计时重新排入。
	mOpeningColdWavePlanInitialized = supported
		&& j.value("openingColdWavePlanInitialized", false);
	mColdWavePhase = mWinterTemperatureInitialized
		? static_cast<ColdWavePhase>(coldWavePhaseValue) : ColdWavePhase::CALM;
	mColdWaveStrength = mWinterTemperatureInitialized
		? static_cast<ColdWaveStrength>(coldWaveStrengthValue) : ColdWaveStrength::STRONG;
	mColdWaveTimer = mWinterTemperatureInitialized
		? std::max(0.0f, j.value("coldWaveTimer", 0.0f)) : 0.0f;
	mAmbientTemperatureC = mWinterTemperatureInitialized
		? std::clamp(j.value("ambientTemperatureC", 6.0f), -12.0f, 6.0f)
		: 6.0f;
	// 旧档缺少计划字段时沿用此前 -12°C/20s/32s 行为，平稳结束当前一轮后再进入新随机规则。
	mColdWaveTargetTemperatureC = mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveTargetTemperatureC", -12.0f), -12.0f, 0.0f)
		: -12.0f;
	mColdWaveCoolingDuration = mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveCoolingDuration", 20.0f), 1.0f, 120.0f)
		: 20.0f;
	mColdWaveHoldDuration = mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveHoldDuration", 57.5f), 1.0f, 180.0f)
		: 57.5f;
	mColdWaveThawDuration = mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveThawDuration", 32.0f), 1.0f, 120.0f)
		: 32.0f;
	mColdWaveForecastDisrupted = mWinterTemperatureInitialized
		&& mColdWavePhase == ColdWavePhase::CALM
		&& j.value("coldWaveForecastDisrupted", false);
	mWinterFrostVariant = mWinterTemperatureInitialized
		? std::clamp(j.value("winterFrostVariant", 0), 0, 2) : 0;
}

float WinterClimate::WarmTemperature() { return kWinterWarmTemperatureC; }
float WinterClimate::ColdTemperature() { return kWinterColdTemperatureC; }
float WinterClimate::FreezingTemperature() { return kWinterFreezeTemperatureC; }
