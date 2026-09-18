#include "Game/Board/Board.h"
#include "Game/AdventureProgression.h"
#include "Game/AudioSystem.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/Ladder.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"
#include "Game/Zombie/Zombie.h"
#include "GameApp.h"
#include "GameRandom.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "ParticleSystem/ParticleSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace {
	constexpr float kFirstRainDelayMin = 90.0f;          // 开局到首场雨的最短等待时间（秒）
	constexpr float kFirstRainDelayMax = 105.0f;          // 开局到首场雨的最长等待时间（秒）
	constexpr int kStormyNightLevel = 36;                 // 暴风雨夜专属冒险关：内部 level 36 即 4-9
	constexpr int kStormyNightForecastWave = 22;          // 第 22 波开始固定发布“暴风雨”预报
	constexpr int kStormyNightStartWave = 23;             // 第 23 波正式进入暴风雨夜，持续到第 30 波
	constexpr float kStormyNightLockedDuration = 3600.0f; // 锁定雨势/雾势的安全运行时长；状态机不会递减
	constexpr float kStormFlashUnitSeconds = 1.5f;        // C# 原版 STORM_FLASH_TIME=150cs 换算的游戏秒
	constexpr float kStormFlashShortDelayMax = 4.0f;      // 原版较密闪电等待上界（游戏秒）
	constexpr float kStormFlashLongDelayMax = 7.5f;       // 原版较疏闪电等待上界（游戏秒）
	constexpr float kStormFlashDelayMin = 3.0f;           // 每轮黑屏后等待闪光的最短游戏秒
	constexpr float kClearWeatherDelayMin = 15.0f;       // 两场雨之间的最短晴空间隔（秒）
	constexpr float kClearWeatherDelayMax = 40.0f;       // 两场雨之间的最长晴空间隔（秒）
	constexpr float kRainDurationMin = 85.0f;            // 一场新雨第一个雨段的最短持续时间（秒）
	constexpr float kRainDurationMax = 150.0f;           // 一场新雨第一个雨段的最长持续时间（秒）
	constexpr float kRainTailDurationMin = 45.0f;        // 雨势切档后尾雨段的最短持续时间（秒）
	constexpr float kRainTailDurationMax = 80.0f;        // 雨势切档后尾雨段的最长持续时间（秒）
	constexpr float kLateLightRainDurationMin = 45.0f;   // 满压力下新小雨最短持续时间（秒），加快低威胁天气周转
	constexpr float kLateLightRainDurationMax = 70.0f;   // 满压力下新小雨最长持续时间（秒）
	constexpr float kLateMediumRainDurationMin = 60.0f;  // 满压力下新中雨最短持续时间（秒）
	constexpr float kLateMediumRainDurationMax = 95.0f;  // 满压力下新中雨最长持续时间（秒）
	constexpr float kLateLightRainTailMin = 20.0f;       // 满压力下尾段小雨最短持续时间（秒）
	constexpr float kLateLightRainTailMax = 35.0f;       // 满压力下尾段小雨最长持续时间（秒）
	constexpr float kLateMediumRainTailMin = 30.0f;      // 满压力下尾段中雨最短持续时间（秒）
	constexpr float kLateMediumRainTailMax = 50.0f;      // 满压力下尾段中雨最长持续时间（秒）
	constexpr float kWeatherForecastLeadTime = 15.0f;    // 阶段结束前多少秒预抽取并展示下一天气
	constexpr float kHeavyRainPromptLeadTime = 5.0f;     // 公开预报揭晓前弹出大雨分级文字警报的提前量（游戏秒）
	constexpr float kMaximumWeatherPanelInterferenceDuration = 300.0f; // 多次整栏黑障叠加后的当前剩余时长上限（游戏秒）
	constexpr int kWeatherForecastAccuracyPercent = 75;  // 前期天气预警准确率（百分比）
	constexpr int kLateWeatherForecastAccuracyPercent = 95; // 满压力天气预警准确率上限（百分比）
	constexpr float kWeatherTransitionDuration = 2.0f;   // 雨势切换时倍率、暗幕与雨声音量的平滑过渡时长（游戏秒）
	constexpr float kLateWeatherRampStart = 0.40f;       // 普通关波次进度超过该比例后开始增强后期天气（0～1）
	constexpr float kAdventurePressureFullProgress = 0.7f; // 普通关到该波次进度时达到完整天气压力（0～1）
	constexpr int kOpeningTyphoonFirstEligibleWave = 6;  // 默认开局保护结束波；进入第 6 波即可按正式规则抽取台风
	constexpr int kSurvivalLateWeatherFullRound = 8;     // 黑夜无尽到该轮起按完整后期天气权重计算
	constexpr int kSurvivalPressureStartRound = 8;       // 黑夜无尽从该轮起在基础雨势之上继续增加天气压力
	constexpr int kSurvivalPressureFullRound = 20;       // 黑夜无尽到该轮达到完整天气压力，之后不再继续放大
	constexpr int kLightRainWeight = 45;                 // 小雨相对权重；数值越大越容易抽中
	constexpr int kMediumRainWeight = 40;                // 中雨相对权重；数值越大越容易抽中
	constexpr int kHeavyRainWeight = 40;                 // 大雨相对权重；数值越大越容易抽中
	constexpr int kLateLightRainWeight = 10;             // 满压力小雨目标权重，保留少量天气层次
	constexpr int kLateMediumRainWeight = 25;            // 满压力中雨目标权重，避免长时间低威胁天气
	constexpr int kLateHeavyRainWeight = 65;             // 满压力大雨目标权重，不含弱天气保底
	constexpr int kClearHoldWeight = 15;                 // 前期晴天阶段结束后继续晴天的相对权重
	constexpr int kLateClearHoldWeight = 0;              // 满压力不连续续晴；每场雨后的晴空间隔仍保留
	constexpr float kWeakWeatherPityStart = 0.75f;       // 天气导演达到该强度后，一次弱天气便触发下轮大雨保底
	constexpr int kWeakWeatherPityMax = 1;               // 连续弱天气计数上限，保证后期最多间隔一轮弱天气
	constexpr int kLightToMediumWeight = 30;             // 初始小雨结束时增强为中雨的相对权重
	constexpr int kLightToHeavyWeight = 45;              // 初始小雨结束时骤增为大雨的相对权重
	constexpr int kLightToClearWeight = 30;              // 初始小雨结束时直接放晴的相对权重
	constexpr int kLightTransitionWeightTotal = kLightToMediumWeight + kLightToHeavyWeight + kLightToClearWeight; // 初始小雨转档总权重
	constexpr int kMediumToLightWeight = 20;             // 中雨结束时衰减为小雨的相对权重
	constexpr int kMediumToClearWeight = 70;             // 中雨结束时直接放晴的相对权重
	constexpr int kLateMediumToLightWeight = 15;         // 后期中雨衰减为小雨的目标权重，减少乏味的小雨尾段
	constexpr int kLateMediumToClearWeight = 95;         // 后期中雨直接放晴的目标权重
	constexpr int kMediumHoldWeight = 15;                // 每场中雨至多一次同档续期的相对权重
	constexpr int kHeavyToMediumWeight = 45;             // 大雨结束时衰减为中雨的相对权重
	constexpr int kHeavyToLightWeight = 20;              // 大雨结束时直接衰减为小雨的相对权重
	constexpr int kHeavyToClearWeight = 40;              // 大雨结束时直接放晴的相对权重
	constexpr int kLateHeavyToMediumWeight = 55;         // 后期大雨衰减为中雨的目标权重
	constexpr int kLateHeavyToLightWeight = 5;           // 后期大雨直接衰减为小雨的目标权重
	constexpr int kLateHeavyToClearWeight = 40;          // 后期大雨直接放晴的目标权重
	constexpr int kHeavyHoldWeight = 20;                 // 每场大雨至多一次同档续期的相对权重
	constexpr float kLightZombieSpeed = 1.15f;           // 小雨僵尸动作与移动速度倍率
	constexpr float kMediumZombieSpeed = 1.25f;          // 中雨僵尸动作与移动速度倍率
	constexpr float kHeavyZombieSpeed = 1.40f;           // 大雨僵尸动作与移动速度倍率
	constexpr float kPressuredLightZombieSpeed = 1.20f;  // 完整后期压力下小雨僵尸动作与移动速度倍率
	constexpr float kPressuredMediumZombieSpeed = 1.35f; // 完整后期压力下中雨僵尸动作与移动速度倍率
	constexpr float kPressuredHeavyZombieSpeed = 1.55f;  // 完整后期压力下大雨僵尸动作与移动速度倍率
	constexpr float kLightPlantActionSpeed = 1.10f;      // 小雨植物攻击、生产、成长与恢复速度倍率
	constexpr float kMediumPlantActionSpeed = 1.15f;     // 中雨植物攻击、生产、成长与恢复速度倍率
	constexpr float kHeavyPlantActionSpeed = 1.20f;      // 大雨植物攻击、生产、成长与恢复速度倍率
	constexpr float kPressuredLightPlantActionSpeed = 1.00f;  // 满压力小雨植物行动倍率，收回前期增益但不惩罚
	constexpr float kPressuredMediumPlantActionSpeed = 0.97f; // 满压力中雨植物行动倍率，提供温和输出压制
	constexpr float kPressuredHeavyPlantActionSpeed = 0.93f;  // 满压力大雨植物行动倍率，避免成熟阵容无视天气
	constexpr float kLightOverlayAlpha = 45.0f;          // 小雨世界蓝灰暗幕透明度（0～255）
	constexpr float kMediumOverlayAlpha = 70.0f;         // 中雨世界蓝灰暗幕透明度（0～255）
	constexpr float kHeavyOverlayAlpha = 120.0f;          // 大雨世界蓝灰暗幕透明度（0～255）
	constexpr float kLightRainVolume = 0.30f;            // 小雨循环音效基础音量（0～1，仍受全局音量控制）
	constexpr float kMediumRainVolume = 0.40f;           // 中雨循环音效基础音量（0～1，仍受全局音量控制）
	constexpr float kHeavyRainVolume = 0.60f;            // 大雨循环音效基础音量（0～1，仍受全局音量控制）
	constexpr float kLightSplashDelayMin = 0.08f;         // 小雨两次地面水花的最短间隔（秒，约每秒 5 次）
	constexpr float kLightSplashDelayMax = 0.09f;         // 小雨两次地面水花的最长间隔（秒，约每秒 5 次）
	constexpr float kMediumSplashDelayMin = 0.05f;        // 中雨两次地面水花的最短间隔（秒，约每秒 10 次）
	constexpr float kMediumSplashDelayMax = 0.07f;        // 中雨两次地面水花的最长间隔（秒，约每秒 10 次）
	constexpr float kHeavySplashDelayMin = 0.01f;         // 大雨两次地面水花的最短间隔（秒，约每秒 20多 次）
	constexpr float kHeavySplashDelayMax = 0.02f;         // 大雨两次地面水花的最长间隔（秒，约每秒 20多 次）
	constexpr float kRainSplashEdgePadding = 18.0f;       // 水花中心距草地网格边缘的安全距离（像素）
	constexpr float kNightRoofChargeLightningBonus = 18.0f; // 现有大雨闪电为黑夜屋顶一次注入的电荷点数
	constexpr float kLightningDelayMin = 3.5f;           // 大雨开始后首次闪电的最短等待时间（秒）
	constexpr float kLightningDelayMax = 7.0f;           // 大雨开始后首次闪电的最长等待时间（秒）
	constexpr float kLightningRepeatMin = 5.0f;          // 大雨中两次闪电的最短间隔（秒）
	constexpr float kLightningRepeatMax = 10.0f;         // 大雨中两次闪电的最长间隔（秒）
	constexpr float kLightningFlashDuration = 0.42f;     // 单次闪电主放电与回闪的总可见时间（秒）
	constexpr int kTyphoonChanceEarlyPercent = 75;       // 新大雨阶段附加台风的前期基础概率（百分比）
	constexpr int kTyphoonChanceLatePercent = 95;        // 满压力新大雨附加台风的基础概率（百分比）
	constexpr int kTyphoonPityPerMissPercent = 20;       // 每连续落空一个新大雨阶段，下次台风概率增加的百分点
	constexpr int kTyphoonChanceMaxPercent = 100;        // 满压力大雨落空一次后，下次台风必定命中
	constexpr int kTyphoonPityMaxMisses = 4;             // 记入概率计算的最大连续落空次数，避免损坏存档放大整数
	constexpr int kTyphoonWeight = 40;                   // 命中台风后普通台风的相对权重
	constexpr int kSevereTyphoonWeight = 45;             // 命中台风后强台风的相对权重
	constexpr int kSuperTyphoonWeight = 45;              // 命中台风后超强台风的相对权重
	constexpr int kLateTyphoonWeight = 15;               // 满压力普通台风目标权重，仍保留少量温和结果
	constexpr int kLateSevereTyphoonWeight = 45;         // 满压力强台风目标权重
	constexpr int kLateSuperTyphoonWeight = 40;          // 满压力超强台风目标权重，明显增压但仍不过半
	constexpr float kWindDirectionDurationMin = 15.0f;   // 同一风向至少维持的游戏秒数
	constexpr float kWindDirectionDurationMax = 25.0f;   // 同一风向至多维持的游戏秒数
	constexpr float kSuperTyphoonDecayMin = 55.0f;       // 超强台风衰减为强台风前的最短持续时间（游戏秒）
	constexpr float kSuperTyphoonDecayMax = 70.0f;       // 超强台风衰减为强台风前的最长持续时间（游戏秒）
	constexpr float kSevereTyphoonDecayMin = 50.0f;      // 强台风衰减为普通台风前的最短持续时间（游戏秒）
	constexpr float kSevereTyphoonDecayMax = 65.0f;      // 强台风衰减为普通台风前的最长持续时间（游戏秒）
	constexpr float kTyphoonDecayMin = 50.0f;            // 普通台风完全消散前的最短持续时间（游戏秒）
	constexpr float kTyphoonDecayMax = 55.0f;            // 普通台风完全消散前的最长持续时间（游戏秒）
	constexpr float kTyphoonWindParticleInterval = 0.30f; // 普通台风风线发射间隔；越大视觉浓度越低（游戏秒）
	constexpr float kSevereWindParticleInterval = 0.20f; // 强台风风线发射间隔；用浓度表达强度（游戏秒）
	constexpr float kSuperWindParticleInterval = 0.10f;  // 超强台风风线发射间隔；三档中视觉浓度最高（游戏秒）
	constexpr float kWindParticleOriginPadding = 40.0f;  // 风线从逻辑画面左右边缘外生成的距离（像素）
	constexpr float kTyphoonGustWarningTime = 5.0f;      // 阵风前常驻实况进入警示色的秒数
	constexpr float kTyphoonPlantSlideDuration = 0.45f;  // 植物逻辑换格后画面平滑追赶的游戏秒数
	constexpr float kSevereGustDuration = 1.80f;         // 强台风单次阵风持续时间（游戏秒）
	constexpr float kSuperGustDuration = 2.50f;          // 超强台风单次阵风持续时间（游戏秒）
	constexpr float kGustPlantMoveProgressMin = 0.25f;   // 植物最早在阵风进度 25% 时结算，避免开始瞬移
	constexpr float kGustPlantMoveProgressMax = 0.75f;   // 植物最晚在阵风进度 75% 时结算，保留受力过程
	constexpr float kSevereGustZombiePeakSpeed = 80.0f;  // 强台风阵风吹动僵尸的峰值速度（像素/游戏秒）
	constexpr float kSuperGustZombiePeakSpeed = 120.0f;   // 超强台风阵风吹动僵尸的峰值速度（像素/游戏秒）
	constexpr float kGustZombieFrontLimitPadding = 60.0f; // 僵尸被吹向前线时距画面右缘的最大余量（像素）
	constexpr float kTyphoonGustIntervalMin = 15.0f;     // 普通台风阵风最短间隔（游戏秒）
	constexpr float kTyphoonGustIntervalMax = 30.0f;     // 普通台风阵风最长间隔（游戏秒）
	constexpr float kSevereGustIntervalMin = 8.0f;      // 强台风阵风最短间隔（游戏秒），阶段内通常触发 1～2 次
	constexpr float kSevereGustIntervalMax = 13.0f;      // 强台风阵风最长间隔；短于最短衰减时间以保证至少一次
	constexpr float kSuperGustIntervalMin = 9.0f;       // 超强台风阵风最短间隔；位移更远，频率略低于强台风
	constexpr float kSuperGustIntervalMax = 14.0f;       // 超强台风阵风最长间隔；短于最短衰减时间以保证至少一次
	constexpr int kTyphoonGustDistance = 0;              // 普通台风吹不动植物，仅保留顺逆风僵尸倍率
	constexpr int kSevereGustDistance = 1;               // 强台风每次吹动的整数格数
	constexpr int kSuperGustDistance = 2;                // 超强台风每次吹动的整数格数
	constexpr int kTyphoonMaxGusts = 0;                  // 普通台风不触发植物位移阵风，降低首次遇见的压迫感
	constexpr int kSevereMaxGusts = 1;                   // 强台风单个大雨阶段最多阵风次数
	constexpr int kSuperMaxGusts = 2;                    // 超强台风单个大雨阶段最多阵风次数
	constexpr float kTyphoonTailwindZombieMove = 1.10f;  // 普通台风顺风僵尸水平移动倍率（相对当前雨天）
	constexpr float kTyphoonHeadwindZombieMove = 0.90f;  // 普通台风逆风僵尸水平移动倍率（相对当前雨天）
	constexpr float kSevereTailwindZombieMove = 1.20f;   // 强台风顺风僵尸水平移动倍率（相对当前雨天）
	constexpr float kSevereHeadwindZombieMove = 0.80f;   // 强台风逆风僵尸水平移动倍率（相对当前雨天）
	constexpr float kSuperTailwindZombieMove = 1.30f;    // 超强台风顺风僵尸水平移动倍率（相对当前雨天）
	constexpr float kSuperHeadwindZombieMove = 0.70f;    // 超强台风逆风僵尸水平移动倍率（相对当前雨天）
	constexpr float kTyphoonTailwindBulletSpeed = 1.15f; // 普通台风顺风轻型植物子弹水平速度倍率
	constexpr float kTyphoonHeadwindBulletSpeed = 0.85f; // 普通台风逆风轻型植物子弹水平速度倍率
	constexpr float kSevereTailwindBulletSpeed = 1.25f;  // 强台风顺风轻型植物子弹水平速度倍率
	constexpr float kSevereHeadwindBulletSpeed = 0.75f;  // 强台风逆风轻型植物子弹水平速度倍率
	constexpr float kSuperTailwindBulletSpeed = 1.45f;   // 超强台风顺风轻型植物子弹水平速度倍率
	constexpr float kSuperHeadwindBulletSpeed = 0.55f;   // 超强台风逆风轻型植物子弹水平速度倍率
	constexpr float kTyphoonTailwindBulletDamage = 1.10f; // 普通台风顺风轻型植物子弹命中伤害倍率
	constexpr float kTyphoonHeadwindBulletDamage = 0.90f; // 普通台风逆风轻型植物子弹命中伤害倍率
	constexpr float kSevereTailwindBulletDamage = 1.15f;  // 强台风顺风轻型植物子弹命中伤害倍率
	constexpr float kSevereHeadwindBulletDamage = 0.85f;  // 强台风逆风轻型植物子弹命中伤害倍率
	constexpr float kSuperTailwindBulletDamage = 1.20f;   // 超强台风顺风轻型植物子弹命中伤害倍率
	constexpr float kSuperHeadwindBulletDamage = 0.80f;   // 超强台风逆风轻型植物子弹命中伤害倍率

	/** 返回雨势、低温形态与实时风向共同决定的降水特效名。 */
	const char* PrecipitationEffectName(RainIntensity intensity,
		WindDirection direction, bool snow)
	{
		if (snow) {
			switch (intensity) {
			case RainIntensity::LIGHT:  return "SnowLight";
			case RainIntensity::MEDIUM: return "SnowMedium";
			case RainIntensity::HEAVY:  return "SnowHeavy";
			case RainIntensity::CLEAR:  return "";
			}
		}
		switch (intensity) {
		case RainIntensity::LIGHT:  return "RainLight";
		case RainIntensity::MEDIUM: return "RainMedium";
		case RainIntensity::HEAVY:
			if (direction == WindDirection::TOWARD_HOUSE) return "RainHeavyTowardHouse";
			if (direction == WindDirection::TOWARD_FRONT) return "RainHeavyTowardFront";
			return "RainHeavy";
		case RainIntensity::CLEAR:  break;
		}
		return "";
	}

	/** 返回实时吹向对应的横向风线粒子名；名字必须匹配 XML 首个 Emitter。 */
	const char* WindEffectName(WindDirection direction)
	{
		switch (direction) {
		case WindDirection::TOWARD_HOUSE: return "WindTowardHouse";
		case WindDirection::TOWARD_FRONT: return "WindTowardFront";
		case WindDirection::NONE:         return "";
		}
		return "";
	}

	/** 按独立二选一结果返回风实际吹向；固定点数仅供 AutoTest 覆盖同向与换向分支。 */
	WindDirection WindDirectionForRoll(int directionRoll)
	{
		const int roll = directionRoll > 0 ? directionRoll : GameRandom::Range(1, 2);
		return roll == 1 ? WindDirection::TOWARD_HOUSE : WindDirection::TOWARD_FRONT;
	}

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

	struct NewWeatherWeights {
		int clear = 0;
		int light = 0;
		int medium = 0;
		int heavy = 0;

		int Total() const { return clear + light + medium + heavy; }
	};

	/** 返回新天气前沿的动态权重；满压力仍保留小/中雨，但不再连续续晴。 */
	NewWeatherWeights BuildNewWeatherWeights(float directorFactor)
	{
		return {
			LerpWeatherWeight(kClearHoldWeight, kLateClearHoldWeight, directorFactor),
			LerpWeatherWeight(kLightRainWeight, kLateLightRainWeight, directorFactor),
			LerpWeatherWeight(kMediumRainWeight, kLateMediumRainWeight, directorFactor),
			LerpWeatherWeight(kHeavyRainWeight, kLateHeavyRainWeight, directorFactor),
		};
	}

	struct TyphoonWeights {
		int normal = 0;
		int severe = 0;
		int super = 0;

		int Total() const { return normal + severe + super; }
	};

	/** 返回台风强度的动态权重；满压力强/超强合计 85%，避免只见无位移普通风。 */
	TyphoonWeights BuildTyphoonWeights(float directorFactor)
	{
		return {
			LerpWeatherWeight(kTyphoonWeight, kLateTyphoonWeight, directorFactor),
			LerpWeatherWeight(kSevereTyphoonWeight, kLateSevereTyphoonWeight, directorFactor),
			LerpWeatherWeight(kSuperTyphoonWeight, kLateSuperTyphoonWeight, directorFactor),
		};
	}

	/** 按雨势与导演强度缩短低威胁新雨；大雨保持原持续时间。 */
	float RandomNewRainDuration(RainIntensity intensity, float directorFactor)
	{
		float minimum = kRainDurationMin;
		float maximum = kRainDurationMax;
		if (intensity == RainIntensity::LIGHT) {
			minimum = LerpWeatherValue(kRainDurationMin, kLateLightRainDurationMin, directorFactor);
			maximum = LerpWeatherValue(kRainDurationMax, kLateLightRainDurationMax, directorFactor);
		}
		else if (intensity == RainIntensity::MEDIUM) {
			minimum = LerpWeatherValue(kRainDurationMin, kLateMediumRainDurationMin, directorFactor);
			maximum = LerpWeatherValue(kRainDurationMax, kLateMediumRainDurationMax, directorFactor);
		}
		return GameRandom::Range(minimum, maximum);
	}

	/** 按雨势与导演强度缩短低威胁尾雨；大雨续期仍使用原区间。 */
	float RandomTailRainDuration(RainIntensity intensity, float directorFactor)
	{
		float minimum = kRainTailDurationMin;
		float maximum = kRainTailDurationMax;
		if (intensity == RainIntensity::LIGHT) {
			minimum = LerpWeatherValue(kRainTailDurationMin, kLateLightRainTailMin, directorFactor);
			maximum = LerpWeatherValue(kRainTailDurationMax, kLateLightRainTailMax, directorFactor);
		}
		else if (intensity == RainIntensity::MEDIUM) {
			minimum = LerpWeatherValue(kRainTailDurationMin, kLateMediumRainTailMin, directorFactor);
			maximum = LerpWeatherValue(kRainTailDurationMax, kLateMediumRainTailMax, directorFactor);
		}
		return GameRandom::Range(minimum, maximum);
	}

	/** 返回台风强度对应的单次阵风位移格数。 */
	int TyphoonGustDistance(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON: return kTyphoonGustDistance;
		case TyphoonStrength::SEVERE:  return kSevereGustDistance;
		case TyphoonStrength::SUPER:   return kSuperGustDistance;
		case TyphoonStrength::NONE:    return 0;
		}
		return 0;
	}

	/** 返回台风强度对应的阶段阵风次数上限。 */
	int TyphoonMaxGusts(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON: return kTyphoonMaxGusts;
		case TyphoonStrength::SEVERE:  return kSevereMaxGusts;
		case TyphoonStrength::SUPER:   return kSuperMaxGusts;
		case TyphoonStrength::NONE:    return 0;
		}
		return 0;
	}

	/** 返回强度对应的一次阵风持续时间；普通台风没有可产生位移的阵风。 */
	float TyphoonGustDuration(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::SEVERE:  return kSevereGustDuration;
		case TyphoonStrength::SUPER:   return kSuperGustDuration;
		case TyphoonStrength::TYPHOON:
		case TyphoonStrength::NONE:    return 0.0f;
		}
		return 0.0f;
	}

	/** 返回阵风吹动僵尸的峰值速度；实际速度还会乘平滑起落包络。 */
	float TyphoonGustZombiePeakSpeed(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::SEVERE:  return kSevereGustZombiePeakSpeed;
		case TyphoonStrength::SUPER:   return kSuperGustZombiePeakSpeed;
		case TyphoonStrength::TYPHOON:
		case TyphoonStrength::NONE:    return 0.0f;
		}
		return 0.0f;
	}

	/** 按台风强度抽取下一次阵风间隔。 */
	float RandomTyphoonGustInterval(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON:
			return GameRandom::Range(kTyphoonGustIntervalMin, kTyphoonGustIntervalMax);
		case TyphoonStrength::SEVERE:
			return GameRandom::Range(kSevereGustIntervalMin, kSevereGustIntervalMax);
		case TyphoonStrength::SUPER:
			return GameRandom::Range(kSuperGustIntervalMin, kSuperGustIntervalMax);
		case TyphoonStrength::NONE:
			return 0.0f;
		}
		return 0.0f;
	}

	/** 以发射频率而不是另做贴图区分台风强度，保证视觉浓度只需在此集中调参。 */
	float TyphoonWindParticleInterval(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON: return kTyphoonWindParticleInterval;
		case TyphoonStrength::SEVERE:  return kSevereWindParticleInterval;
		case TyphoonStrength::SUPER:   return kSuperWindParticleInterval;
		case TyphoonStrength::NONE:    return 0.0f;
		}
		return 0.0f;
	}

	/** 按当前强度抽取单向衰减阶段时长；台风不会在同一场大雨中反向增强。 */
	float RandomTyphoonStrengthDuration(TyphoonStrength strength)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON:
			return GameRandom::Range(kTyphoonDecayMin, kTyphoonDecayMax);
		case TyphoonStrength::SEVERE:
			return GameRandom::Range(kSevereTyphoonDecayMin, kSevereTyphoonDecayMax);
		case TyphoonStrength::SUPER:
			return GameRandom::Range(kSuperTyphoonDecayMin, kSuperTyphoonDecayMax);
		case TyphoonStrength::NONE:
			return 0.0f;
		}
		return 0.0f;
	}

	/** 返回台风强度对应的顺风/逆风僵尸水平移动倍率。 */
	float TyphoonZombieMoveMultiplier(TyphoonStrength strength, bool tailwind)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON:
			return tailwind ? kTyphoonTailwindZombieMove : kTyphoonHeadwindZombieMove;
		case TyphoonStrength::SEVERE:
			return tailwind ? kSevereTailwindZombieMove : kSevereHeadwindZombieMove;
		case TyphoonStrength::SUPER:
			return tailwind ? kSuperTailwindZombieMove : kSuperHeadwindZombieMove;
		case TyphoonStrength::NONE:
			return 1.0f;
		}
		return 1.0f;
	}

	/** 返回台风强度对应的顺风/逆风轻型植物子弹水平速度倍率。 */
	float TyphoonPlantBulletSpeedMultiplier(TyphoonStrength strength, bool tailwind)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON:
			return tailwind ? kTyphoonTailwindBulletSpeed : kTyphoonHeadwindBulletSpeed;
		case TyphoonStrength::SEVERE:
			return tailwind ? kSevereTailwindBulletSpeed : kSevereHeadwindBulletSpeed;
		case TyphoonStrength::SUPER:
			return tailwind ? kSuperTailwindBulletSpeed : kSuperHeadwindBulletSpeed;
		case TyphoonStrength::NONE:
			return 1.0f;
		}
		return 1.0f;
	}

	/** 返回台风强度对应的顺风/逆风轻型植物子弹命中伤害倍率。 */
	float TyphoonPlantBulletDamageMultiplier(TyphoonStrength strength, bool tailwind)
	{
		switch (strength) {
		case TyphoonStrength::TYPHOON:
			return tailwind ? kTyphoonTailwindBulletDamage : kTyphoonHeadwindBulletDamage;
		case TyphoonStrength::SEVERE:
			return tailwind ? kSevereTailwindBulletDamage : kSevereHeadwindBulletDamage;
		case TyphoonStrength::SUPER:
			return tailwind ? kSuperTailwindBulletDamage : kSuperHeadwindBulletDamage;
		case TyphoonStrength::NONE:
			return 1.0f;
		}
		return 1.0f;
	}

	/** 返回指定雨势在当前后期压力下的僵尸速度倍率，供天气过渡插值复用。 */
	float ZombieSpeedForRain(RainIntensity intensity, float pressureFactor)
	{
		float baseSpeed = 1.0f;
		float pressuredSpeed = 1.0f;
		switch (intensity) {
		case RainIntensity::LIGHT:
			baseSpeed = kLightZombieSpeed;
			pressuredSpeed = kPressuredLightZombieSpeed;
			break;
		case RainIntensity::MEDIUM:
			baseSpeed = kMediumZombieSpeed;
			pressuredSpeed = kPressuredMediumZombieSpeed;
			break;
		case RainIntensity::HEAVY:
			baseSpeed = kHeavyZombieSpeed;
			pressuredSpeed = kPressuredHeavyZombieSpeed;
			break;
		case RainIntensity::CLEAR:
			break;
		}
		const float pressure = std::clamp(pressureFactor, 0.0f, 1.0f);
		return baseSpeed + (pressuredSpeed - baseSpeed) * pressure;
	}

	/** 返回指定雨势在当前压力下的植物行动倍率，供天气过渡插值复用。 */
	float PlantSpeedForRain(RainIntensity intensity, float pressureFactor)
	{
		float baseSpeed = 1.0f;
		float pressuredSpeed = 1.0f;
		switch (intensity) {
		case RainIntensity::LIGHT:
			baseSpeed = kLightPlantActionSpeed;
			pressuredSpeed = kPressuredLightPlantActionSpeed;
			break;
		case RainIntensity::MEDIUM:
			baseSpeed = kMediumPlantActionSpeed;
			pressuredSpeed = kPressuredMediumPlantActionSpeed;
			break;
		case RainIntensity::HEAVY:
			baseSpeed = kHeavyPlantActionSpeed;
			pressuredSpeed = kPressuredHeavyPlantActionSpeed;
			break;
		case RainIntensity::CLEAR:
			break;
		}
		const float pressure = std::clamp(pressureFactor, 0.0f, 1.0f);
		return baseSpeed + (pressuredSpeed - baseSpeed) * pressure;
	}

	/** 返回指定雨势的世界暗幕 alpha，供过渡插值复用。 */
	float OverlayAlphaForRain(RainIntensity intensity)
	{
		switch (intensity) {
		case RainIntensity::LIGHT:  return kLightOverlayAlpha;
		case RainIntensity::MEDIUM: return kMediumOverlayAlpha;
		case RainIntensity::HEAVY:  return kHeavyOverlayAlpha;
		case RainIntensity::CLEAR:  return 0.0f;
		}
		return 0.0f;
	}

	/** 返回指定雨势的雨声请求音量，晴天为 0。 */
	float RainVolumeForIntensity(RainIntensity intensity)
	{
		switch (intensity) {
		case RainIntensity::LIGHT:  return kLightRainVolume;
		case RainIntensity::MEDIUM: return kMediumRainVolume;
		case RainIntensity::HEAVY:  return kHeavyRainVolume;
		case RainIntensity::CLEAR:  return 0.0f;
		}
		return 0.0f;
	}

	/** 按当前雨势抽取下一次地面水花间隔；雨越大，水花越频繁。 */
	float RandomRainSplashDelay(RainIntensity intensity)
	{
		switch (intensity) {
		case RainIntensity::LIGHT:
			return GameRandom::Range(kLightSplashDelayMin, kLightSplashDelayMax);
		case RainIntensity::MEDIUM:
			return GameRandom::Range(kMediumSplashDelayMin, kMediumSplashDelayMax);
		case RainIntensity::HEAVY:
			return GameRandom::Range(kHeavySplashDelayMin, kHeavySplashDelayMax);
		case RainIntensity::CLEAR:
			return 0.0f;
		}
		return 0.0f;
	}

	int RainTransitionWeightTotal(RainIntensity intensity, bool canIntensify,
		bool canHold, float lateFactor)
	{
		switch (intensity) {
		case RainIntensity::LIGHT:  return canIntensify ? kLightTransitionWeightTotal : 0;
		case RainIntensity::MEDIUM:
			return LerpWeatherWeight(kMediumToLightWeight, kLateMediumToLightWeight, lateFactor)
				+ LerpWeatherWeight(kMediumToClearWeight, kLateMediumToClearWeight, lateFactor)
				+ (canHold ? kMediumHoldWeight : 0);
		case RainIntensity::HEAVY:
			return LerpWeatherWeight(kHeavyToMediumWeight, kLateHeavyToMediumWeight, lateFactor)
				+ LerpWeatherWeight(kHeavyToLightWeight, kLateHeavyToLightWeight, lateFactor)
				+ LerpWeatherWeight(kHeavyToClearWeight, kLateHeavyToClearWeight, lateFactor)
				+ (canHold ? kHeavyHoldWeight : 0);
		case RainIntensity::CLEAR:  return 0;
		}
		return 0;
	}

	/** 解析动态权重落点；初始小雨可增强，切档后只会继续衰减。 */
	RainIntensity RainTransitionForRoll(RainIntensity intensity, bool canIntensify,
		bool canHold, float lateFactor, int roll)
	{
		switch (intensity) {
		case RainIntensity::LIGHT:
			if (!canIntensify) return RainIntensity::CLEAR;
			if (roll <= kLightToMediumWeight) return RainIntensity::MEDIUM;
			if (roll <= kLightToMediumWeight + kLightToHeavyWeight) return RainIntensity::HEAVY;
			return RainIntensity::CLEAR;
		case RainIntensity::MEDIUM: {
			const int toLight = LerpWeatherWeight(
				kMediumToLightWeight, kLateMediumToLightWeight, lateFactor);
			const int toClear = LerpWeatherWeight(
				kMediumToClearWeight, kLateMediumToClearWeight, lateFactor);
			if (roll <= toLight) return RainIntensity::LIGHT;
			if (roll <= toLight + toClear) return RainIntensity::CLEAR;
			return canHold ? RainIntensity::MEDIUM : RainIntensity::CLEAR;
		}
		case RainIntensity::HEAVY: {
			const int toMedium = LerpWeatherWeight(
				kHeavyToMediumWeight, kLateHeavyToMediumWeight, lateFactor);
			const int toLight = LerpWeatherWeight(
				kHeavyToLightWeight, kLateHeavyToLightWeight, lateFactor);
			const int toClear = LerpWeatherWeight(
				kHeavyToClearWeight, kLateHeavyToClearWeight, lateFactor);
			if (roll <= toMedium) return RainIntensity::MEDIUM;
			if (roll <= toMedium + toLight) return RainIntensity::LIGHT;
			if (roll <= toMedium + toLight + toClear) return RainIntensity::CLEAR;
			return canHold ? RainIntensity::HEAVY : RainIntensity::CLEAR;
		}
		case RainIntensity::CLEAR:
			return RainIntensity::CLEAR;
		}
		return RainIntensity::CLEAR;
	}

	/** 枚举当前动态权重真正允许的下一天气，错误预报只能从这些候选中选择。 */
	int BuildPlausibleForecasts(RainIntensity current, bool canIntensify, bool canHold,
		float directorFactor, bool forceHeavy, std::array<RainIntensity, 4>& forecasts)
	{
		int count = 0;
		switch (current) {
		case RainIntensity::CLEAR: {
			if (forceHeavy) {
				forecasts[0] = RainIntensity::HEAVY;
				return 1;
			}
			const NewWeatherWeights weights = BuildNewWeatherWeights(directorFactor);
			if (weights.clear > 0) forecasts[count++] = RainIntensity::CLEAR;
			if (weights.light > 0) forecasts[count++] = RainIntensity::LIGHT;
			if (weights.medium > 0) forecasts[count++] = RainIntensity::MEDIUM;
			if (weights.heavy > 0) forecasts[count++] = RainIntensity::HEAVY;
			return count;
		}
		case RainIntensity::LIGHT:
			if (!canIntensify) {
				forecasts[0] = RainIntensity::CLEAR;
				return 1;
			}
			forecasts = { RainIntensity::MEDIUM, RainIntensity::HEAVY, RainIntensity::CLEAR };
			return 3;
		case RainIntensity::MEDIUM: {
			const int toLight = LerpWeatherWeight(
				kMediumToLightWeight, kLateMediumToLightWeight, directorFactor);
			const int toClear = LerpWeatherWeight(
				kMediumToClearWeight, kLateMediumToClearWeight, directorFactor);
			if (toLight > 0) forecasts[count++] = RainIntensity::LIGHT;
			if (toClear > 0) forecasts[count++] = RainIntensity::CLEAR;
			if (canHold && kMediumHoldWeight > 0) forecasts[count++] = RainIntensity::MEDIUM;
			return count;
		}
		case RainIntensity::HEAVY: {
			const int toMedium = LerpWeatherWeight(
				kHeavyToMediumWeight, kLateHeavyToMediumWeight, directorFactor);
			const int toLight = LerpWeatherWeight(
				kHeavyToLightWeight, kLateHeavyToLightWeight, directorFactor);
			const int toClear = LerpWeatherWeight(
				kHeavyToClearWeight, kLateHeavyToClearWeight, directorFactor);
			if (toMedium > 0) forecasts[count++] = RainIntensity::MEDIUM;
			if (toLight > 0) forecasts[count++] = RainIntensity::LIGHT;
			if (toClear > 0) forecasts[count++] = RainIntensity::CLEAR;
			if (canHold && kHeavyHoldWeight > 0) forecasts[count++] = RainIntensity::HEAVY;
			return count;
		}
		}
		return 0;
	}
}

float Board::GetZombieRainSpeedMultiplier() const
{
	const float progress = GetWeatherTransitionProgress();
	const float pressure = GetWeatherPressureFactor();
	const float previous = ZombieSpeedForRain(mPreviousRainIntensity, pressure);
	return previous + (ZombieSpeedForRain(mRainIntensity, pressure) - previous) * progress;
}

/** 返回开局台风保护是否正在约束当前 Board；生存模式只保护第一轮。 */
bool Board::IsOpeningTyphoonProtectionActive() const
{
	if (!GameAPP::GetInstance().mOpeningTyphoonProtectionEnabled || !SupportsTyphoon()) {
		return false;
	}
	const bool isOpeningRound = !mIsSurvival || mSurvivalRound <= 1;
	return isOpeningRound && mCurrentWave < kOpeningTyphoonFirstEligibleWave;
}

/** 当前新大雨若进行台风判定时的实际概率，包含开局保护、波次成长与连续落空保底。 */
int Board::GetCurrentTyphoonChancePercent() const
{
	if (!SupportsTyphoon()) return 0;
	if (IsOpeningTyphoonProtectionActive()) return 0;
	const int baseChance = LerpWeatherWeight(kTyphoonChanceEarlyPercent,
		kTyphoonChanceLatePercent, GetWeatherDirectorFactor());
	return std::min(kTyphoonChanceMaxPercent,
		baseChance + mHeavyPhasesWithoutTyphoon * kTyphoonPityPerMissPercent);
}

float Board::GetZombieWindMoveMultiplier(bool movingTowardFront) const
{
	if (!HasTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| mWindDirection == WindDirection::NONE) return 1.0f;
	const bool tailwind = (movingTowardFront
		&& mWindDirection == WindDirection::TOWARD_FRONT)
		|| (!movingTowardFront && mWindDirection == WindDirection::TOWARD_HOUSE);
	return TyphoonZombieMoveMultiplier(mTyphoonStrength, tailwind);
}

float Board::GetPlantBulletWindSpeedMultiplier(bool movingTowardFront) const
{
	if (!HasTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| mWindDirection == WindDirection::NONE) return 1.0f;
	const bool tailwind = (movingTowardFront
		&& mWindDirection == WindDirection::TOWARD_FRONT)
		|| (!movingTowardFront && mWindDirection == WindDirection::TOWARD_HOUSE);
	return TyphoonPlantBulletSpeedMultiplier(mTyphoonStrength, tailwind);
}

float Board::GetPlantBulletWindDamageMultiplier(bool movingTowardFront) const
{
	if (!HasTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| mWindDirection == WindDirection::NONE) return 1.0f;
	const bool tailwind = (movingTowardFront
		&& mWindDirection == WindDirection::TOWARD_FRONT)
		|| (!movingTowardFront && mWindDirection == WindDirection::TOWARD_HOUSE);
	return TyphoonPlantBulletDamageMultiplier(mTyphoonStrength, tailwind);
}

bool Board::IsTyphoonGustWarning() const
{
	return HasTyphoon() && !mTyphoonGustActive && mTyphoonGustsRemaining > 0
		&& mWindGustTimer > 0.0f && mWindGustTimer <= kTyphoonGustWarningTime;
}

/**
 * 阵风速度采用 4p(1-p) 包络从零升至峰值再回落；魅惑只改变自主行走方向，
 * 不改变空气对物体施加的物理方向，因此敌对与魅惑僵尸共用同一有符号漂移。
 */
float Board::GetZombieGustDriftVelocity() const
{
	if (!mTyphoonGustActive || mRainIntensity != RainIntensity::HEAVY
		|| mActiveGustDuration <= 0.0f) return 0.0f;
	const float progress = std::clamp(
		1.0f - mActiveGustTimer / mActiveGustDuration, 0.0f, 1.0f);
	const float envelope = 4.0f * progress * (1.0f - progress);
	const float speed = TyphoonGustZombiePeakSpeed(mActiveGustStrength) * envelope;
	return mActiveGustDirection == WindDirection::TOWARD_FRONT ? speed : -speed;
}

float Board::GetZombieGustFrontLimit() const
{
	return static_cast<float>(SCENE_WIDTH) + kGustZombieFrontLimitPadding;
}

float Board::GetPlantRainActionSpeedMultiplier() const
{
	const float progress = GetWeatherTransitionProgress();
	const float pressure = GetWeatherPressureFactor();
	const float previous = PlantSpeedForRain(mPreviousRainIntensity, pressure);
	return previous + (PlantSpeedForRain(mRainIntensity, pressure) - previous) * progress;
}

float Board::GetRainOverlayAlpha() const
{
	const float progress = GetWeatherTransitionProgress();
	const float previous = OverlayAlphaForRain(mPreviousRainIntensity);
	return previous + (OverlayAlphaForRain(mRainIntensity) - previous) * progress;
}

bool Board::IsStormyNightForecastActive() const
{
	return mLevel == kStormyNightLevel && mCurrentWave == kStormyNightForecastWave;
}

bool Board::IsStormyNightActive() const
{
	return mLevel == kStormyNightLevel && mCurrentWave >= kStormyNightStartWave;
}

bool Board::IsStormyNightFlashOn() const
{
	if (!IsStormyNightActive()) return false;
	switch (mStormyNightFlashPattern) {
	case 1:
	case 2:
		return mStormyNightFlashTimer < kStormFlashUnitSeconds * 2.0f;
	case 3:
		return mStormyNightFlashTimer < kStormFlashUnitSeconds;
	default:
		return false;
	}
}

/**
 * 复刻 C# `DrawStormFlash` 的两层线性幕：闪光开始时先降低黑幕并叠白光，随后恢复全黑。
 * pattern 1 是强弱双闪，pattern 2 是三秒长闪，pattern 3 是一点五秒短闪。
 */
void Board::GetStormyNightOverlayAlphas(float& blackAlpha, float& whiteAlpha) const
{
	blackAlpha = 0.0f;
	whiteAlpha = 0.0f;
	if (!IsStormyNightActive()) return;
	blackAlpha = 255.0f;

	float flashTime = -1.0f;
	float maxAmount = 255.0f;
	switch (mStormyNightFlashPattern) {
	case 1:
		if (mStormyNightFlashTimer < kStormFlashUnitSeconds * 2.0f) {
			if (mStormyNightFlashTimer > kStormFlashUnitSeconds) {
				flashTime = mStormyNightFlashTimer - kStormFlashUnitSeconds;
			}
			else {
				flashTime = mStormyNightFlashTimer;
				maxAmount = 92.0f;
			}
		}
		break;
	case 2:
		if (mStormyNightFlashTimer < kStormFlashUnitSeconds * 2.0f) {
			flashTime = mStormyNightFlashTimer * 0.5f;
		}
		break;
	case 3:
		if (mStormyNightFlashTimer < kStormFlashUnitSeconds) {
			flashTime = mStormyNightFlashTimer;
		}
		break;
	default:
		break;
	}
	if (flashTime < 0.0f) return;

	flashTime = std::clamp(flashTime, 0.0f, kStormFlashUnitSeconds);
	blackAlpha = 255.0f - maxAmount * (flashTime / kStormFlashUnitSeconds);
	const float whiteStart = kStormFlashUnitSeconds * 0.5f;
	if (flashTime > whiteStart) {
		whiteAlpha = maxAmount * (flashTime - whiteStart)
			/ (kStormFlashUnitSeconds - whiteStart);
	}

	// 原版每六个计数抖动一次黑幕 alpha；这里用 Board 游戏帧哈希复刻闪烁且不消费出怪 RNG。
	std::uint32_t flickerState = static_cast<std::uint32_t>(mBoardFrame / 4);
	flickerState = flickerState * 1664525u + 1013904223u;
	const int flicker = static_cast<int>((flickerState >> 24) & 63u) - 32;
	blackAlpha = std::clamp(blackAlpha + static_cast<float>(flicker), 0.0f, 255.0f);
}

float Board::GetStormyNightBlackAlpha() const
{
	float blackAlpha = 0.0f;
	float whiteAlpha = 0.0f;
	GetStormyNightOverlayAlphas(blackAlpha, whiteAlpha);
	return blackAlpha;
}

float Board::GetStormyNightWhiteAlpha() const
{
	float blackAlpha = 0.0f;
	float whiteAlpha = 0.0f;
	GetStormyNightOverlayAlphas(blackAlpha, whiteAlpha);
	return whiteAlpha;
}

/** 后期强度按波次推进；无尽模式额外按轮次抬高下限，防止新一轮又退回早期天气。 */
float Board::GetWeatherLateGameFactor() const
{
	const float waveProgress = mMaxWave > 0
		? std::clamp(static_cast<float>(mCurrentWave) / static_cast<float>(mMaxWave), 0.0f, 1.0f)
		: 0.0f;
	float overallProgress = waveProgress;
	if (mIsSurvival && kSurvivalLateWeatherFullRound > 1) {
		const float roundProgress = std::clamp(
			static_cast<float>(mSurvivalRound - 1)
				/ static_cast<float>(kSurvivalLateWeatherFullRound - 1), 0.0f, 1.0f);
		overallProgress = std::max(overallProgress, roundProgress);
	}

	const float linear = std::clamp((overallProgress - kLateWeatherRampStart)
		/ (1.0f - kLateWeatherRampStart), 0.0f, 1.0f);
	return linear * linear * (3.0f - 2.0f * linear);
}

/**
 * 返回独立天气压力曲线；玩法倍率直接使用，天气导演再与既有场次成长取较大值。
 * 普通关从 40% 波次进度起成长、75% 时达到完整压力；黑夜无尽从第 8 轮平滑成长到第 20 轮。
 * 该值完全由已持久化的波次/轮次派生，不需要新增存档字段。
 */
float Board::GetWeatherPressureFactor() const
{
	if (!mIsSurvival) {
		if (kAdventurePressureFullProgress <= kLateWeatherRampStart) return 1.0f;
		const float waveProgress = mMaxWave > 0
			? std::clamp(static_cast<float>(mCurrentWave) / static_cast<float>(mMaxWave),
				0.0f, 1.0f)
			: 0.0f;
		const float linear = std::clamp((waveProgress - kLateWeatherRampStart)
			/ (kAdventurePressureFullProgress - kLateWeatherRampStart), 0.0f, 1.0f);
		return linear * linear * (3.0f - 2.0f * linear);
	}
	if (kSurvivalPressureFullRound <= kSurvivalPressureStartRound) return 1.0f;

	const float linear = std::clamp(
		static_cast<float>(mSurvivalRound - kSurvivalPressureStartRound)
			/ static_cast<float>(kSurvivalPressureFullRound - kSurvivalPressureStartRound),
		0.0f, 1.0f);
	return linear * linear * (3.0f - 2.0f * linear);
}

/** 天气导演取既有出现场次成长与独立压力曲线的较大值，避免任何旧后期关卡倒退。 */
float Board::GetWeatherDirectorFactor() const
{
	return std::max(GetWeatherLateGameFactor(), GetWeatherPressureFactor());
}

/** 后期更可靠但仍保留误报；主人的平衡上限固定为 90%。 */
int Board::GetCurrentWeatherForecastAccuracyPercent() const
{
	return LerpWeatherWeight(kWeatherForecastAccuracyPercent,
		kLateWeatherForecastAccuracyPercent, GetWeatherDirectorFactor());
}

int Board::GetCurrentNewWeatherWeight(RainIntensity intensity) const
{
	const NewWeatherWeights weights = BuildNewWeatherWeights(GetWeatherDirectorFactor());
	switch (intensity) {
	case RainIntensity::CLEAR:  return weights.clear;
	case RainIntensity::LIGHT:  return weights.light;
	case RainIntensity::MEDIUM: return weights.medium;
	case RainIntensity::HEAVY:  return weights.heavy;
	}
	return 0;
}

/** 满足导演阈值且上一轮为弱天气时，下一次晴天出发的天气前沿强制为大雨。 */
bool Board::ShouldForceHeavyWeather() const
{
	return mRainIntensity == RainIntensity::CLEAR
		&& GetWeatherDirectorFactor() >= kWeakWeatherPityStart
		&& mWeakWeatherPhasesSinceHeavy >= kWeakWeatherPityMax;
}

int Board::GetNextWeatherRollTotal() const
{
	if (mRainIntensity == RainIntensity::CLEAR) {
		if (ShouldForceHeavyWeather()) return 1;
		return BuildNewWeatherWeights(GetWeatherDirectorFactor()).Total();
	}
	return RainTransitionWeightTotal(mRainIntensity, mRainCanIntensify, mRainCanHold,
		GetWeatherDirectorFactor());
}

/** 只在晴天揭晓一个新天气前沿时记账；前期结果不会预存成后期保底。 */
void Board::RecordNewWeatherOutcome(RainIntensity next)
{
	if (GetWeatherDirectorFactor() < kWeakWeatherPityStart) {
		mWeakWeatherPhasesSinceHeavy = 0;
		return;
	}
	if (next == RainIntensity::HEAVY) {
		mWeakWeatherPhasesSinceHeavy = 0;
		return;
	}
	mWeakWeatherPhasesSinceHeavy = std::min(
		mWeakWeatherPhasesSinceHeavy + 1, kWeakWeatherPityMax);
}

/** 返回当前两秒天气过渡的平滑进度；无过渡时视为已经到达目标雨势。 */
float Board::GetWeatherTransitionProgress() const
{
	if (mWeatherTransitionTimer <= 0.0f) return 1.0f;
	const float linear = std::clamp(1.0f
		- mWeatherTransitionTimer / kWeatherTransitionDuration, 0.0f, 1.0f);
	return linear * linear * (3.0f - 2.0f * linear);
}

/** 雨声音量与暗幕、玩法倍率共用同一平滑进度。 */
float Board::GetRainAudioVolume() const
{
	if (SupportsWinterTemperature()
		&& mAmbientTemperatureC <= GetWinterFreezingTemperatureC()) return 0.0f;
	const float progress = GetWeatherTransitionProgress();
	const float previous = RainVolumeForIntensity(mPreviousRainIntensity);
	return previous + (RainVolumeForIntensity(mRainIntensity) - previous) * progress;
}

/** 立即把天气枚举切到目标，同时保留旧雨势供后续两秒插值。 */
void Board::BeginWeatherTransition(RainIntensity target)
{
	mPreviousRainIntensity = mRainIntensity;
	mRainIntensity = target;
	mWeatherTransitionTimer = mPreviousRainIntensity == target
		? 0.0f : kWeatherTransitionDuration;
}

/** 从存档恢复过渡；旧档、同雨势或归零状态直接按目标天气稳定显示。 */
void Board::RestoreWeatherTransition(RainIntensity previous, float remaining)
{
	mPreviousRainIntensity = previous;
	mWeatherTransitionTimer = std::clamp(remaining, 0.0f, kWeatherTransitionDuration);
	if (mWeatherTransitionTimer <= 0.0f || mPreviousRainIntensity == mRainIntensity) {
		mPreviousRainIntensity = mRainIntensity;
		mWeatherTransitionTimer = 0.0f;
	}
}

/** 推进倍率、暗幕和雨声的统一过渡；结束放晴过渡后才真正停止循环雨声。 */
void Board::UpdateWeatherTransition(float deltaTime)
{
	if (mWeatherTransitionTimer <= 0.0f) return;
	mWeatherTransitionTimer = std::max(0.0f, mWeatherTransitionTimer - deltaTime);
	RefreshZombieWeatherSpeeds();

	if (mRainIntensity != RainIntensity::CLEAR
		|| mPreviousRainIntensity != RainIntensity::CLEAR) {
		StartRainAudio();
	}
	if (mWeatherTransitionTimer <= 0.0f) {
		mPreviousRainIntensity = mRainIntensity;
		RefreshZombieWeatherSpeeds();
		if (mRainIntensity == RainIntensity::CLEAR) StopRainAudio();
		else StartRainAudio();
	}
}

/** AutoTest 固定状态使用：跳过视觉过渡并立即应用目标天气的最终倍率与声音。 */
void Board::FinishWeatherTransitionImmediately()
{
	mPreviousRainIntensity = mRainIntensity;
	mWeatherTransitionTimer = 0.0f;
	RefreshZombieWeatherSpeeds();
	if (mRainIntensity == RainIntensity::CLEAR) StopRainAudio();
	else StartRainAudio();
}

/** 第 23 波只执行一次：锁定大雨、大雾和强台风，并沿用强台风原有的一次阵风额度。 */
void Board::ActivateStormyNight()
{
	if (!IsStormyNightActive()) return;
	mStormyNightInitialized = true;
	// C# 4-10 入场从 StormFlash2 的中段开始；此后再进入随机三节奏循环。
	mStormyNightFlashPattern = 2;
	mStormyNightFlashTimer = kStormFlashUnitSeconds;
	ClampStormyNightWaveCountdown();

	BeginRain(RainIntensity::HEAVY, kStormyNightLockedDuration, false, false, false);
	// 暴风雨夜与第 23 波同步骤然降临，玩法倍率不沿用普通天气的两秒渐变。
	FinishWeatherTransitionImmediately();
	mLightningTimer = 0.0f;
	mWeakWeatherPhasesSinceHeavy = 0;
	mHeavyPhasesWithoutTyphoon = 0;

	mFogWeatherInitialized = true;
	mFogDispersal = 0.0f;
	mFogVisualOffsetX = 0.0f;
	BeginFogWeather(FogWeatherIntensity::DENSE, kStormyNightLockedDuration);

	StopTyphoon();
	mTyphoonStrength = TyphoonStrength::SEVERE;
	mWindDirection = WindDirectionForRoll(0);
	mTyphoonStrengthTimer = kStormyNightLockedDuration;
	mWindDirectionTimer = GameRandom::Range(
		kWindDirectionDurationMin, kWindDirectionDurationMax);
	mTyphoonGustsRemaining = kSevereMaxGusts;
	mWindGustTimer = RandomTyphoonGustInterval(mTyphoonStrength);
	mWindParticleTimer = 0.0f;
	RefreshZombieWeatherSpeeds();
	RestartRainVisualForWindChange();
}

/**
 * 暴风雨锁定由波次派生；读入旧档时补做一次初始化，新档则保留已消费的阵风与闪光计时。
 * 正常逐帧只修正可能被损坏档或测试入口破坏的组合，不会返还一次性阵风。
 */
void Board::EnforceStormyNightWeather()
{
	if (!IsStormyNightActive()) return;
	if (!mStormyNightInitialized) {
		ActivateStormyNight();
		return;
	}

	ClampStormyNightWaveCountdown();
	mWeatherInitialized = true;
	if (mRainIntensity != RainIntensity::HEAVY) {
		BeginRain(RainIntensity::HEAVY, kStormyNightLockedDuration, false, false, false);
		FinishWeatherTransitionImmediately();
	}
	mWeatherTimer = kStormyNightLockedDuration;
	mRainCanIntensify = false;
	mRainCanHold = false;
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mLightningTimer = 0.0f;
	ClearPendingHeavyRainWarning();

	mFogWeatherInitialized = true;
	if (mFogWeatherIntensity != FogWeatherIntensity::DENSE) {
		BeginFogWeather(FogWeatherIntensity::DENSE, kStormyNightLockedDuration);
	}
	mFogWeatherTimer = kStormyNightLockedDuration;
	ClearFogWeatherForecast();

	if (mTyphoonStrength != TyphoonStrength::SEVERE) {
		const WindDirection preservedDirection = mWindDirection;
		StopTyphoon();
		mTyphoonStrength = TyphoonStrength::SEVERE;
		mWindDirection = (preservedDirection == WindDirection::TOWARD_HOUSE
			|| preservedDirection == WindDirection::TOWARD_FRONT)
			? preservedDirection : WindDirectionForRoll(0);
		mWindDirectionTimer = GameRandom::Range(
			kWindDirectionDurationMin, kWindDirectionDurationMax);
		// 已初始化后的异常修复按阵风已消费处理，禁止天气切换或读档返还额度。
		mTyphoonGustsRemaining = 0;
		mWindGustTimer = 0.0f;
		mWindParticleTimer = 0.0f;
		RestartRainVisualForWindChange();
	}
	mTyphoonStrengthTimer = kStormyNightLockedDuration;
	if (mStormyNightFlashPattern < 1 || mStormyNightFlashPattern > 3
		|| mStormyNightFlashTimer <= 0.0f) {
		ScheduleNextStormyNightFlash();
	}
}

/** 按原版 4.5～9 秒范围安排下一种闪光节奏；随机结果和剩余时间都会进入关卡存档。 */
void Board::ScheduleNextStormyNightFlash()
{
	mStormyNightFlashPattern = GameRandom::Range(1, 3);
	const float maximumDelay = GameRandom::Range(0, 1) == 0
		? kStormFlashLongDelayMax : kStormFlashShortDelayMax;
	mStormyNightFlashTimer = kStormFlashUnitSeconds
		+ GameRandom::Range(kStormFlashDelayMin, maximumDelay);
}

/** 推进 C# 三种闪光节奏，并在主闪与 pattern 1 的回闪节点分别播放雷声。 */
void Board::UpdateStormyNightFlash(float deltaTime)
{
	if (!IsStormyNightActive() || deltaTime <= 0.0f) return;
	if (mStormyNightFlashTimer <= 0.0f) ScheduleNextStormyNightFlash();
	const float previous = mStormyNightFlashTimer;
	mStormyNightFlashTimer = std::max(0.0f, mStormyNightFlashTimer - deltaTime);
	const auto crossed = [previous, this](float threshold) {
		return previous > threshold && mStormyNightFlashTimer <= threshold;
	};

	bool playThunder = false;
	if (mStormyNightFlashPattern == 1) {
		playThunder = crossed(kStormFlashUnitSeconds * 2.0f)
			|| crossed(kStormFlashUnitSeconds);
	}
	else if (mStormyNightFlashPattern == 2) {
		playThunder = crossed(kStormFlashUnitSeconds * 2.0f);
	}
	else if (mStormyNightFlashPattern == 3) {
		playThunder = crossed(kStormFlashUnitSeconds);
	}
	if (playThunder) {
		PlayWeatherThunder();
	}
	if (mStormyNightFlashTimer <= 0.0f) ScheduleNextStormyNightFlash();
}

void Board::InitializeWeather()
{
	if (mWeatherInitialized) return;
	// 主进度由“当前雨势 + 一个复用倒计时”驱动：CLEAR 时倒计时代表距首场雨/下一场雨，
	// 下雨时则代表当前雨段剩余时间；另用布尔值记录初始小雨是否还拥有一次增强资格。
	// 第一大关把倒计时保持为 0；冒险第二大关起及三种生存地图均启用天气。
	mWeatherInitialized = true;
	mRainIntensity = RainIntensity::CLEAR;
	mPreviousRainIntensity = RainIntensity::CLEAR;
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;
	mLightningTimer = 0.0f;
	mRainSplashTimer = 0.0f;
	mRainCanIntensify = false;
	mRainCanHold = false;
	mWeatherTransitionTimer = 0.0f;
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mWeatherPanelInterferenceTimer = 0.0f;
	ClearPendingHeavyRainWarning();
	mRainVisualActive = false;
	mRainVisualEffectName.clear();
	mWeakWeatherPhasesSinceHeavy = 0;
	mHeavyPhasesWithoutTyphoon = 0;
	mRoofRunoffCharge = 0.0f;
	mRoofRunoffRetainedCharge = 0.0f;
	mRoofRunoffPhase = RoofRunoffPhase::IDLE;
	mRoofRunoffPhaseTimer = 0.0f;
	mRoofRunoffRowMask = 0;
	mNightRoofCharge = 0.0f;
	mNightRoofOvercharge = 0.0f;
	mNightRoofChargePhase = NightRoofChargePhase::CHARGING;
	mNightRoofChargePhaseTimer = 0.0f;
	mNightRoofChargeRow = -1;
	mNightRoofChargeGuided = false;
	mNightRoofChargeGuideID = NULL_ZOMBIE_ID;
	mNightRoofChargeRouteUsedMonteCarlo = false;
	mNightRoofChargeRouteStats = {};
	mNightRoofChargeRouteDecisionMicros = 0;
	mNightRoofHijackerSelectionAttempted = false;
	mNightRoofHijackerID = NULL_ZOMBIE_ID;
	mNightRoofHijackerWarningExtended = false;
	mNightRoofHijackerFinalizing = false;
	StopTyphoon();
	mWeatherTimer = SupportsWeather()
		? GameRandom::Range(kFirstRainDelayMin, kFirstRainDelayMax)
		: 0.0f;
}

void Board::RefreshZombieWeatherSpeeds()
{
	for (int id : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (zombie) zombie->RefreshAnimSpeedForWeather();
	}
}

void Board::EmitRainEffect(float duration)
{
	if (!g_particleSystem || mRainIntensity == RainIntensity::CLEAR || duration <= 0.0f) return;
	const std::string effectName = PrecipitationEffectName(
		mRainIntensity, mWindDirection, IsWinterPrecipitationSnow());
	if (effectName.empty()) return;
	if (!mRainVisualEffectName.empty() && mRainVisualEffectName != effectName) {
		// 风向或雨势切换时只停旧雨的发射器；在途雨丝保留到自然淡出。
		g_particleSystem->StopEffect(mRainVisualEffectName);
	}
	// Box 发射器以屏幕上沿中央为基准铺满逻辑画面；运行期时长覆盖 XML 上限，
	// 使随机雨长与读档剩余时间都能和玩法倍率同步结束。
	g_particleSystem->EmitEffect(effectName,
		Vector(static_cast<float>(SCENE_WIDTH) * 0.5f, -60.0f),
		LAYER_EFFECTS_WORLD, duration);
	mRainVisualActive = true;
	mRainVisualEffectName = effectName;
}

/** 台风开始、结束或翻向后按剩余雨时无缝切换定向雨丝。 */
void Board::RestartRainVisualForWindChange()
{
	if (mRainIntensity != RainIntensity::HEAVY || mWeatherTimer <= 0.0f) return;
	mRainVisualActive = false;
	EmitRainEffect(mWeatherTimer);
}

/** 在当前地形的逻辑网格内随机选择落点，播放短促的原版雨滴水花与扩散圆圈。 */
void Board::TriggerRainGroundSplash()
{
	if (!g_particleSystem || mRows <= 0 || mColumns <= 0) return;

	// 用完整网格边界而非窗口随机值，既覆盖战场又给 33px 水花留下屏内余量。
	const float minX = CELL_INITALIZE_POS_X + kRainSplashEdgePadding;
	const float maxX = CELL_INITALIZE_POS_X + mColumns * CELL_COLLIDER_SIZE_X
		- kRainSplashEdgePadding;
	const float minY = mCellInitialY + kRainSplashEdgePadding;
	const float maxY = mCellInitialY + mRows * mCellHeight
		- kRainSplashEdgePadding;
	if (maxX <= minX || maxY <= minY) return;

	const float splashX = GameRandom::Range(minX, maxX);
	float splashY = GameRandom::Range(minY, maxY);
	if (IsRoofBackground()) {
		// 屋顶每行是随 X 偏移的斜带；先选行，再在该行内部取局部 Y，避免矩形采样落到天空。
		const int row = GameRandom::Range(0, mRows - 1);
		const float halfHeight = mCellHeight * 0.5f;
		splashY = GetRowCenterYAtX(row, splashX) + GameRandom::Range(
			-halfHeight + kRainSplashEdgePadding,
			halfHeight - kRainSplashEdgePadding);
	}
	g_particleSystem->EmitEffect("RainGroundSplash", Vector(splashX, splashY),
		LAYER_EFFECTS_WORLD);
}

/** 推进地面水花节奏；计时器是纯视觉状态，雨势切换和读档后都会重新起拍。 */
void Board::UpdateRainGroundSplash(float deltaTime)
{
	if (mRainIntensity == RainIntensity::CLEAR || IsWinterPrecipitationSnow()) return;
	mRainSplashTimer -= deltaTime;
	if (mRainSplashTimer > 0.0f) return;

	TriggerRainGroundSplash();
	mRainSplashTimer = RandomRainSplashDelay(mRainIntensity);
}

bool Board::IsRainEffectEmitting() const
{
	return g_particleSystem && mRainIntensity != RainIntensity::CLEAR
		&& g_particleSystem->IsEffectEmitting(
			PrecipitationEffectName(mRainIntensity, mWindDirection,
				IsWinterPrecipitationSnow()));
}

void Board::StartRainAudio()
{
	if (SupportsWinterTemperature()
		&& mAmbientTemperatureC <= GetWinterFreezingTemperatureC()) {
		StopRainAudio();
		return;
	}
	if (mRainIntensity == RainIntensity::CLEAR
		&& (mWeatherTransitionTimer <= 0.0f
			|| mPreviousRainIntensity == RainIntensity::CLEAR)) return;
	// 原版 FoleyType.Rain 绑定 SOUND_RAIN 且带 Loop 标志；这里只把音量按雨势分层。
	AudioSystem::PlayLoopingSound(ResourceKeys::Sounds::SOUND_RAIN, GetRainAudioVolume());
}

void Board::StopRainAudio()
{
	AudioSystem::StopLoopingSound(ResourceKeys::Sounds::SOUND_RAIN);
}

/** 按当前阶段规则抽取下一天气；只由预警准备阶段调用一次。 */
RainIntensity Board::RollNextWeather(int forcedRoll)
{
	const float directorFactor = GetWeatherDirectorFactor();
	if (mRainIntensity == RainIntensity::CLEAR) {
		if (ShouldForceHeavyWeather()) return RainIntensity::HEAVY;
		const NewWeatherWeights weights = BuildNewWeatherWeights(directorFactor);
		const int total = weights.Total();
		const int roll = forcedRoll > 0 ? std::clamp(forcedRoll, 1, total)
			: GameRandom::Range(1, total);
		if (roll <= weights.clear) return RainIntensity::CLEAR;
		if (roll <= weights.clear + weights.light) return RainIntensity::LIGHT;
		if (roll <= weights.clear + weights.light + weights.medium) return RainIntensity::MEDIUM;
		return RainIntensity::HEAVY;
	}

	const int total = RainTransitionWeightTotal(
		mRainIntensity, mRainCanIntensify, mRainCanHold, directorFactor);
	if (total <= 0) return RainIntensity::CLEAR;
	const int roll = forcedRoll > 0 ? std::clamp(forcedRoll, 1, total)
		: GameRandom::Range(1, total);
	return RainTransitionForRoll(
		mRainIntensity, mRainCanIntensify, mRainCanHold, directorFactor, roll);
}

/** 锁定真实天气，并按 75%～90% 动态准确率生成公开预报。 */
void Board::PrepareWeatherForecast(int weatherRoll)
{
	if (mWeatherForecastReady) return;
	mWeatherForecastDisrupted = false;
	ClearPendingHeavyRainWarning();
	mActualForecastRainIntensity = RollNextWeather(weatherRoll);
	mForecastRainIntensity = mActualForecastRainIntensity;

	// 错误预报仍须来自当前非零权重候选；弱天气保底或确定性尾段没有错误候选时强制报准。
	if (GameRandom::Range(1, 100) > GetCurrentWeatherForecastAccuracyPercent()) {
		std::array<RainIntensity, 4> plausible{};
		const int plausibleCount = BuildPlausibleForecasts(
			mRainIntensity, mRainCanIntensify, mRainCanHold,
			GetWeatherDirectorFactor(), ShouldForceHeavyWeather(), plausible);
		std::array<RainIntensity, 4> wrongForecasts{};
		int wrongCount = 0;
		for (int i = 0; i < plausibleCount; ++i) {
			if (plausible[i] != mActualForecastRainIntensity) {
				wrongForecasts[wrongCount++] = plausible[i];
			}
		}
		if (wrongCount > 0) {
			mForecastRainIntensity = wrongForecasts[GameRandom::Range(0, wrongCount - 1)];
		}
	}
	mWeatherForecastReady = true;
	PreparePendingHeavyTyphoon();
}

/** 判断本次预警是否需要锁定大雨等级：公开报大雨用于真假一致的警报，真实新大雨用于切档。 */
bool Board::NeedsPendingHeavyForecastState() const
{
	return mWeatherForecastReady
		&& (mForecastRainIntensity == RainIntensity::HEAVY
			|| (mActualForecastRainIntensity == RainIntensity::HEAVY
				&& mRainIntensity != RainIntensity::HEAVY));
}

/**
 * 公开预报为大雨时锁定同分布的警报等级；真实下一段为新大雨时同一状态也是待生效台风初态。
 * 这里只消费随机数，不提前改变当前天气、台风保底或玩法倍率；只有真实新大雨切档才兑现结果。
 */
void Board::PreparePendingHeavyTyphoon(int chanceRoll, int strengthRoll)
{
	if (mPendingHeavyTyphoonPrepared || !NeedsPendingHeavyForecastState()) return;

	mPendingHeavyTyphoonPrepared = true;
	mPendingHeavyTyphoonStrength = TyphoonStrength::NONE;
	mPendingHeavyWindDirection = WindDirection::NONE;
	mPendingHeavyTyphoonStrengthTimer = 0.0f;
	mPendingHeavyWindDirectionTimer = 0.0f;
	mPendingHeavyWindGustTimer = 0.0f;
	mPendingHeavyTyphoonGustsRemaining = 0;
	mPendingHeavyRainPromptVariant = GameRandom::Range(0, 2);
	if (!SupportsTyphoon()) return;
	mPendingHeavyTyphoonOpeningProtected = IsOpeningTyphoonProtectionActive();
	if (mPendingHeavyTyphoonOpeningProtected) return;

	const int chance = GetCurrentTyphoonChancePercent();
	if (chanceRoll <= 0) chanceRoll = GameRandom::Range(1, 100);
	if (chanceRoll > chance) return;

	const TyphoonWeights weights = BuildTyphoonWeights(GetWeatherDirectorFactor());
	const int totalWeight = weights.Total();
	const int roll = strengthRoll > 0
		? std::clamp(strengthRoll, 1, totalWeight)
		: GameRandom::Range(1, totalWeight);
	if (roll <= weights.normal) {
		mPendingHeavyTyphoonStrength = TyphoonStrength::TYPHOON;
	}
	else if (roll <= weights.normal + weights.severe) {
		mPendingHeavyTyphoonStrength = TyphoonStrength::SEVERE;
	}
	else {
		mPendingHeavyTyphoonStrength = TyphoonStrength::SUPER;
	}
	mPendingHeavyWindDirection = WindDirectionForRoll(0);
	mPendingHeavyTyphoonStrengthTimer =
		RandomTyphoonStrengthDuration(mPendingHeavyTyphoonStrength);
	mPendingHeavyWindDirectionTimer = GameRandom::Range(
		kWindDirectionDurationMin, kWindDirectionDurationMax);
	mPendingHeavyTyphoonGustsRemaining =
		TyphoonMaxGusts(mPendingHeavyTyphoonStrength);
	mPendingHeavyWindGustTimer = mPendingHeavyTyphoonGustsRemaining > 0
		? RandomTyphoonGustInterval(mPendingHeavyTyphoonStrength) : 0.0f;
}

/** 清除上一份大雨预警及其待生效台风状态，避免后续天气误消费旧结果。 */
void Board::ClearPendingHeavyRainWarning()
{
	mPendingHeavyTyphoonPrepared = false;
	mPendingHeavyTyphoonOpeningProtected = false;
	mPendingHeavyTyphoonStrength = TyphoonStrength::NONE;
	mPendingHeavyWindDirection = WindDirection::NONE;
	mPendingHeavyTyphoonStrengthTimer = 0.0f;
	mPendingHeavyWindDirectionTimer = 0.0f;
	mPendingHeavyWindGustTimer = 0.0f;
	mPendingHeavyTyphoonGustsRemaining = 0;
	mPendingHeavyRainPromptVariant = 0;
	mHeavyRainPromptShown = false;
}

/** 公开预报为大雨时，在揭晓前最后 5 个游戏秒显示一次已锁定等级的风暴警报。 */
void Board::MaybeShowHeavyRainPrompt()
{
	if (mHeavyRainPromptShown || !mWeatherForecastReady
		|| mWeatherForecastDisrupted
		|| IsWeatherPanelInterferenceActive()
		|| mForecastRainIntensity != RainIntensity::HEAVY
		|| mWeatherTimer > kHeavyRainPromptLeadTime || !mPresentation) return;
	if (!mPendingHeavyTyphoonPrepared) PreparePendingHeavyTyphoon();
	if (!mPendingHeavyTyphoonPrepared) return;
	mPresentation->ShowHeavyRainWarning(
		mPendingHeavyTyphoonStrength, mPendingHeavyRainPromptVariant);
	mHeavyRainPromptShown = true;
}

/** 检查公开预报是否落在当前状态机的合法候选中，供界面诊断与 AutoTest 使用。 */
bool Board::IsWeatherForecastPlausible() const
{
	if (!mWeatherForecastReady) return true;
	std::array<RainIntensity, 4> plausible{};
	const int plausibleCount = BuildPlausibleForecasts(
		mRainIntensity, mRainCanIntensify, mRainCanHold,
		GetWeatherDirectorFactor(), ShouldForceHeavyWeather(), plausible);
	return std::find(plausible.begin(), plausible.begin() + plausibleCount,
		mForecastRainIntensity) != plausible.begin() + plausibleCount;
}

/** 揭晓锁定的真实天气；雨势或台风等级错误时通知场景显示非模态失败提示。 */
void Board::ConsumeWeatherForecast()
{
	if (!mWeatherForecastReady) PrepareWeatherForecast();
	const RainIntensity forecast = mForecastRainIntensity;
	const RainIntensity next = mActualForecastRainIntensity;
	// 大雨续期不会兑现新抽取的 pending 台风，实际等级仍是揭晓时正在生效的台风。
	// 新大雨则会消费同一份 pending 初态，因此预报和实际等级在正常路径中保持一致。
	const TyphoonStrength forecastTyphoon = forecast == RainIntensity::HEAVY
		&& mPendingHeavyTyphoonPrepared
		? mPendingHeavyTyphoonStrength : TyphoonStrength::NONE;
	const TyphoonStrength actualTyphoon = next == RainIntensity::HEAVY
		? (mRainIntensity == RainIntensity::HEAVY
			? mTyphoonStrength
			: (mPendingHeavyTyphoonPrepared
				? mPendingHeavyTyphoonStrength : TyphoonStrength::NONE))
		: TyphoonStrength::NONE;
	if (!mWeatherForecastDisrupted && !IsWeatherPanelInterferenceActive()
		&& (forecast != next || forecastTyphoon != actualTyphoon) && mPresentation) {
		mPresentation->ShowWeatherForecastFailure(
			forecast, next, forecastTyphoon, actualTyphoon);
	}
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;

	if (mRainIntensity == RainIntensity::CLEAR) {
		RecordNewWeatherOutcome(next);
		if (next == RainIntensity::CLEAR) {
			// 损坏存档或未知枚举的保守兜底：重新进入晴空间隔，避免空雨段循环。
			EndRain();
			return;
		}
		BeginRain(next, RandomNewRainDuration(next, GetWeatherDirectorFactor()),
			next == RainIntensity::LIGHT, true);
		return;
	}

	if (next == RainIntensity::CLEAR) {
		EndRain();
		return;
	}
	// 同档续期或真正切档都会消费本场唯一续期资格，后续只走有界衰减链。
	BeginRain(next, RandomTailRainDuration(next, GetWeatherDirectorFactor()), false, false);
}

/**
 * 新大雨阶段只判定一次是否附加台风，并锁定强度、初始方向和本阶段阵风预算。
 * 正式流程传 0 使用随机点数；AutoTest 可传 1-based 固定点数验证概率、保底和强度边界。
 */
void Board::StartTyphoonForHeavyPhase(int chanceRoll, int strengthRoll,
	WindDirection forcedDirection)
{
	StopTyphoon();
	if (!SupportsTyphoon()) return;
	// 保护期不是一次随机落空，不能累计台风 pity，否则第 6 波会被反向推成近似必出。
	if (IsOpeningTyphoonProtectionActive()) return;
	const int chance = GetCurrentTyphoonChancePercent();
	if (chanceRoll <= 0) chanceRoll = GameRandom::Range(1, 100);
	if (chanceRoll > chance) {
		mHeavyPhasesWithoutTyphoon = std::min(
			mHeavyPhasesWithoutTyphoon + 1, kTyphoonPityMaxMisses);
		return;
	}
	mHeavyPhasesWithoutTyphoon = 0;

	const TyphoonWeights weights = BuildTyphoonWeights(GetWeatherDirectorFactor());
	const int totalWeight = weights.Total();
	const int roll = strengthRoll > 0 ? strengthRoll : GameRandom::Range(1, totalWeight);
	if (roll <= weights.normal) {
		mTyphoonStrength = TyphoonStrength::TYPHOON;
	}
	else if (roll <= weights.normal + weights.severe) {
		mTyphoonStrength = TyphoonStrength::SEVERE;
	}
	else {
		mTyphoonStrength = TyphoonStrength::SUPER;
	}
	const bool validForcedDirection = forcedDirection == WindDirection::TOWARD_HOUSE
		|| forcedDirection == WindDirection::TOWARD_FRONT;
	mWindDirection = validForcedDirection ? forcedDirection : WindDirectionForRoll(0);
	mTyphoonStrengthTimer = RandomTyphoonStrengthDuration(mTyphoonStrength);
	mWindDirectionTimer = GameRandom::Range(
		kWindDirectionDurationMin, kWindDirectionDurationMax);
	mTyphoonGustsRemaining = TyphoonMaxGusts(mTyphoonStrength);
	mWindGustTimer = mTyphoonGustsRemaining > 0
		? RandomTyphoonGustInterval(mTyphoonStrength) : 0.0f;
	RefreshZombieWeatherSpeeds();
}

/**
 * 把预警期锁定的台风完整状态兑现到刚开始的大雨。
 * 台风保底只在此处更新，因此退出或读档不会把尚未来临的大雨提前计入结果。
 */
void Board::ConsumePendingHeavyTyphoon()
{
	if (!SupportsTyphoon()) {
		ClearPendingHeavyRainWarning();
		StopTyphoon();
		return;
	}
	if (!mPendingHeavyTyphoonPrepared) {
		StartTyphoonForHeavyPhase();
		return;
	}
	const TyphoonStrength strength = mPendingHeavyTyphoonStrength;
	const WindDirection direction = mPendingHeavyWindDirection;
	const float strengthTimer = mPendingHeavyTyphoonStrengthTimer;
	const float gustTimer = mPendingHeavyWindGustTimer;
	const float directionTimer = mPendingHeavyWindDirectionTimer;
	const int gustsRemaining = mPendingHeavyTyphoonGustsRemaining;
	const bool openingProtected = mPendingHeavyTyphoonOpeningProtected;
	ClearPendingHeavyRainWarning();

	if (strength == TyphoonStrength::NONE) {
		StopTyphoon();
		if (!openingProtected) {
			mHeavyPhasesWithoutTyphoon = std::min(
				mHeavyPhasesWithoutTyphoon + 1, kTyphoonPityMaxMisses);
		}
		return;
	}
	mHeavyPhasesWithoutTyphoon = 0;
	RestoreTyphoonState(strength, direction,
		strengthTimer, gustTimer, directionTimer, gustsRemaining);
}

/** 夹紧并恢复会决定下一轮是否强制大雨的连续弱天气次数。 */
void Board::RestoreWeakWeatherPity(int weakWeatherPhases)
{
	mWeakWeatherPhasesSinceHeavy = std::clamp(
		weakWeatherPhases, 0, kWeakWeatherPityMax);
}

/** 夹紧并恢复会影响下次台风随机判定的连续落空次数。 */
void Board::RestoreTyphoonPity(int missedHeavyPhases)
{
	mHeavyPhasesWithoutTyphoon = std::clamp(
		missedHeavyPhases, 0, kTyphoonPityMaxMisses);
}

/** 从存档恢复大雨预报警报等级或尚未生效的真实台风结果；损坏组合只清空，不重 roll。 */
void Board::RestorePendingHeavyTyphoon(bool prepared, bool openingProtected,
	TyphoonStrength strength,
	WindDirection direction, float strengthTimer, float gustTimer,
	float directionTimer, int gustsRemaining, int promptVariant)
{
	ClearPendingHeavyRainWarning();
	if (!prepared || !NeedsPendingHeavyForecastState()) return;
	if (!SupportsTyphoon()) {
		mPendingHeavyTyphoonPrepared = true;
		mPendingHeavyRainPromptVariant = std::clamp(promptVariant, 0, 2);
		return;
	}
	const bool validStrength = strength >= TyphoonStrength::NONE
		&& strength <= TyphoonStrength::SUPER;
	const bool validDirection = strength == TyphoonStrength::NONE
		? direction == WindDirection::NONE
		: (direction == WindDirection::TOWARD_HOUSE
			|| direction == WindDirection::TOWARD_FRONT);
	if (!validStrength || !validDirection
		|| (openingProtected && strength != TyphoonStrength::NONE)) return;

	mPendingHeavyTyphoonPrepared = true;
	mPendingHeavyTyphoonOpeningProtected = openingProtected;
	mPendingHeavyTyphoonStrength = strength;
	mPendingHeavyWindDirection = direction;
	mPendingHeavyTyphoonStrengthTimer = strength == TyphoonStrength::NONE
		? 0.0f : std::max(0.0f, strengthTimer);
	mPendingHeavyWindDirectionTimer = strength == TyphoonStrength::NONE
		? 0.0f : std::max(0.0f, directionTimer);
	mPendingHeavyTyphoonGustsRemaining = strength == TyphoonStrength::NONE
		? 0 : std::clamp(gustsRemaining, 0, TyphoonMaxGusts(strength));
	mPendingHeavyWindGustTimer = mPendingHeavyTyphoonGustsRemaining > 0
		? std::max(0.0f, gustTimer) : 0.0f;
	mPendingHeavyRainPromptVariant = std::clamp(promptVariant, 0, 2);
}

/** 清空全部台风派生状态；中雨、小雨、晴天和旧档默认都以此为单位元。 */
void Board::StopTyphoon()
{
	mWindParticleTimer = 0.0f;
	mTyphoonStrength = TyphoonStrength::NONE;
	mWindDirection = WindDirection::NONE;
	mTyphoonStrengthTimer = 0.0f;
	mWindDirectionTimer = 0.0f;
	mWindGustTimer = 0.0f;
	mTyphoonGustsRemaining = 0;
	mTyphoonGustActive = false;
	mActiveGustStrength = TyphoonStrength::NONE;
	mActiveGustDirection = WindDirection::NONE;
	mActiveGustDuration = 0.0f;
	mActiveGustTimer = 0.0f;
	mActiveGustPlantMoveTimer = 0.0f;
	mActiveGustPlantMoved = false;
	mLastTyphoonMovedPlants = 0;
	mLastTyphoonLostPlants = 0;
	mLastTyphoonBlockedPlantSteps = 0;
	RefreshZombieWeatherSpeeds();
}

/** 从存档恢复已经判定过的台风结果；无效组合只会安全退化为无台风，不重新随机。 */
void Board::RestoreTyphoonState(TyphoonStrength strength, WindDirection direction,
	float strengthTimer, float gustTimer, float directionTimer, int gustsRemaining)
{
	StopTyphoon();
	if (!SupportsTyphoon()) return;
	const bool validStrength = strength == TyphoonStrength::TYPHOON
		|| strength == TyphoonStrength::SEVERE || strength == TyphoonStrength::SUPER;
	const bool validDirection = direction == WindDirection::TOWARD_HOUSE
		|| direction == WindDirection::TOWARD_FRONT;
	if (mRainIntensity != RainIntensity::HEAVY || !validStrength || !validDirection) return;

	mTyphoonStrength = strength;
	mWindDirection = direction;
	mTyphoonStrengthTimer = std::max(0.0f, strengthTimer);
	mWindGustTimer = std::max(0.0f, gustTimer);
	mWindDirectionTimer = std::max(0.0f, directionTimer);
	mTyphoonGustsRemaining = std::clamp(gustsRemaining, 0, TyphoonMaxGusts(strength));
	RefreshZombieWeatherSpeeds();
}

/**
 * 恢复一场已经开始的阵风。锁定值必须与当前台风一致；旧档和损坏组合保持非阵风状态，
 * 未结算植物的剩余时刻夹在阵风余时内，保证读档后至多结算一次。
 */
void Board::RestoreActiveTyphoonGust(bool active, TyphoonStrength strength,
	WindDirection direction, float duration, float remaining,
	float plantMoveRemaining, bool plantMoved)
{
	if (!active || !HasTyphoon() || strength != mTyphoonStrength
		|| direction != mWindDirection || TyphoonGustDistance(strength) <= 0
		|| duration <= 0.0f || remaining <= 0.0f) return;
	mTyphoonGustActive = true;
	mActiveGustStrength = strength;
	mActiveGustDirection = direction;
	mActiveGustDuration = duration;
	mActiveGustTimer = std::clamp(remaining, 0.0f, duration);
	mActiveGustPlantMoved = plantMoved;
	mActiveGustPlantMoveTimer = plantMoved ? 0.0f
		: std::clamp(plantMoveRemaining, 0.0f, mActiveGustTimer);
	mWindGustTimer = 0.0f;
}

/**
 * 台风只沿 SUPER→SEVERE→TYPHOON→NONE 衰减。风向保持不变，阵风预算只会被新上限截断而不补充，
 * 避免衰减反而给玩家追加惩罚；倍率与风线浓度从下一帧起自动采用新强度。
 */
void Board::WeakenTyphoon()
{
	if (!HasTyphoon()) return;
	TyphoonStrength next = TyphoonStrength::NONE;
	switch (mTyphoonStrength) {
	case TyphoonStrength::SUPER:    next = TyphoonStrength::SEVERE; break;
	case TyphoonStrength::SEVERE:   next = TyphoonStrength::TYPHOON; break;
	case TyphoonStrength::TYPHOON:
	case TyphoonStrength::NONE:     next = TyphoonStrength::NONE; break;
	}

	if (next == TyphoonStrength::NONE) {
		StopTyphoon();
		RestartRainVisualForWindChange();
		if (mPresentation && !IsWeatherPanelInterferenceActive()) {
			mPresentation->ShowCurrentWeatherNotice();
		}
		return;
	}
	mTyphoonStrength = next;
	mTyphoonStrengthTimer = RandomTyphoonStrengthDuration(next);
	mTyphoonGustsRemaining = std::min(mTyphoonGustsRemaining, TyphoonMaxGusts(next));
	mWindGustTimer = mTyphoonGustsRemaining > 0
		? RandomTyphoonGustInterval(next) : 0.0f;
	mWindParticleTimer = 0.0f;
	RefreshZombieWeatherSpeeds();
	if (mPresentation && !IsWeatherPanelInterferenceActive()) {
		mPresentation->ShowCurrentWeatherNotice();
	}
}

/**
 * 风向维持一段时间后独立重抽；允许继续吹向当前方向，避免固定左右交替被玩家预测。
 * 不在每次阵风临时重抽，玩家仍可根据面板中的实时方向应对当前阵风。
 */
void Board::RerollWindDirection(int directionRoll)
{
	if (!HasTyphoon()) return;
	const WindDirection previousDirection = mWindDirection;
	mWindDirection = WindDirectionForRoll(directionRoll);
	mWindDirectionTimer = GameRandom::Range(
		kWindDirectionDurationMin, kWindDirectionDurationMax);
	if (mWindDirection == previousDirection) return;
	// 下一帧立即发射新方向的风线；旧方向粒子会在自身不足 1.25 秒的寿命内自然淡出。
	mWindParticleTimer = 0.0f;
	RestartRainVisualForWindChange();
}

bool Board::RedirectTyphoonFromBlover(WindDirection direction)
{
	if (!HasTyphoon()
		|| (direction != WindDirection::TOWARD_HOUSE
			&& direction != WindDirection::TOWARD_FRONT)) {
		return false;
	}

	const bool changed = mWindDirection != direction;
	mWindDirection = direction;
	// 玩家主动改向后重新获得一个完整方向阶段，避免旧计时恰好归零而在下一帧立刻随机翻回。
	mWindDirectionTimer = GameRandom::Range(
		kWindDirectionDurationMin, kWindDirectionDurationMax);
	if (mTyphoonGustActive) {
		// 活动阵风的锁定方向也是玩法权威：后续僵尸漂移与尚未结算的植物换格立即跟随。
		mActiveGustDirection = direction;
	}
	if (changed) {
		mWindParticleTimer = 0.0f;
		RestartRainVisualForWindChange();
	}
	return true;
}

/**
 * 周期性发射覆盖画面的横向风线。三档共用同一粒子资源，只用发射间隔表达浓度；
 * 该计时器是纯视觉瞬态，读档后从零开始即可按已恢复的强度与方向重建。
 */
void Board::UpdateTyphoonWindVisual(float deltaTime)
{
	if (!g_particleSystem || !HasTyphoon() || mWindDirection == WindDirection::NONE) return;
	mWindParticleTimer -= deltaTime;
	if (mWindParticleTimer > 0.0f) return;

	const float originX = mWindDirection == WindDirection::TOWARD_FRONT
		? -kWindParticleOriginPadding
		: static_cast<float>(SCENE_WIDTH) + kWindParticleOriginPadding;
	g_particleSystem->EmitEffect(WindEffectName(mWindDirection),
		Vector(originX, static_cast<float>(SCENE_HEIGHT) * 0.5f), LAYER_EFFECTS_WORLD);
	mWindParticleTimer = TyphoonWindParticleInterval(mTyphoonStrength);
}

/**
 * 推进台风风向、阵风等待与活动阶段。活动阵风锁定强度和风向，因而衰减/转向计时暂缓；
 * 阵风预算耗尽后持续风仍会影响僵尸自主移动与轻型子弹。
 */
void Board::UpdateTyphoon(float deltaTime)
{
	if (!SupportsTyphoon()) {
		if (HasTyphoon()) StopTyphoon();
		return;
	}
	if (!HasTyphoon()) return;
	if (mRainIntensity != RainIntensity::HEAVY) {
		StopTyphoon();
		return;
	}
	UpdateTyphoonWindVisual(deltaTime);
	if (mTyphoonGustActive) {
		UpdateActiveTyphoonGust(deltaTime);
		return;
	}
	if (IsStormyNightActive()) {
		// 4-9 终局固定强台风；只保留风向重抽和一次性阵风预算，不走强度衰减。
		mTyphoonStrengthTimer = kStormyNightLockedDuration;
	}
	else {
		mTyphoonStrengthTimer -= deltaTime;
		if (mTyphoonStrengthTimer <= 0.0f) WeakenTyphoon();
	}
	if (!HasTyphoon()) return;

	mWindDirectionTimer -= deltaTime;
	if (mWindDirectionTimer <= 0.0f) RerollWindDirection();
	if (mTyphoonGustsRemaining <= 0) {
		mWindGustTimer = 0.0f;
		return;
	}

	mWindGustTimer -= deltaTime;
	if (mWindGustTimer > 0.0f) return;
	BeginTyphoonGust(true);
}

/**
 * 启动一次短阵风并一次性抽好植物受力时刻。每帧只推进确定的计时，不重复掷概率，
 * 因而不同帧率和存读档都不会改变本次阵风是否/何时移动植物。
 */
bool Board::BeginTyphoonGust(bool consumeBudget, float forcedPlantMoveIn)
{
	if (!SupportsTyphoon() || !HasTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| mWindDirection == WindDirection::NONE || mTyphoonGustActive) return false;
	mLastTyphoonMovedPlants = 0;
	mLastTyphoonLostPlants = 0;
	mLastTyphoonBlockedPlantSteps = 0;
	const float duration = TyphoonGustDuration(mTyphoonStrength);
	if (duration <= 0.0f) return true;
	if (consumeBudget) {
		if (mTyphoonGustsRemaining <= 0) return false;
		--mTyphoonGustsRemaining;
	}

	mTyphoonGustActive = true;
	mActiveGustStrength = mTyphoonStrength;
	mActiveGustDirection = mWindDirection;
	mActiveGustDuration = duration;
	mActiveGustTimer = duration;
	mActiveGustPlantMoveTimer = forcedPlantMoveIn >= 0.0f
		? std::clamp(forcedPlantMoveIn, 0.0f, duration)
		: duration * GameRandom::Range(kGustPlantMoveProgressMin, kGustPlantMoveProgressMax);
	mActiveGustPlantMoved = false;
	mWindGustTimer = 0.0f;
	if (mActiveGustPlantMoveTimer <= 0.0f) {
		TriggerTyphoonPlantMove(mActiveGustStrength, mActiveGustDirection);
		mActiveGustPlantMoved = true;
	}
	return true;
}

/** 推进活动阵风；先跨越并结算随机植物时刻，再结束阵风，避免长帧漏掉整格位移。 */
void Board::UpdateActiveTyphoonGust(float deltaTime)
{
	if (!mTyphoonGustActive) return;
	if (!mActiveGustPlantMoved) {
		mActiveGustPlantMoveTimer = std::max(0.0f,
			mActiveGustPlantMoveTimer - deltaTime);
		if (mActiveGustPlantMoveTimer <= 0.0f) {
			TriggerTyphoonPlantMove(mActiveGustStrength, mActiveGustDirection);
			mActiveGustPlantMoved = true;
		}
	}
	mActiveGustTimer = std::max(0.0f, mActiveGustTimer - deltaTime);
	if (mActiveGustTimer <= 0.0f) EndTyphoonGust();
}

/** 结束活动阵风并按当前衰减档位安排下一次；预算耗尽则只保留持续风。 */
void Board::EndTyphoonGust()
{
	mTyphoonGustActive = false;
	mActiveGustStrength = TyphoonStrength::NONE;
	mActiveGustDirection = WindDirection::NONE;
	mActiveGustDuration = 0.0f;
	mActiveGustTimer = 0.0f;
	mActiveGustPlantMoveTimer = 0.0f;
	mActiveGustPlantMoved = false;
	mWindGustTimer = mTyphoonGustsRemaining > 0
		? RandomTyphoonGustInterval(mTyphoonStrength) : 0.0f;
}

/**
 * 同一阵风按已锁定吹向逐格、从前缘到后缘结算全部植物；
 * 锚定植物所在格自身不移动，只对直接撞入该格的植物逐格派发撞击，不传导后方植物链；
 * 植物被吹出棋盘或吹入弹坑时死亡，弹坑不能反直觉地充当挡风墙。
 * 每次换格先更新 Cell、row/column 与碰撞箱，再让植物画面用瞬态偏移追赶；
 * 因此滑动中保存只会记录目标格，读档不会恢复半格状态或重复位移。
 */
void Board::TriggerTyphoonPlantMove(TyphoonStrength strength, WindDirection direction)
{
	mLastTyphoonMovedPlants = 0;
	mLastTyphoonLostPlants = 0;
	mLastTyphoonBlockedPlantSteps = 0;
	if (!HasTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| direction == WindDirection::NONE) return;

	const int columnDelta = direction == WindDirection::TOWARD_FRONT ? 1 : -1;
	const int distance = TyphoonGustDistance(strength);
	std::unordered_set<int> movedPlantIDs;
	std::unordered_set<int> lostPlantIDs;
	std::unordered_set<int> anchorFeedbackPlantIDs;
	for (int step = 0; step < distance; ++step) {
		for (int row = 0; row < mRows; ++row) {
			const int firstColumn = columnDelta > 0 ? mColumns - 1 : 0;
			const int endColumn = columnDelta > 0 ? -1 : mColumns;
			for (int column = firstColumn; column != endColumn; column -= columnDelta) {
				Cell* source = GetCell(row, column);
				if (!source || source->IsEmpty()) continue;
				Ladder* sourceLadder = GetLadderAt(row, column);
				const int underID = source->GetUnderPlantID();
				const int normalID = source->GetNormalPlantID();
				const int pumpkinID = source->GetPumpkinPlantID();
				const int overlayID = source->GetOverlayPlantID();
				Plant* under = mEntityRegistry.GetPlant(underID);
				Plant* normal = mEntityRegistry.GetPlant(normalID);
				Plant* pumpkin = mEntityRegistry.GetPlant(pumpkinID);
				Plant* overlay = mEntityRegistry.GetPlant(overlayID);
				if (!under || !under->IsActive()) {
					source->ClearUnderPlantID();
					under = nullptr;
				}
				if (!normal || !normal->IsActive()) {
					source->ClearNormalPlantID();
					normal = nullptr;
				}
				if (!pumpkin || !pumpkin->IsActive()) {
					source->ClearPumpkinPlantID();
					pumpkin = nullptr;
				}
				if (!overlay || !overlay->IsActive()) {
					source->ClearOverlayPlantID();
					overlay = nullptr;
				}
				if (!under && !normal && !pumpkin && !overlay) continue;
				// 冰像封存期间整个植物组合都与外部玩法隔离；台风不得先改 Cell 再让
				// Plant::MoveToGridCell 拒绝，从而制造逻辑格与实体坐标分裂。
				if ((under && under->IsIceSealed())
					|| (normal && normal->IsIceSealed())
					|| (pumpkin && pumpkin->IsIceSealed())
					|| (overlay && overlay->IsIceSealed())) {
					continue;
				}

				if (normal && IsMultiCellPlantType(normal->mPlantType)) {
					// 次级占格只是一株实体的别名；整株只在逻辑锚点处原子结算一次。
					if (normal->mRow != row || normal->mColumn != column) continue;
					struct FootprintStack {
						Cell* cell = nullptr;
						int row = -1;
						int column = -1;
						std::array<int, 4> ids{};
						std::array<Plant*, 4> plants{};
						Ladder* ladder = nullptr;
					};
					const PlantFootprint footprint = GetPlantFootprint(normal->mPlantType);
					std::vector<FootprintStack> stacks;
					stacks.reserve(footprint.count);
					std::unordered_set<int> sourcePlantIDs;
					bool anchored = false;
					for (std::size_t i = 0; i < footprint.count; ++i) {
						FootprintStack stack;
						stack.row = row + footprint.cells[i].rowOffset;
						stack.column = column + footprint.cells[i].columnOffset;
						stack.cell = GetCell(stack.row, stack.column);
						if (!stack.cell) continue;
						stack.ids = { stack.cell->GetUnderPlantID(),
							stack.cell->GetNormalPlantID(), stack.cell->GetPumpkinPlantID(),
							stack.cell->GetOverlayPlantID() };
						for (std::size_t layer = 0; layer < stack.ids.size(); ++layer) {
							Plant* member = mEntityRegistry.GetPlant(stack.ids[layer]);
							if (!member || !member->IsActive()) {
								stack.ids[layer] = NULL_PLANT_ID;
								continue;
							}
							stack.plants[layer] = member;
							sourcePlantIDs.insert(member->mPlantID);
							anchored = anchored || member->AnchorsPlantCellAgainstTyphoon();
						}
						stack.ladder = GetLadderAt(stack.row, stack.column);
						stacks.push_back(stack);
					}
					if (stacks.size() != footprint.count || anchored) continue;

					bool lost = false;
					bool blocked = false;
					Plant* blockingAnchor = nullptr;
					for (const FootprintStack& stack : stacks) {
						const int targetColumn = stack.column + columnDelta;
						if (targetColumn < 0 || targetColumn >= mColumns
							|| HasCraterAt(stack.row, targetColumn)) {
							lost = true;
							break;
						}
						Cell* target = GetCell(stack.row, targetColumn);
						if (!target) {
							blocked = true;
							break;
						}
						for (const int targetID : { target->GetUnderPlantID(),
							target->GetNormalPlantID(), target->GetPumpkinPlantID(),
							target->GetOverlayPlantID() }) {
							if (targetID != NULL_PLANT_ID
								&& sourcePlantIDs.find(targetID) == sourcePlantIDs.end()) {
								blocked = true;
								blockingAnchor = GetTopPlantAt(stack.row, targetColumn);
								break;
							}
						}
						if (blocked) break;
					}
					if (lost) {
						for (const int id : sourcePlantIDs) {
							if (Plant* member = mEntityRegistry.GetPlant(id)) {
								lostPlantIDs.insert(id);
								member->Die();
							}
						}
						continue;
					}
					if (blocked) {
						if (blockingAnchor && blockingAnchor->AnchorsPlantCellAgainstTyphoon()) {
							const bool showFeedback = anchorFeedbackPlantIDs
								.insert(blockingAnchor->mPlantID).second;
							blockingAnchor->OnTyphoonPlantImpact(showFeedback);
							++mLastTyphoonBlockedPlantSteps;
						}
						continue;
					}

					// 先清空全部源格，再按相对格映射写入目标，避免相邻格重叠时覆盖尚未搬走的承载物。
					for (const FootprintStack& stack : stacks) {
						if (stack.cell->GetUnderPlantID() == stack.ids[0]) stack.cell->ClearUnderPlantID();
						if (stack.cell->GetNormalPlantID() == stack.ids[1]) stack.cell->ClearNormalPlantID();
						if (stack.cell->GetPumpkinPlantID() == stack.ids[2]) stack.cell->ClearPumpkinPlantID();
						if (stack.cell->GetOverlayPlantID() == stack.ids[3]) stack.cell->ClearOverlayPlantID();
					}
					std::unordered_set<int> repositionedPlantIDs;
					for (const FootprintStack& stack : stacks) {
						const int targetColumn = stack.column + columnDelta;
						Cell* target = GetCell(stack.row, targetColumn);
						if (stack.ids[0] != NULL_PLANT_ID) target->SetUnderPlantID(stack.ids[0]);
						if (stack.ids[1] != NULL_PLANT_ID) target->SetNormalPlantID(stack.ids[1]);
						if (stack.ids[2] != NULL_PLANT_ID) target->SetPumpkinPlantID(stack.ids[2]);
						if (stack.ids[3] != NULL_PLANT_ID) target->SetOverlayPlantID(stack.ids[3]);
						for (Plant* member : stack.plants) {
							if (member && repositionedPlantIDs.insert(member->mPlantID).second) {
								const int memberTargetColumn = member == normal
									? normal->mColumn + columnDelta : targetColumn;
								member->MoveToGridCell(stack.row, memberTargetColumn,
									kTyphoonPlantSlideDuration);
								movedPlantIDs.insert(member->mPlantID);
							}
						}
						if (stack.ladder) stack.ladder->MoveToGridCell(stack.row, targetColumn);
						RefreshPlantStackRenderOrder(target);
					}
					continue;
				}
				const Plant* sourceAnchor = pumpkin && pumpkin->AnchorsPlantCellAgainstTyphoon()
					? pumpkin
					: (normal && normal->AnchorsPlantCellAgainstTyphoon()
						? normal
						: (under && under->AnchorsPlantCellAgainstTyphoon() ? under : nullptr));
				if (sourceAnchor) continue;

				const int targetColumn = column + columnDelta;
				if (targetColumn < 0 || targetColumn >= mColumns) {
					if (under) {
						lostPlantIDs.insert(underID);
						under->Die();
					}
					if (normal) {
						lostPlantIDs.insert(normalID);
						normal->Die();
					}
					if (pumpkin) {
						lostPlantIDs.insert(pumpkinID);
						pumpkin->Die();
					}
					if (overlay) {
						lostPlantIDs.insert(overlayID);
						overlay->Die();
					}
					continue;
				}

				Cell* target = GetCell(row, targetColumn);
				if (!target) continue;
				if (!target->IsEmpty()) {
					Plant* anchor = GetTopPlantAt(row, targetColumn);
					if (anchor && anchor->AnchorsPlantCellAgainstTyphoon()) {
						const bool showFeedback =
							anchorFeedbackPlantIDs.insert(anchor->mPlantID).second;
						anchor->OnTyphoonPlantImpact(showFeedback);
						++mLastTyphoonBlockedPlantSteps;
					}
					continue;
				}
				if (HasCraterAt(row, targetColumn)) {
					if (under) {
						lostPlantIDs.insert(underID);
						under->Die();
					}
					if (normal) {
						lostPlantIDs.insert(normalID);
						normal->Die();
					}
					if (pumpkin) {
						lostPlantIDs.insert(pumpkinID);
						pumpkin->Die();
					}
					if (overlay) {
						lostPlantIDs.insert(overlayID);
						overlay->Die();
					}
					continue;
				}
				source->ClearUnderPlantID();
				source->ClearNormalPlantID();
				source->ClearPumpkinPlantID();
				source->ClearOverlayPlantID();
				if (under) {
					target->SetUnderPlantID(underID);
					under->MoveToGridCell(row, targetColumn, kTyphoonPlantSlideDuration);
					movedPlantIDs.insert(underID);
				}
				if (normal) {
					target->SetNormalPlantID(normalID);
					normal->MoveToGridCell(row, targetColumn, kTyphoonPlantSlideDuration);
					movedPlantIDs.insert(normalID);
				}
				if (pumpkin) {
					target->SetPumpkinPlantID(pumpkinID);
					pumpkin->MoveToGridCell(row, targetColumn, kTyphoonPlantSlideDuration);
					movedPlantIDs.insert(pumpkinID);
				}
				if (overlay) {
					target->SetOverlayPlantID(overlayID);
					overlay->MoveToGridCell(row, targetColumn, kTyphoonPlantSlideDuration);
					movedPlantIDs.insert(overlayID);
				}
				if (sourceLadder) {
					// 扶梯是植物组合的格附件：逻辑格与 Cell 同帧切换，绘制继续共享宿主的追赶偏移。
					if (Ladder* staleTarget = GetLadderAt(row, targetColumn);
						staleTarget && staleTarget != sourceLadder) {
						RemoveLadderAt(row, targetColumn);
					}
					sourceLadder->MoveToGridCell(row, targetColumn);
				}
				RefreshPlantStackRenderOrder(target);
			}
		}
	}
	mLastTyphoonMovedPlants = static_cast<int>(movedPlantIDs.size());
	mLastTyphoonLostPlants = static_cast<int>(lostPlantIDs.size());
}

void Board::BeginRain(RainIntensity intensity, float duration, bool canIntensify, bool canHold,
	bool allowTyphoonRoll)
{
	if (intensity == RainIntensity::CLEAR || duration <= 0.0f) return;
	const bool wasHeavy = mRainIntensity == RainIntensity::HEAVY;
	BeginWeatherTransition(intensity);
	if (intensity != RainIntensity::HEAVY) {
		ClearPendingHeavyRainWarning();
		StopTyphoon();
	}
	else if (!wasHeavy) {
		// 小雨后续若已增强为大雨，本轮已经兑现压力，不再把它算作弱天气欠账。
		mWeakWeatherPhasesSinceHeavy = 0;
		if (allowTyphoonRoll) ConsumePendingHeavyTyphoon();
		else {
			ClearPendingHeavyRainWarning();
			StopTyphoon();
		}
	}
	else {
		ClearPendingHeavyRainWarning();
	}
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;
	mWeatherTimer = duration;
	mRainCanIntensify = canIntensify && intensity == RainIntensity::LIGHT;
	mRainCanHold = canHold && intensity != RainIntensity::LIGHT;
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mRainSplashTimer = RandomRainSplashDelay(intensity);
	mLightningTimer = (intensity == RainIntensity::HEAVY
		&& !IsWinterPrecipitationSnow())
		? GameRandom::Range(kLightningDelayMin, kLightningDelayMax)
		: 0.0f;
	mRainVisualActive = false;
	RefreshZombieWeatherSpeeds();
	// 同档续期也新建发射器，让旧雨丝自然收尾并与新雨段无缝衔接。
	EmitRainEffect(duration);
	StartRainAudio();
	if (mPresentation && !IsWeatherPanelInterferenceActive()) {
		mPresentation->ShowCurrentWeatherNotice();
	}
}

/**
 * 督军天气命令只沿雨势强度向上覆盖，并复用 Board 的正式切档、粒子、声音和存档字段。
 * 周期中雨不续同档，避免靠固定指挥节奏把雨永久锁住；残血大雨可一次性补足最低持续时间。
 */
bool Board::TriggerRoofMarshalWeather(RainIntensity target, float duration,
	bool extendSameIntensity)
{
	if (!SupportsWeather() || !mWeatherInitialized || IsStormyNightActive()
		|| duration <= 0.0f
		|| (target != RainIntensity::MEDIUM && target != RainIntensity::HEAVY)) {
		return false;
	}

	const int currentRank = static_cast<int>(mRainIntensity);
	const int targetRank = static_cast<int>(target);
	if (currentRank > targetRank) return false;
	if (mRainIntensity == target
		&& (!extendSameIntensity || mWeatherTimer >= duration)) {
		return false;
	}

	const float appliedDuration = mRainIntensity == target
		? std::max(mWeatherTimer, duration)
		: duration;
	// 督军只号令雨势，不额外抽取台风；自然大雨已存在时同档延长会保留其既有台风状态。
	BeginRain(target, appliedDuration, false, false, false);
	return true;
}

void Board::FinishRainPhase(int transitionRoll)
{
	const float directorFactor = GetWeatherDirectorFactor();
	const RainIntensity next = RainTransitionForRoll(
		mRainIntensity, mRainCanIntensify, mRainCanHold,
		directorFactor, transitionRoll);
	if (next == RainIntensity::CLEAR) {
		EndRain();
		return;
	}

	// 同档续期或切档后统一进入衰减链，不再拥有增强或继续维持资格。
	BeginRain(next, RandomTailRainDuration(next, directorFactor), false, false);
}

void Board::EndRain()
{
	ClearPendingHeavyRainWarning();
	StopTyphoon();
	BeginWeatherTransition(RainIntensity::CLEAR);
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;
	mWeatherTimer = GameRandom::Range(kClearWeatherDelayMin, kClearWeatherDelayMax);
	mLightningTimer = 0.0f;
	mRainSplashTimer = 0.0f;
	mRainCanIntensify = false;
	mRainCanHold = false;
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mRainVisualActive = false;
	mRainVisualEffectName.clear();
	RefreshZombieWeatherSpeeds();
	if (mWeatherTransitionTimer > 0.0f) StartRainAudio();
	else StopRainAudio();
	if (mPresentation && !IsWeatherPanelInterferenceActive()) {
		mPresentation->ShowCurrentWeatherNotice();
	}
}

void Board::TriggerLightning()
{
	if (mRainIntensity != RainIntensity::HEAVY || IsWinterPrecipitationSnow()) return;
	// 黑夜屋顶把现有大雨闪电作为独立雷荷的一次增量；不改变雨势或坡面径流。
	AddNightRoofCharge(kNightRoofChargeLightningBonus);
	if (IsStormyNightActive()) {
		// 暴风雨夜复用原版全屏短闪，不再叠加普通大雨的程序化闪电路径。
		mStormyNightFlashPattern = 3;
		mStormyNightFlashTimer = kStormFlashUnitSeconds;
		PlayWeatherThunder();
		return;
	}
	if (!mPresentation) return;
	// 雷声与程序化闪电从同一触发点发起，保证自然天气与 AutoTest 路径音画同步。
	PlayWeatherThunder();
	mPresentation->ShowLightningStrike(kLightningFlashDuration);
}

void Board::UpdateWeather(float deltaTime)
{
	if (!mWeatherInitialized || deltaTime <= 0.0f) return;
	UpdatePolarNightEnvironment(deltaTime);
	UpdateWinterTemperature(deltaTime);
	// 场景积累器只由背景资格决定；通用天气的冒险进度门槛不能反向关闭屋顶固有机制。
	UpdateRoofRunoff(deltaTime);
	UpdateNightRoofCharge(deltaTime);
	if (!SupportsWeather()) return;
	if (IsStormyNightActive()) {
		EnforceStormyNightWeather();
		UpdateWeatherTransition(deltaTime);
		if (!IsRainEffectEmitting()) {
			mRainVisualActive = false;
			EmitRainEffect(kStormyNightLockedDuration);
		}
		if (!IsWinterPrecipitationSnow()) UpdateRainGroundSplash(deltaTime);
		UpdateTyphoon(deltaTime);
		UpdateStormyNightFlash(deltaTime);
		return;
	}
	UpdateWeatherTransition(deltaTime);

	// 每帧只推进当前阶段的倒计时。雨中归零会按当前强度决定续期、增强、衰减或放晴；
	// CLEAR 阶段归零可继续晴天或进入新雨，雨链本身仍按资格形成有界循环。
	mWeatherTimer -= deltaTime;
	if (mWeatherTimer <= kWeatherForecastLeadTime && !mWeatherForecastReady) {
		PrepareWeatherForecast();
	}
	MaybeShowHeavyRainPrompt();
	if (mRainIntensity != RainIntensity::CLEAR && mWeatherTimer > 0.0f
		&& !IsRainEffectEmitting()) {
		// 粒子系统若因场景清理或异常耗尽而与天气状态脱节，用剩余时长自动补发。
		mRainVisualActive = false;
		EmitRainEffect(mWeatherTimer);
	}
	if (mRainIntensity != RainIntensity::CLEAR && mWeatherTimer > 0.0f
		&& !IsWinterPrecipitationSnow()) {
		UpdateRainGroundSplash(deltaTime);
	}
	if (mRainIntensity == RainIntensity::HEAVY && mWeatherTimer > 0.0f) {
		UpdateTyphoon(deltaTime);
		if (!IsWinterPrecipitationSnow()) {
			mLightningTimer -= deltaTime;
			if (mLightningTimer <= 0.0f) {
				TriggerLightning();
				mLightningTimer = GameRandom::Range(kLightningRepeatMin, kLightningRepeatMax);
			}
		}
		else {
			mLightningTimer = 0.0f;
		}
	}

	if (mWeatherTimer > 0.0f) return;
	ConsumeWeatherForecast();
}

void Board::SetRainForTesting(RainIntensity intensity, float duration, bool canIntensify)
{
	if (!SupportsWeather()) return;
	if (g_particleSystem) g_particleSystem->ClearAll();
	StopRainAudio();
	mWeatherInitialized = true;
	mPreviousRainIntensity = mRainIntensity;
	mWeatherTransitionTimer = 0.0f;
	ClearPendingHeavyRainWarning();
	mForecastRainIntensity = RainIntensity::CLEAR;
	mActualForecastRainIntensity = RainIntensity::CLEAR;
	mWeatherForecastReady = false;
	mWeatherForecastDisrupted = false;
	mRainVisualActive = false;
	mRainVisualEffectName.clear();
	mHeavyPhasesWithoutTyphoon = 0;
	StopTyphoon();

	if (intensity == RainIntensity::CLEAR) {
		mRainIntensity = RainIntensity::CLEAR;
		mPreviousRainIntensity = RainIntensity::CLEAR;
		mWeatherTimer = std::max(duration, 0.1f);
		mLightningTimer = 0.0f;
		mRainSplashTimer = 0.0f;
		mRainCanIntensify = false;
		mRainCanHold = false;
		FinishWeatherTransitionImmediately();
		if (mPresentation && !IsWeatherPanelInterferenceActive()) {
			mPresentation->ShowCurrentWeatherNotice();
		}
		return;
	}
	BeginRain(intensity, std::max(duration, 0.1f), canIntensify, true, false);
	FinishWeatherTransitionImmediately();
}

bool Board::SetWeatherForecastForTesting(RainIntensity forecast, RainIntensity actual, float revealIn)
{
	if (!SupportsWeather() || !mWeatherInitialized) {
		return false;
	}
	ClearPendingHeavyRainWarning();
	mForecastRainIntensity = forecast;
	mActualForecastRainIntensity = actual;
	mWeatherForecastReady = true;
	mWeatherForecastDisrupted = false;
	mWeatherTimer = std::max(revealIn, 0.1f);
	PreparePendingHeavyTyphoon();
	return true;
}

/** 用确定性等级覆盖大雨预报警报或真实台风初态；NONE 同样表示已锁定。 */
bool Board::SetPendingHeavyTyphoonForTesting(TyphoonStrength strength, int promptVariant)
{
	if (!NeedsPendingHeavyForecastState()
		|| strength < TyphoonStrength::NONE || strength > TyphoonStrength::SUPER
		|| promptVariant < -1 || promptVariant > 2) {
		return false;
	}
	mPendingHeavyTyphoonPrepared = true;
	mPendingHeavyTyphoonOpeningProtected = false;
	mPendingHeavyTyphoonStrength = strength;
	mPendingHeavyWindDirection = strength == TyphoonStrength::NONE
		? WindDirection::NONE : WindDirection::TOWARD_HOUSE;
	mPendingHeavyTyphoonStrengthTimer = strength == TyphoonStrength::NONE ? 0.0f : 60.0f;
	mPendingHeavyWindDirectionTimer = strength == TyphoonStrength::NONE ? 0.0f : 20.0f;
	mPendingHeavyTyphoonGustsRemaining = TyphoonMaxGusts(strength);
	mPendingHeavyWindGustTimer = mPendingHeavyTyphoonGustsRemaining > 0 ? 15.0f : 0.0f;
	if (promptVariant >= 0) mPendingHeavyRainPromptVariant = promptVariant;
	mHeavyRainPromptShown = false;
	return true;
}

/** 用固定权重落点准备真实新天气；公开预报强制等于实际结果，避免测试再依赖第二次随机。 */
bool Board::PrepareWeatherForecastForTesting(int weatherRoll, float revealIn)
{
	if (!SupportsWeather()
		|| !mWeatherInitialized || mRainIntensity != RainIntensity::CLEAR
		|| mWeatherForecastReady) return false;
	const int total = GetNextWeatherRollTotal();
	if (weatherRoll < 1 || weatherRoll > total) return false;
	PrepareWeatherForecast(weatherRoll);
	mForecastRainIntensity = mActualForecastRainIntensity;
	mWeatherTimer = std::max(revealIn, 0.1f);
	return true;
}

bool Board::AdvanceRainPhaseForTesting(int transitionRoll)
{
	if (mRainIntensity == RainIntensity::CLEAR) return false;
	const float directorFactor = GetWeatherDirectorFactor();
	const int total = RainTransitionWeightTotal(
		mRainIntensity, mRainCanIntensify, mRainCanHold, directorFactor);
	if (total > 0 && (transitionRoll < 1 || transitionRoll > total)) return false;

	// 测试会在雨段尚未自然到期时强制切档；先清旧雨丝，模拟生产路径中旧发射器已到期。
	if (g_particleSystem) g_particleSystem->ClearAll();
	mRainVisualActive = false;
	mRainVisualEffectName.clear();
	if (total > 0) FinishRainPhase(transitionRoll);
	else EndRain();
	FinishWeatherTransitionImmediately();
	return true;
}

bool Board::TriggerLightningForTesting()
{
	if (mRainIntensity != RainIntensity::HEAVY || IsWinterPrecipitationSnow()) return false;
	TriggerLightning();
	return true;
}

bool Board::SetTyphoonForTesting(TyphoonStrength strength, WindDirection direction,
	float gustIn, float directionIn, int gustsRemaining, float decayIn)
{
	if (mRainIntensity != RainIntensity::HEAVY) return false;
	if (!SupportsTyphoon() && strength != TyphoonStrength::NONE) return false;
	if (strength == TyphoonStrength::NONE) {
		StopTyphoon();
		RestartRainVisualForWindChange();
		return true;
	}
	RestoreTyphoonState(strength, direction, decayIn, gustIn, directionIn, gustsRemaining);
	RestartRainVisualForWindChange();
	return HasTyphoon();
}

bool Board::RerollWindDirectionForTesting(int directionRoll)
{
	if (!HasTyphoon() || directionRoll < 1 || directionRoll > 2) return false;
	RerollWindDirection(directionRoll);
	return true;
}

bool Board::RollTyphoonForTesting(int chanceRoll, int strengthRoll, WindDirection direction)
{
	const int totalWeight = BuildTyphoonWeights(GetWeatherDirectorFactor()).Total();
	const bool validDirection = direction == WindDirection::TOWARD_HOUSE
		|| direction == WindDirection::TOWARD_FRONT;
	if (!SupportsTyphoon() || mRainIntensity != RainIntensity::HEAVY
		|| chanceRoll < 1 || chanceRoll > 100
		|| strengthRoll < 1 || strengthRoll > totalWeight || !validDirection) return false;
	StartTyphoonForHeavyPhase(chanceRoll, strengthRoll, direction);
	RestartRainVisualForWindChange();
	return true;
}

bool Board::TriggerTyphoonGustForTesting(float plantMoveIn)
{
	if (!HasTyphoon() || mRainIntensity != RainIntensity::HEAVY) return false;
	return BeginTyphoonGust(false, plantMoveIn);
}

void Board::TriggerRainGroundSplashForTesting()
{
	TriggerRainGroundSplash();
}

bool Board::SupportsWeather() const
{
	// 基础天气保留唯一的进度门槛：正式一大关不启用；
	// 无尽默认启用，但极夜地形始终使用独立三仪表环境。
	if (SupportsPolarNightEnvironment() || IsMineBackground() || IsColdStorage()) return false;
	if (mIsSurvival) return true;
	return AdventureProgression::IsAdventureLevel(mLevel)
		&& AdventureProgression::GetAreaNumber(mLevel) >= 2;
}

/** 统一判定玩家是否允许当前地图生成或保留台风。 */
bool Board::SupportsTyphoon() const
{
	return SupportsWeather()
		&& GameAPP::GetInstance().mTyphoonWeatherEnabled
		&& mBackGround != Background::WINTER_GARDEN;
}

bool Board::HasDisruptibleWeatherForecast() const
{
	return HasWeatherForecast() || HasColdWaveForecast() || HasFogWeatherForecast();
}

bool Board::SupportsWeatherPanelInterference() const
{
	return SupportsWeather() || SupportsWinterTemperature() || SupportsFogWeather();
}

bool Board::CanBeginWeatherPanelInterference() const
{
	return SupportsWeatherPanelInterference()
		&& mWeatherPanelInterferenceTimer < kMaximumWeatherPanelInterferenceDuration;
}

float Board::GetMaximumWeatherPanelInterferenceDuration() const
{
	return kMaximumWeatherPanelInterferenceDuration;
}

int Board::DisruptWeatherForecastPanel()
{
	int disruptedMask = 0;
	if (HasWeatherForecast()) {
		if (mPresentation) mPresentation->CancelHeavyRainWarning();
		mWeatherForecastDisrupted = true;
		disruptedMask |= 1;
	}
	if (DisruptColdWaveForecast()) disruptedMask |= 2;
	if (HasFogWeatherForecast()) {
		mFogWeatherForecastDisrupted = true;
		disruptedMask |= 4;
	}
	return disruptedMask;
}

/**
 * 向当前剩余时长追加一个有界整栏黑障；bit3 区分“成功补时但当帧尚无公开预报”的有效提交。
 */
int Board::BeginWeatherPanelInterference(float duration)
{
	if (!CanBeginWeatherPanelInterference() || !std::isfinite(duration)
		|| duration <= 0.0f) return 0;
	mWeatherPanelInterferenceTimer = std::min(
		kMaximumWeatherPanelInterferenceDuration,
		mWeatherPanelInterferenceTimer + duration);
	const int disruptedMask = DisruptWeatherForecastPanel();
	if (mPresentation) {
		mPresentation->CancelHeavyRainWarning();
		mPresentation->CancelWeatherInformationNotices();
	}
	return disruptedMask | 8;
}

/**
 * 黑障期间每帧在雨雪、寒潮和雾势都完成推进后截获新广播，再按游戏时间递减窗口。
 */
void Board::UpdateWeatherPanelInterference(float deltaTime)
{
	if (!IsWeatherPanelInterferenceActive()) return;
	DisruptWeatherForecastPanel();
	mWeatherPanelInterferenceTimer = std::max(
		0.0f, mWeatherPanelInterferenceTimer - std::max(0.0f, deltaTime));
}
