#include "FogWeather.h"
#include "GameRandom.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace {
	constexpr float kFirstFogWeatherDelayMin = 45.0f;    // 夜间泳池开局到首次独立雾势抽取的最短游戏秒
	constexpr float kFirstFogWeatherDelayMax = 70.0f;    // 夜间泳池开局到首次独立雾势抽取的最长游戏秒
	constexpr float kDefaultFogWeatherDurationMin = 50.0f; // 原版默认雾休整阶段的最短持续游戏秒
	constexpr float kDefaultFogWeatherDurationMax = 80.0f; // 原版默认雾休整阶段的最长持续游戏秒
	constexpr float kLateDefaultFogWeatherDurationMin = 20.0f; // 满压力后默认雾休整的最短游戏秒
	constexpr float kLateDefaultFogWeatherDurationMax = 35.0f; // 满压力后默认雾休整的最长游戏秒
	constexpr float kElevatedFogWeatherDurationMin = 35.0f; // 小雾、普通迷雾或大雾事件的最短持续游戏秒
	constexpr float kElevatedFogWeatherDurationMax = 55.0f; // 小雾、普通迷雾或大雾事件的最长持续游戏秒
	constexpr float kFogWeatherForecastLeadTime = 15.0f; // 独立雾势揭晓前展示预报的游戏秒
	constexpr int kDenseFogChancePercent = 45;           // 夜间泳池前期每次雾势抽取进入大雾的概率
	constexpr int kLateDenseFogChancePercent = 85;       // 满压力时每次雾势抽取进入大雾的概率
	constexpr float kSuperFogDispersalRate = 0.28f;      // 超强台风每游戏秒累积的雾驱散比例
	constexpr float kFogReturnRate = 0.06f;              // 停风后基础雾每游戏秒恢复的驱散比例
	constexpr float kFogMaximumDriftX = 180.0f;          // 持续台风把雾团推向当前风向的最大水平像素
	constexpr float kFogDriftSpeed = 240.0f;             // 雾团追随风向或回到原位的最大像素/游戏秒

	/** 在两档调参权重间插值并四舍五入，保证后期变化连续而非跨波次突跳。 */
	int LerpWeatherWeight(int earlyWeight, int lateWeight, float lateFactor)
	{
		return static_cast<int>(std::lround(static_cast<float>(earlyWeight)
			+ static_cast<float>(lateWeight - earlyWeight) * lateFactor));
	}

	/** 在线性调参端点间插值；调用方负责提供已经夹紧的天气导演强度。 */
	float LerpWeatherValue(float earlyValue, float lateValue, float directorFactor)
	{
		return earlyValue + (lateValue - earlyValue) * directorFactor;
	}
}

int FogWeather::DenseChance(float directorFactor)
{
    return LerpWeatherWeight(kDenseFogChancePercent, kLateDenseFogChancePercent, directorFactor);
}

void FogWeather::Initialize(bool supported)
{
	if (!supported) {
		mFogWeatherInitialized = false;
		mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
		mFogWeatherTimer = 0.0f;
		mFogDispersal = 0.0f;
		mFogVisualOffsetX = 0.0f;
		mFogAnimationTime = 0.0f;
		ClearForecast();
		return;
	}
	if (mFogWeatherInitialized) return;

	mFogWeatherInitialized = true;
	mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	mFogWeatherTimer = GameRandom::Range(
		kFirstFogWeatherDelayMin, kFirstFogWeatherDelayMax);
	mFogDispersal = 0.0f;
	mFogVisualOffsetX = 0.0f;
	mFogAnimationTime = 0.0f;
	ClearForecast();
}

void FogWeather::ClearForecast()
{
	mFogWeatherForecastReady = false;
	mFogWeatherForecastDisrupted = false;
	mForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	mActualForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
}

FogWeatherIntensity FogWeather::RollNext(float directorFactor)
{
	if (mFogWeatherIntensity != FogWeatherIntensity::DEFAULT) {
		return FogWeatherIntensity::DEFAULT;
	}
	// 默认雾才抽签；增强雾的回落阶段不消费 RNG，保持其他系统的随机序列。
	const int roll = GameRandom::Range(1, 100);
	const int denseChance = DenseChance(directorFactor);
	if (roll <= denseChance) return FogWeatherIntensity::DENSE;
	const int smallUpperBound = denseChance + (100 - denseChance + 1) / 2;
	return roll <= smallUpperBound
		? FogWeatherIntensity::SMALL : FogWeatherIntensity::NORMAL;
}

void FogWeather::PrepareForecast(float directorFactor)
{
	if (mFogWeatherForecastReady) return;
	mFogWeatherForecastDisrupted = false;
	mActualForecastFogWeatherIntensity = RollNext(directorFactor);
	mForecastFogWeatherIntensity = mActualForecastFogWeatherIntensity;
	mFogWeatherForecastReady = true;
}

bool FogWeather::Begin(FogWeatherIntensity intensity, float duration)
{
    const bool changed = mFogWeatherIntensity != intensity;
    mFogWeatherIntensity = intensity;
    mFogWeatherTimer = std::max(duration, 0.1f);
    ClearForecast();
    return changed;
}

float FogWeather::RandomDefaultDuration(float directorFactor)
{
	return GameRandom::Range(
		LerpWeatherValue(kDefaultFogWeatherDurationMin,
			kLateDefaultFogWeatherDurationMin, directorFactor),
		LerpWeatherValue(kDefaultFogWeatherDurationMax,
			kLateDefaultFogWeatherDurationMax, directorFactor));
}

bool FogWeather::ConsumeForecast(float directorFactor)
{
	if (!mFogWeatherForecastReady) PrepareForecast(directorFactor);
	const FogWeatherIntensity next = mActualForecastFogWeatherIntensity;
	if (next != FogWeatherIntensity::DEFAULT) {
		return Begin(next, GameRandom::Range(
			kElevatedFogWeatherDurationMin, kElevatedFogWeatherDurationMax));
	}
	return Begin(FogWeatherIntensity::DEFAULT, RandomDefaultDuration(directorFactor));
}

bool FogWeather::AdvanceWeather(float deltaTime, float directorFactor)
{
	if (!mFogWeatherInitialized || deltaTime <= 0.0f) return false;
	// 沿用每步最多切换一次的时间语义，不把本步超时带入下一阶段。
	mFogWeatherTimer -= deltaTime;
	if (mFogWeatherTimer <= kFogWeatherForecastLeadTime
		&& !mFogWeatherForecastReady) {
		PrepareForecast(directorFactor);
	}
	if (mFogWeatherTimer <= 0.0f) {
		return ConsumeForecast(directorFactor);
	}
	return false;
}

bool FogWeather::AdvanceDispersal(float deltaTime, TyphoonStrength strength, WindDirection direction,
    bool hasTyphoon, float directorFactor)
{
    if (!mFogWeatherInitialized || deltaTime <= 0.0f) return false;
	const float dispersalRate = strength == TyphoonStrength::SUPER
		? kSuperFogDispersalRate : -kFogReturnRate;
	mFogDispersal = std::clamp(
		mFogDispersal + dispersalRate * deltaTime, 0.0f, 1.0f);

	float targetOffsetX = 0.0f;
	if (hasTyphoon && direction != WindDirection::NONE) {
		const float sign = direction == WindDirection::TOWARD_FRONT ? 1.0f : -1.0f;
		targetOffsetX = sign * kFogMaximumDriftX
			* std::max(0.2f, mFogDispersal);
	}
	const float maxOffsetDelta = kFogDriftSpeed * deltaTime;
	mFogVisualOffsetX += std::clamp(
		targetOffsetX - mFogVisualOffsetX, -maxOffsetDelta, maxOffsetDelta);

	if (mFogWeatherIntensity != FogWeatherIntensity::DEFAULT
		&& mFogDispersal >= 0.999f) {
		return Begin(FogWeatherIntensity::DEFAULT, RandomDefaultDuration(directorFactor));
	}
	return false;
}

void FogWeather::Restore(bool supported, bool initialized, FogWeatherIntensity intensity,
    FogWeatherIntensity forecast, FogWeatherIntensity actual, float timer, bool forecastReady,
    float dispersal, float visualOffsetX)
{
	mFogWeatherInitialized = initialized && supported;
	mFogWeatherIntensity = mFogWeatherInitialized
		? intensity : FogWeatherIntensity::DEFAULT;
	mFogWeatherTimer = mFogWeatherInitialized ? std::max(0.0f, timer) : 0.0f;
	mFogWeatherForecastReady = mFogWeatherInitialized && forecastReady;
	mFogWeatherForecastDisrupted = false;
	mForecastFogWeatherIntensity = mFogWeatherForecastReady
		? forecast : FogWeatherIntensity::DEFAULT;
	mActualForecastFogWeatherIntensity = mFogWeatherForecastReady
		? actual : FogWeatherIntensity::DEFAULT;
	mFogDispersal = mFogWeatherInitialized
		? std::clamp(dispersal, 0.0f, 1.0f) : 0.0f;
	mFogVisualOffsetX = mFogWeatherInitialized
		? std::clamp(visualOffsetX, -kFogMaximumDriftX, kFogMaximumDriftX) : 0.0f;
	mFogAnimationTime = 0.0f;
}

bool FogWeather::SetFixedWeather(FogWeatherIntensity intensity, float duration, bool resetDispersal)
{
    mFogWeatherInitialized = true;
    if (resetDispersal) {
        mFogDispersal = 0.0f;
        mFogVisualOffsetX = 0.0f;
    }
    return Begin(intensity, duration);
}

void FogWeather::ApplyDeviceControl(bool supported, FogWeatherIntensity intensity)
{
    mFogWeatherIntensity = intensity;
    mFogWeatherInitialized = supported;
    // 设备接管保留旧公开/真实值；无 ready 标志时它们不可见，读档会按旧规则归零。
    mFogWeatherForecastReady = false;
}

void FogWeather::DisruptForecast()
{
    if (HasForecast()) mFogWeatherForecastDisrupted = true;
}

void FogWeather::SetForecast(FogWeatherIntensity forecast, FogWeatherIntensity actual, float revealIn)
{
	mForecastFogWeatherIntensity = forecast;
	mActualForecastFogWeatherIntensity = actual;
	mFogWeatherForecastReady = true;
	mFogWeatherForecastDisrupted = false;
	mFogWeatherTimer = std::max(revealIn, 0.1f);
}

bool FogWeather::SetDispersal(float dispersal)
{
	if (!mFogWeatherInitialized
		|| !std::isfinite(dispersal)) return false;
	mFogDispersal = std::clamp(dispersal, 0.0f, 1.0f);
	return true;
}

void FogWeather::AdvanceAnimation(float seconds)
{
    mFogAnimationTime = std::fmod(mFogAnimationTime + seconds, 3600.0f);
}

void FogWeather::Save(nlohmann::json& j) const
{
	j["fogWeatherInitialized"] = mFogWeatherInitialized;
	j["fogWeatherIntensity"] = static_cast<int>(mFogWeatherIntensity);
	j["forecastFogWeatherIntensity"] =
		static_cast<int>(mForecastFogWeatherIntensity);
	j["actualForecastFogWeatherIntensity"] =
		static_cast<int>(mActualForecastFogWeatherIntensity);
	j["fogWeatherTimer"] = mFogWeatherTimer;
	j["fogWeatherForecastReady"] = mFogWeatherForecastReady;
	j["fogWeatherForecastDisrupted"] = mFogWeatherForecastDisrupted;
	j["fogDispersal"] = mFogDispersal;
	j["fogVisualOffsetX"] = mFogVisualOffsetX;
}

void FogWeather::Load(const nlohmann::json& j, bool supported)
{
	if (j.contains("fogWeatherIntensity")) {
		const int fogWeatherValue = j.value("fogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const int forecastFogWeatherValue = j.value("forecastFogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const int actualForecastFogWeatherValue = j.value("actualForecastFogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const bool validFogWeather = fogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& fogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool validForecastFogWeather = forecastFogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& forecastFogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool validActualForecastFogWeather = actualForecastFogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& actualForecastFogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool fogForecastReady = j.value("fogWeatherForecastReady", false)
			&& j.contains("actualForecastFogWeatherIntensity")
			&& validForecastFogWeather && validActualForecastFogWeather;
		// 关卡基准由当前进度派生；这里只恢复雾势、未来抽取和台风驱散结果。
		Restore(supported,
			j.value("fogWeatherInitialized", true),
			validFogWeather
				? static_cast<FogWeatherIntensity>(fogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			fogForecastReady
				? static_cast<FogWeatherIntensity>(forecastFogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			fogForecastReady
				? static_cast<FogWeatherIntensity>(actualForecastFogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			j.value("fogWeatherTimer", 0.0f), fogForecastReady,
			j.value("fogDispersal", 0.0f), j.value("fogVisualOffsetX", 0.0f));
		mFogWeatherForecastDisrupted = fogForecastReady
			&& j.value("fogWeatherForecastDisrupted", false);
	}
}
