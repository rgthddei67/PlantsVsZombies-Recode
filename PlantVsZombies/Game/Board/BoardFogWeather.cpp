#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/AdventureProgression.h"
#include "Game/Plant/Plantern.h"
#include "GameRandom.h"

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
	constexpr int kSmallFogColumnExpansion = 1;          // 小雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr int kNormalFogColumnExpansion = 2;         // 普通迷雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr int kDenseFogColumnExpansion = 3;          // 大雾相对原版雾线向房屋方向额外覆盖的棋盘列数
	constexpr float kBaseFogEdgeAlpha = 200.0f;          // 小雾和普通迷雾最左边缘格的目标 alpha
	constexpr float kDenseFogEdgeAlpha = 225.0f;         // 大雾最左边缘格的目标 alpha
	constexpr float kFogInteriorAlpha = 255.0f;          // 雾区内部格的目标 alpha
	constexpr float kFogFillRate = 180.0f;               // 雾生成或回流时每游戏秒最多增加的 alpha
	constexpr float kFogClearRate = 320.0f;              // 台风驱散时每游戏秒最多减少的 alpha
	constexpr float kSuperFogDispersalRate = 0.28f;      // 超强台风每游戏秒累积的雾驱散比例
	constexpr float kFogReturnRate = 0.06f;              // 停风后基础雾每游戏秒恢复的驱散比例
	constexpr float kFogMaximumDriftX = 180.0f;          // 持续台风把雾团推向当前风向的最大水平像素
	constexpr float kFogDriftSpeed = 240.0f;             // 雾团追随风向或回到原位的最大像素/游戏秒
	constexpr float kFogTileLeftOverlap = 15.0f;         // 雾格相对逻辑列左缘向左覆盖的像素
	constexpr float kFogTileTopOverlap = 65.0f;          // 雾格相对首行逻辑顶缘向上覆盖的像素

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

/** 大雾概率复用天气导演压力，但保持独立抽取，不改写雨势权重与保底。 */
int Board::GetDenseFogChancePercent() const
{
	if (!SupportsFogWeather()) return 0;
	return LerpWeatherWeight(kDenseFogChancePercent,
		kLateDenseFogChancePercent, GetWeatherDirectorFactor());
}


/** 初始化当前迷雾关卡的独立雾势；基础雾线仍可由关内进度调节。 */
void Board::InitializeFogWeather()
{
	const bool supportsFog = SupportsStageFog();
	const int fogCellCount = supportsFog ? mColumns * (mRows + 1) : 0;
	if (static_cast<int>(mFogCellAlpha.size()) != fogCellCount) {
		mFogCellAlpha.assign(fogCellCount, 0.0f);
	}
	if (!supportsFog) {
		mFogWeatherInitialized = false;
		mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
		mFogWeatherTimer = 0.0f;
		mFogDispersal = 0.0f;
		mFogVisualOffsetX = 0.0f;
		mFogAnimationTime = 0.0f;
		ClearFogWeatherForecast();
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
	ClearFogWeatherForecast();
}

/** 清除已经锁定的独立雾势预报；不会改变当前雾势或阶段倒计时。 */
void Board::ClearFogWeatherForecast()
{
	mFogWeatherForecastReady = false;
	mFogWeatherForecastDisrupted = false;
	mForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	mActualForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
}

/**
 * 按“原版默认雾 → 小雾/普通迷雾/大雾事件 → 默认雾”的节奏抽取下一雾势。
 * 大雾使用动态概率，剩余概率在小雾和普通迷雾之间尽量均分。
 */
FogWeatherIntensity Board::RollNextFogWeather(int forcedRoll)
{
	if (mFogWeatherIntensity != FogWeatherIntensity::DEFAULT) {
		return FogWeatherIntensity::DEFAULT;
	}
	const int roll = forcedRoll > 0
		? std::clamp(forcedRoll, 1, 100)
		: GameRandom::Range(1, 100);
	const int denseChance = GetDenseFogChancePercent();
	if (roll <= denseChance) return FogWeatherIntensity::DENSE;
	const int smallUpperBound = denseChance + (100 - denseChance + 1) / 2;
	return roll <= smallUpperBound
		? FogWeatherIntensity::SMALL : FogWeatherIntensity::NORMAL;
}

/**
 * 锁定下一雾势预报。
 * 首版雾势预报保持准确，避免在现有仅表达雨势误报的提示端口中伪造第二种失败语义。
 */
void Board::PrepareFogWeatherForecast(int fogRoll)
{
	if (mFogWeatherForecastReady || !SupportsFogWeather()) return;
	mFogWeatherForecastDisrupted = false;
	mActualForecastFogWeatherIntensity = RollNextFogWeather(fogRoll);
	mForecastFogWeatherIntensity = mActualForecastFogWeatherIntensity;
	mFogWeatherForecastReady = true;
}

/** 进入普通迷雾或大雾事件；只改变独立雾势，不修改雨势、台风或战斗目标选择。 */
void Board::BeginFogWeather(FogWeatherIntensity intensity, float duration)
{
	if (intensity == FogWeatherIntensity::DEFAULT) {
		EndFogWeather(duration);
		return;
	}
	const bool changed = mFogWeatherIntensity != intensity;
	mFogWeatherIntensity = intensity;
	mFogWeatherTimer = std::max(duration, 0.1f);
	ClearFogWeatherForecast();
	if (changed && mPresentation && !IsWeatherPanelInterferenceActive()) {
		mPresentation->ShowCurrentWeatherNotice();
	}
}

/** 结束增强雾势并回到原版默认雾，随后重新安排下一次独立雾势抽取。 */
void Board::EndFogWeather(float defaultDuration)
{
	const bool changed = mFogWeatherIntensity != FogWeatherIntensity::DEFAULT;
	mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	mFogWeatherTimer = std::max(defaultDuration, 0.1f);
	ClearFogWeatherForecast();
	if (changed && mPresentation && !IsWeatherPanelInterferenceActive()) {
		mPresentation->ShowCurrentWeatherNotice();
	}
}

/** 随天气导演缩短后期低压默认雾段，但始终保留可预期的短暂恢复窗口。 */
float Board::RandomDefaultFogWeatherDuration() const
{
	const float directorFactor = GetWeatherDirectorFactor();
	return GameRandom::Range(
		LerpWeatherValue(kDefaultFogWeatherDurationMin,
			kLateDefaultFogWeatherDurationMin, directorFactor),
		LerpWeatherValue(kDefaultFogWeatherDurationMax,
			kLateDefaultFogWeatherDurationMax, directorFactor));
}

/** 揭晓已经锁定的真实雾势，并给新阶段分配独立持续时间。 */
void Board::ConsumeFogWeatherForecast()
{
	if (!mFogWeatherForecastReady) PrepareFogWeatherForecast();
	const FogWeatherIntensity next = mActualForecastFogWeatherIntensity;
	if (next != FogWeatherIntensity::DEFAULT) {
		BeginFogWeather(next, GameRandom::Range(
			kElevatedFogWeatherDurationMin, kElevatedFogWeatherDurationMax));
		return;
	}
	EndFogWeather(RandomDefaultFogWeatherDuration());
}

/** 独立推进雾势阶段和预报；暂停与倍速都跟随 Board 的游戏时间。 */
void Board::UpdateFogWeather(float deltaTime)
{
	if (!SupportsFogWeather() || IsWeatherStation()) return;
	mFogWeatherTimer -= deltaTime;
	if (mFogWeatherTimer <= kFogWeatherForecastLeadTime
		&& !mFogWeatherForecastReady) {
		PrepareFogWeatherForecast();
	}
	if (mFogWeatherTimer <= 0.0f) {
		ConsumeFogWeatherForecast();
	}
}

/**
 * 仅超强台风累积驱散雾；较弱台风只推动雾团，并让已有驱散量继续回流。
 * 增强雾势事件被超强台风完全吹散后提前结束，并回到原版默认雾休整。
 */
void Board::UpdateFogDispersal(float deltaTime)
{
	const float dispersalRate = mTyphoonStrength == TyphoonStrength::SUPER
		? kSuperFogDispersalRate : -kFogReturnRate;
	mFogDispersal = std::clamp(
		mFogDispersal + dispersalRate * deltaTime, 0.0f, 1.0f);

	float targetOffsetX = 0.0f;
	if (HasTyphoon() && mWindDirection != WindDirection::NONE) {
		const float direction = mWindDirection == WindDirection::TOWARD_FRONT ? 1.0f : -1.0f;
		targetOffsetX = direction * kFogMaximumDriftX
			* std::max(0.2f, mFogDispersal);
	}
	const float maxOffsetDelta = kFogDriftSpeed * deltaTime;
	mFogVisualOffsetX += std::clamp(
		targetOffsetX - mFogVisualOffsetX, -maxOffsetDelta, maxOffsetDelta);

	if (mFogWeatherIntensity != FogWeatherIntensity::DEFAULT
		&& mFogDispersal >= 0.999f) {
		EndFogWeather(RandomDefaultFogWeatherDuration());
	}
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
	const float visibility = 1.0f - mFogDispersal;
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
	if (!mFogWeatherInitialized || deltaTime <= 0.0f || !SupportsStageFog()) return;
	// 暴风雨夜把大雾锁到通关；驱散、漂移与逐格 alpha 仍走原有唯一结算链。
	if (!IsStormyNightActive()) UpdateFogWeather(deltaTime);
	UpdateFogDispersal(deltaTime);
	UpdateFogCellAlpha(deltaTime, false);
	mFogAnimationTime = std::fmod(mFogAnimationTime + deltaTime, 3600.0f);
}

/** 从关卡存档恢复会影响未来雾势和台风回流的状态；逐格 alpha 在实体恢复完成后重建。 */
void Board::RestoreFogState(bool initialized, FogWeatherIntensity intensity,
	FogWeatherIntensity forecast, FogWeatherIntensity actual,
	float timer, bool forecastReady, float dispersal, float visualOffsetX)
{
	mFogWeatherInitialized = initialized && SupportsFogWeather();
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
	mFogCellAlpha.assign(SupportsStageFog() ? mColumns * (mRows + 1) : 0, 0.0f);
}

bool Board::SetFogWeatherForTesting(FogWeatherIntensity intensity, float duration)
{
	if (!SupportsFogWeather()) return false;
	mFogWeatherInitialized = true;
	mFogDispersal = 0.0f;
	mFogVisualOffsetX = 0.0f;
	ClearFogWeatherForecast();
	if (intensity == FogWeatherIntensity::DEFAULT) {
		EndFogWeather(std::max(duration, 0.1f));
	}
	else {
		BeginFogWeather(intensity, std::max(duration, 0.1f));
	}
	return mFogWeatherIntensity == intensity;
}

bool Board::SetFogWeatherForecastForTesting(FogWeatherIntensity forecast,
	FogWeatherIntensity actual, float revealIn)
{
	if (!SupportsFogWeather() || !mFogWeatherInitialized) return false;
	mForecastFogWeatherIntensity = forecast;
	mActualForecastFogWeatherIntensity = actual;
	mFogWeatherForecastReady = true;
	mFogWeatherForecastDisrupted = false;
	mFogWeatherTimer = std::max(revealIn, 0.1f);
	return true;
}

bool Board::SetFogDispersalForTesting(float dispersal)
{
	if (!SupportsStageFog() || !mFogWeatherInitialized
		|| !std::isfinite(dispersal)) return false;
	mFogDispersal = std::clamp(dispersal, 0.0f, 1.0f);
	return true;
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
	switch (mFogWeatherIntensity) {
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
	switch (mFogWeatherIntensity) {
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
			- kFogTileLeftOverlap + mFogVisualOffsetX,
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
