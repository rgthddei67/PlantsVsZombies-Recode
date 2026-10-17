#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/AdventureProgression.h"
#include "Game/Plant/Plantern.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kSmallFogColumnExpansion = 1;          // 小雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr int kNormalFogColumnExpansion = 2;         // 普通迷雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr int kDenseFogColumnExpansion = 3;          // 大雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr float kBaseFogEdgeAlpha = 200.0f;          // 小雾和普通迷雾最左边缘格的目标 alpha
	constexpr float kDenseFogEdgeAlpha = 225.0f;         // 大雾最左边缘格的目标 alpha
	constexpr float kFogInteriorAlpha = 255.0f;          // 雾区内部格的目标 alpha
	constexpr float kFogFillRate = 180.0f;               // 雾生成或回流时每游戏秒最多增加的 alpha
	constexpr float kFogClearRate = 320.0f;              // 台风驱散时每游戏秒最多减少的 alpha
	constexpr float kFogTileLeftOverlap = 15.0f;         // 雾格相对逻辑列左缘向左覆盖的像素
	constexpr float kFogTileTopOverlap = 65.0f;          // 雾格相对首行逻辑顶缘向上覆盖的像素
}

/** 大雾概率复用天气导演压力，但保持独立抽取，不改写雨势权重与保底。 */
int Board::GetDenseFogChancePercent() const
{
    return SupportsFogWeather() ? FogWeather::DenseChance(GetWeatherDirectorFactor()) : 0;
}


/** 初始化当前迷雾关卡的独立雾势；基础雾线仍可由关内进度调节。 */
void Board::InitializeFogWeather()
{
    const bool supported = SupportsStageFog();
    const int count = supported ? mColumns * (mRows + 1) : 0;
    if (static_cast<int>(mFogCellAlpha.size()) != count) mFogCellAlpha.assign(count, 0.0f);
    mFogWeather.Initialize(supported);
}

/** 独立推进雾势阶段和预报；暂停与倍速都跟随 Board 的游戏时间。 */
void Board::UpdateFogWeather(float deltaTime)
{
    if (!SupportsFogWeather() || IsWeatherStation()) return;
    NotifyFogWeatherChange(mFogWeather.AdvanceWeather(deltaTime, GetWeatherDirectorFactor()));
}

/**
 * 仅超强台风累积驱散雾；较弱台风只推动雾团，并让已有驱散量继续回流。
 * 增强雾势事件被超强台风完全吹散后提前结束，并回到原版默认雾休整。
 */
void Board::UpdateFogDispersal(float deltaTime)
{
    NotifyFogWeatherChange(mFogWeather.AdvanceDispersal(deltaTime, mTyphoonStrength,
        mWindDirection, HasTyphoon(), GetWeatherDirectorFactor()));
}

/**
 * 把关卡基准、三档增强雾势扩展和台风驱散合成为逐格 alpha。
 * 读档收尾可直接对齐终态，常规更新则继续按填充/消散速率平滑追赶。
 */
void Board::UpdateFogCellAlpha(float deltaTime, bool snapToTarget)
{
	const int drawRows = GetFogDrawRowCount();
	const int fogCellCount = mColumns * drawRows;
	if (static_cast<int>(mFogCellAlpha.size()) != fogCellCount) {
		mFogCellAlpha.assign(fogCellCount, 0.0f);
	}
	if (fogCellCount <= 0) return;

	const int leftColumn = GetEffectiveFogLeftColumn();
	const float visibility = 1.0f - GetFogDispersal();
	const float edgeAlpha = IsDenseFogWeather()
		? kDenseFogEdgeAlpha : kBaseFogEdgeAlpha;
	for (int row = 0; row < drawRows; ++row) {
		for (int col = 0; col < mColumns; ++col) {
			float target = 0.0f;
			if (col == leftColumn) target = edgeAlpha * visibility;
			else if (col > leftColumn) target = kFogInteriorAlpha * visibility;
			target *= 1.0f - GetPlanternIllumination(row, col);

			float& alpha = mFogCellAlpha[row * mColumns + col];
			if (snapToTarget) {
				alpha = target;
			}
			else {
				const float rate = target >= alpha ? kFogFillRate : kFogClearRate;
				const float maxDelta = rate * deltaTime;
				alpha += std::clamp(target - alpha, -maxDelta, maxDelta);
			}
		}
	}
}

/** 推进当前迷雾关卡的雾势、台风驱散与纹理呼吸；其他关卡保持零开销。 */
void Board::UpdateFog(float deltaTime)
{
	if (!IsFogWeatherInitialized() || deltaTime <= 0.0f || !SupportsStageFog()) return;
	// 暴风雨夜把大雾锁到通关；驱散、漂移与逐格 alpha 仍走原有唯一结算链。
	if (!IsStormyNightActive()) UpdateFogWeather(deltaTime);
	UpdateFogDispersal(deltaTime);
	UpdateFogCellAlpha(deltaTime, false);
	mFogWeather.AdvanceAnimation(deltaTime);
}

/** 从关卡存档恢复会影响未来雾势和台风回流的状态；逐格 alpha 在实体恢复完成后重建。 */
void Board::LoadFogWeather(const nlohmann::json& document)
{
    mFogWeather.Load(document, SupportsFogWeather());
    mFogCellAlpha.assign(SupportsStageFog() ? mColumns * (mRows + 1) : 0, 0.0f);
}

bool Board::SetFogWeatherForTesting(FogWeatherIntensity intensity, float duration)
{
    if (!SupportsFogWeather()) return false;
    NotifyFogWeatherChange(mFogWeather.SetFixedWeather(intensity, duration, true));
    return GetFogWeatherIntensity() == intensity;
}

bool Board::SetFogWeatherForecastForTesting(FogWeatherIntensity forecast,
    FogWeatherIntensity actual, float revealIn)
{
    if (!SupportsFogWeather() || !IsFogWeatherInitialized()) return false;
    mFogWeather.SetForecast(forecast, actual, revealIn);
    return true;
}

bool Board::SetFogDispersalForTesting(float dispersal)
{
    return SupportsStageFog() && mFogWeather.SetDispersal(dispersal);
}

bool Board::SupportsStageFog() const
{
	// 第四大关继续由背景提供通用雾场；其他背景的固定关卡统一由冒险进度表登记。
	return IsStationDeviceUnlocked(WeatherStationRules::FOG) || mBackGround == Background::NIGHT_WATER_POOL
		|| AdventureProgression::HasLevelSpecificFogMechanics(mLevel);
}


bool Board::SupportsPlanternMechanics() const
{
	if (!SupportsStageFog()) return false;
	// 4-1 仍只教学基础雾；固定复用关卡始终包含路灯花燃料、照明、索敌与雾火闭环。
	return AdventureProgression::HasLevelSpecificFogMechanics(mLevel)
		|| AdventureProgression::GetLevelNumberInArea(mLevel) >= 2;
}

bool Board::SupportsFogWeather() const
{
	// 雾势沿用基础雾场的单一门禁；雨势与雾势仍保持两个独立开关。
	return SupportsStageFog();
}

/** 按九关制关内进度换算原版覆盖曲线；6-9 复用末关基准，便于集中调难。 */
int Board::GetBaseFogLeftColumn() const
{
	if (!SupportsStageFog()) return mColumns;
	if (IsWeatherStation()) return std::max(0,mColumns-4);
	const int levelInArea = AdventureProgression::GetLevelNumberInArea(mLevel);
	if (levelInArea <= 1) return std::min(6, mColumns - 1);
	if (levelInArea <= 6) return std::min(5, mColumns - 1);
	return std::min(4, mColumns - 1);
}

int Board::GetFogLayerCount() const
{
	if (!SupportsStageFog()) return 0;
	switch (GetFogWeatherIntensity()) {
	case FogWeatherIntensity::DEFAULT: return 1;
	case FogWeatherIntensity::SMALL:  return 2;
	case FogWeatherIntensity::NORMAL: return 2;
	case FogWeatherIntensity::DENSE:  return 3;
	}
	return 1;
}

int Board::GetEffectiveFogLeftColumn() const
{
	if (IsWeatherStation() && mWeatherStation.controls[WeatherStationRules::FOG].value==0) return mColumns;
	const int baseColumn = GetBaseFogLeftColumn();
	if (!SupportsStageFog()) return baseColumn;
	int expansion = 0;
	switch (GetFogWeatherIntensity()) {
	case FogWeatherIntensity::DEFAULT: break;
	case FogWeatherIntensity::SMALL:  expansion = kSmallFogColumnExpansion; break;
	case FogWeatherIntensity::NORMAL: expansion = kNormalFogColumnExpansion; break;
	case FogWeatherIntensity::DENSE:  expansion = kDenseFogColumnExpansion; break;
	}
	return std::max(0, baseColumn - expansion);
}

float Board::GetFogCellAlpha(int row, int col) const
{
	if (row < 0 || row >= GetFogDrawRowCount()
		|| col < 0 || col >= mColumns) return 0.0f;
	const int index = row * mColumns + col;
	return index < static_cast<int>(mFogCellAlpha.size())
		? mFogCellAlpha[index] : 0.0f;
}

Plantern* Board::GetActivePlantern() const
{
	Plant* plant = mEntityRegistry.GetPlant(mActivePlanternID);
	auto* plantern = dynamic_cast<Plantern*>(plant);
	return plantern && !plantern->IsSquished() ? plantern : nullptr;
}

float Board::GetPlanternIllumination(int row, int col) const
{
	if (!SupportsPlanternMechanics()) return 0.0f;
	const Plantern* plantern = GetActivePlantern();
	if (!plantern || !plantern->HasUsableLight()) return 0.0f;

	return PlanternRules::Illumination(plantern->GetGear(),row-plantern->mRow,col-plantern->mColumn);
}

int Board::GetFogTileVariant(int row, int col) const
{
	// 模拟原版 mGridCelLook 的逐格随机外观，但保持读档稳定且不消费玩法随机数。
	unsigned hash = 2166136261u;
	auto mix = [&hash](unsigned value) {
		hash ^= value;
		hash *= 16777619u;
		hash ^= hash >> 13;
	};
	mix(static_cast<unsigned>(std::max(mLevel, 0)));
	mix(static_cast<unsigned>(std::max(row, 0)));
	mix(static_cast<unsigned>(std::max(col, 0)));
	hash ^= hash >> 16;
	hash *= 0x7feb352du;
	hash ^= hash >> 15;
	return static_cast<int>(hash % 8u);
}

Vector Board::GetFogTilePosition(int row, int col) const
{
	return Vector(
		CELL_INITALIZE_POS_X + static_cast<float>(col) * CELL_COLLIDER_SIZE_X
			- kFogTileLeftOverlap + GetFogVisualOffsetX(),
		mCellInitialY + static_cast<float>(row) * mCellHeight - kFogTileTopOverlap);
}

int Board::GetVisibleFogCellCount() const
{
	return static_cast<int>(std::count_if(
		mFogCellAlpha.begin(), mFogCellAlpha.end(),
		[](float alpha) { return alpha >= 1.0f; }));
}

int Board::GetMaximumFogAlpha() const
{
	if (mFogCellAlpha.empty()) return 0;
	return static_cast<int>(std::lround(*std::max_element(
		mFogCellAlpha.begin(), mFogCellAlpha.end())));
}

/** 状态机只报告变化；提示和干扰门禁始终由 Board 的展示端口处理。 */
void Board::NotifyFogWeatherChange(bool changed)
{
    if (changed && mPresentation && !IsWeatherPanelInterferenceActive()) {
        mPresentation->ShowCurrentWeatherNotice();
    }
}

void Board::SaveFogWeather(nlohmann::json& document) const
{
    mFogWeather.Save(document);
}
