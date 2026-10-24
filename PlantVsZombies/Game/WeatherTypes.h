#pragma once

/** 黑夜天气强度；CLEAR 是所有倍率与视觉效果的单位元。 */
enum class RainIntensity {
	CLEAR,
	LIGHT,
	MEDIUM,
	HEAVY,
	TORRENT // 仅水岸植物园洪水章节开放的暴雨
};

/** 大雨及暴雨共享单位倍率、雷电和台风资格。 */
constexpr bool IsHeavyRainIntensity(RainIntensity value) {
	return value == RainIntensity::HEAVY || value == RainIntensity::TORRENT;
}

/** 冬日花园独立寒潮阶段；温度只由这套状态机推进，不读取雨势或台风。 */
enum class ColdWavePhase {
	CALM,
	COOLING,
	COLD,
	THAWING
};

/** 单次寒潮锁定的强度档；它决定目标温区和各阶段时长范围。 */
enum class ColdWaveStrength {
	WEAK,
	NORMAL,
	STRONG
};

/**
 * 四大关独立雾势；它与雨势正交，因此任意雾势可以和任意雨档、台风同时存在。
 * DEFAULT 对应原版基础覆盖，SMALL/NORMAL/DENSE 依次增加覆盖距离。
 */
enum class FogWeatherIntensity {
	DEFAULT,
	SMALL,
	NORMAL,
	DENSE
};

/** 仅在大雨阶段存在的台风强度；NONE 表示没有附加台风。 */
enum class TyphoonStrength {
	NONE,
	TYPHOON,
	SEVERE,
	SUPER
};

/** 风实际吹向的棋盘方向；使用“吹向”口径，避免与气象学来向混淆。 */
enum class WindDirection {
	NONE,
	TOWARD_HOUSE,
	TOWARD_FRONT
};

/** 昼夜屋顶共用的坡面径流阶段；IDLE 时 charge 可继续随雨势积累。 */
enum class RoofRunoffPhase {
	IDLE,
	WARNING,
	FLOWING
};

/** 黑夜屋顶独立雷荷阶段；CHARGING 时电荷随雨势积累或在晴夜泄漏。 */
enum class NightRoofChargePhase {
	CHARGING,
	WARNING,
	DISCHARGING
};

/** 极夜雪原的隐藏环境计划阶段；旧雨势与寒潮不得读写这些状态。 */
enum class PolarNightPhase {
	DORMANT,
	BUILDUP,
	DANGER_HOLD,
	RECOVERY,
	WHITEOUT_RAMP,
	SNOW_BLIND,
	FADE
};

/** 垂直风切变的实际吹向；NONE 只用于非强风和状态规范化。 */
enum class VerticalWindDirection {
	NONE,
	UP,
	DOWN
};

/** 雪穴从两秒预留雪堆过渡到持续活动态。 */
enum class SnowHolePhase {
	NONE,
	FORMING,
	ACTIVE
};

/** 每行至多一个雪穴；列为零基索引，timer 只在形成阶段使用。 */
struct SnowHoleState {
	int column = -1;
	SnowHolePhase phase = SnowHolePhase::NONE;
	float timer = 0.0f;
};
