#include "Game/Board/Board.h"
#include "Game/AdventureProgression.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "ParticleSystem/ParticleSystem.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	constexpr float kWinterFrostFrontierAlphaScale = 1.0f; // 附加霜枝相对基础冻土透明度的亮度倍率
	constexpr int kWinterSafeColumnCount = 3;            // 温室侧永不冻结的安全列数
}

/** 建立冬日花园独立寒潮初态；其他地图保持温暖单位元。 */
void Board::InitializeWinterTemperature()
{
    mWinterClimate.Initialize(SupportsWinterTemperature(),
        AdventureProgression::HasOpeningColdWaveScript(mLevel),
        mLevel == AdventureProgression::AREA_SEVEN_FINAL_LEVEL);
}

/**
 * 推进寒潮降温、低温维持与回暖；降水只在跨过冰点时重建视觉，绝不参与温度计算。
 */
void Board::UpdateWinterTemperature(float deltaTime)
{
	if (!SupportsWinterTemperature()) {
		InitializeWinterTemperature();
		return;
	}
	if (!mWinterClimate.Initialized()) InitializeWinterTemperature();
	if (deltaTime <= 0.0f) return;

	const bool wasSnowing = IsWinterPrecipitationSnow();
	mWinterClimate.Advance(deltaTime);

	const bool isSnowing = IsWinterPrecipitationSnow();
	if (wasSnowing == isSnowing) return;
	if (!mRainVisualEffectName.empty() && g_particleSystem) {
		g_particleSystem->StopEffect(mRainVisualEffectName);
	}
	mRainVisualActive = false;
	if (mRainIntensity != RainIntensity::CLEAR && mWeatherTimer > 0.0f) {
		EmitRainEffect(mWeatherTimer);
	}
	if (isSnowing) StopRainAudio();
	else StartRainAudio();
}


/** 提供温度计与测试共用的归一化液柱高度，避免 UI 复制寒潮温区。 */
float Board::GetWinterTemperatureGaugeRatio() const
{
	return std::clamp((mWinterClimate.Temperature() - WinterClimate::ColdTemperature())
		/ (WinterClimate::WarmTemperature() - WinterClimate::ColdTemperature()), 0.0f, 1.0f);
}

float Board::GetWinterMinimumTemperatureC() const
{
	return WinterClimate::ColdTemperature();
}

float Board::GetWinterMaximumTemperatureC() const
{
	return WinterClimate::WarmTemperature();
}

float Board::GetWinterFreezingTemperatureC() const
{
	return WinterClimate::FreezingTemperature();
}

/** 寒潮预报直接读取同一个确定性阶段计时，保证揭晓结果准确且不进入雨势误报流程。 */
bool Board::HasColdWaveForecast() const
{
    return SupportsWinterTemperature() && mWinterClimate.HasForecast();
}

bool Board::IsColdWaveForecastDisruptionVisible() const
{
    return SupportsWinterTemperature() && mWinterClimate.IsForecastDisruptionVisible();
}

/** 让一次已公开预报在本轮内失效，并按稳定植物 ID 原子清除所有依赖准备。 */
bool Board::DisruptColdWaveForecast()
{
	if (!SupportsWinterTemperature() || !mWinterClimate.DisruptForecast()) return false;
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	for (int plantID : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (plant && plant->IsActive()) plant->OnColdWaveForecastDisrupted();
	}
	return true;
}

bool Board::IsColdWaveActive() const
{
    return SupportsWinterTemperature() && mWinterClimate.IsActive();
}

/** 温度每越过两个负温档，冻土从最右侧多推进一列；左侧三列温室区永远安全。 */
int Board::GetFrozenColumnCount() const
{
	if (!SupportsWinterTemperature()
		|| mWinterClimate.Temperature() > WinterClimate::FreezingTemperature()) return 0;
	const int maximumFrozen = std::max(0, mColumns - kWinterSafeColumnCount);
	const int coldBand = static_cast<int>(std::floor(-mWinterClimate.Temperature() / 2.0f)) + 1;
	return std::clamp(coldBand, 1, maximumFrozen);
}

/** 预报与实际霜线共用同一温度分档公式，但只读取本轮已经锁定的最低温。 */
int Board::GetForecastFrozenColumnCount() const
{
	if (!HasColdWaveForecast()
		|| mWinterClimate.Target() > WinterClimate::FreezingTemperature()) return 0;
	const int maximumFrozen = std::max(0, mColumns - kWinterSafeColumnCount);
	const int coldBand = static_cast<int>(
		std::floor(-mWinterClimate.Target() / 2.0f)) + 1;
	return std::clamp(coldBand, 1, maximumFrozen);
}

/**
 * 在相邻两档玩法冻结列之间按温度线性插值，让霜线平滑移动；整数交点仍与正式格线一致。
 */
float Board::GetWinterFrostVisualColumnCount() const
{
	if (!SupportsWinterTemperature()) return 0.0f;
	const float maximumFrozen = static_cast<float>(
		std::max(0, mColumns - kWinterSafeColumnCount));
	return std::clamp(1.0f - mWinterClimate.Temperature() / 2.0f,
		0.0f, maximumFrozen);
}

int Board::GetFirstFrozenColumn() const
{
	return mColumns - GetFrozenColumnCount();
}

bool Board::IsCellFrozen(int row, int col) const
{
	return row >= 0 && row < mRows && col >= 0 && col < mColumns
		&& GetFrozenColumnCount() > 0 && col >= GetFirstFrozenColumn();
}


bool Board::IsCellInColdWaveForecast(int row, int col) const
{
	const int frozenColumns = GetForecastFrozenColumnCount();
	return row >= 0 && row < mRows && col >= 0 && col < mColumns
		&& frozenColumns > 0 && col >= mColumns - frozenColumns;
}

bool Board::IsPlantFootprintFrozen(PlantType type, int row, int anchorColumn) const
{
	if (!SupportsWinterTemperature()) return false;
	const PlantFootprint footprint = GetPlantFootprint(type);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		if (IsCellFrozen(row + footprint.cells[i].rowOffset,
			anchorColumn + footprint.cells[i].columnOffset)) return true;
	}
	return false;
}

bool Board::IsWinterPrecipitationSnow() const
{
	return SupportsWinterTemperature()
		&& mWinterClimate.Temperature() <= WinterClimate::FreezingTemperature()
		&& mRainIntensity != RainIntensity::CLEAR;
}

bool Board::SetWinterTemperatureForTesting(float temperatureC,
	ColdWavePhase phase, float remaining, ColdWaveStrength strength,
	float targetTemperatureC, float coolingDuration, float holdDuration,
	float thawDuration, int frostVariant)
{
    if (!SupportsWinterTemperature()) return false;
    const bool wasSnowing = IsWinterPrecipitationSnow();
    if (!mWinterClimate.SetForTesting(temperatureC, phase, remaining, strength,
        targetTemperatureC, coolingDuration, holdDuration, thawDuration, frostVariant)) return false;

	StopTyphoon();
	const bool isSnowing = IsWinterPrecipitationSnow();
	if (wasSnowing != isSnowing) {
		if (!mRainVisualEffectName.empty() && g_particleSystem) {
			g_particleSystem->StopEffect(mRainVisualEffectName);
		}
		mRainVisualActive = false;
		if (mRainIntensity != RainIntensity::CLEAR && mWeatherTimer > 0.0f) {
			EmitRainEffect(mWeatherTimer);
		}
		if (isSnowing) StopRainAudio();
		else StartRainAudio();
	}
	return true;
}


/** 绘制从僵尸侧向温室推进的连续霜雪纹理，并按本轮锁定变体叠加不规则霜枝。 */
void Board::DrawWinterFrost(Graphics* g) const
{
	if (!g || !SupportsWinterTemperature()) return;
	const float visualColumns = GetWinterFrostVisualColumnCount();
	if (visualColumns <= 0.0f) return;
	const Texture* frost = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_FROST_OVERLAY_V2, false);
	if (!frost) return;

	constexpr float kFrostFrontierBleed = 24.0f; // 不规则霜枝越过逻辑冻融线的最大视觉宽度，单位像素
	const float boundaryX = CELL_INITALIZE_POS_X
		+ (static_cast<float>(mColumns) - visualColumns) * CELL_COLLIDER_SIZE_X;
	const float left = boundaryX - kFrostFrontierBleed;
	const float width = visualColumns * CELL_COLLIDER_SIZE_X
		+ kFrostFrontierBleed;
	const float height = static_cast<float>(mRows) * mCellHeight;
	const float maximumFrozen = static_cast<float>(
		std::max(1, mColumns - kWinterSafeColumnCount));
	const float alpha = visualColumns <= 1.0f
		? 75.0f * visualColumns
		: 75.0f + 170.0f * std::clamp(
			(visualColumns - 1.0f) / (maximumFrozen - 1.0f), 0.0f, 1.0f);
	const glm::vec4 tint(255.0f, 255.0f, 255.0f, alpha);
	const BlendMode previousBlend = g->GetBlendMode();
	g->SetBlendMode(BlendMode::Alpha);
	g->DrawTexture(frost, left, mCellInitialY, width, height, 0.0f, tint);

	// 变体贴图使用纯黑底加色混合：黑色不覆盖草坪，只把左侧冻融前缘的霜枝提亮。
	if (mWinterClimate.FrostVariant() > 0) {
		const std::string& frontierKey = mWinterClimate.FrostVariant() == 1
			? ResourceKeys::Textures::IMAGE_WINTER_FROST_FRONTIER_VARIANT_1
			: ResourceKeys::Textures::IMAGE_WINTER_FROST_FRONTIER_VARIANT_2;
		const Texture* frontier = ResourceManager::GetInstance().GetTexture(
			frontierKey, false);
		if (frontier) {
			g->SetBlendMode(BlendMode::Add);
			g->DrawTexture(frontier, left, mCellInitialY, width, height, 0.0f,
				glm::vec4(255.0f, 255.0f, 255.0f,
					alpha * kWinterFrostFrontierAlphaScale));
		}
	}
	g->SetBlendMode(previousBlend);
}

void Board::SaveWinterClimate(nlohmann::json& document) const { mWinterClimate.Save(document); }
void Board::LoadWinterClimate(const nlohmann::json& document) { mWinterClimate.Load(document, SupportsWinterTemperature()); }
