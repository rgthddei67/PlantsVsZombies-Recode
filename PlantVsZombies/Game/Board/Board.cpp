#include "Game/Board/Board.h"
#include "Logger.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/LawnMower.h"
#include "Game/Shovel.h"
#include "Game/Sun.h"
#include "Game/Trophy.h"
#include "Game/Crater.h"
#include "Game/Ladder.h"
#include "Game/IceWall.h"
#include "Game/GroundRift.h"
#include "Game/AdventureProgression.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/CardSlotManager.h"
#include "Game/Card.h"
#include "GameRandom.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/Imitater.h"
#include "Game/Plant/Blover.h"
#include "Game/Plant/PlantUpgradeRules.h"
#include "Game/Plant/PlantFootprint.h"
#include "Game/Plant/Plantern.h"
#include "Game/Plant/CobCannon.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/BobsledTeamZombie.h"
#include "Game/Zombie/IceStatueExecutionerZombie.h"
#include "Game/Zombie/MagneticItem.h"
#include "Game/MistFuel.h"

#include "Game/EntityRegistry.h"
#include "Game/RenderOrder.h"
#include "Game/AudioSystem.h"
#include "Game/Plant/GameDataManager.h"
#include "GameApp.h"
#include "FileManager.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "ParticleSystem/ParticleSystem.h"
#include "Graphics.h"
#include "Profiler.h"
#include <unordered_set>
#include <climits>
#include <array>
#include <algorithm>   // std::max, std::swap
#include <cmath>       // std::lround
#include <cstdint>
#include <limits>

namespace {
	constexpr float kHxyWaveBudgetMultiplier = 0.7f; // HXY专属出怪预算相对难度1的倍率
	constexpr int kHxyStartingSunBonus = 300; // HXY专属每次新开局额外阳光，续局与生存换轮不重复发放
	constexpr double kHxyArmorHealthMultiplier = 0.75; // HXY专属所有防具当前及最大生命倍率，本体不变
	/** 返回当前地形唯一的关卡音乐资源键，供预构建与正式播放共用。 */
	const std::string& BackgroundMusicKey(Background background)
	{
		switch (background)
		{
		case Background::GROUND_DAY:
			return ResourceKeys::Music::MUSIC_DAY;
		case Background::GROUND_NIGHT:
			return ResourceKeys::Music::MUSIC_NIGHT;
		case Background::WATER_POOL:
			return ResourceKeys::Music::MUSIC_POOL;
		case Background::NIGHT_WATER_POOL:
			return ResourceKeys::Music::MUSIC_FOG;
		case Background::ROOF:
			return ResourceKeys::Music::MUSIC_ROOF;
		case Background::NIGHT_ROOF:
			return ResourceKeys::Music::MUSIC_NIGHT;
		case Background::WINTER_GARDEN:
			return ResourceKeys::Music::MUSIC_DAY;
		case Background::GLOOMCRYSTAL_MINE:
		case Background::POLAR_NIGHT_SNOWFIELD:
			return ResourceKeys::Music::MUSIC_NIGHT;
		}
		return ResourceKeys::Music::MUSIC_DAY;
	}

	/** 集中保留地刺系的背景地形规则；屋顶必须拒绝无法放入花盆的地刺系。 */
	bool IsSpikeweedTerrainRestricted(Background background)
	{
		switch (background) {
		case Background::ROOF:
		case Background::NIGHT_ROOF:
			return true;
		default:
			return false;
		}
	}

	constexpr float kPoolCellInitialY = 85.0f;            // 泳池背景共用的六行网格首行顶部世界坐标（像素）
	constexpr float kPoolCellHeight = 85.0f;              // 泳池六行的逻辑格高（像素）；列宽仍保持 80
	constexpr float kRoofCellInitialYOffsetY = -10.0f;    // 屋顶五行网格相对普通草坪首行顶部的上移量（像素）
	constexpr float kRoofCellHeight = 85.0f;              // 屋顶五行的逻辑格高（像素）；与原版屋顶行距一致
	constexpr int kRoofSlopeColumnCount = 5;              // 从房屋侧起参与斜坡抬升的逻辑列数
	constexpr float kRoofSlopeRisePerPixel = 0.25f;       // 屋顶向房屋侧每移动 1 像素增加的屏幕 Y（像素/像素）
	constexpr float kZombieSpawnBaseOffsetY = 2.0f;       // 第一、二大关已确认正确的僵尸行中心统一基线（像素）
	constexpr float kRoofZombieAlignmentOffsetY = 17.5f; // 屋顶僵尸脚底相对 85px 行中心的美术校准量，单位：像素
	constexpr float kPoolBackgroundZombieSpawnYOffset = 0.0f; // 所有泳池背景、所有行共用的僵尸额外基线，单位：像素
	constexpr float kThirdAreaZombieAlignmentOffsetY = 10.0f; // 仅第三大关所有行使用的地图对齐基线，单位：像素
	constexpr float kPoolRowZombieSpawnYOffset = 30.0f;   // 仅水路行僵尸的画面下沉量；碰撞基线不含此值，单位：像素
	constexpr int kPoolFirstRow = 2;                      // 泳池第一条水路的 0-based 行号
	constexpr int kPoolLastRow = 3;                       // 泳池最后一条水路的 0-based 行号
	constexpr int kPoolFirstWaterSpawnWave = 5;           // 泳池自然波次从第几波起允许选择水路
	constexpr float kIceTrailDuration = 40.0f;            // 冰车进入战场后每次延伸刷新冰道的寿命，单位秒
	constexpr float kGoldenIceTrailDuration = 35.0f;      // 黄色冰道每次延伸刷新的寿命，单位秒
	constexpr float kIceTrailFadeDuration = 0.1f;         // 冰道最后渐隐时长，单位秒
	constexpr float kIceTrailLeftLimit = 25.0f;           // 原版非屋顶冰道左缘最小 X，单位 px
	constexpr float kIceTrailCapBodyOverlap = 8.0f;       // 端盖与主体纹理的水平咬合量，单位 px
	constexpr float kIceTrailGridProbeOffset = 12.0f;     // 原版由冰道左缘推导首个禁种格的采样偏移，单位 px
	constexpr float kRoofMowerTerrainOffsetY = 9.0f;      // RoofCleaner 逻辑原点相对连续行中心的下移量，单位：像素
	constexpr float kRoofPreviewZombieMinX = 1160.0f;     // 主人实测的屋顶选卡预览世界坐标左缘，单位：像素
	constexpr float kRoofPreviewZombieMaxX = 1365.0f;     // 主人实测的屋顶选卡预览世界坐标右缘，单位：像素
	constexpr int kRoofPreviewZombieFirstRow = 1;         // 顶行会令僵尸头部进入天空区；预览只使用第 2～5 行
	constexpr float kRoofMarshalBossSpawnX = 1000.0f;     // 督军高血量不水平推进，必须在右侧可见纵深直接登场，单位：像素
	constexpr float kIceTrailTopOffset = 20.0f;           // 冰道相对逻辑行顶的绘制偏移，单位 px
	constexpr float kPolarHoleSpawnWarningSeconds = 1.0f; // 正式波次经雪穴出生前的雪雾预警，游戏秒
	constexpr float kPolarSnowBlindRange = 3.0f * CELL_COLLIDER_SIZE_X; // 雪盲自动索敌真实半径，像素
	constexpr float kStormyNightWavePointMultiplier = 2.0f; // 暴风雨夜每波僵尸生成点数倍率
	constexpr float kStormyNightNextWaveSeconds = 5.0f;   // 暴风雨夜普通出波最大间隔；血量阈值仍可提前出波
	constexpr float kFogTargetingAlphaThreshold = 96.0f; // 4-2 起远程索敌仍可接受的最大逐格雾 alpha
	constexpr int kFogTargetingMarginColumns = 1;         // 植物可从当前可见边界额外看入薄雾的格数
	constexpr float kFogCloseDetectionRange = 100.0f;    // 雾中不依赖照明的近身感知横向距离（像素）
	constexpr int kMistFuelEarlyRewardAmount = 15;        // 首波单只携带者死亡时提供的雾火量
	constexpr int kMistFuelLateRewardAmount = 10;         // 最终波单只携带者死亡时提供的雾火量
	constexpr int kMistFuelCarriersPerWave = 3;           // 每波最多分配的雾火携带者数量
	constexpr float kMistFuelEarlyBaseCarrierChance = 0.50f; // 首波普通耐久僵尸加入保底累计器的基础份额
	constexpr float kMistFuelLateBaseCarrierChance = 0.25f; // 最终波普通耐久僵尸加入保底累计器的基础份额
	constexpr float kMistFuelEarlyHeavyCarrierBonus = 0.25f; // 首波高耐久僵尸相对普通僵尸最多追加的累计份额
	constexpr float kMistFuelLateHeavyCarrierBonus = 0.15f; // 最终波高耐久僵尸相对普通僵尸最多追加的累计份额
	constexpr int kEliteDancerMutationChancePercent = 50; // 台风以上普通舞王变异为精英舞王的概率（百分比）
	constexpr int kEliteDancerMaxPerWave = 2;             // 每波最多允许生成的精英舞王数量；超额候选直接跳过
	constexpr int kReinforcedDoorMaxPerWave = 2;          // 每波最多正式生成的加固铁门数量；超额候选直接跳过
	constexpr int kElitePolevaulterMaxPerWave = 2;        // 每波最多正式生成的精英撑杆数量；超额候选直接跳过
	constexpr int kGildedZamboniMaxPerWave = 1;           // 每波最多正式生成的鎏金冰车数量；超额候选直接跳过
	constexpr int kEliteDolphinRiderMaxPerWave = 1;       // 每波最多正式生成的精英海豚数量；超额候选直接跳过
	constexpr int kEliteJackInTheBoxMaxPerWave = 2;       // 每波最多正式生成的精英小丑数量；超额候选直接跳过
	constexpr int kEliteDiggerMaxPerWave = 1;             // 每波最多正式生成的爆破工头数量；超额候选直接跳过
	constexpr int kElitePogoMaxPerWave = 1;               // 每波最多正式生成的精英跳跳数量；超额候选直接跳过
	constexpr int kEliteLadderMaxPerWave = 1;             // 每波最多正式生成的精英扶梯数量；超额候选直接跳过
	constexpr int kEliteCatapultMaxPerWave = 1;           // 每波最多正式生成的导流投篮车数量；超额候选直接跳过
	constexpr int kRedeyeGargantuarMaxPerAdventureWave = 3; // 冒险每波最多正式生成的红眼巨人数量；无尽不启用此上限
	constexpr int kInsulatorMaxPerWave = 2;               // 每个正式波次最多成功生成的绝缘僵尸数量
	constexpr int kHijackerMaxPerWave = 2;                // 每个正式波次最多成功生成的劫持者数量
	constexpr int kHijackerSpawnCooldownWaves = 2;        // 成功处决后封锁的后续完整正式波次数
	constexpr int kGroundingZombieMaxPerWave = 2;         // 每个正式波次最多成功生成的接地僵尸数量
	constexpr int kBobsledTeamMaxPerWave = 3;             // 一次候选四人同乘；正式波次最多接纳三队
	constexpr int kIceWallEngineerMaxPerWave = 1;         // 每个正式波次最多成功生成一只冰墙工程师
	constexpr int kIceCrackDrillMaxPerWave = 3;           // 每个正式波次最多成功生成三只冰裂钻机
	constexpr int kWeatherJammerMaxPerWave = 2;           // 每个正式波次最多成功生成两只气象干扰僵尸
	constexpr int kIceStatueExecutionerMaxPerWave = 5;   // 每个正式波次最多成功生成五只冰像处刑者
	constexpr int kSnowBurrowTutorialMaxPerWave = 1;      // 8-1 每波与同时最多一只潜雪僵尸
	constexpr int kSnowBurrowCompositeMaxPerWave = 2;     // 8-2 起每波与同时最多两只潜雪僵尸
	constexpr int kSnowBurrowTutorialLevel = 64;           // 内部 64 即 8-1，雪穴形成后保底一次潜雪出生
	constexpr int kAdaptiveHelmetTutorialLevel = 66;      // 内部 66 即 8-3，首轮白毛风结束后独立教学
	constexpr int kAdaptiveHelmetMaxPerWave = 4;          // 8-3 起每波最多四只，不设同时或累计上限
	constexpr int kThermalSniperTutorialLevel = 68;       // 内部 68 即 8-5，第三波独立保底后开放自然候选
	constexpr int kThermalSniperCompositeLevel = 69;      // 内部 69 即 8-6，第一波开放且第二波额外保底
	constexpr int kThermalSniperTutorialWave = 3;         // 8-5 首次教学的额外保底波次
	constexpr int kThermalSniperCompositeWave = 2;        // 8-6 组合教学的额外保底波次
	constexpr int kThermalSniperTutorialMaxPerWave = 3;   // 8-5 每波最多三只
	constexpr int kThermalSniperCompositeMaxPerWave = 4;  // 8-6 每波最多四只
	constexpr int kAuroraPriestLevel = 70;                 // 内部 70 即 8-7
	constexpr int kAuroraPriestGuaranteeWave = 3;         // 8-7 第三波额外保底
	constexpr int kAuroraPriestMaxPerWave = 3;             // 极光祭司每波累计上限
	constexpr int kAuroraPriestMaxActive = 4;              // 敌对同时上限
	constexpr int kPolarClockmakerLevel = 71;              // 内部 71 即 8-8
	constexpr int kPolarClockmakerGuaranteeWave = 2;       // 8-8 第二波额外保底
	constexpr int kPolarClockmakerMaxPerWave = 4;          // 极夜钟匠每波累计上限
	constexpr int kPolarClockmakerMaxActive = 8;           // 敌对同时上限
	constexpr int kHijackerTutorialLevel = 49;             // 内部 49 即 6-4，使用第七波固定单体教学
	constexpr int kHijackerTutorialWave = 7;               // 6-4 首次登场的固定教学波
	constexpr int kHealerTutorialLevel = 51;               // 内部 51 即 6-6，使用第三波额外保底
	constexpr int kHealerTutorialWave = 3;                 // 6-6 首次登场的额外保底波次
	constexpr int kWeatherJammerTutorialLevel = 60;        // 内部 60 即 7-6，使用第三波额外保底
	constexpr int kWeatherJammerTutorialWave = 3;          // 7-6 首次登场的额外保底波次
	constexpr int kIceExecutionerTutorialLevel = 61;       // 7-7 首次教学冰像处刑者的冒险关卡
	constexpr int kIceExecutionerTutorialWave = 3;         // 7-7 第三波额外保底一只处刑者
	constexpr int kEliteScaredyShroomPlantLimit = 4;      // 每个关卡累计最多种植的精英胆小菇数量
	constexpr int kPumpkinProtectionCellRadius = 1;       // 南瓜头范围爆炸保护的逻辑格半径；1 表示自身九宫格
	constexpr int kPumpkinAreaDamageMultiplier = 5;       // 特殊僵尸范围伤害被南瓜头拦截时的默认基础伤害倍率
	constexpr int kMonteCarloMaxZombies = 16;             // 单个样本最多推进的当前敌方僵尸数
	constexpr float kMonteCarloStepSeconds = 0.25f;       // 纯数值推演固定步长，单位：游戏秒
	constexpr float kMonteCarloPlantDecisionSeconds = 2.0f; // 样本内玩家尝试从实际卡槽种植的间隔秒数
	constexpr float kMonteCarloTerminalBlockedSecondUtility = 12.0f; // 终局每秒剩余破墙时间对应的防守效用分
	constexpr float kMonteCarloTerminalBlockedSecondsCap = 90.0f; // 单株终局破墙时间最多计入的秒数
	constexpr int kWaveCandidateAttemptLimit = MAX_ZOMBIES_PER_WAVE * 10; // 单波候选尝试上限，防止仅剩受限类型时死循环
	constexpr float kThunderSoundVolume = 0.75f;         // 闪电出现时雷声相对音效音量

	/** 在线性调参端点间插值；调用方负责提供已经夹紧的天气导演强度。 */
	float LerpWeatherValue(float earlyValue, float lateValue, float directorFactor)
	{
		return earlyValue + (lateValue - earlyValue) * directorFactor;
	}
}

// 复刻原版 TodCommon.TodAnimateCurve(..., TodCurves.Linear)：把 round 在 [startRound,endRound]
// 归一化并钳到 [0,1]，在 [fromVal,toVal] 间线性插值，四舍五入取整。
static int SurvivalCurveLerp(int startRound, int endRound, int round,
                             int fromVal, int toVal)
{
	if (endRound <= startRound) return toVal;            // 防 0 除
	float t = static_cast<float>(round - startRound)
	        / static_cast<float>(endRound - startRound);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return static_cast<int>(std::lround(
		static_cast<float>(fromVal) + t * static_cast<float>(toVal - fromVal)));
}

Board::Board(BoardPresentation* presentation, Background background, int level)
{
	mPresentation = presentation;
	mLevel = level;
	mBackGround = background;
	mIsSurvival = IsSurvivalEndlessLevel(level);
	mHxyModeEnabled = GameAPP::GetInstance().mHxyModeEnabled;
	mPlantDamageEchoHitCounter = 0; // 每次进入新棋盘都清掉旧局的十连击进度，读档随后按存档覆盖。

	if (mLevel >= 1)
	{
		mLevelName.clear();
		int mBigLevel = AdventureProgression::GetAreaNumber(mLevel);
		int mSmallLevel = AdventureProgression::GetLevelNumberInArea(mLevel);
		mLevelName = u8"关卡 " + std::to_string(mBigLevel) + u8"-" + std::to_string(mSmallLevel);
	}
	mSpawnZombieList.reserve(32);
	mSpawnZombieList.push_back(ZombieType::ZOMBIE_NORMAL);
	mPreviewZombieList.reserve(32);

	if (mIsSurvival)
	{
		mSurvivalRound = 1;
		mPerkManager.Clear();   // 新生存局：词条清零（读档时由 Load 覆盖）
		mMaxWave = SURVIVAL_WAVES_PER_ROUND;
		BuildSurvivalSpawnList(mSurvivalRound);
		UpdateSurvivalLevelName();
	}
	else if (MiniGame::IsMiniGame(mLevel)) {
		mLevelName = std::string(u8"小游戏：") + MiniGame::NAME;
		mSun = MiniGame::INITIAL_SUN;
		mMaxWave = MiniGame::WAVES;
		mZombieCountDown = MiniGame::PREPARATION_SECONDS;
		mSpawnZombieList = { ZombieType::ZOMBIE_NORMAL, ZombieType::ZOMBIE_TRAFFIC_CONE,
			ZombieType::ZOMBIE_POLEVAULTER, ZombieType::ZOMBIE_BUCKET,
			ZombieType::ZOMBIE_NEWSPAPER, ZombieType::ZOMBIE_DOOR, ZombieType::ZOMBIE_FOOTBALL };
	}
	else if (mLevel > 0)
	{
		LoadSpawnListFromJson();
	}

	InitializeCell(IsPoolBackground() ? 5 : 4, 8);
	if (IsMineBackground()) {
		mMineGrid.Initialize(mLevel == AdventureProgression::AREA_NINE_FINAL_LEVEL ? 4 : mLevel >= 79 ? 3 : mLevel >= 77 ? 2 : mLevel >= 75 ? 1 : 0);
		mMineFogNextWave = GetMineFogOpeningWave();
		// 初始阳光由关卡表唯一维护；地形初始化不能覆盖已经加载的开局经济。
	}
	// 各地图起始阳光完成后只加一次；续局随后用保存的阳光覆盖，不重复领奖。
	if (mHxyModeEnabled) mSun += kHxyStartingSunBonus;
	// 屋顶预览会读取行高与连续坡面；必须在网格尺寸完成初始化后再生成。
	CreatePreviewZombies();
	mIceMinX.fill(GetIceTrailRightX());
	mIceTimer.fill(0.0f);
	mGoldenIceMinX.fill(GetIceTrailRightX());
	mGoldenIceTimer.fill(0.0f);
	InitializeRows();
}

Board::~Board()
{
	// 原版 Board.DisposeBoard 无条件 StopFoley(Rain)；离开关卡时同样保证循环声不泄漏到菜单。
	StopRainAudio();
}

void Board::ClampStormyNightWaveCountdown()
{
	mZombieCountDown = std::min(mZombieCountDown, kStormyNightNextWaveSeconds);
}

void Board::PlayWeatherThunder()
{
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_THUNDER, kThunderSoundVolume);
}

void Board::ConfigureMonteCarloCombatConfig(
	PlantDefenseMonteCarlo::Config& config,
	int rolloutCount, float horizonSeconds) const
{
	config.rolloutCount = rolloutCount;
	config.maxZombiesPerRollout = kMonteCarloMaxZombies;
	config.horizonSeconds = horizonSeconds;
	config.stepSeconds = kMonteCarloStepSeconds;
	config.plantDecisionInterval = kMonteCarloPlantDecisionSeconds;
	config.terminalBlockedSecondUtility = kMonteCarloTerminalBlockedSecondUtility;
	config.terminalBlockedSecondsCap = kMonteCarloTerminalBlockedSecondsCap;
}

void Board::ConfigureMonteCarloPlantImpactConfig(
	PlantDefenseMonteCarlo::Config& config,
	int rolloutCount, float horizonSeconds,
	int damage, float radius) const
{
	ConfigureMonteCarloCombatConfig(config, rolloutCount, horizonSeconds);
	config.impactDamage = static_cast<float>(damage);
	config.impactRadius = radius;
	config.pumpkinProtectionCellRadius = kPumpkinProtectionCellRadius;
	config.pumpkinImpactDamageMultiplier =
		static_cast<float>(kPumpkinAreaDamageMultiplier);
}

int Board::GetRoofSlopeColumnCount() const
{
	return kRoofSlopeColumnCount;
}

/**
 * 结束读档生命周期，并按已恢复的雾势、驱散量和路灯花状态直接建立首帧迷雾。
 * 正常开局与后续天气变化仍走逐帧平滑，只有重进存档跳过从透明开始的暴露窗口。
 */
void Board::CompleteLoadRestore()
{
	mIsLoadSave = false;
	if (mFogWeatherInitialized && SupportsStageFog()) {
		UpdateFogCellAlpha(0.0f, true);
	}
}

/** 夹紧并恢复当前波已经成功生成的精英舞王数量。 */
void Board::RestoreEliteDancerWaveSpawnCount(int count)
{
	mEliteDancersSpawnedThisWave = std::clamp(count, 0, kEliteDancerMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的加固铁门数量。 */
void Board::RestoreReinforcedDoorWaveSpawnCount(int count)
{
	mReinforcedDoorsSpawnedThisWave = std::clamp(count, 0, kReinforcedDoorMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的精英撑杆数量。 */
void Board::RestoreElitePolevaulterWaveSpawnCount(int count)
{
	mElitePolevaultersSpawnedThisWave = std::clamp(count, 0, kElitePolevaulterMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的鎏金冰车数量。 */
void Board::RestoreGildedZamboniWaveSpawnCount(int count)
{
	mGildedZambonisSpawnedThisWave = std::clamp(count, 0, kGildedZamboniMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的精英海豚骑士数量。 */
void Board::RestoreEliteDolphinRiderWaveSpawnCount(int count)
{
	mEliteDolphinRidersSpawnedThisWave =
		std::clamp(count, 0, kEliteDolphinRiderMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的精英小丑数量。 */
void Board::RestoreEliteJackInTheBoxWaveSpawnCount(int count)
{
	mEliteJackInTheBoxesSpawnedThisWave =
		std::clamp(count, 0, kEliteJackInTheBoxMaxPerWave);
}

void Board::RestoreEliteDiggerWaveSpawnCount(int count)
{
	mEliteDiggersSpawnedThisWave = std::clamp(count, 0, kEliteDiggerMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的精英跳跳数量。 */
void Board::RestoreElitePogoWaveSpawnCount(int count)
{
	mElitePogosSpawnedThisWave = std::clamp(count, 0, kElitePogoMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的精英扶梯数量。 */
void Board::RestoreEliteLadderWaveSpawnCount(int count)
{
	mEliteLaddersSpawnedThisWave = std::clamp(count, 0, kEliteLadderMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的导流投篮车数量。 */
void Board::RestoreEliteCatapultWaveSpawnCount(int count)
{
	mEliteCatapultsSpawnedThisWave = std::clamp(
		count, 0, kEliteCatapultMaxPerWave);
}

/** 恢复冒险红眼的波次名额；无尽不消费也不保留该上限计数。 */
void Board::RestoreRedeyeGargantuarWaveSpawnCount(int count)
{
	mRedeyeGargantuarsSpawnedThisWave = mIsSurvival ? 0 : std::clamp(
		count, 0, kRedeyeGargantuarMaxPerAdventureWave);
}

/** 夹紧并恢复当前波已经正式生成的绝缘僵尸数量。 */
void Board::RestoreInsulatorWaveSpawnCount(int count)
{
	mInsulatorsSpawnedThisWave = std::clamp(count, 0, kInsulatorMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的劫持者数量。 */
void Board::RestoreHijackerWaveSpawnCount(int count)
{
	mHijackersSpawnedThisWave = std::clamp(count, 0, kHijackerMaxPerWave);
}

/** 恢复成功处决后的波次冷却；旧档缺字段时由调用方传入中性零值。 */
void Board::RestoreHijackerSpawnCooldown(int wavesRemaining, bool blockedThisWave)
{
	mHijackerSpawnCooldownWavesRemaining = std::clamp(
		wavesRemaining, 0, kHijackerSpawnCooldownWaves);
	mHijackerSpawnBlockedThisWave = blockedThisWave;
}

/** 成功释放立即封锁本波余下候选，并从下一波起完整跳过两波。 */
void Board::BeginHijackerSpawnCooldown()
{
	mHijackerSpawnCooldownWavesRemaining = kHijackerSpawnCooldownWaves;
	mHijackerSpawnBlockedThisWave = true;
}

/** 在正式生成前消费一份未来冷却，封锁标志保留到下一次换波。 */
void Board::AdvanceHijackerSpawnCooldownForNewWave()
{
	mHijackerSpawnBlockedThisWave = mHijackerSpawnCooldownWavesRemaining > 0;
	if (mHijackerSpawnCooldownWavesRemaining > 0) {
		--mHijackerSpawnCooldownWavesRemaining;
	}
}

/** 夹紧并恢复当前波已经正式生成的接地僵尸数量。 */
void Board::RestoreGroundingZombieWaveSpawnCount(int count)
{
	mGroundingZombiesSpawnedThisWave = std::clamp(
		count, 0, kGroundingZombieMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的雪橇车队数量；三名跟随者不计入此值。 */
void Board::RestoreBobsledTeamWaveSpawnCount(int count)
{
	mBobsledTeamsSpawnedThisWave = std::clamp(count, 0, kBobsledTeamMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的冰墙工程师数量。 */
void Board::RestoreIceWallEngineerWaveSpawnCount(int count)
{
	mIceWallEngineersSpawnedThisWave = std::clamp(
		count, 0, kIceWallEngineerMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的冰裂钻机数量。 */
void Board::RestoreIceCrackDrillWaveSpawnCount(int count)
{
	mIceCrackDrillsSpawnedThisWave = std::clamp(
		count, 0, kIceCrackDrillMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的气象干扰僵尸数量。 */
void Board::RestoreWeatherJammerWaveSpawnCount(int count)
{
	mWeatherJammersSpawnedThisWave = std::clamp(
		count, 0, kWeatherJammerMaxPerWave);
}

/** 夹紧并恢复当前波已经正式生成的冰像处刑者数量。 */
void Board::RestoreIceStatueExecutionerWaveSpawnCount(int count)
{
	mIceStatueExecutionersSpawnedThisWave = std::clamp(
		count, 0, kIceStatueExecutionerMaxPerWave);
}

/** 旧档缺字段时以零值恢复；本波计数按当前关卡的实际限额夹紧。 */
void Board::RestoreSnowBurrowSpawnState(int count, bool tutorialHoleSpawnConsumed)
{
	const int maxPerWave = mLevel == kSnowBurrowTutorialLevel
		? kSnowBurrowTutorialMaxPerWave : kSnowBurrowCompositeMaxPerWave;
	mSnowBurrowsSpawnedThisWave = std::clamp(count, 0, maxPerWave);
	mSnowBurrowTutorialHoleSpawnConsumed = tutorialHoleSpawnConsumed;
}

void Board::RestoreAdaptiveHelmetSpawnState(
	int waveCount, bool tutorialWaveSpawned)
{
	const bool tutorialLevel = !mIsSurvival
		&& mLevel == kAdaptiveHelmetTutorialLevel;
	mAdaptiveHelmetsSpawnedThisWave = std::clamp(
		waveCount, 0, kAdaptiveHelmetMaxPerWave);
	mAdaptiveHelmetTutorialWaveSpawned = tutorialLevel
		&& tutorialWaveSpawned;
}

void Board::RestoreThermalSniperSpawnState(int waveCount, bool tutorialSpawned)
{
	const int maxPerWave = mLevel == kThermalSniperTutorialLevel
		? kThermalSniperTutorialMaxPerWave : kThermalSniperCompositeMaxPerWave;
	mThermalSnipersSpawnedThisWave = std::clamp(waveCount, 0, maxPerWave);
	mThermalSniperTutorialSpawned = !mIsSurvival
		&& (mLevel == kThermalSniperTutorialLevel
			|| mLevel == kThermalSniperCompositeLevel)
		&& tutorialSpawned;
}

/**
 * 将正式波次选中的普通舞王按当前黑夜强天气变异为精英舞王。
 * 每波至多成功三次；失败点数不消费上限，天气减弱后既有精英仍保留。
 */
ZombieType Board::ResolveRainMutationType(ZombieType selected, int mutationRoll)
{
	if (selected != ZombieType::ZOMBIE_DANCER
		|| !GameAPP::GetInstance().GetBackgroundIsNight(mBackGround)
		|| mRainIntensity != RainIntensity::HEAVY
		|| mTyphoonStrength == TyphoonStrength::NONE) {
		return selected;
	}

	const int roll = mutationRoll > 0 ? mutationRoll : GameRandom::Range(1, 100);
	if (roll < 1 || roll > 100 || roll > kEliteDancerMutationChancePercent) return selected;
	// 已达上限的成功变异候选直接作废，由正式挑选循环继续抽取；不能退回普通舞王占掉本次刷新。
	if (mEliteDancersSpawnedThisWave >= kEliteDancerMaxPerWave) {
		return ZombieType::NUM_ZOMBIE_TYPES;
	}
	++mEliteDancersSpawnedThisWave;
	return ZombieType::ZOMBIE_ELITE_DANCER;
}

/**
 * 解析正式波次候选。超过每波上限时返回 NUM_ZOMBIE_TYPES，调用方继续挑选且不消耗预算。
 */
ZombieType Board::ResolveWaveZombieType(ZombieType selected, int mutationRoll)
{
	if (selected == ZombieType::ZOMBIE_REINFORCED_DOOR) {
		if (mReinforcedDoorsSpawnedThisWave >= kReinforcedDoorMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mReinforcedDoorsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_POLEVAULTER) {
		if (mElitePolevaultersSpawnedThisWave >= kElitePolevaulterMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mElitePolevaultersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_GILDED_ZAMBONI) {
		if (mGildedZambonisSpawnedThisWave >= kGildedZamboniMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mGildedZambonisSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER) {
		if (mEliteDolphinRidersSpawnedThisWave >= kEliteDolphinRiderMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mEliteDolphinRidersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX) {
		if (mEliteJackInTheBoxesSpawnedThisWave >= kEliteJackInTheBoxMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mEliteJackInTheBoxesSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_DIGGER) {
		if (mEliteDiggersSpawnedThisWave >= kEliteDiggerMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mEliteDiggersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_POGO) {
		if (mElitePogosSpawnedThisWave >= kElitePogoMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mElitePogosSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_LADDER) {
		if (mEliteLaddersSpawnedThisWave >= kEliteLadderMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mEliteLaddersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ELITE_CATAPULT) {
		if (mEliteCatapultsSpawnedThisWave >= kEliteCatapultMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mEliteCatapultsSpawnedThisWave;
	}
	// 红眼只在冒险波次限额；无尽刻意不读写此计数，保留其原有的无限压力曲线。
	if (!mIsSurvival && selected == ZombieType::ZOMBIE_REDEYE_GARGANTUAR) {
		if (mRedeyeGargantuarsSpawnedThisWave >= kRedeyeGargantuarMaxPerAdventureWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mRedeyeGargantuarsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_INSULATOR) {
		if (mInsulatorsSpawnedThisWave >= kInsulatorMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mInsulatorsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_HIJACKER) {
		const bool forcedTutorialWave = !mIsSurvival
			&& mLevel == kHijackerTutorialLevel
			&& mCurrentWave == kHijackerTutorialWave;
		if ((!forcedTutorialWave && IsHijackerSpawnBlocked())
			|| mHijackersSpawnedThisWave >= kHijackerMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mHijackersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_GROUNDING) {
		if (mGroundingZombiesSpawnedThisWave >= kGroundingZombieMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mGroundingZombiesSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_BOBSLED_TEAM) {
		if (mBobsledTeamsSpawnedThisWave >= kBobsledTeamMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mBobsledTeamsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ICE_WALL_ENGINEER) {
		if (mIceWallEngineersSpawnedThisWave >= kIceWallEngineerMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mIceWallEngineersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ICE_CRACK_DRILL) {
		if (mIceCrackDrillsSpawnedThisWave >= kIceCrackDrillMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mIceCrackDrillsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_WEATHER_JAMMER) {
		if (mWeatherJammersSpawnedThisWave >= kWeatherJammerMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mWeatherJammersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER) {
		if (mIceStatueExecutionersSpawnedThisWave
			>= kIceStatueExecutionerMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mIceStatueExecutionersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_SNOW_BURROW) {
		const int maxPerWave = mLevel == kSnowBurrowTutorialLevel
			? kSnowBurrowTutorialMaxPerWave : kSnowBurrowCompositeMaxPerWave;
		if (mSnowBurrowsSpawnedThisWave >= maxPerWave
			|| CountActiveOrPendingZombieType(selected) >= maxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mSnowBurrowsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_ADAPTIVE_HELMET) {
		const bool tutorialLevel = !mIsSurvival
			&& mLevel == kAdaptiveHelmetTutorialLevel;
		if ((tutorialLevel && !mPolarFirstWhiteoutCompleted)
			|| mAdaptiveHelmetsSpawnedThisWave >= kAdaptiveHelmetMaxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mAdaptiveHelmetsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_THERMAL_SNIPER && !mIsSurvival) {
		const int maxPerWave = mLevel == kThermalSniperTutorialLevel
			? kThermalSniperTutorialMaxPerWave : kThermalSniperCompositeMaxPerWave;
		if ((mLevel == kThermalSniperTutorialLevel
				&& !mThermalSniperTutorialSpawned)
			|| mThermalSnipersSpawnedThisWave >= maxPerWave) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mThermalSnipersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_AURORA_PRIEST) {
		if (mAuroraPriestsSpawnedThisWave >= kAuroraPriestMaxPerWave
			|| CountActiveOrPendingZombieType(selected) >= kAuroraPriestMaxActive) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mAuroraPriestsSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_POLAR_CLOCKMAKER) {
		if (mClockmakersSpawnedThisWave >= kPolarClockmakerMaxPerWave
			|| CountActiveOrPendingZombieType(selected) >= kPolarClockmakerMaxActive) {
			return ZombieType::NUM_ZOMBIE_TYPES;
		}
		++mClockmakersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_CRYSTAL_DRUMMER) {
		if (mCrystalDrummersSpawnedThisWave >= 1 || CountActiveOrPendingZombieType(selected) >= 2) return ZombieType::NUM_ZOMBIE_TYPES;
		++mCrystalDrummersSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_SUN_THIEF) {
		if (mSunThievesSpawnedThisWave >= 2 || CountActiveOrPendingZombieType(selected) >= 3) return ZombieType::NUM_ZOMBIE_TYPES;
		++mSunThievesSpawnedThisWave;
	}
	if (selected == ZombieType::ZOMBIE_CRYSTAL_HORN_MINER) {
		if (mCrystalMinersSpawnedThisWave >= 1 || CountActiveOrPendingZombieType(selected) >= 2)
			return ZombieType::NUM_ZOMBIE_TYPES;
		++mCrystalMinersSpawnedThisWave;
	}
	return ResolveRainMutationType(selected, mutationRoll);
}

int Board::CountActiveOrPendingZombieType(ZombieType type) const
{
	int count = static_cast<int>(std::count_if(mPendingSnowHoleSpawns.begin(),
		mPendingSnowHoleSpawns.end(), [type](const PendingSnowHoleSpawn& pending) {
			return pending.type == type;
		}));
	for (int id : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (zombie && zombie->IsActive() && !zombie->IsDying()
			&& !zombie->IsMindControlled() && zombie->mZombieType == type) {
			++count;
		}
	}
	return count;
}

Zombie* Board::CreateResolvedWaveZombie(ZombieType actualType, int row, float x)
{
	const ZombieType terrainType = ResolveTerrainZombieType(actualType, row);
	Zombie* zombie = CreateZombie(terrainType, row, x);
	// 只在正式候选真正创建成功后解锁；解析失败、预览、读档和通用直造都不能伪造遭遇。
	if (zombie && actualType == ZombieType::ZOMBIE_ELITE_DANCER) {
		GameAPP::GetInstance().RecordEliteDancerEncounter();
	}
	return zombie;
}

void Board::FinalizeIceStatueExecutionerLoad()
{
	std::vector<int> zombieIDs = mEntityRegistry.GetAllZombieIDs();
	std::sort(zombieIDs.begin(), zombieIDs.end());
	for (const int zombieID : zombieIDs) {
		if (auto* executioner = dynamic_cast<IceStatueExecutionerZombie*>(
			mEntityRegistry.GetZombie(zombieID))) {
			executioner->FinalizeIceSealLoad();
		}
	}

	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	for (const int plantID : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsIceSealed()) continue;
		const int ownerID = plant->GetIceSealOwnerZombieID();
		auto* executioner = dynamic_cast<IceStatueExecutionerZombie*>(
			mEntityRegistry.GetZombie(ownerID));
		if (!executioner || !executioner->OwnsIceSealFor(plantID)
			|| !IsPlantFootprintFrozen(
				plant->mPlantType, plant->mRow, plant->mColumn)) {
			plant->ReleaseIceSeal(ownerID);
		}
	}
}

/**
 * 正式波次只在活动雪穴处建立延迟事务；预算与行选择已经由调用方提交，
 * 因而预警期间封穴只能把这一只改回右侧入口，不能取消或重抽僵尸。
 */
bool Board::CreateOrQueueWaveZombie(ZombieType actualType, int row, float rightEdgeX,
	bool tutorialSnowBurrow)
{
	if (SupportsPolarNightEnvironment() && row >= 0
		&& row < static_cast<int>(mSnowHoles.size())
		&& mSnowHoles[row].phase == SnowHolePhase::ACTIVE) {
		const int holeColumn = mSnowHoles[row].column;
		const bool warningAlreadyVisible = std::any_of(
			mPendingSnowHoleSpawns.begin(), mPendingSnowHoleSpawns.end(),
			[row, holeColumn](const PendingSnowHoleSpawn& pending) {
				return pending.row == row && pending.holeColumn == holeColumn;
			});
		mPendingSnowHoleSpawns.push_back({ actualType, row, holeColumn,
			mCurrentWave, kPolarHoleSpawnWarningSeconds, tutorialSnowBurrow });
		if (!warningAlreadyVisible && g_particleSystem) {
			g_particleSystem->EmitEffect("SnowHolePuff",
				GetCellCenterPosition(row, holeColumn));
		}
		if (!warningAlreadyVisible) {
			AudioSystem::PlaySound(
				ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES, 0.35f);
		}
		return true;
	}

	Zombie* zombie = CreateResolvedWaveZombie(actualType, row, rightEdgeX);
	if (!zombie) return false;
	AssignMistFuelReward(zombie);
	return true;
}

void Board::TrySpawnAdaptiveHelmetTutorialWave()
{
	if (mIsSurvival || mLevel != kAdaptiveHelmetTutorialLevel
		|| mAdaptiveHelmetTutorialWaveSpawned
		|| !mPolarFirstWhiteoutCompleted) {
		return;
	}

	const int row = SelectSpawnRow(ZombieType::ZOMBIE_ADAPTIVE_HELMET);
	if (row < 0) return;
	const ZombieType actualType = ResolveWaveZombieType(
		ZombieType::ZOMBIE_ADAPTIVE_HELMET);
	if (actualType == ZombieType::NUM_ZOMBIE_TYPES) return;

	Zombie* zombie = CreateResolvedWaveZombie(actualType, row,
		static_cast<float>(SCENE_WIDTH) + 40.0f);
	if (!zombie) {
		--mAdaptiveHelmetsSpawnedThisWave;
		return;
	}
	mAdaptiveHelmetTutorialWaveSpawned = true;
	UpdateZombieMetrics();
}

bool Board::IsPoolBackground() const
{
	return mBackGround == Background::WATER_POOL
		|| mBackGround == Background::NIGHT_WATER_POOL;
}

bool Board::IsRoofBackground() const
{
	return mBackGround == Background::ROOF
		|| mBackGround == Background::NIGHT_ROOF;
}

float Board::GetRoofSlopeEndX() const
{
	return CELL_INITALIZE_POS_X
		+ static_cast<float>(kRoofSlopeColumnCount) * CELL_COLLIDER_SIZE_X;
}

/**
 * 把 C# 屋顶的连续坡面换算为当前 1100 宽场景的 Board 网格坐标。
 * 平台段保持行中心不变，房屋侧按 1:4 坡度向屏幕下方抬升。
 */
float Board::GetRowCenterYAtX(int row, float worldX) const
{
	if (row < 0 || row >= mRows) return -1.0f;

	float centerY = mCellInitialY + static_cast<float>(row) * mCellHeight
		+ mCellHeight * 0.5f;
	if (IsRoofBackground()) {
		centerY += std::max(0.0f, GetRoofSlopeEndX() - worldX)
			* kRoofSlopeRisePerPixel;
	}
	return centerY;
}

float Board::GetMowerTerrainY(int row, float worldX) const
{
	const float centerY = GetRowCenterYAtX(row, worldX);
	if (centerY < 0.0f) return centerY;
	return centerY + (IsRoofBackground() ? kRoofMowerTerrainOffsetY : -3.0f);
}

bool Board::TryPreventIceExecutionSeal(Plant& target) const
{
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	for (const int plantID : plantIDs) {
		Plant* provider = mEntityRegistry.GetPlant(plantID);
		if (!provider || !provider->IsActive() || provider->IsSquished()
			|| provider->IsBungeeTargeted() || provider->IsIceSealed()
			|| provider->IsShutdown()
			|| std::abs(provider->mRow - target.mRow) > 1
			|| std::abs(provider->mColumn - target.mColumn) > 1) {
			continue;
		}
		if (provider->TryPreventIceExecutionSealFor(&target)) return true;
	}
	return false;
}

bool Board::IsZombieObscuredByFog(const Zombie* zombie) const
{
	if (!zombie || !SupportsPlanternMechanics() || mColumns <= 0) return false;
	const int column = std::clamp(static_cast<int>(
		(zombie->GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X),
		0, mColumns - 1);
	const int row = std::clamp(zombie->mRow, 0, mRows - 1);
	return GetFogCellAlpha(row, column) > kFogTargetingAlphaThreshold;
}

bool Board::CanPlantAcquireZombie(const Plant* plant, const Zombie* zombie)
{
	if (!plant || !zombie || !plant->CanAcquireZombie(zombie)) return false;
	if (MineBlocksSegment(plant->GetPosition(), zombie->GetPosition())) return false;
	if (IsPolarSnowBlindActive() && mDawnNavigationTimer <= 0.0f) {
		auto centerOf = [](const GameObject* object,
			const ColliderComponent* collider) {
			if (!object) return Vector::zero();
			if (!collider) return object->GetTransform()
				? object->GetTransform()->GetPosition() : Vector::zero();
			const SDL_FRect bounds = collider->GetBoundingBox();
			return Vector(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f);
		};
		const Vector plantCenter = centerOf(plant, plant->GetColliderComponent());
		const Vector zombieCenter = centerOf(zombie, zombie->GetColliderComponent());
		if ((zombieCenter - plantCenter).sqrMagnitude()
			> kPolarSnowBlindRange * kPolarSnowBlindRange
			&& !PreparePolarLobbedNavigation(plant)) return false;
	}
	if (!SupportsPlanternMechanics()) return true;
	const Vector plantPosition = plant->GetPosition();
	const Vector zombiePosition = zombie->GetPosition();
	if (std::abs(zombie->mRow - plant->mRow) <= 1
		&& std::abs(zombiePosition.x - plantPosition.x)
			<= kFogCloseDetectionRange) {
		return true;
	}

	const int column = std::clamp(static_cast<int>(
		(zombiePosition.x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X),
		0, mColumns - 1);
	const int row = std::clamp(zombie->mRow, 0, mRows - 1);
	if (GetFogCellAlpha(row, column) <= kFogTargetingAlphaThreshold) return true;

	// 当前格仍被浓雾遮挡时，只允许从植物一侧已经看清的边界再深入一格。
	if (zombiePosition.x == plantPosition.x) return false;
	const int directionTowardPlant = zombiePosition.x > plantPosition.x ? -1 : 1;
	for (int step = 1; step <= kFogTargetingMarginColumns; ++step) {
		const int adjacentColumn = column + directionTowardPlant * step;
		if (adjacentColumn < 0 || adjacentColumn >= mColumns) break;
		if (GetFogCellAlpha(row, adjacentColumn) <= kFogTargetingAlphaThreshold) {
			return true;
		}
	}
	return false;
}

float Board::GetPlanternSunProductionMultiplier(const Plant* producer) const
{
	if (!producer) return 1.0f;
	const Plantern* plantern = GetActivePlantern();
	if (!plantern || !plantern->HasUsableLight()) return 1.0f;
	const float illumination = GetPlanternIllumination(producer->mRow, producer->mColumn);
	if (illumination <= 0.0f) return 1.0f;

	float peak = 1.0f;
	switch (plantern->GetGear()) {
	case PlanternGear::OFF: break;
	case PlanternGear::LOW: peak = 1.10f; break;
	case PlanternGear::MEDIUM: peak = 1.20f; break;
	case PlanternGear::HIGH: peak = 1.35f; break;
	}
	return 1.0f + (peak - 1.0f) * illumination;
}

float Board::GetPlanternFuel() const
{
	const Plantern* plantern = GetActivePlantern();
	return plantern ? plantern->GetFuel() : 0.0f;
}

float Board::GetPlanternFuelRatio() const
{
	const Plantern* plantern = GetActivePlantern();
	return plantern ? plantern->GetFuelRatio() : 0.0f;
}

int Board::GetPlanternGearValue() const
{
	const Plantern* plantern = GetActivePlantern();
	return plantern ? static_cast<int>(plantern->GetGear()) : 0;
}

float Board::GetPlanternFuelFullHintTimer() const
{
	const Plantern* plantern = GetActivePlantern();
	return plantern ? plantern->GetFuelFullHintTimer() : 0.0f;
}

void Board::SetPlanternGear(PlanternGear gear)
{
	if (Plantern* plantern = GetActivePlantern()) plantern->SetGear(gear);
}

void Board::NotifyPlanternRemoved(int plantID)
{
	if (mActivePlanternID == plantID) mActivePlanternID = NULL_PLANT_ID;
}

void Board::TogglePlanternGearMenu()
{
	if (mPresentation && GetActivePlantern()) {
		mPresentation->TogglePlanternGearMenu();
	}
}

void Board::CollectMistFuelFromZombie(Zombie* zombie)
{
	if (!zombie || !SupportsPlanternMechanics()
		|| zombie->IsMindControlled()) return;
	const float reward = zombie->ClaimMistFuelReward()
		* static_cast<float>(mPerkManager.GetMistFuelMultiplier());
	if (reward <= 0.0f) return;

	Plantern* plantern = GetActivePlantern();
	if (!plantern) return;
	const float accepted = plantern->ReserveFuel(reward);
	if (accepted <= 0.0f) return;

	GameObjectManager::GetInstance().CreateGameObject<MistFuel>(
		LAYER_EFFECTS, this,
		zombie->GetVisualPosition() + Vector(0.0f, -24.0f),
		plantern->mPlantID, accepted);
}

void Board::RelayZombieDeathWard(const Zombie* source)
{
	if (!source || !mPerkManager.HasZombieDeathRelay()
		|| source->IsMindControlled()) return;
	Zombie* recipient = nullptr;
	for (int zombieID : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* candidate = mEntityRegistry.GetZombie(zombieID);
		if (!candidate || candidate == source || !candidate->IsActive()
			|| candidate->IsDying() || candidate->IsMindControlled()
			|| candidate->mRow != source->mRow) continue;
		if (!recipient || candidate->GetPosition().x < recipient->GetPosition().x
			|| (candidate->GetPosition().x == recipient->GetPosition().x
				&& candidate->mZombieID < recipient->mZombieID)) {
			recipient = candidate;
		}
	}
	if (recipient) recipient->mFreeHitsRemaining = std::max(
		recipient->mFreeHitsRemaining, 1);
}

bool Board::SetPlanternFuelForTesting(float fuel)
{
	if (!std::isfinite(fuel) || fuel < 0.0f) return false;
	Plantern* plantern = GetActivePlantern();
	if (!plantern) return false;
	plantern->SetFuel(fuel);
	return true;
}

bool Board::AwardPlanternFuelForTesting(float amount)
{
	if (!std::isfinite(amount) || amount <= 0.0f) return false;
	Plantern* plantern = GetActivePlantern();
	if (!plantern) return false;
	plantern->AddFuel(amount);
	return true;
}

bool Board::IsPoolRow(int row) const
{
	return IsPoolBackground() && row >= kPoolFirstRow && row <= kPoolLastRow;
}

bool Board::IsPoolSquare(int row, int col) const
{
	return IsPoolRow(row) && col >= 0 && col < mColumns;
}

bool Board::IsPoolWorldPosition(int row, float x) const
{
	const float poolRight = CELL_INITALIZE_POS_X
		+ static_cast<float>(mColumns) * CELL_COLLIDER_SIZE_X;
	return IsPoolRow(row) && x >= CELL_INITALIZE_POS_X && x < poolRight;
}

/** 返回指定类型能否被正式选行逻辑放进水路；无水路状态机的品种在此集中排除。 */
bool Board::CanZombieTypeSpawnInPool(ZombieType type) const
{
	switch (type) {
	case ZombieType::ZOMBIE_ZAMBONI:
	case ZombieType::ZOMBIE_GILDED_ZAMBONI:
	case ZombieType::ZOMBIE_CATAPULT:
	case ZombieType::ZOMBIE_ELITE_CATAPULT:
	case ZombieType::ZOMBIE_POGO:
	case ZombieType::ZOMBIE_ELITE_POGO:
	case ZombieType::ZOMBIE_DIGGER:
	case ZombieType::ZOMBIE_ELITE_DIGGER:
	case ZombieType::ZOMBIE_BOBSLED_TEAM:
	case ZombieType::NUM_ZOMBIE_TYPES:
		return false;
	default:
		return true;
	}
}

Vector Board::GetCellCenterPosition(int row, int col) const
{
	const float cellLeftX = CELL_INITALIZE_POS_X
		+ static_cast<float>(col) * CELL_COLLIDER_SIZE_X;
	float centerY = mCellInitialY + static_cast<float>(row) * mCellHeight
		+ mCellHeight * 0.5f;
	if (IsRoofBackground()) {
		// 格子沿用原版按列离散抬升；僵尸则通过 GetRowCenterYAtX 沿同一坡面连续移动。
		centerY += std::max(0.0f, GetRoofSlopeEndX() - cellLeftX)
			* kRoofSlopeRisePerPixel;
	}
	return Vector(cellLeftX + CELL_COLLIDER_SIZE_X * 0.5f, centerY);
}

void Board::ExtendIceTrail(int row, float frontX)
{
	if (row < 0 || row >= mRows || row >= static_cast<int>(mIceTimer.size())
		|| IsPoolRow(row)) return;

	// 原版 Zamboni 在屋顶把冰道左缘钳在坡顶平台；斜坡既不结冰，也不参与禁种判定。
	const float leftLimit = IsRoofBackground() ? GetRoofSlopeEndX() : kIceTrailLeftLimit;
	const float clampedFront = std::max(frontX, leftLimit);
	mIceMinX[row] = std::min(mIceMinX[row], clampedFront);
	// 车辆还在屏幕右侧入场时就激活冰道；左缘钳在屏幕右边界，进入画面后自然连续增长。
	mIceTimer[row] = kIceTrailDuration;
}

void Board::ExtendGoldenIceTrail(int row, float frontX)
{
	if (row < 0 || row >= mRows || row >= static_cast<int>(mGoldenIceTimer.size())
		|| IsPoolRow(row)) return;

	const float leftLimit = IsRoofBackground() ? GetRoofSlopeEndX() : kIceTrailLeftLimit;
	const float clampedFront = std::max(frontX, leftLimit);
	mGoldenIceMinX[row] = std::min(mGoldenIceMinX[row], clampedFront);
	mGoldenIceTimer[row] = kGoldenIceTrailDuration;
}

void Board::ShortenIceTrail(int row, float maxRemainingSeconds)
{
	if (row < 0 || row >= mRows || row >= static_cast<int>(mIceTimer.size())
		|| row >= static_cast<int>(mGoldenIceTimer.size())) return;
	const float limit = std::max(0.0f, maxRemainingSeconds);
	if (mIceTimer[row] > 0.0f) {
		mIceTimer[row] = std::min(mIceTimer[row], limit);
	}
	if (mGoldenIceTimer[row] > 0.0f) {
		mGoldenIceTimer[row] = std::min(mGoldenIceTimer[row], limit);
	}
}

bool Board::IsIceAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns
		|| row >= static_cast<int>(mIceTimer.size())
		|| row >= static_cast<int>(mGoldenIceTimer.size())) return false;
	auto trailCovers = [this, col](float minX, float timer) {
		if (timer <= 0.0f) return false;
		// 保留 mColumns 作为尚未覆盖棋盘的哨兵，避免场外冰道被夹到最后一格。
		const int startCol = std::clamp(static_cast<int>(std::floor(
			(minX + kIceTrailGridProbeOffset - CELL_INITALIZE_POS_X)
			/ CELL_COLLIDER_SIZE_X)), 0, mColumns);
		return col >= startCol;
	};
	return trailCovers(mIceMinX[row], mIceTimer[row])
		|| trailCovers(mGoldenIceMinX[row], mGoldenIceTimer[row]);
}

bool Board::IsGoldenIceAtWorld(int row, float worldX) const
{
	if (row < 0 || row >= mRows || row >= static_cast<int>(mGoldenIceTimer.size())
		|| mGoldenIceTimer[row] <= 0.0f) return false;
	return worldX >= mGoldenIceMinX[row] && worldX <= GetIceTrailRightX();
}

float Board::GetIceTrailMinX(int row) const
{
	return row >= 0 && row < mRows && row < static_cast<int>(mIceMinX.size())
		? mIceMinX[row] : 0.0f;
}

float Board::GetIceTrailTimeRemaining(int row) const
{
	return row >= 0 && row < mRows && row < static_cast<int>(mIceTimer.size())
		? mIceTimer[row] : 0.0f;
}

float Board::GetGoldenIceTrailMinX(int row) const
{
	return row >= 0 && row < mRows && row < static_cast<int>(mGoldenIceMinX.size())
		? mGoldenIceMinX[row] : 0.0f;
}

float Board::GetGoldenIceTrailTimeRemaining(int row) const
{
	return row >= 0 && row < mRows && row < static_cast<int>(mGoldenIceTimer.size())
		? mGoldenIceTimer[row] : 0.0f;
}

float Board::GetIceTrailRightX() const
{
	return static_cast<float>(SCENE_WIDTH);
}

void Board::UpdateIceTrails(float deltaTime)
{
	if (deltaTime <= 0.0f || mBoardState != BoardState::GAME) return;
	for (int row = 0; row < mRows && row < static_cast<int>(mIceTimer.size()); ++row) {
		if (mIceTimer[row] > 0.0f) {
			mIceTimer[row] = std::max(0.0f, mIceTimer[row] - deltaTime);
		}
		if (mIceTimer[row] <= 0.0f) {
			mIceMinX[row] = GetIceTrailRightX();
		}
		if (mGoldenIceTimer[row] > 0.0f) {
			mGoldenIceTimer[row] = std::max(0.0f, mGoldenIceTimer[row] - deltaTime);
		}
		if (mGoldenIceTimer[row] <= 0.0f) {
			mGoldenIceMinX[row] = GetIceTrailRightX();
		}
	}
}

void Board::DrawIceTrails(Graphics* g) const
{
	if (!g) return;
	auto& resources = ResourceManager::GetInstance();
	const Texture* iceBody = resources.GetTexture(ResourceKeys::Textures::IMAGE_ICE, false);
	const Texture* iceCap = resources.GetTexture(ResourceKeys::Textures::IMAGE_ICE_CAP, false);
	const Texture* goldenBody = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_GOLDEN_ICE, false);
	const Texture* goldenCap = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_GOLDEN_ICE_CAP, false);

	const float boardRight = GetIceTrailRightX();
	auto drawTrail = [this, g](int row, float minX, float remaining, float rightX,
		const Texture* body, const Texture* cap) {
		if (remaining <= 0.0f || rightX <= minX + 0.01f || !body || !cap
			|| body->width <= 0 || body->height <= 0) return;
		const float alpha = 255.0f * std::clamp(
			remaining / kIceTrailFadeDuration, 0.0f, 1.0f);
		const glm::vec4 tint(255.0f, 255.0f, 255.0f, alpha);
		// 原版固定取最右侧平地行高；屋顶冰道因此始终水平，且不会沿斜坡弯折。
		const float drawY = GetRowCenterYAtX(row, GetIceTrailRightX())
			- mCellHeight * 0.5f + kIceTrailTopOffset;
		const float bodyStart = minX + kIceTrailCapBodyOverlap;
		const float length = std::max(0.0f, rightX - bodyStart);
		float drawX = bodyStart;
		float firstWidth = std::fmod(length, static_cast<float>(body->width));
		if (firstWidth > 0.01f) {
			g->DrawTextureRegion(body,
				static_cast<float>(body->width) - firstWidth, 0.0f,
				firstWidth, static_cast<float>(body->height),
				drawX, drawY, firstWidth, static_cast<float>(body->height),
				0.0f, tint);
			drawX += firstWidth;
		}
		while (drawX < rightX - 0.01f) {
			const float width = std::min(static_cast<float>(body->width), rightX - drawX);
			g->DrawTextureRegion(body, 0.0f, 0.0f, width,
				static_cast<float>(body->height), drawX, drawY, width,
				static_cast<float>(body->height), 0.0f, tint);
			drawX += width;
		}
		g->DrawTexture(cap, minX, drawY,
			static_cast<float>(cap->width), static_cast<float>(cap->height),
			0.0f, tint);
	};

	for (int row = 0; row < mRows && row < static_cast<int>(mIceTimer.size()); ++row) {
		const bool goldenActive = mGoldenIceTimer[row] > 0.0f;
		// 普通冰道只画到黄色冰道左缘；重叠段完全交给黄色材质，避免半透明蓝底串色。
		const float ordinaryRight = goldenActive
			? std::clamp(mGoldenIceMinX[row], mIceMinX[row], boardRight)
			: boardRight;
		drawTrail(row, mIceMinX[row], mIceTimer[row], ordinaryRight, iceBody, iceCap);
		drawTrail(row, mGoldenIceMinX[row], mGoldenIceTimer[row],
			boardRight, goldenBody, goldenCap);
	}
}

void Board::InitializeCell(int rows, int cols)
{
	mRows = rows + 1;
	mColumns = cols + 1;
	if (IsPoolBackground()) {
		mCellInitialY = kPoolCellInitialY;
		mCellHeight = kPoolCellHeight;
	}
	else if (IsRoofBackground()) {
		mCellInitialY = CELL_INITALIZE_POS_Y + kRoofCellInitialYOffsetY;
		mCellHeight = kRoofCellHeight;
	}
	else {
		mCellInitialY = CELL_INITALIZE_POS_Y;
		mCellHeight = CELL_COLLIDER_SIZE_Y;
	}
	mCells.resize(mRows);
	for (int i = 0; i < mRows; i++)
	{
		mCells[i].resize(mColumns);
		for (int j = 0; j < mColumns; j++)
		{
			const Vector center = GetCellCenterPosition(i, j);
			Vector position(center.x - CELL_COLLIDER_SIZE_X * 0.5f,
				center.y - mCellHeight * 0.5f);
			Cell* cell = GameObjectManager::GetInstance().CreateGameObject<Cell>(
				LAYER_BACKGROUND, i, j, position,
				Vector(CELL_COLLIDER_SIZE_X, mCellHeight));
			mCells[i][j] = cell;
		}
	}
}

void Board::CreateBoom(const Vector& position, int plantRow, int damage)
{
	g_particleSystem->EmitEffect("CherryBomb", position);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CHERRYBOMB, 0.4f);
	ShakeBoard(3.0f, -4.0f);   // 原版 ShakeBoard(3,-4)：0.12s 单次弹跳

	// 水路僵尸的 Transform 含美术下沉，纵向命中必须使用僵尸与植物的逻辑行。
	for (int row = plantRow - 1; row <= plantRow + 1; ++row) {
		mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (zombie->IsMindControlled() || MineBlocksSegment(position, zombie->GetPosition())) return;
			if (std::abs(zombie->GetPosition().x - position.x) <= 130.0f) {
				// 统一灰烬入口内部决定化灰或数值扣血；特殊僵尸可拒绝化灰并限制每次灰烬伤害。
				zombie->TakePlantAshDamage(damage);
			}
		});
	}
	// 原版对僵尸使用圆形命中，但扶梯另按爆心格的 3x3 方形范围清除。
	RemoveLaddersInBlastSquare(position, plantRow, 1);
}

void Board::CreateDoomBoom(const Vector& position, int plantRow, int damage)
{
	g_particleSystem->EmitEffect("Doom", position);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DOOMSHROOM, 0.5f);
	// 比樱桃更剧烈：双倍振幅 + 0.5s 衰减正弦来回甩 5 个半周期（原版两者同为 3,-4，主人要求毁灭菇加强）
	ShakeBoard(6.0f, -9.0f, 0.5f, 5);
	std::vector<int> zombieIDs = mEntityRegistry.GetAllZombieIDs();
	for (auto zombieID : zombieIDs)
	{
		if (auto zombie = mEntityRegistry.GetZombie(zombieID)) {
			if (zombie->IsMindControlled() || MineBlocksSegment(position, zombie->GetPosition())) continue;
			// 圆(半径 250) vs 僵尸判定矩形 [x±25]×[y-65,y+35]，镜像原版 GetCircleRectOverlap；
			// 250 纵向天然覆盖 ±2 行有余，无需再按行数过滤
			Vector zombiePosition = zombie->GetPosition();
			float nearestX = std::clamp(position.x, zombiePosition.x - 25.0f, zombiePosition.x + 25.0f);
			float nearestY = std::clamp(position.y, zombiePosition.y - 65.0f, zombiePosition.y + 35.0f);
			float dx = position.x - nearestX;
			float dy = position.y - nearestY;
			if (dx * dx + dy * dy <= 250.0f * 250.0f)
			{
				zombie->TakePlantAshDamage(damage);
			}
		}
	}
	// 毁灭菇沿用原版 rowRange=3，清除爆心格周围 7x7 方形范围内的扶梯。
	RemoveLaddersInBlastSquare(position, plantRow, 3);
}

void Board::ShakeBoard(float amountX, float amountY, float durationSeconds, int oscillations)
{
	mShakeDuration = (durationSeconds > 0.0f) ? durationSeconds : 0.12f;
	mShakeTimer = mShakeDuration;
	mShakeAmountX = amountX;
	mShakeAmountY = amountY;
	mShakeOscillations = (oscillations > 0) ? oscillations : 1;
}

Vector Board::GetShakeOffset() const
{
	if (mShakeTimer <= 0.0f) return Vector(0.0f, 0.0f);
	float t = 1.0f - mShakeTimer / mShakeDuration;   // 0→1
	float wave;
	if (mShakeOscillations <= 1) {
		// 原版 TodCurves.Bounce：三角波 0→1→0（峰值在半程）
		wave = 1.0f - std::abs(1.0f - t * 2.0f);
	}
	else {
		// 衰减正弦：sin 每半周期变号=方向来回甩，(1-t) 包络收敛回原位
		wave = std::sin(t * 3.14159265f * static_cast<float>(mShakeOscillations)) * (1.0f - t);
	}
	// 原版符号：mX = base - amountX*wave（正 amountX 向左）、mY = 0 + amountY*wave
	return Vector(-mShakeAmountX * wave, mShakeAmountY * wave);
}

Crater* Board::AddCrater(int row, int column, float timeLeft)
{
	auto crater = GameObjectManager::GetInstance().CreateGameObjectAsShared<Crater>(
		LAYER_GAME_OBJECT, this, row, column, timeLeft);
	if (crater) {
		mCraters.push_back(crater);
	}
	return crater.get();
}

bool Board::HasCraterAt(int row, int column)
{
	bool found = false;
	// 弹坑到时在自身 Update 里自毁，这里顺带收缩失效的 weak_ptr
	mCraters.erase(std::remove_if(mCraters.begin(), mCraters.end(),
		[&](const std::weak_ptr<Crater>& weak) {
			auto crater = weak.lock();
			if (!crater || !crater->IsActive()) return true;
			if (crater->mRow == row && crater->mColumn == column) found = true;
			return false;
		}), mCraters.end());
	return found;
}

Ladder* Board::AddLadder(int row, int column, LadderStyle style)
{
	if (row < 0 || row >= mRows || column < 0 || column >= mColumns) return nullptr;
	if (Ladder* existing = GetLadderAt(row, column)) return existing;
	auto ladder = GameObjectManager::GetInstance().CreateGameObjectAsShared<Ladder>(
		LAYER_GAME_ZOMBIE, this, row, column, style);
	if (ladder) mLadders.push_back(ladder);
	return ladder.get();
}

Ladder* Board::GetLadderAt(int row, int column)
{
	Ladder* found = nullptr;
	mLadders.erase(std::remove_if(mLadders.begin(), mLadders.end(),
		[&](const std::weak_ptr<Ladder>& weak) {
			auto ladder = weak.lock();
			if (!ladder || !ladder->IsActive()) return true;
			if (ladder->mRow == row && ladder->mColumn == column) found = ladder.get();
			return false;
		}), mLadders.end());
	return found;
}

bool Board::RemoveLadderAt(int row, int column)
{
	bool removed = false;
	mLadders.erase(std::remove_if(mLadders.begin(), mLadders.end(),
		[&](const std::weak_ptr<Ladder>& weak) {
			auto ladder = weak.lock();
			if (!ladder || !ladder->IsActive()) return true;
			if (ladder->mRow != row || ladder->mColumn != column) return false;
			ladder->SetActive(false);
			GameObjectManager::GetInstance().DestroyGameObject(ladder.get());
			removed = true;
			return true;
		}), mLadders.end());
	return removed;
}

int Board::RemoveLaddersInRow(int row)
{
	int removed = 0;
	for (int column = 0; column < mColumns; ++column) {
		if (RemoveLadderAt(row, column)) ++removed;
	}
	return removed;
}

IceWall* Board::AddIceWall(int row, float centerX,
	int health, int maxHealth, float thawDamageRemainder,
	bool constructionComplete, int builderZombieID)
{
	if (row < 0 || row >= mRows || GetIceWall()
		|| (!constructionComplete && builderZombieID == NULL_ZOMBIE_ID)) return nullptr;
	auto wall = GameObjectManager::GetInstance().CreateGameObjectAsShared<IceWall>(
		LAYER_GAME_ZOMBIE, this, row, centerX,
		health, maxHealth, thawDamageRemainder,
		constructionComplete, builderZombieID);
	if (!wall) return nullptr;
	mIceWall = wall;
	return wall.get();
}

IceWall* Board::GetIceWall()
{
	auto wall = mIceWall.lock();
	if (!wall || !wall->IsActive()) {
		mIceWall.reset();
		return nullptr;
	}
	return wall.get();
}

IceWall* Board::GetIceWallInRow(int row)
{
	IceWall* wall = GetIceWall();
	return wall && wall->GetRow() == row ? wall : nullptr;
}

bool Board::RemoveIceWall(IceWall* wall)
{
	auto current = mIceWall.lock();
	if (!wall || !current || current.get() != wall) return false;
	mIceWall.reset();
	current->SetActive(false);
	GameObjectManager::GetInstance().DestroyGameObject(current);
	return true;
}

GroundRift* Board::AddGroundRift(int row, float frontX, int nextColumn,
	float downstreamDamageMultiplier)
{
	if (row < 0 || row >= mRows || mColumns <= 0 || !std::isfinite(frontX)
		|| !std::isfinite(downstreamDamageMultiplier)) return nullptr;
	auto rift = GameObjectManager::GetInstance().CreateGameObjectAsShared<GroundRift>(
		LAYER_GAME_PLANT, this, row, frontX,
		std::clamp(nextColumn, -1, mColumns - 1),
		std::clamp(downstreamDamageMultiplier, 0.0f, 1.0f));
	if (!rift) return nullptr;
	mGroundRifts.emplace_back(rift);
	return rift.get();
}

std::vector<GroundRift*> Board::GetGroundRifts()
{
	std::vector<GroundRift*> active;
	mGroundRifts.erase(std::remove_if(mGroundRifts.begin(), mGroundRifts.end(),
		[&active](const std::weak_ptr<GroundRift>& weak) {
			auto rift = weak.lock();
			if (!rift || !rift->IsActive()) return true;
			active.push_back(rift.get());
			return false;
		}), mGroundRifts.end());
	return active;
}

bool Board::RemoveGroundRift(GroundRift* rift)
{
	if (!rift) return false;
	for (auto it = mGroundRifts.begin(); it != mGroundRifts.end(); ++it) {
		auto current = it->lock();
		if (!current || !current->IsActive()) continue;
		if (current.get() != rift) continue;
		mGroundRifts.erase(it);
		current->SetActive(false);
		GameObjectManager::GetInstance().DestroyGameObject(current);
		return true;
	}
	return false;
}

int Board::RemoveLaddersInBlastSquare(
	const Vector& position, int centerRow, int cellRange)
{
	if (mRows <= 0 || mColumns <= 0) return 0;
	const int range = std::max(0, cellRange);
	const int blastRow = std::clamp(centerRow, 0, mRows - 1);
	// PixelToGridXKeepOnBoard 的当前场景等价换算：以格子左边界分段并钳在棋盘内。
	const int blastColumn = std::clamp(static_cast<int>(std::floor(
		(position.x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X)),
		0, mColumns - 1);

	int removed = 0;
	mLadders.erase(std::remove_if(mLadders.begin(), mLadders.end(),
		[&](const std::weak_ptr<Ladder>& weak) {
			auto ladder = weak.lock();
			if (!ladder || !ladder->IsActive()) return true;
			if (std::abs(ladder->mRow - blastRow) > range
				|| std::abs(ladder->mColumn - blastColumn) > range) {
				return false;
			}
			ladder->SetActive(false);
			GameObjectManager::GetInstance().DestroyGameObject(ladder.get());
			++removed;
			return true;
		}), mLadders.end());
	return removed;
}

bool Board::ExtractNearestLadderForMagnet(
	int plantRow, int plantColumn, MagneticItem& item)
{
	Ladder* nearest = nullptr;
	float nearestScore = 0.0f;
	for (const auto& weak : mLadders) {
		auto ladder = weak.lock();
		if (!ladder || !ladder->IsActive()) continue;
		const int columnDistance = ladder->mColumn - plantColumn;
		const int rowDistance = ladder->mRow - plantRow;
		const int cellDistance = std::max(std::abs(columnDistance), std::abs(rowDistance));
		if (cellDistance > 2) continue;
		const float score = static_cast<float>(cellDistance)
			+ static_cast<float>(std::abs(rowDistance)) * 0.05f;
		if (!nearest || score < nearestScore) {
			nearest = ladder.get();
			nearestScore = score;
		}
	}
	if (!nearest) return false;

	item.textureKey = nearest->GetTextureKey();
	item.worldPosition = nearest->GetVisualCenter();
	item.destinationOffset = Vector(
		10.0f + GameRandom::Range(-10.0f, 10.0f),
		GameRandom::Range(-10.0f, 10.0f));
	item.drawScale = 0.8f;
	return RemoveLadderAt(nearest->mRow, nearest->mColumn);
}

Sun* Board::CreateSun(const Vector& position, bool needAnimation)
{
	auto sun = GameObjectManager::GetInstance().CreateGameObjectAsShared<Sun>
		(LAYER_GAME_COIN, this, position, 0.85f, "Sun",
			needAnimation, true);
	if (sun) {
		mEntityRegistry.AddCoin(sun);
	}

	return sun.get();
}

Sun* Board::CreateSun(float x, float y, bool needAnimation) {
	return CreateSun(Vector(x, y), needAnimation);
}

SmallSun* Board::CreateSmallSun(const Vector& position, bool needAnimation)
{
	auto sun = GameObjectManager::GetInstance().CreateGameObjectAsShared<SmallSun>
		(LAYER_GAME_COIN, this, position, 0.6f, "SmallSun",
			needAnimation, true);
	if (sun) {
		mEntityRegistry.AddCoin(sun);
	}

	return sun.get();
}

SmallSun* Board::CreateSmallSun(float x, float y, bool needAnimation) {
	return CreateSmallSun(Vector(x, y), needAnimation);
}

void Board::CreateTrophy(const Vector& position)
{
	if (mTrophySpawned) return;
	mTrophySpawned = true;
	auto trophy = GameObjectManager::GetInstance().CreateGameObjectAsShared<Trophy>(
		LAYER_GAME_COIN, this, position);
	mTrophy = trophy;
}

void Board::CreateCobCannonExplosion(const Vector& position, int targetRow, int damage)
{
	constexpr float kCobBlastRadius = 115.0f; // 原版 CobBig 爆心半径，单位：px
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("CobCannonBlastMark", position);
		g_particleSystem->EmitEffect("CobCannonPopcorn", position);
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DOOMSHROOM, 0.5f);
	ShakeBoard(3.0f, -4.0f);

	for (int row = targetRow - 1; row <= targetRow + 1; ++row) {
		mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (!zombie || !zombie->IsActive() || zombie->IsDying()
				|| zombie->IsMindControlled()
				|| !zombie->CanBeAffectedByCobCannonExplosion()
				|| MineBlocksSegment(position, zombie->GetPosition())) return;
			SDL_FRect bounds{};
			if (const ColliderComponent* collider = zombie->GetColliderComponent()) {
				bounds = collider->GetBoundingBox();
			}
			else {
				const Vector zombiePosition = zombie->GetPosition();
				bounds = { zombiePosition.x - 25.0f, zombiePosition.y - 65.0f,
					50.0f, 100.0f };
			}
			const float nearestX = std::clamp(position.x, bounds.x, bounds.x + bounds.w);
			const float nearestY = std::clamp(position.y, bounds.y, bounds.y + bounds.h);
			const float dx = position.x - nearestX;
			const float dy = position.y - nearestY;
			if (dx * dx + dy * dy <= kCobBlastRadius * kCobBlastRadius) {
				zombie->TakePlantAshDamage(damage);
			}
		});
	}
	// 原版玉米炮与樱桃炸弹相同：扶梯按爆心格周围 3x3 方形范围清除。
	RemoveLaddersInBlastSquare(position, targetRow, 1);
}

bool Board::IsValidCobCannonAnchor(int row, int anchorColumn) const
{
	const PlantFootprint footprint = GetPlantFootprint(PlantType::PLANT_COBCANNON);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		const int occupiedRow = row + footprint.cells[i].rowOffset;
		const int occupiedColumn = anchorColumn + footprint.cells[i].columnOffset;
		if (occupiedRow < 0 || occupiedRow >= mRows
			|| occupiedColumn < 0 || occupiedColumn >= mColumns) {
			return false;
		}
		Plant* kernel = GetNormalPlantAt(occupiedRow, occupiedColumn);
		if (!kernel || !kernel->IsActive() || kernel->mPlantHealth <= 0
			|| kernel->IsSquished() || kernel->IsBungeeTargeted()
			|| kernel->mPlantType != PlantType::PLANT_KERNELPULT
			|| GetPumpkinAt(occupiedRow, occupiedColumn)
			|| GetOverlayPlantAt(occupiedRow, occupiedColumn)) {
			return false;
		}
	}
	return true;
}

bool Board::ResolvePlantPlacementAnchor(PlantType type, int row, int col,
	int& anchorRow, int& anchorColumn) const
{
	anchorRow = row;
	anchorColumn = col;
	if (type != PlantType::PLANT_COBCANNON) {
		return row >= 0 && row < mRows && col >= 0 && col < mColumns;
	}
	if (IsValidCobCannonAnchor(row, col)) return true;
	if (IsValidCobCannonAnchor(row, col - 1)) {
		anchorColumn = col - 1;
		return true;
	}
	return false;
}

bool Board::OccupyPlantFootprint(PlantType type, int row, int anchorColumn,
	int plantID, const std::vector<int>& replacePlantIDs)
{
	const PlantFootprint footprint = GetPlantFootprint(type);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		Cell* cell = GetCell(row + footprint.cells[i].rowOffset,
			anchorColumn + footprint.cells[i].columnOffset);
		if (!cell) return false;
		const int occupiedID = cell->GetNormalPlantID();
		if (occupiedID != NULL_PLANT_ID
			&& std::find(replacePlantIDs.begin(), replacePlantIDs.end(), occupiedID)
				== replacePlantIDs.end()) {
			return false;
		}
	}
	// 校验全部成功后才写入，避免半株植物残留在棋盘上。
	for (std::size_t i = 0; i < footprint.count; ++i) {
		GetCell(row + footprint.cells[i].rowOffset,
			anchorColumn + footprint.cells[i].columnOffset)->SetNormalPlantID(plantID);
	}
	return true;
}

bool Board::CanPlantAt(PlantType type, int row, int col)
{
	// 工具卡使用来源/目的两段事务，不可作为普通植物落种。
	if (type == PlantType::PLANT_CARRYVINE) return false;
	if (!MiniGame::AllowsPlant(mLevel, type)) return false;
	if (!HasPlantingQuota(type)) return false;
	int anchorRow = row;
	int anchorColumn = col;
	if (!ResolvePlantPlacementAnchor(type, row, col, anchorRow, anchorColumn)) return false;
	const PlantFootprint polarFootprint = GetPlantFootprint(type);
	for (std::size_t i = 0; i < polarFootprint.count; ++i) {
		if (!CanPlantOnMineCell(anchorRow + polarFootprint.cells[i].rowOffset,
			anchorColumn + polarFootprint.cells[i].columnOffset)) return false;
		if (HasSnowHoleAt(anchorRow + polarFootprint.cells[i].rowOffset,
			anchorColumn + polarFootprint.cells[i].columnOffset)) return false;
	}
	if (IsPlantFootprintFrozen(type, anchorRow, anchorColumn)) return false;
	if (IsIceAt(anchorRow, anchorColumn)) return false;
	if (type == PlantType::PLANT_COBCANNON) {
		const PlantFootprint footprint = GetPlantFootprint(type);
		for (std::size_t i = 0; i < footprint.count; ++i) {
			const int occupiedRow = anchorRow + footprint.cells[i].rowOffset;
			const int occupiedColumn = anchorColumn + footprint.cells[i].columnOffset;
			if (IsIceAt(occupiedRow, occupiedColumn)
				|| HasCraterAt(occupiedRow, occupiedColumn)) return false;
		}
		return IsValidCobCannonAnchor(anchorRow, anchorColumn);
	}

	row = anchorRow;
	col = anchorColumn;
	Cell* cell = GetCell(row, col);
	if (!cell || HasCraterAt(row, col)) return false;

	const bool isWater = IsPoolSquare(row, col);
	Plant* underPlant = mEntityRegistry.GetPlant(cell->GetUnderPlantID());
	Plant* normalPlant = mEntityRegistry.GetPlant(cell->GetNormalPlantID());
	const bool hasLilyPad = underPlant
		&& underPlant->mPlantType == PlantType::PLANT_LILYPAD;
	const bool hasFlowerPot = underPlant && underPlant->IsRoofSupportPlant();
	if (type == PlantType::PLANT_INSTANT_COFFEE) {
		// 原版 flying layer：只允许覆盖仍睡眠、尚未进入唤醒且未被蹦极抓取的普通层蘑菇。
		return cell->GetOverlayPlantID() == NULL_PLANT_ID
			&& normalPlant && normalPlant->GetSleepState()
			&& !normalPlant->IsWakingUp()
			&& !normalPlant->IsBungeeTargeted();
	}
	if (IsUpgradePlantType(type)) {
		// 紫卡按规则选择 normal 或 under；承载层升级不会检查或覆盖上层植物。
		Plant* basePlant = GetUpgradePlantLayer(type) == PlantUpgradeLayer::UNDER
			? underPlant : normalPlant;
		return basePlant && basePlant->IsActive()
			&& basePlant->mPlantHealth > 0 && !basePlant->IsSquished()
			&& !basePlant->IsBungeeTargeted()
			&& basePlant->mPlantType == GetUpgradeBasePlantType(type);
	}
	if (type == PlantType::PLANT_PUMPKINSHELL) {
		// 南瓜有独立外壳层，但水路与屋顶仍分别要求正确的承载植物。
		if (cell->GetPumpkinPlantID() != NULL_PLANT_ID) return false;
		if (normalPlant && IsMultiCellPlantType(normalPlant->mPlantType)) return false;
		if (isWater) return hasLilyPad;
		if (IsRoofBackground()) return hasFlowerPot;
		return true;
	}
	if (type == PlantType::PLANT_FLOWERPOT) {
		// 原版允许花盆落在任意非水地面；屋顶以外通常只是不推荐选择。
		return !isWater && cell->IsEmpty();
	}
	if ((type == PlantType::PLANT_SPIKEWEED
		|| type == PlantType::PLANT_SPIKEROCK)
		&& (isWater || IsSpikeweedTerrainRestricted(mBackGround))) {
		// 地刺系既不能隔着睡莲扎水面，也不能隔着花盆扎屋顶瓦片。
		return false;
	}
	if (IsRoofBackground()
		&& type != PlantType::PLANT_LILYPAD
		&& type != PlantType::PLANT_TANGLEKELP
		&& type != PlantType::PLANT_SEASHROOM) {
		// 屋顶普通植物只占 normal 层，必须由 under 层的花盆承载。
		return hasFlowerPot && cell->GetNormalPlantID() == NULL_PLANT_ID;
	}
	if (type == PlantType::PLANT_LILYPAD
		|| type == PlantType::PLANT_TANGLEKELP
		|| type == PlantType::PLANT_SEASHROOM) {
		// 三种水生植物都直接落水；后两者占普通层，因此空格判断也禁止叠在睡莲上。
		return isWater && cell->IsEmpty();
	}
	if (isWater) {
		// 土豆雷没有水面形态；即使已有睡莲也不能落在水路。
		if (type == PlantType::PLANT_POTATOMINE) return false;
		return hasLilyPad
			&& cell->GetNormalPlantID() == NULL_PLANT_ID;
	}
	return cell->GetNormalPlantID() == NULL_PLANT_ID
		&& (cell->GetUnderPlantID() == NULL_PLANT_ID || hasFlowerPot);
}

bool Board::HasPlantingQuota(PlantType type) const
{
	if (type == PlantType::PLANT_DAWNLOTUS) {
		for (int plantID : mEntityRegistry.GetAllPlantIDs()) {
			const Plant* plant = mEntityRegistry.GetPlant(plantID);
			if (plant && plant->IsActive()
				&& plant->GetPlacementType() == PlantType::PLANT_DAWNLOTUS) return false;
		}
		return true;
	}
	if (type == PlantType::PLANT_PLANTERN) {
		// 模仿者占位虽然还不是 Plantern 实例，也必须预留唯一名额。
		return mActivePlanternID == NULL_PLANT_ID;
	}
	return type != PlantType::PLANT_ELITE_SCAREDYSHROOM
		|| mEliteScaredyShroomsPlanted < kEliteScaredyShroomPlantLimit;
}

bool Board::BeginCobCannonTargeting(int row, int col)
{
	if (!CanBeginCobCannonTargeting(row, col)) return false;
	auto* cannon = dynamic_cast<CobCannon*>(GetNormalPlantAt(row, col));
	mTargetingCobCannonID = cannon->mPlantID;
	mCursorObjectManager.Activate(CursorObjectType::COB_CANNON_TARGET, [this]() {
		mTargetingCobCannonID = NULL_PLANT_ID;
	});
	return true;
}

bool Board::CanBeginCobCannonTargeting(int row, int col) const
{
	if (mCursorObjectManager.GetActiveType() != CursorObjectType::NONE) return false;
	const auto* cannon = dynamic_cast<const CobCannon*>(GetNormalPlantAt(row, col));
	return cannon && cannon->IsActive() && cannon->IsReady();
}

bool Board::FireTargetedCobCannonAt(const Vector& target)
{
	if (!IsCobCannonTargeting()) return false;
	const float rowHeight = GetCellHeight();
	const float boardTop = mRows > 0
		? GetRowCenterYAtX(0, target.x) - rowHeight * 0.5f : -1.0f;
	if (mRows <= 0 || rowHeight <= 0.0f || boardTop < 0.0f || target.y < boardTop) {
		// 与原版草坪上界门禁一致：点到卡槽/UI 区只取消准星，不提交炮击。
		mCursorObjectManager.ClearActive();
		return false;
	}
	// 只用屋顶在 target.x 处的连续坡面划分逻辑行；target 本身保持玩家点击像素不变，
	// 避免原版先转格再写回 Y 所产生的屋顶“上界之风”落点偏移。
	const int targetRow = std::clamp(static_cast<int>(std::floor(
		(target.y - boardTop) / rowHeight)), 0, mRows - 1);
	auto* cannon = dynamic_cast<CobCannon*>(
		mEntityRegistry.GetPlant(mTargetingCobCannonID));
	const bool fired = cannon && cannon->IsActive()
		&& cannon->FireAt(target, targetRow);
	mCursorObjectManager.ClearActive();
	return fired;
}

void Board::CancelCobCannonTargeting(int plantID)
{
	if (plantID == NULL_PLANT_ID || plantID != mTargetingCobCannonID) return;
	mCursorObjectManager.ClearActive();
}

bool Board::HasPlantingRequirement(PlantType type) const
{
	if (type == PlantType::PLANT_COBCANNON) {
		for (int row = 0; row < mRows; ++row) {
			for (int column = 0; column < mColumns - 1; ++column) {
				if (IsValidCobCannonAnchor(row, column)) return true;
			}
		}
		return false;
	}
	const PlantType baseType = GetUpgradeBasePlantType(type);
	if (baseType == PlantType::NUM_PLANT_TYPES) return true;
	for (const int plantID : mEntityRegistry.GetAllPlantIDs()) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (plant && plant->IsActive() && !plant->IsSquished()
			&& plant->mPlantHealth > 0 && plant->mPlantType == baseType) {
			return true;
		}
	}
	return false;
}

int Board::GetEliteScaredyShroomPlantLimit() const
{
	return kEliteScaredyShroomPlantLimit;
}

Plant* Board::GetTopPlantAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;
	Cell* cell = mCells[row][col];
	return cell ? mEntityRegistry.GetPlant(cell->GetTopPlantID()) : nullptr;
}

Plant* Board::GetCatapultTargetPlantAt(int row, int col) const
{
	const std::array<Plant*, 4> layers = {
		GetOverlayPlantAt(row, col),
		GetNormalPlantAt(row, col),
		GetPumpkinAt(row, col),
		GetUnderPlantAt(row, col),
	};
	for (Plant* plant : layers) {
		if (plant && plant->IsActive() && plant->mPlantHealth > 0
			&& !plant->IsSquished() && plant->CanBeEaten()) {
			return plant;
		}
	}
	return nullptr;
}

Plant* Board::FindAirborneThreatProtector(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;

	// 原版按植物容器顺序返回第一株；实体 ID 保留种植先后，排序后可在重叠保护区稳定复刻。
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	for (const int plantID : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (plant && plant->ProtectsCellFromAirborneThreat(row, col)) return plant;
	}
	return nullptr;
}

Plant* Board::GetJumpBlockingPlantAt(int row, int col, ZombieJumpType jumpType) const
{
	// 跳跃阻拦是格内植物能力，不等同于啃食顶层；南瓜等非阻拦外壳要继续向内查询。
	const std::array<Plant*, 3> layers = {
		GetPumpkinAt(row, col),
		GetNormalPlantAt(row, col),
		GetUnderPlantAt(row, col),
	};
	for (Plant* plant : layers) {
		if (plant && plant->IsActive() && plant->mPlantHealth > 0
			&& plant->BlocksZombieJump(jumpType)) {
			return plant;
		}
	}
	return nullptr;
}

Plant* Board::GetUnderPlantAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;
	Cell* cell = mCells[row][col];
	return cell ? mEntityRegistry.GetPlant(cell->GetUnderPlantID()) : nullptr;
}

Plant* Board::GetNormalPlantAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;
	Cell* cell = mCells[row][col];
	return cell ? mEntityRegistry.GetPlant(cell->GetNormalPlantID()) : nullptr;
}

Plant* Board::GetPumpkinAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;
	Cell* cell = mCells[row][col];
	return cell ? mEntityRegistry.GetPlant(cell->GetPumpkinPlantID()) : nullptr;
}

Plant* Board::GetOverlayPlantAt(int row, int col) const
{
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) return nullptr;
	Cell* cell = mCells[row][col];
	return cell ? mEntityRegistry.GetPlant(cell->GetOverlayPlantID()) : nullptr;
}

/** 快照格内分层实体 ID 后逐一重新解析，允许回调安全结束植物生命周期。 */
void Board::ForEachActivePlantInCell(int row, int col,
	const std::function<void(Plant&)>& action)
{
	if (!action || row < 0 || row >= mRows || col < 0 || col >= mColumns) return;
	const Cell* cell = mCells[row][col];
	if (!cell) return;

	const std::array<int, 4> plantIDs = {
		cell->GetOverlayPlantID(),
		cell->GetPumpkinPlantID(),
		cell->GetNormalPlantID(),
		cell->GetUnderPlantID(),
	};
	for (const int plantID : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || plant->IsPreview()
			|| plant->IsSquished() || plant->mPlantHealth <= 0) {
			continue;
		}
		action(*plant);
	}
}

/** 先冻结同格目标集合与拦截语义，再逐层承伤，避免上层死亡改变本次命中集合。 */
WinterGroundImpactResponse Board::ApplyWinterGroundImpactToCell(int row, int col,
	WinterGroundImpactKind kind, int damage, DamageSource source)
{
	std::array<int, 4> plantIDs{};
	std::size_t plantCount = 0;
	ForEachActivePlantInCell(row, col, [&](Plant& plant) {
		plantIDs[plantCount++] = plant.mPlantID;
	});

	WinterGroundImpactResponse response;
	for (std::size_t i = 0; i < plantCount; ++i) {
		Plant* plant = mEntityRegistry.GetPlant(plantIDs[i]);
		if (!plant || !plant->IsActive() || plant->IsPreview()
			|| plant->IsSquished() || plant->mPlantHealth <= 0) {
			continue;
		}
		const WinterGroundImpactResponse candidate =
			plant->ResolveWinterGroundImpact(kind);
		if (candidate.intercepted) {
			response = candidate;
			break;
		}
	}

	if (damage <= 0) return response;
	for (std::size_t i = 0; i < plantCount; ++i) {
		Plant* plant = mEntityRegistry.GetPlant(plantIDs[i]);
		if (!plant || !plant->IsActive() || plant->IsPreview()
			|| plant->IsSquished() || plant->mPlantHealth <= 0) {
			continue;
		}
		plant->TakeWinterGroundImpactDamage(kind, damage, source);
	}
	return response;
}

/**
 * 在目标九宫格中稳定选择最近的活动南瓜头；南瓜本体只由自己承伤，避免外壳连锁保护。
 */
Plant* Board::FindPumpkinAreaProtector(const Plant& plant) const
{
	if (!plant.IsActive() || plant.mRow < 0 || plant.mRow >= mRows
		|| plant.mColumn < 0 || plant.mColumn >= mColumns) {
		return nullptr;
	}

	if (plant.mPlantType == PlantType::PLANT_PUMPKINSHELL) {
		Plant* pumpkin = GetPumpkinAt(plant.mRow, plant.mColumn);
		return pumpkin && pumpkin->IsActive()
			&& pumpkin->mPlantID == plant.mPlantID ? pumpkin : nullptr;
	}

	Plant* best = nullptr;
	int bestDistanceSquared = INT_MAX;
	for (int row = std::max(0, plant.mRow - kPumpkinProtectionCellRadius);
		row <= std::min(mRows - 1, plant.mRow + kPumpkinProtectionCellRadius);
		++row) {
		for (int column = std::max(0,
			plant.mColumn - kPumpkinProtectionCellRadius);
			column <= std::min(mColumns - 1,
				plant.mColumn + kPumpkinProtectionCellRadius); ++column) {
			Plant* candidate = GetPumpkinAt(row, column);
			if (!candidate || !candidate->IsActive()) continue;

			const int rowDelta = row - plant.mRow;
			const int columnDelta = column - plant.mColumn;
			const int distanceSquared = rowDelta * rowDelta
				+ columnDelta * columnDelta;
			const bool stableTieBreak = best
				&& distanceSquared == bestDistanceSquared
				&& (candidate->mRow < best->mRow
					|| (candidate->mRow == best->mRow
						&& (candidate->mColumn < best->mColumn
							|| (candidate->mColumn == best->mColumn
								&& candidate->mPlantID < best->mPlantID))));
			if (!best || distanceSquared < bestDistanceSquared || stableTieBreak) {
				best = candidate;
				bestDistanceSquared = distanceSquared;
			}
		}
	}
	return best;
}

void Board::ApplyPumpkinProtectedZombieAreaDamage(int baseDamage,
	const std::function<bool(const Plant&)>& overlapsArea)
{
	ApplyPumpkinProtectedZombieAreaDamage(baseDamage,
		kPumpkinAreaDamageMultiplier, overlapsArea);
}

/**
 * 先按原范围收集命中层，再按九宫格保护者 ID 归并，避免密集或水路叠层重复扣壳。
 */
void Board::ApplyPumpkinProtectedZombieAreaDamage(int baseDamage,
	int pumpkinDamageMultiplier,
	const std::function<bool(const Plant&)>& overlapsArea)
{
	if (baseDamage <= 0 || pumpkinDamageMultiplier <= 0 || !overlapsArea) return;

	std::vector<int> unprotectedPlantIDs;
	std::unordered_set<int> protectedPumpkinIDSet;
	for (const int plantID : mEntityRegistry.GetAllPlantIDs()) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || !overlapsArea(*plant)) continue;

		if (Plant* pumpkin = FindPumpkinAreaProtector(*plant)) {
			protectedPumpkinIDSet.insert(pumpkin->mPlantID);
		}
		else {
			unprotectedPlantIDs.push_back(plantID);
		}
	}

	// 无外壳格保持旧行为：范围实际命中的 under/normal 各自吃一次基础伤害。
	for (const int plantID : unprotectedPlantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (plant && plant->IsActive()) {
			plant->TakeDamage(baseDamage, DamageSource::ZOMBIE);
		}
	}

	const int pumpkinDamage = baseDamage > INT_MAX / pumpkinDamageMultiplier
		? INT_MAX : baseDamage * pumpkinDamageMultiplier;
	std::vector<int> protectedPumpkinIDs(
		protectedPumpkinIDSet.begin(), protectedPumpkinIDSet.end());
	std::sort(protectedPumpkinIDs.begin(), protectedPumpkinIDs.end());
	for (const int pumpkinID : protectedPumpkinIDs) {
		Plant* pumpkin = mEntityRegistry.GetPlant(pumpkinID);
		if (pumpkin && pumpkin->IsActive()) {
			pumpkin->TakeDamage(pumpkinDamage, DamageSource::ZOMBIE);
		}
	}
}

void Board::RefreshPlantStackRenderOrder(Cell* cell)
{
	if (!cell) return;
	Plant* under = mEntityRegistry.GetPlant(cell->GetUnderPlantID());
	Plant* normal = mEntityRegistry.GetPlant(cell->GetNormalPlantID());
	Plant* pumpkin = mEntityRegistry.GetPlant(cell->GetPumpkinPlantID());
	Plant* overlay = mEntityRegistry.GetPlant(cell->GetOverlayPlantID());
	std::vector<Plant*> stack;
	for (Plant* plant : { under, normal, pumpkin, overlay }) {
		if (plant) stack.push_back(plant);
	}
	// 按承载层次排列已有号，分配凭据随号转移；之后铲除任意层只释放该层自己的号。
	for (size_t i = 0; i < stack.size(); ++i) {
		const auto lowest = std::min_element(stack.begin() + i, stack.end(),
			[](const Plant* a, const Plant* b) { return a->IsDrawnBefore(*b); });
		GameObjectManager::GetInstance().SwapRenderOrders(stack[i], *lowest);
	}
}

Plant* Board::CreatePlant(PlantType plantType, int row, int column,
	bool skipsettings, bool isPreview)
{
	return CreatePlantInternal(plantType, plantType, row, column,
		skipsettings, isPreview, false);
}

Plant* Board::CreatePlayerPlant(PlantType plantType, int row, int column)
{
	return CreatePlantInternal(plantType, plantType, row, column,
		false, false, true);
}

Plant* Board::CreateImitaterPlant(PlantType targetType, int row, int column)
{
	if ((targetType == PlantType::PLANT_IMITATER || targetType == PlantType::PLANT_CARRYVINE)
		|| IsUpgradePlantType(targetType)
		|| !GameDataManager::GetInstance().HasPlant(targetType)) {
		return nullptr;
	}
	return CreatePlantInternal(PlantType::PLANT_IMITATER, targetType,
		row, column, false, false, true);
}

Plant* Board::CreatePlantInternal(PlantType actualType, PlantType placementType,
	int row, int column, bool skipsettings, bool isPreview, bool playerDeployment)
{
	if (!isPreview && placementType == PlantType::PLANT_CARRYVINE) return nullptr;
	if (!isPreview && !MiniGame::AllowsPlant(mLevel, placementType)) return nullptr;
	const int requestedRow = row;
	const int requestedColumn = column;
	if (!isPreview && !skipsettings
		&& !ResolvePlantPlacementAnchor(placementType, requestedRow, requestedColumn,
			row, column)) {
		return nullptr;
	}
	// 检查行列是否有效
	if (row < 0 || row >= mRows || column < 0 || column >= mColumns) {
		LOG_ERROR("Board") << "无效的行列位置: (" << row << ", " << column << ")";
		return nullptr;
	}

	// 正式创建入口也执行累计次数闸门，覆盖 AutoTest/develop 等绕过 CanPlantAt 的调用者。
	// 读档实体恢复由已保存的累计计数约束，不能在逐株重建时重复消耗次数。
	// 玩家落种始终消费次数；读档生命周期标记只能豁免内部实体恢复，不能豁免玩家输入。
	if (IsMineBackground() && !isPreview && !mIsLoadSave
		&& !CanPlantAt(placementType, row, column)) return nullptr;
	const bool consumesPlantingQuota = !isPreview && !skipsettings
		&& (playerDeployment || !mIsLoadSave);
	if (consumesPlantingQuota && !HasPlantingQuota(placementType)) {
		return nullptr;
	}
	if (consumesPlantingQuota
		&& IsPlantFootprintFrozen(placementType, row, column)) {
		return nullptr;
	}
	if (consumesPlantingQuota) {
		const PlantFootprint footprint = GetPlantFootprint(placementType);
		for (std::size_t i = 0; i < footprint.count; ++i) {
			if (HasSnowHoleAt(row + footprint.cells[i].rowOffset,
				column + footprint.cells[i].columnOffset)) return nullptr;
		}
	}
	const bool isOverlayPlant = placementType == PlantType::PLANT_INSTANT_COFFEE;
	const bool isUpgradePlant = IsUpgradePlantType(placementType);
	if (isOverlayPlant && consumesPlantingQuota
		&& !CanPlantAt(placementType, row, column)) {
		return nullptr;
	}

	Plant* upgradeBasePlant = nullptr;
	std::vector<Plant*> upgradeBasePlants;
	bool inheritedSleeping = false;
	float inheritedWakeUpTimer = 0.0f;
	if (isUpgradePlant && !isPreview && !skipsettings) {
		if (!CanPlantAt(placementType, row, column)) return nullptr;
		upgradeBasePlant = GetUpgradePlantLayer(placementType) == PlantUpgradeLayer::UNDER
			? GetUnderPlantAt(row, column) : GetNormalPlantAt(row, column);
		if (!upgradeBasePlant) return nullptr;
		upgradeBasePlants.push_back(upgradeBasePlant);
		if (placementType == PlantType::PLANT_COBCANNON) {
			Plant* rearKernel = GetNormalPlantAt(row, column + 1);
			if (!rearKernel || rearKernel == upgradeBasePlant) return nullptr;
			upgradeBasePlants.push_back(rearKernel);
		}
		if (GetUpgradePlantLayer(placementType) == PlantUpgradeLayer::NORMAL) {
			inheritedSleeping = upgradeBasePlant->GetSleepState();
			inheritedWakeUpTimer = upgradeBasePlant->GetWakeUpTimeRemaining();
		}
	}

	// 根据植物类型创建对应的植物
	std::shared_ptr<Plant> plant = GameAPP::GetInstance().InstantiatePlant(
		actualType, this, row, column, isPreview);
	if (auto imitater = std::dynamic_pointer_cast<Imitater>(plant)) {
		imitater->SetImitaterTarget(placementType);
	}

	if (plant && !isPreview && !skipsettings) {
		Cell* cell = GetCell(row, column);
		const bool isUnderPlant = IsUnderPlantLayerType(placementType);
		const bool isPumpkinPlant = placementType == PlantType::PLANT_PUMPKINSHELL;
		const int occupiedID = isUnderPlant
			? cell->GetUnderPlantID()
			: (isPumpkinPlant ? cell->GetPumpkinPlantID()
				: (isOverlayPlant ? cell->GetOverlayPlantID() : cell->GetNormalPlantID()));
		const bool replacesExpectedBase = isUpgradePlant && upgradeBasePlant
			&& occupiedID == upgradeBasePlant->mPlantID;
		if (occupiedID != NULL_PLANT_ID && !replacesExpectedBase) {
			plant->Die();
			return nullptr;
		}
		mEntityRegistry.AddPlant(plant);

		// 将植物与格子关联
		if (isUnderPlant) cell->SetUnderPlantID(plant->mPlantID);
		else if (isPumpkinPlant) cell->SetPumpkinPlantID(plant->mPlantID);
		else if (isOverlayPlant) cell->SetOverlayPlantID(plant->mPlantID);
		else {
			std::vector<int> replacePlantIDs;
			replacePlantIDs.reserve(upgradeBasePlants.size());
			for (Plant* base : upgradeBasePlants) {
				if (base) replacePlantIDs.push_back(base->mPlantID);
			}
			if (!OccupyPlantFootprint(placementType, row, column,
				plant->mPlantID, replacePlantIDs)) {
				plant->Die();
				return nullptr;
			}
		}
		if (replacesExpectedBase) {
			// 先把格子切到新 ID，再让旧株死亡；ReleaseGridSlot 只清自己的 ID，因此替换原子化。
			for (Plant* base : upgradeBasePlants) {
				if (base) base->Die();
			}
			if (actualType != PlantType::PLANT_IMITATER
				&& GetUpgradePlantLayer(placementType) == PlantUpgradeLayer::NORMAL
				&& inheritedSleeping) {
				plant->SetSleepState(true);
				plant->RestoreSleepState(true, inheritedWakeUpTimer);
			}
			else if (actualType != PlantType::PLANT_IMITATER
				&& GetUpgradePlantLayer(placementType) == PlantUpgradeLayer::NORMAL) {
				plant->SetSleepState(false);
			}
		}
		const PlantFootprint footprint = GetPlantFootprint(placementType);
		for (std::size_t i = 0; i < footprint.count; ++i) {
			if (Cell* occupiedCell = GetCell(
				row + footprint.cells[i].rowOffset,
				column + footprint.cells[i].columnOffset)) {
				RefreshPlantStackRenderOrder(occupiedCell);
			}
		}
		if (placementType == PlantType::PLANT_ELITE_SCAREDYSHROOM && consumesPlantingQuota) {
			++mEliteScaredyShroomsPlanted;
		}
		if (placementType == PlantType::PLANT_PLANTERN) {
			mActivePlanternID = plant->mPlantID;
		}
		if (playerDeployment) {
			NotifyPlayerPlantDeployed(*plant, placementType);
		}
	}

	return plant.get();
}

void Board::NotifyPlayerPlantDeployed(const Plant& plant, PlantType placementType)
{
	const int baseMaxHealth = std::max(1,
		GameDataManager::GetInstance().GetPlantSimulationProfile(
			placementType).baseHealth);
	mEntityRegistry.ForEachZombieInRow(plant.mRow, [&](Zombie* zombie) {
		if (zombie) zombie->OnPlayerPlantDeployed(plant, baseMaxHealth);
	});
}

Plant* Board::MorphImitater(Imitater* imitater)
{
	if (!imitater || !imitater->IsActive() || imitater->mBoard != this
		|| !imitater->HasValidTarget()) {
		return nullptr;
	}

	const PlantType targetType = imitater->GetImitaterTarget();
	const int plantID = imitater->mPlantID;
	const int row = imitater->mRow;
	const int column = imitater->mColumn;
	std::shared_ptr<Plant> replacement = GameAPP::GetInstance().InstantiatePlant(
		targetType, this, row, column, false);
	if (!replacement) return nullptr;

	// Cell 继续保存同一个 ID；先覆盖注册表，再静默回收旧对象，避免 ReleaseGridSlot
	// 把刚刚移交给目标植物的层或双格 footprint 清空。
	mEntityRegistry.AddPlantWithID(replacement, plantID);
	replacement->SetImitatedAppearance(true);
	if (auto* blover = dynamic_cast<Blover*>(replacement.get())) {
		blover->SetBlowDirection(imitater->GetInheritedBloverDirection());
	}
	if (targetType == PlantType::PLANT_PLANTERN) {
		mActivePlanternID = plantID;
	}
	const PlantFootprint footprint = GetPlantFootprint(targetType);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		RefreshPlantStackRenderOrder(GetCell(
			row + footprint.cells[i].rowOffset,
			column + footprint.cells[i].columnOffset));
	}
	imitater->RetireAfterReplacement();
	return replacement.get();
}

Zombie* Board::CreateZombie(ZombieType zombieType, int row, float x, bool skipsettings, bool isPreview) {
	// y 由 row 与地形上的当前 x 共同决定；屋顶出生点因此直接落在连续坡面上。
	float y = GetZombieSpawnY(row, x);
	if (y < 0.0f) y = 0.0f;

	std::shared_ptr<Zombie> zombie = GameAPP::GetInstance().InstantiateZombie
	(zombieType, this, x, y, row, isPreview);
	if (!zombie) return nullptr;

	mZombieNumber++;

	if (!isPreview && !skipsettings) {
		mEntityRegistry.AddZombie(zombie);
		zombie->mSpawnWave = this->mCurrentWave;
		// 按当前难度来源对整只僵尸血量施加全局倍率（默认 1，目前由生存模式按轮次提供）。
		// 仅在此波次生成路径施加；读档走 CreateZombieWithID 直接还原已含倍率的存档血量，不重复缩放。
		zombie->ApplyHealthMultiplier(GetZombieHpMultiplier(),
			mHxyModeEnabled ? kHxyArmorHealthMultiplier : 1.0);
		// 词条②：按当前词条层数设定出生免伤次数（无词条→0）。读档走 CreateZombieWithID 不在此赋值，
		// 由 LoadProtectedData 还原（与血量倍率同契约）。
		zombie->mFreeHitsRemaining = GetPerkManager().GetZombieInvulnHits();
		zombie->PlaySpawnSound();
	}
	return zombie.get();
}

Bullet* Board::CreateBullet(BulletType bulletType, int row, const Vector& position, bool skipsettings)
{
	// 使用对象池创建子弹
	BulletPool* bulletPool = GameObjectManager::GetInstance().GetBulletPool();
	if (!bulletPool) {
		LOG_ERROR("Board") << "CreateBullet 对象池未初始化";
		return nullptr;
	}

	// 从对象池获取子弹（shared_ptr 局部变量，用于把 weak_ptr 注册进 EntityRegistry）
	std::shared_ptr<Bullet> bullet = bulletPool->AcquireShared
	(this, bulletType, row, Vector(10, 10), position);

	if (bullet && !skipsettings) {
		mEntityRegistry.AddBullet(bullet);
	}

	return bullet.get();
}

inline void Board::CleanupExpiredObjects()
{
	// 清理已过期的植物ID映射
	// TODO 如果其他地方也有存储植物ID,也要删除
	std::vector<int> removedPlants = mEntityRegistry.CleanupExpired();

	// 遍历被清理的植物ID，清除对应Cell中的植物ID
	for (int plantID : removedPlants) {
		CleanPlantFromCells(plantID);
	}
}

inline void Board::CleanPlantFromCells(int plantID)
{
	for (size_t i = 0; i < mCells.size(); i++) {
		for (size_t j = 0; j < mCells[i].size(); j++) {
			if (mCells[i][j]->GetUnderPlantID() == plantID)
				mCells[i][j]->ClearUnderPlantID();
			if (mCells[i][j]->GetNormalPlantID() == plantID)
				mCells[i][j]->ClearNormalPlantID();
			if (mCells[i][j]->GetPumpkinPlantID() == plantID)
				mCells[i][j]->ClearPumpkinPlantID();
			if (mCells[i][j]->GetOverlayPlantID() == plantID)
				mCells[i][j]->ClearOverlayPlantID();
		}
	}
}

inline void Board::UpdateSunFalling(float deltaTime)
{
	if (MiniGame::IsMiniGame(mLevel)) return;
	mSunCountDown -= deltaTime;
	if (mSunCountDown <= 0.0f)
	{
		mSunCountDown = SPAWN_SUN_TIME;
		Vector sunPos(
			GameRandom::Range(50.0f, 770.0f),      // 50~770
			GameRandom::Range(-110.0f, -20.0f)    // -110~-20
		);
		auto sun = CreateSun(sunPos, false);
		sun->StartLinearFall();
	}
}

/** 用环境小阳光补偿泳池六路防线与睡莲成本，不占用玩家卡槽。 */
inline void Board::UpdatePoolSunFalling(float deltaTime)
{
	mPoolSunCountDown -= deltaTime;
	if (mPoolSunCountDown > 0.0f) return;

	mPoolSunCountDown = POOL_SUN_SPAWN_TIME;
	const int row = GameRandom::Range(2, 3);
	const int column = GameRandom::Range(0, mColumns - 1);
	Vector sunPos = GetCellCenterPosition(row, column);
	sunPos.x += GameRandom::Range(-20.0f, 20.0f);
	sunPos.y -= 20.0f;
	CreateSmallSun(sunPos, true);
}

void Board::UpdateLevel()
{
	if (mBoardState != BoardState::GAME) return;
	float deltaTime = DeltaTime::GetDeltaTime();

	if (mBackGround == Background::GROUND_DAY || mBackGround == Background::WATER_POOL ||
		mBackGround == Background::ROOF) {
		UpdateSunFalling(deltaTime);
	}
	if (mBackGround == Background::WATER_POOL || mBackGround == Background::NIGHT_WATER_POOL) {
		UpdatePoolSunFalling(deltaTime);
	}

	// 词条③：植物回血全局脉冲（生存专用；无词条→GetPlantRegenPerPulse()=0，整循环跳过，零开销）。
	// O(n) 遍历只在脉冲触发的那一帧发生（每 5s 一次），非每帧扫描。
	mPlantRegenTimer += deltaTime;
	if (mPlantRegenTimer >= mPerkManager.GetPlantRegenInterval())
	{
		mPlantRegenTimer = 0.0f;
		int heal = mPerkManager.GetPlantRegenPerPulse();
		if (heal > 0)
		{
			for (int id : mEntityRegistry.GetAllPlantIDs())
			{
				Plant* p = mEntityRegistry.GetPlant(id);
				if (!p || p->IsPreview() || p->IsIceSealed()) continue;
				int cap = mPerkManager.GetPlantRegenHpCap(p->mPlantMaxHealth);
				if (p->mPlantHealth < cap)
				{
					int healed = p->mPlantHealth + heal;
					p->mPlantHealth = (healed > cap) ? cap : healed;
				}
			}
		}
	}

	if (mCurrentWave >= mMaxWave)
	{
		return;
	}

	// 开发者面板「暂停刷怪」：冻结出波倒计时与本波清空提前出波，SummonNextWave 直调入口不受影响
	if (GameAPP::mDevSpawnPaused)
	{
		return;
	}

	mZombieCountDown -= deltaTime;

	if (mCurrentWave > 0 && mPendingSnowHoleSpawns.empty()
		&& mCurrectWaveZombieHP <= mNextWaveSpawnZombieHP)
	{
		mZombieCountDown = 0.0f;
	}

	if (mZombieCountDown <= 0.0f)
	{
		// 一大波僵尸处理
		if ((mCurrentWave + 1) % 10 == 0)
		{
			if (mCurrentWave + 1 == mMaxWave) BeginPolarFinalWavePrelude();
			mHugeWaveCountDown += deltaTime;
			if (!mHasHugeWaveSound)
			{
				mHasHugeWaveSound = true;
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_HUGEWAVE, 0.7f);
				if (mPresentation)
					mPresentation->ShowPrompt(
						ResourceKeys::Textures::IMAGE_HUGE_WAVE_APPROACHING,
						0.4f,
						4.0f,
						0.3f);
			}
			// 原版在一大波警告期间强制进入 burst。黑夜曲的鼓轨是独立段落，
			// 更早切入；其余场景等警告牌展示一段时间后再在小节边界加入鼓组。
			const float burstDelay = (mBackGround == Background::GROUND_NIGHT) ? 0.5f : 3.5f;
			if (!mHasHugeWaveMusicBurst && mHugeWaveCountDown >= burstDelay)
			{
				mHasHugeWaveMusicBurst = true;
				AudioSystem::StartMusicBurst();
			}
			if (mHugeWaveCountDown >= 7.5f)
			{
				mHugeWaveCountDown = 0.0f;
				mHasHugeWaveSound = false;
				mHasHugeWaveMusicBurst = false;
			}
			else
			{
				return;
			}
		}
		SummonNextWave();
	}
}

// 推进并生成下一波（波次+1、首波/最后一波/大波音效提示、生成僵尸、波血量记账）。
// 由 Update 出波倒计时归零调用；开发者面板「下一波」也直接调用（暂停中同样可出波）。
void Board::SummonNextWave()
{
	mCurrentWave++;
	ActivatePolarFinalWaveImmediately();
	// 在本波任何候选解析前承接冷却，保证整个波次（含稍后的显式正式候选）都保持封锁。
	AdvanceHijackerSpawnCooldownForNewWave();
	mZombieCountDown = IsStormyNightActive()
		? kStormyNightNextWaveSeconds : (MiniGame::IsMiniGame(mLevel)
			? MiniGame::WAVE_SECONDS : NEXTWAVE_COUNT_MAX);
	if (IsStormyNightActive() && !mStormyNightInitialized) {
		ActivateStormyNight();
	}
	// 普通关压力随波次推进；先刷新存活僵尸，再生成使用同一新倍率的本波僵尸。
	RefreshZombieWeatherSpeeds();
	mEliteDancersSpawnedThisWave = 0;
	mReinforcedDoorsSpawnedThisWave = 0;
	mElitePolevaultersSpawnedThisWave = 0;
	mGildedZambonisSpawnedThisWave = 0;
	mEliteDolphinRidersSpawnedThisWave = 0;
	mEliteJackInTheBoxesSpawnedThisWave = 0;
	mEliteDiggersSpawnedThisWave = 0;
	mElitePogosSpawnedThisWave = 0;
	mEliteLaddersSpawnedThisWave = 0;
	mEliteCatapultsSpawnedThisWave = 0;
	mRedeyeGargantuarsSpawnedThisWave = 0;
	mInsulatorsSpawnedThisWave = 0;
	mHijackersSpawnedThisWave = 0;
	mGroundingZombiesSpawnedThisWave = 0;
	mBobsledTeamsSpawnedThisWave = 0;
	mIceWallEngineersSpawnedThisWave = 0;
	mIceCrackDrillsSpawnedThisWave = 0;
	mWeatherJammersSpawnedThisWave = 0;
	mIceStatueExecutionersSpawnedThisWave = 0;
	mSnowBurrowsSpawnedThisWave = 0;
	mAdaptiveHelmetsSpawnedThisWave = 0;
	mThermalSnipersSpawnedThisWave = 0;
	mAuroraPriestsSpawnedThisWave = 0;
	mClockmakersSpawnedThisWave = 0;
	mCrystalMinersSpawnedThisWave = 0;
	mCrystalDrummersSpawnedThisWave = 0;
	mSunThievesSpawnedThisWave = 0;
	mMistFuelAssignedThisWave = 0;
	if (mCurrentWave == 1)
	{
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FIRSTWAVE, 0.7f);
		if (mPresentation) mPresentation->ActivateWaveProgress();
	}
	if (mCurrentWave == mMaxWave)
	{
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FINALWAVE, 0.7f);
		if (mPresentation)
			mPresentation->ShowPrompt(
				ResourceKeys::Textures::IMAGE_FINAL_WAVE,
				0.3f,
				2.0f,
				0.4f);
	}
	if (mCurrentWave % 10 == 0)
	{
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_AFTERHUGEWAVE, 0.7f);
	}

	if (IsMineBackground() && mMinePlannedWave == mCurrentWave) {
		for (const auto& entry : mMineWavePlan) {
			const ZombieType actual = ResolveWaveZombieType(entry.first);
			if (actual != ZombieType::NUM_ZOMBIE_TYPES)
				CreateOrQueueWaveZombie(actual, entry.second, static_cast<float>(SCENE_WIDTH) + 40.0f);
		}
		mMineWavePlan.clear();
	} else TrySummonZombie();
	TrySummonAdventureBoss();
	FinalizeWaveSpawnThreshold();
	PrepareMineWave();
}

/**
 * 关卡编排只在准确的最终波读取显式 BOSS 槽位；越过最终波的开发者直调不会重复创建。
 */
void Board::TrySummonAdventureBoss()
{
	if (mCurrentWave != mMaxWave || mCurrentWave <= 0) return;

	switch (AdventureProgression::GetBossSlot(mLevel)) {
	case AdventureProgression::BossSlot::ROOF_MARSHAL:
		// 固定中路便于玩家识别首领入场；Y 仍由 CreateZombie 按屋顶坡面统一解析。
		CreateZombie(ZombieType::ZOMBIE_ROOF_MARSHAL, mRows / 2,
			kRoofMarshalBossSpawnX);
		break;
	case AdventureProgression::BossSlot::NONE:
		break;
	}
}

/** 为每个出怪候选创建选卡展示对象，并为编队候选补充无玩法注册的纯展示成员。 */
/** 为选卡创建独立展示对象；大型角色在收官预览中避开顶部卡槽。 */
void Board::CreatePreviewZombies()
{
	if (mBoardState != BoardState::CHOOSE_CARD || !mPreviewZombieList.empty()
		|| mSpawnZombieList.empty()) return;

	mPreviewZombieList.clear();
	for (ZombieType zombieType : mSpawnZombieList)
	{
		// 选卡镜头会平移到扩展世界区域；屋顶使用主人实测的专属世界坐标范围。
		const float spawnX = IsRoofBackground()
			? GameRandom::Range(kRoofPreviewZombieMinX, kRoofPreviewZombieMaxX)
			: GameRandom::Range(mSpawnZombiePos1.x, mSpawnZombiePos2.x);
		std::shared_ptr<Zombie> preview;
		if (IsRoofBackground()) {
			// 屋顶预览仍需占一条视觉路径，才能消费连续坡面；其他地图保留自由散落布局。
			const int row = GameRandom::Range(
				std::min(kRoofPreviewZombieFirstRow, mRows - 1), mRows - 1);
			preview = GameAPP::GetInstance().InstantiateZombie(
				zombieType, this, spawnX, GetZombieSpawnY(row, spawnX), row, true);
		}
		else {
			float spawnY = GameRandom::Range(mSpawnZombiePos1.y, mSpawnZombiePos2.y);
			// 红眼巨人的头部高于普通僵尸，9-9预览保留足够上沿空间。
			if (mLevel == AdventureProgression::AREA_NINE_FINAL_LEVEL
				&& zombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR) spawnY = std::max(spawnY, 225.0f);
			preview = GameAPP::GetInstance().InstantiateZombieFree(
				zombieType, this, spawnX, spawnY);
		}
		if (!preview) continue;
		auto addPreview = [this](const std::shared_ptr<Zombie>& member) {
			if (!member) return;
			// 与 Zombie::Die 中的 mZombieNumber-- 保持平衡（预览僵尸 board == this，销毁时会递减）。
			mZombieNumber++;
			mPreviewZombieList.push_back(member.get());
		};

		if (auto* leader = dynamic_cast<BobsledTeamZombie*>(preview.get())) {
			// 出怪表仍只有一个正式候选；选卡宣传画面单独补齐三名无 ID、无碰撞的展示队员。
			leader->ConfigurePreviewTeamMember(0);
			addPreview(preview);
			const Vector leaderPosition = preview->GetPosition();
			for (int slot = 1; slot < 4; ++slot) {
				auto member = GameAPP::GetInstance().InstantiateZombieFree(
					zombieType, this,
					leaderPosition.x + BobsledTeamZombie::GetPreviewMemberOffsetX(slot),
					leaderPosition.y);
				if (member) addPreview(member);
				if (auto* rider = dynamic_cast<BobsledTeamZombie*>(member.get())) {
					rider->ConfigurePreviewTeamMember(slot);
				}
			}
			continue;
		}

		addPreview(preview);
	}
}

Bullet* Board::CreatePlantBullet(BulletType bulletType, int row,
	const Vector& position, PlantType originPlant)
{
	Bullet* bullet = CreateBullet(bulletType, row, position);
	if (bullet) {
		bullet->SetPlantDamageOrigin(PlantDamageOrigin::FromPlant(originPlant));
	}
	return bullet;
}

void Board::DestroyPreviewZombies()
{
	if (mPreviewZombieList.empty()) return;

	for (Zombie* zombie : mPreviewZombieList)
	{
		if (zombie)
		{
			zombie->Die();
		}
	}
	mPreviewZombieList.clear();
}

void Board::InitializeRows()
{
	mRowInfos.clear();
	mRowInfos.resize(mRows);
	for (int i = 0; i < mRows; i++)
	{
		mRowInfos[i].rowIndex = i;
		mRowInfos[i].weight = 1.0f;
		mRowInfos[i].smoothWeight = 1.0f;
		mRowInfos[i].loseMower = -3;
		mRowInfos[i].lastPicked = 0;
		mRowInfos[i].secondLastPicked = 0;
	}
}

void Board::SetRowLoseMower(int row)
{
	if (row < 0 || row >= static_cast<int>(mRowInfos.size())) return;
	mRowInfos[row].loseMower = mCurrentWave;
}

bool Board::IsSpawnRowCompatible(ZombieType type, int row) const
{
	// 车辆没有转弯能力；第二组唯一完整直道位于上入口。
	if (SupportsMineFog() && type == ZombieType::ZOMBIE_ZAMBONI) return row == 0;
	// 海豚僵尸依赖池沿入水状态机，只能在泳池背景的两条水路生成。
	if (type == ZombieType::ZOMBIE_DOLPHIN_RIDER
		|| type == ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER) {
		return IsPoolBackground() && row >= 0 && row < mRows && IsPoolRow(row);
	}
	// 普通冰车已支持昼/夜屋顶的连续坡面；鎏金冰车仍保持未解禁状态。
	if (IsRoofBackground() && type == ZombieType::ZOMBIE_GILDED_ZAMBONI) return false;
	if (!IsPoolBackground()) return row >= 0 && row < mRows;
	if (row < 0 || row >= mRows) return false;
	if (IsPoolRow(row)) {
		// 普通/路障/铁桶会在选行后由 ResolveTerrainZombieType 换成专用泳池版本；
		// 其余允许类型保持原类型，并复用 Zombie 基类的通用入水与裁剪。
		return CanZombieTypeSpawnInPool(type);
	}

	switch (type) {
	case ZombieType::ZOMBIE_POOL_NORMAL:
	case ZombieType::ZOMBIE_POOL_CONE:
	case ZombieType::ZOMBIE_POOL_BUCKET:
		return false;
	default:
		return true;
	}
}

// 自然波次选行在静态地形兼容性之上，再应用泳池开局四波的水路保护期。
bool Board::IsNaturalWaveSpawnRowCompatible(ZombieType type, int row) const
{
	if (!IsSpawnRowCompatible(type, row)) return false;
	if (IsMineBackground() && !mMineGrid.entrance[row]) return false;
	return !IsPoolBackground() || mCurrentWave >= kPoolFirstWaterSpawnWave || !IsPoolRow(row);
}

ZombieType Board::ResolveTerrainZombieType(ZombieType selected, int row) const
{
	if (!IsPoolRow(row)) return selected;
	switch (selected) {
	case ZombieType::ZOMBIE_NORMAL:
		return ZombieType::ZOMBIE_POOL_NORMAL;
	case ZombieType::ZOMBIE_TRAFFIC_CONE:
		return ZombieType::ZOMBIE_POOL_CONE;
	case ZombieType::ZOMBIE_BUCKET:
		return ZombieType::ZOMBIE_POOL_BUCKET;
	default:
		return selected;
	}
}

inline int Board::SelectSpawnRow(ZombieType type, int wave)
{
	if (wave < 0) wave = mCurrentWave;
	if (mRowInfos.empty()) InitializeRows();

	// 第一步：根据 loseMower 计算基础权重
	float totalWeight = 0.0f;
	for (int i = 0; i < mRows; i++)
	{
		if (!IsNaturalWaveSpawnRowCompatible(type, i)) {
			mRowInfos[i].weight = 0.0f;
			continue;
		}
		int mowerTest = wave - mRowInfos[i].loseMower;
		if (mowerTest <= 1)       mRowInfos[i].weight = 0.01f;
		else if (mowerTest <= 2)  mRowInfos[i].weight = 0.5f;
		else                      mRowInfos[i].weight = 1.0f;
		totalWeight += mRowInfos[i].weight;
	}

	// 第二步：计算平滑权重（避免重复选同一行）
	float smoothTotal = 0.0f;
	for (int i = 0; i < mRows; i++)
	{
		if (!IsNaturalWaveSpawnRowCompatible(type, i)) {
			mRowInfos[i].smoothWeight = 0.0f;
			continue;
		}
		float wp = (totalWeight > 0.0f) ? (mRowInfos[i].weight / totalWeight) : 0.0f;
		if (wp >= ROW_WEIGHT_THRESHOLD)
		{
			float pLast = (6.0f * static_cast<float>(mRowInfos[i].lastPicked) * wp
				+ 6.0f * wp - 3.0f) / 4.0f;
			float pSecond = (static_cast<float>(mRowInfos[i].secondLastPicked) * wp
				+ wp - 1.0f) / 4.0f;
			float combined = pLast + pSecond;
			if (combined < 0.01f) combined = 0.01f;
			if (combined > 100.0f) combined = 100.0f;
			mRowInfos[i].smoothWeight = wp * combined;
		}
		else
		{
			mRowInfos[i].smoothWeight = 0.01f;
		}
		smoothTotal += mRowInfos[i].smoothWeight;
	}

	// 第三步：加权随机选行
	if (smoothTotal <= 0.0f) {
		return -1;
	}

	float randNum = GameRandom::Range(0.0f, smoothTotal);
	float cumulative = 0.0f;
	int lastCompatibleRow = -1;
	for (int i = 0; i < mRows; i++)
	{
		if (!IsNaturalWaveSpawnRowCompatible(type, i)) continue;
		lastCompatibleRow = i;
		cumulative += mRowInfos[i].smoothWeight;
		if (cumulative >= randNum) return i;
	}
	return lastCompatibleRow;
}

inline int Board::GetSurvivalPickWeight(ZombieType type) const
{
	int base = GameDataManager::GetInstance().GetZombieWeight(type);
	if (!mIsSurvival) return base;
	int flags = mSurvivalRound - 1;   // 已完成轮数(对应原版 survivalFlagsCompleted)
	if (type == ZombieType::ZOMBIE_NORMAL)
		return SurvivalCurveLerp(SURVIVAL_DILUTE_START_FLAG, SURVIVAL_DILUTE_END_FLAG,
		                         flags, base, base / 10);   // 原版 Normal: base → base/10
	if (type == ZombieType::ZOMBIE_TRAFFIC_CONE)
		return SurvivalCurveLerp(SURVIVAL_DILUTE_START_FLAG, SURVIVAL_DILUTE_END_FLAG,
		                         flags, base, base / 4);     // 原版 Cone: base → base/4
	return base;
}

inline ZombieType Board::GetWeightedRandomZombie()
{
	if (mSpawnZombieList.empty()) return ZombieType::ZOMBIE_NORMAL;

	int totalWeight = 0;
	for (ZombieType type : mSpawnZombieList)
		totalWeight += GetSurvivalPickWeight(type);

	if (totalWeight <= 0) return mSpawnZombieList[0];

	int randVal = GameRandom::Range(0, totalWeight - 1);
	for (ZombieType type : mSpawnZombieList)
	{
		randVal -= GetSurvivalPickWeight(type);
		if (randVal < 0) return type;
	}
	return mSpawnZombieList[0];
}

inline ZombieType Board::GetCheapestZombie()
{
	ZombieType cheapest = ZombieType::ZOMBIE_NORMAL;
	int minCost = INT_MAX;
	for (ZombieType type : mSpawnZombieList)
	{
		int cost = GameDataManager::GetInstance().GetZombieWeight(type);
		if (cost < minCost) { minCost = cost; cheapest = type; }
	}
	return cheapest;
}

/** 按预算和当前关的登场门槛抽取品种；矿场预报传入下一波，不能使用当前波判断阶段。 */
inline ZombieType Board::PickZombieType(int remainingPoints, int wave)
{
	if (wave < 0) wave = mCurrentWave;
	for (int attempt = 0; attempt < 1000; attempt++)
	{
		ZombieType type = GetWeightedRandomZombie();
		int cost = GameDataManager::GetInstance().GetZombieWeight(type);
		int minWave = GameDataManager::GetInstance().GetZombieAppearWave(type);
		// 收官先留开凿与布阵时间，再叠加金雾、支援和重装；仅延后本关，不改全局品种配置。
		if (!mIsSurvival && mLevel == AdventureProgression::AREA_NINE_FINAL_LEVEL) {
			switch (type) {
			case ZombieType::ZOMBIE_ADAPTIVE_HELMET: minWave = 8; break; // 第8波起加入抗性前排
			case ZombieType::ZOMBIE_CRYSTAL_HORN_MINER: minWave = 10; break; // 第10波起加入冲阵压力
			case ZombieType::ZOMBIE_SUN_THIEF: minWave = 12; break; // 第12波起干扰扩建经济
			case ZombieType::ZOMBIE_CRYSTAL_DRUMMER: minWave = 15; break; // 第15波起叠加鼓舞
			case ZombieType::ZOMBIE_HEALER: minWave = 20; break; // 第20波起加入续航支援
			case ZombieType::ZOMBIE_REDEYE_GARGANTUAR: minWave = 30; break; // 第30波后进入重装收官段
			default: break;
			}
		}
		if (!mIsSurvival && mLevel == 78 && type == ZombieType::ZOMBIE_AURORA_PRIEST) minWave = 15;
		if (!mIsSurvival && mLevel == 74 && (type == ZombieType::ZOMBIE_PINK_FOOTBALL
			|| type == ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX)) minWave = 4;
		if (!mIsSurvival && mLevel == kThermalSniperCompositeLevel
			&& type == ZombieType::ZOMBIE_THERMAL_SNIPER) minWave = 1;
		if (remainingPoints >= cost && (mIsSurvival || wave >= minWave))
			return type;
	}
	return GetCheapestZombie();
}

/** 按关卡编排创建本波敌人；小游戏固定阵列，冒险/生存保留点数选池。 */
inline void Board::TrySummonZombie()
{
	if (mCurrentWave > mMaxWave) return;

	float x = static_cast<float>(SCENE_WIDTH) + 40;
	if (MiniGame::IsMiniGame(mLevel)) {
		// 每路保底进攻，后半程加入纵深；编排只影响本小游戏，复用正式出生入口。
		const int ranks = mCurrentWave == MiniGame::WAVES ? 4
			: (mCurrentWave >= 8 ? 3 : (mCurrentWave >= 3 ? 2 : 1));
		for (int rank = 0; rank < ranks; ++rank) {
			for (int row = 0; row < mRows; ++row) {
				ZombieType type = ZombieType::ZOMBIE_NORMAL;
				if (mCurrentWave >= 3 && (row + mCurrentWave + rank) % 3 == 0)
					type = ZombieType::ZOMBIE_TRAFFIC_CONE;
				if (mCurrentWave >= 5 && rank == 0 && row == mCurrentWave % mRows)
					type = ZombieType::ZOMBIE_POLEVAULTER;
				if (mCurrentWave >= 7 && rank == ranks - 1 && (row + mCurrentWave) % 2 == 0)
					type = ZombieType::ZOMBIE_BUCKET;
				// 铁门迫使玩家动用灰烬/压杀；橄榄球检验是否保留救场预算。
				if (mCurrentWave >= 4 && rank == 0 && row == (mCurrentWave + 2) % mRows)
					type = ZombieType::ZOMBIE_NEWSPAPER;
				if (mCurrentWave >= 6 && rank == 1 && row == (mCurrentWave + 1) % mRows)
					type = ZombieType::ZOMBIE_DOOR;
				if (mCurrentWave >= 8 && rank == 0 && (row == mCurrentWave % mRows
					|| (mCurrentWave == MiniGame::WAVES && row % 2 == 0)))
					type = ZombieType::ZOMBIE_FOOTBALL;
				CreateOrQueueWaveZombie(type, row, x + rank * CELL_COLLIDER_SIZE_X);
			}
		}
		return;
	}
	// 6-4 第七波是劫持者的整波单体教学，不受正常第九波出怪门槛限制。
	if (!mIsSurvival && mLevel == kHijackerTutorialLevel
		&& mCurrentWave == kHijackerTutorialWave) {
		const ZombieType actualType = ResolveWaveZombieType(ZombieType::ZOMBIE_HIJACKER);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0) {
				CreateOrQueueWaveZombie(actualType, row, x);
			}
		}
		return;
	}
	// 6-6 第三波额外保底一只急救员；它不消耗正常预算，且普通池仍可自然再选中急救员。
	if (!mIsSurvival && mLevel == kHealerTutorialLevel
		&& mCurrentWave == kHealerTutorialWave) {
		const ZombieType actualType = ResolveWaveZombieType(ZombieType::ZOMBIE_HEALER);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0) {
				CreateOrQueueWaveZombie(actualType, row, x);
			}
		}
	}
	// 7-6 第三波额外保底一只气象干扰僵尸；不消耗预算，但仍消费本波唯一名额。
	if (!mIsSurvival && mLevel == kWeatherJammerTutorialLevel
		&& mCurrentWave == kWeatherJammerTutorialWave) {
		const ZombieType actualType = ResolveWaveZombieType(
			ZombieType::ZOMBIE_WEATHER_JAMMER);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0) {
				CreateOrQueueWaveZombie(actualType, row, x);
			}
		}
	}
	// 7-7 第三波额外保底一只冰像处刑者；不消耗预算，但仍消费本波唯一名额。
	if (!mIsSurvival && mLevel == kIceExecutionerTutorialLevel
		&& mCurrentWave == kIceExecutionerTutorialWave) {
		const ZombieType actualType = ResolveWaveZombieType(
			ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0) {
				CreateOrQueueWaveZombie(actualType, row, x);
			}
		}
	}
	// 8-1 的雪穴形成后，在首个可用波次锁定一只潜雪僵尸；封穴使本次退回右侧且不消费全关保底。
	if (!mIsSurvival && mLevel == kSnowBurrowTutorialLevel
		&& !mSnowBurrowTutorialHoleSpawnConsumed) {
		int tutorialRow = -1;
		for (int row = 0; row < std::min(mRows,
			static_cast<int>(mSnowHoles.size())); ++row) {
			if (mSnowHoles[row].phase == SnowHolePhase::ACTIVE) {
				tutorialRow = row;
				break;
			}
		}
		if (tutorialRow >= 0) {
			const ZombieType actualType = ResolveWaveZombieType(
				ZombieType::ZOMBIE_SNOW_BURROW);
			if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
				CreateOrQueueWaveZombie(actualType, tutorialRow, x, true);
			}
		}
	}
	// 8-5/8-6 的保底不消耗普通预算，但会消费本波品种名额并随存档保持一次性。
	const bool thermalTutorialEdge = !mIsSurvival && !mThermalSniperTutorialSpawned
		&& ((mLevel == kThermalSniperTutorialLevel
				&& mCurrentWave == kThermalSniperTutorialWave)
			|| (mLevel == kThermalSniperCompositeLevel
				&& mCurrentWave == kThermalSniperCompositeWave));
	const int thermalWaveCap = mLevel == kThermalSniperTutorialLevel
		? kThermalSniperTutorialMaxPerWave : kThermalSniperCompositeMaxPerWave;
	if (thermalTutorialEdge && mThermalSnipersSpawnedThisWave < thermalWaveCap) {
		const int row = SelectSpawnRow(ZombieType::ZOMBIE_THERMAL_SNIPER);
		if (row >= 0) {
			++mThermalSnipersSpawnedThisWave;
			if (CreateResolvedWaveZombie(ZombieType::ZOMBIE_THERMAL_SNIPER,
				row, x)) {
				mThermalSniperTutorialSpawned = true;
			}
			else {
				--mThermalSnipersSpawnedThisWave;
			}
		}
	}
	// 8-7/8-8 保底属于额外正式实体，不消耗正常波次预算，但仍消费品种波次与同时名额。
	if (!mIsSurvival && mLevel == kAuroraPriestLevel
		&& mCurrentWave == kAuroraPriestGuaranteeWave
		&& !mAuroraPriestGuaranteeConsumed) {
		const ZombieType actualType = ResolveWaveZombieType(
			ZombieType::ZOMBIE_AURORA_PRIEST);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0 && CreateResolvedWaveZombie(actualType, row, x)) {
				mAuroraPriestGuaranteeConsumed = true;
			}
		}
	}
	if (!mIsSurvival && mLevel == kPolarClockmakerLevel
		&& mCurrentWave == kPolarClockmakerGuaranteeWave
		&& !mClockmakerGuaranteeConsumed) {
		const ZombieType actualType = ResolveWaveZombieType(
			ZombieType::ZOMBIE_POLAR_CLOCKMAKER);
		if (actualType != ZombieType::NUM_ZOMBIE_TYPES) {
			const int row = SelectSpawnRow(actualType);
			if (row >= 0 && CreateResolvedWaveZombie(actualType, row, x)) {
				mClockmakerGuaranteeConsumed = true;
			}
		}
	}

	int remainingPoints = CalculateWaveZombiePoints();
	int zombiesSpawned = 0;
	int candidatesExamined = 0;

	while (remainingPoints > 0 && zombiesSpawned < MAX_ZOMBIES_PER_WAVE
		&& candidatesExamined < kWaveCandidateAttemptLimit)
	{
		++candidatesExamined;
		ZombieType selected = PickZombieType(remainingPoints);
		int cost = GameDataManager::GetInstance().GetZombieWeight(selected);
		if (cost <= 0) break;

		// 每波受限类型在源头拒绝：不选行、不扣预算，也不替换成会干扰出怪池的普通类型。
		const ZombieType actualType = ResolveWaveZombieType(selected);
		if (actualType == ZombieType::NUM_ZOMBIE_TYPES) continue;

		int row = SelectSpawnRow(actualType);
		// 地图没有任何合法行时跳过候选，禁止把地形不兼容品种静默塞进第 1 路。
		if (row < 0) continue;

		// 更新行追踪计数器
		for (int i = 0; i < mRows; i++)
		{
			if (mRowInfos[i].weight > 0.0f)
			{
				mRowInfos[i].lastPicked++;
				mRowInfos[i].secondLastPicked++;
			}
		}
		mRowInfos[row].secondLastPicked = mRowInfos[row].lastPicked;
		mRowInfos[row].lastPicked = 0;

		if (CreateOrQueueWaveZombie(actualType, row, x))
		{
			zombiesSpawned++;
			remainingPoints -= cost;
		}
	}
}

/** 返回本关雾火经济从首波宽松供给到最终波紧缩供给的平滑进度。 */
float Board::GetMistFuelScarcityFactor() const
{
	if (mMaxWave <= 1) return 0.0f;
	const float linear = std::clamp(
		static_cast<float>(mCurrentWave - 1) / static_cast<float>(mMaxWave - 1),
		0.0f, 1.0f);
	return linear * linear * (3.0f - 2.0f * linear);
}

int Board::GetMistFuelRewardAmount() const
{
	return static_cast<int>(std::lround(LerpWeatherValue(
		static_cast<float>(kMistFuelEarlyRewardAmount),
		static_cast<float>(kMistFuelLateRewardAmount),
		GetMistFuelScarcityFactor())));
}

int Board::GetMistFuelWaveBudget() const
{
	return GetMistFuelRewardAmount() * kMistFuelCarriersPerWave;
}

float Board::GetMistFuelBaseCarrierChance() const
{
	return LerpWeatherValue(kMistFuelEarlyBaseCarrierChance,
		kMistFuelLateBaseCarrierChance, GetMistFuelScarcityFactor());
}

float Board::GetMistFuelHeavyCarrierBonus() const
{
	return LerpWeatherValue(kMistFuelEarlyHeavyCarrierBonus,
		kMistFuelLateHeavyCarrierBonus, GetMistFuelScarcityFactor());
}

/**
 * 只给正式波次出生分配雾火：首波提高命中以缓解低出怪量断供，之后独立于天气压力逐波
 * 压低单团价值、携带概率和单波预算；未命中的份额仍跨波累计，避免长期完全断供。
 */
void Board::AssignMistFuelReward(Zombie* zombie)
{
	if (!zombie || !SupportsPlanternMechanics()) return;
	const int rewardAmount = GetMistFuelRewardAmount();
	if (mMistFuelAssignedThisWave + rewardAmount > GetMistFuelWaveBudget()) return;

	const int totalMaxHealth = std::max(0, zombie->mBodyMaxHealth)
		+ std::max(0, zombie->mHelmMaxHealth)
		+ std::max(0, zombie->mShieldMaxHealth);
	const float heavyFactor = std::clamp(
		(static_cast<float>(totalMaxHealth) - 270.0f) / 1800.0f, 0.0f, 1.0f);
	mMistFuelDropAccumulator += GetMistFuelBaseCarrierChance()
		+ GetMistFuelHeavyCarrierBonus() * heavyFactor;

	const float hitChance = std::min(mMistFuelDropAccumulator, 1.0f);
	if (GameRandom::Range(0.0f, 1.0f) > hitChance) return;
	mMistFuelDropAccumulator = std::max(
		0.0f, mMistFuelDropAccumulator - 1.0f);
	mMistFuelAssignedThisWave += rewardAmount;
	zombie->SetMistFuelReward(static_cast<float>(rewardAmount));
}

inline int Board::CalculateWaveZombiePoints(int wave) const
{
	if (wave < 0) wave = mCurrentWave;
	// 基础点数
	float points = (static_cast<float>(wave) / 3 + 1.0f) * 1000.0f;

	// 专属模式固定以难度1为基准，不再叠乘菜单难度；地图与生存轮次修正照常保留。
	points *= (mHxyModeEnabled ? kHxyWaveBudgetMultiplier
		: static_cast<float>(GameAPP::GetInstance().Difficulty)) * 0.5f;
	if (IsStormyNightActive()) {
		points *= kStormyNightWavePointMultiplier;
	}

	// 生存模式：单波点数预算随轮次递增（每轮 mCurrentWave 会重置，故由轮次系数补偿）
	if (mIsSurvival)
	{
		points *= (1.0f + SURVIVAL_BUDGET_GROWTH * static_cast<float>(mSurvivalRound - 1));
	}

	// 判断是否为旗帜波
	bool isFlagWave = (wave % 10 == 0);
	if (isFlagWave)
	{
		points *= 2.5f;
	}

	// 防溢出：float 超过 INT_MAX 时 static_cast<int> 是 UB(实测得 INT_MIN)，
	// 会让本波 remainingPoints<=0 而一只僵尸都不刷。钳到 INT_MAX。
	// 注意 (float)INT_MAX == 2147483648.0f(2^31,比 INT_MAX 大 1)，故用 >= 比较。
	if (points >= static_cast<float>(INT_MAX))
	{
		return INT_MAX;
	}
	return static_cast<int>(points);
}

int Board::GetCurrentWaveZombiePoints() const
{
	return CalculateWaveZombiePoints();
}

void Board::UpdateZombieMetrics()
{
	int64_t TotalHP = 0, CurrectWaveHP = 0;
	int hostileZombieCountForMusic = 0;
	for (auto zombieID : mEntityRegistry.GetAllZombieIDs())
	{
		if (auto zombie = mEntityRegistry.GetZombie(zombieID))
		{
			if (zombie->IsMindControlled()) continue;	// 判断是不是魅惑
			if (!zombie->IsDying() && zombie->HasHead())
			{
				++hostileZombieCountForMusic;
			}

			int64_t zombieHp = static_cast<int64_t>(zombie->mBodyHealth) +
				static_cast<int64_t>(zombie->mHelmHealth) + static_cast<int64_t>(zombie->mShieldHealth);

			TotalHP += zombieHp;
			if (zombie->mSpawnWave == this->mCurrentWave)
			{
				CurrectWaveHP += zombieHp;
			}
		}
	}

	mTotalZombieHP = TotalHP;
	mCurrectWaveZombieHP = CurrectWaveHP;
	mHostileZombieCountForMusic = hostileZombieCountForMusic;
}

void Board::FinalizeWaveSpawnThreshold()
{
	UpdateZombieMetrics();
	// 雪穴只延迟创建，不能让尚未入场的本波候选缺席提前出波基准。
	const bool pendingCurrentWave = std::any_of(
		mPendingSnowHoleSpawns.begin(), mPendingSnowHoleSpawns.end(),
		[this](const PendingSnowHoleSpawn& pending) {
			return pending.spawnWave == mCurrentWave;
		});
	mNextWaveSpawnZombieHP = pendingCurrentWave ? 0 : static_cast<int64_t>(
		GameRandom::Range(0.5f, 0.65f) * static_cast<double>(mCurrectWaveZombieHP));
}

void Board::Update()
{
	// 固定步预算不依赖渲染帧；间隔覆盖一次最多三步的追帧，避免同一渲染帧重叠推演。
	if (DeltaTime::GetDeltaTime() > 0.0f) {
		if (mMonteCarloHealerDecisionCooldownSteps > 0) {
			--mMonteCarloHealerDecisionCooldownSteps;
		}
		if (mMonteCarloBungeeDecisionCooldownSteps > 0) {
			--mMonteCarloBungeeDecisionCooldownSteps;
		}
	}
	// 节拍帧随游戏时间而非逻辑步推进：暂停时逻辑步照跑（UI 要消费点击）但 dt=0，
	// 若无条件 ++，暂停中舞王/伴舞会随节拍翻转瞬间切轨；倍速下也与 Animator 的缩放 dt 同步。
	mBoardFrameAccum += DeltaTime::GetDeltaTime() / DeltaTime::GetFixedStep();
	while (mBoardFrameAccum >= 1.0f) {
		mBoardFrameAccum -= 1.0f;
		mBoardFrame++;
	}
	// 屏幕抖动倒计时：乘 dt 口径（暂停 dt=0 冻结，倍速等比加速），与弹坑计时一致
	if (mShakeTimer > 0.0f) {
		mShakeTimer -= DeltaTime::GetDeltaTime();
	}
	// 天气属于整片场景而非波次逻辑：生存轮间也自然推进；暂停时 dt=0 与粒子同步冻结。
	UpdateWeather(DeltaTime::GetDeltaTime());
	// 夜间泳池迷雾与雨势正交，但同样使用游戏时间并消费更新后的台风强度和实时风向。
	UpdateFog(DeltaTime::GetDeltaTime());
	UpdateWeatherPanelInterference(DeltaTime::GetDeltaTime());
	UpdateIceTrails(DeltaTime::GetDeltaTime());
	UpdatePolarFinaleRituals(DeltaTime::GetDeltaTime());
	UpdateMine(DeltaTime::GetDeltaTime());
	UpdateMineFog(DeltaTime::GetDeltaTime());
	UpdateEchoWaves(DeltaTime::GetDeltaTime());
	CleanupExpiredObjects();
	mUpdateZombieMetricsTimer += DeltaTime::GetDeltaTime();
	if (mUpdateZombieMetricsTimer >= 0.5f)
	{
		mUpdateZombieMetricsTimer = 0.0f;
		UpdateZombieMetrics();
	}
	UpdateLevel();
	AudioSystem::UpdateAdaptiveMusic(DeltaTime::GetDeltaTime(), mHostileZombieCountForMusic);
}

void Board::PrepareMineWave()
{
	if (!IsMineBackground() || mMinePlannedWave == mCurrentWave + 1) return;
	mMineWavePlan.clear();
	mMinePlannedWave = mCurrentWave + 1;
	if (mMinePlannedWave > mMaxWave) return;
	int remaining = CalculateWaveZombiePoints(mMinePlannedWave);
	// 第五波独立教学：组合占用本波预算，不再追加随机候选。
	if (mLevel == 77 && mMinePlannedWave == 5) {
		mMineWavePlan = {{ZombieType::ZOMBIE_SUN_THIEF,1},{ZombieType::ZOMBIE_NORMAL,1},{ZombieType::ZOMBIE_NORMAL,1}};
		return;
	}
	// 新鼓手独立教学组合占用整波，沿用正式出生解析器的累计/同时上限。
	if (mLevel == 79 && mMinePlannedWave == 3) {
		mMineWavePlan = {{ZombieType::ZOMBIE_CRYSTAL_DRUMMER,0},{ZombieType::ZOMBIE_NORMAL,0},{ZombieType::ZOMBIE_NORMAL,0}};
		return;
	}
	bool excavatorPlanned = false;
	bool crystalPlanned = false;
	if (mLevel == 75 && mMinePlannedWave == 3) {
		mMineWavePlan.emplace_back(ZombieType::ZOMBIE_CRYSTAL_HORN_MINER,0);
		mMineWavePlan.emplace_back(ZombieType::ZOMBIE_NORMAL,0);
		mMineWavePlan.emplace_back(ZombieType::ZOMBIE_NORMAL,0);
		remaining = std::max(0,remaining - 5000);
		crystalPlanned = true;
	}
	// 第三波的保证名额先扣正常预算；后续随机抽取至多占用一个本波名额。
	if (mLevel == 74 && mMinePlannedWave == 3) {
		const int row = SelectSpawnRow(ZombieType::ZOMBIE_EXCAVATOR,mMinePlannedWave);
		if (row >= 0) {
			mMineWavePlan.emplace_back(ZombieType::ZOMBIE_EXCAVATOR,row);
			remaining -= GameDataManager::GetInstance().GetZombieWeight(ZombieType::ZOMBIE_EXCAVATOR);
			excavatorPlanned = true;
		}
	}
	for (int attempt = 0; remaining > 0 && attempt < kWaveCandidateAttemptLimit
		&& mMineWavePlan.size() < MAX_ZOMBIES_PER_WAVE; ++attempt) {
		const ZombieType type = PickZombieType(remaining, mMinePlannedWave);
		const int alreadyPlanned = static_cast<int>(std::count_if(mMineWavePlan.begin(),mMineWavePlan.end(),
			[type](const auto& entry) { return entry.first == type; }));
		// 本组重引入的旧威胁在预抽时就遵守正式累计上限，避免预报超额、出生时丢掉预算。
		int planLimit=MAX_ZOMBIES_PER_WAVE;
		switch (type) {
		case ZombieType::ZOMBIE_ELITE_POLEVAULTER: planLimit=kElitePolevaulterMaxPerWave; break;
		case ZombieType::ZOMBIE_ELITE_LADDER: planLimit=kEliteLadderMaxPerWave; break;
		case ZombieType::ZOMBIE_ADAPTIVE_HELMET: planLimit=kAdaptiveHelmetMaxPerWave; break;
		case ZombieType::ZOMBIE_THERMAL_SNIPER: planLimit=kThermalSniperCompositeMaxPerWave; break;
		case ZombieType::ZOMBIE_REDEYE_GARGANTUAR: planLimit=kRedeyeGargantuarMaxPerAdventureWave; break;
		default: break;
		}
		if (alreadyPlanned>=planLimit) continue;
		if (type == ZombieType::ZOMBIE_CRYSTAL_DRUMMER && (alreadyPlanned >= 1
			|| alreadyPlanned + CountActiveOrPendingZombieType(type) >= 2)) continue;
		if (type == ZombieType::ZOMBIE_SUN_THIEF && (alreadyPlanned >= 2
			|| alreadyPlanned + CountActiveOrPendingZombieType(type) >= 3)) continue;
		if (type == ZombieType::ZOMBIE_AURORA_PRIEST && (alreadyPlanned >= kAuroraPriestMaxPerWave
			|| alreadyPlanned + CountActiveOrPendingZombieType(type) >= kAuroraPriestMaxActive)) continue;
		if (type == ZombieType::ZOMBIE_REINFORCED_DOOR && alreadyPlanned >= kReinforcedDoorMaxPerWave) continue;
		if (type == ZombieType::ZOMBIE_EXCAVATOR && excavatorPlanned) continue;
		if (type == ZombieType::ZOMBIE_CRYSTAL_HORN_MINER
			&& (crystalPlanned || CountActiveOrPendingZombieType(type) >= 2)) continue;
		if (type == ZombieType::ZOMBIE_POLAR_CLOCKMAKER) {
			const int planned = static_cast<int>(std::count_if(mMineWavePlan.begin(),mMineWavePlan.end(),
				[type](const auto& entry) { return entry.first == type; }));
			if (planned >= kPolarClockmakerMaxPerWave
				|| planned + CountActiveOrPendingZombieType(type) >= kPolarClockmakerMaxActive) continue;
		}
		// 预报必须包含实际可生成的阵容；沿用精英小丑既有上限，不另设9-2专属限制。
		if (type == ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX
			&& std::count_if(mMineWavePlan.begin(),mMineWavePlan.end(),[type](const auto& entry) {
				return entry.first == type;
			}) >= kEliteJackInTheBoxMaxPerWave) continue;
		const int cost = GameDataManager::GetInstance().GetZombieWeight(type);
		if (cost <= 0) break;
		const int row = SelectSpawnRow(type, mMinePlannedWave);
		if (row < 0) continue;
		for (auto& info : mRowInfos) if (info.weight > 0.0f) {
			++info.lastPicked; ++info.secondLastPicked;
		}
		mRowInfos[row].secondLastPicked = mRowInfos[row].lastPicked;
		mRowInfos[row].lastPicked = 0;
		mMineWavePlan.emplace_back(type, row);
		if (type == ZombieType::ZOMBIE_EXCAVATOR) excavatorPlanned = true;
		if (type == ZombieType::ZOMBIE_CRYSTAL_HORN_MINER) crystalPlanned = true;
		remaining -= cost;
	}
}

int Board::GetMineForecastEntranceMask() const
{
	int mask = 0;
	for (const auto& entry : mMineWavePlan) mask |= 1 << entry.second;
	return mask;
}

void Board::StartGame()
{
	DestroyPreviewZombies();
	if (mPresentation) {
		mPresentation->ShowShovel();
	}
	if (!mIsLoadSave) {
		InitializeMowers();
	}
	mBoardState = BoardState::GAME;
	PrepareMineWave();
	InitializeWeather();
	InitializeWinterTemperature();
	InitializePolarNightEnvironment();
	InitializeFogWeather();
	EnforceStormyNightWeather();
	// 读档恢复到一场雨中时，玩法状态已经由存档还原；粒子是瞬态资源，需按剩余时间重建一次。
	if (mRainIntensity != RainIntensity::CLEAR && !mRainVisualActive)
	{
		EmitRainEffect(mWeatherTimer);
	}
	// 雨转晴途中读档时目标枚举已经是 CLEAR，但旧雨声仍应按剩余过渡时间淡出。
	if (mRainIntensity != RainIntensity::CLEAR
		|| (mWeatherTransitionTimer > 0.0f
			&& mPreviousRainIntensity != RainIntensity::CLEAR)) {
		StartRainAudio();
	}
	RefreshZombieWeatherSpeeds();

	PlayBackgroundMusic();
}

/**
 * 按 C# CutScene.AddFlowerPots 的列优先顺序，为新开的屋顶冒险关铺设初始花盆。
 * 当前九关制把原版 5-1/5-2/后续屋顶关的 5/4/3 列规则映射到内部 37/38/39～54。
 */
void Board::InitializeStartingFlowerPots()
{
	if (!IsRoofBackground()) return;

	int columnCount = 3;
	if (AdventureProgression::IsAdventureLevel(mLevel)
		&& AdventureProgression::GetAreaNumber(mLevel) == 5) {
		const int levelInArea = AdventureProgression::GetLevelNumberInArea(mLevel);
		if (levelInArea == 1) columnCount = 5;
		else if (levelInArea == 2) columnCount = 4;
	}

	// 原版外层遍历列、内层遍历行；保留创建顺序，令实体 ID 与演出层叠同源可复现。
	for (int column = 0; column < std::min(columnCount, mColumns); ++column) {
		for (int row = 0; row < mRows; ++row) {
			if (CanPlantAt(PlantType::PLANT_FLOWERPOT, row, column)) {
				CreatePlant(PlantType::PLANT_FLOWERPOT, row, column);
			}
		}
	}
}

void Board::PrepareBackgroundMusic()
{
	AudioSystem::PrepareMusic(BackgroundMusicKey(mBackGround));
}

void Board::PlayBackgroundMusic()
{
	AudioSystem::PlayMusic(BackgroundMusicKey(mBackGround), -1);
}

void Board::GameOver()
{
	DeltaTime::SetPaused(true);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LOSTGAME, 0.65f);
	AudioSystem::StopMusic();
	if (mPresentation)
		mPresentation->GameOver();
	mBoardState = BoardState::LOSE_GAME;
}

void Board::OnSurvivalRoundClear()
{
	if (!mIsSurvival) return;
	for (int plantID : mEntityRegistry.GetAllPlantIDs()) {
		if (Plant* plant = mEntityRegistry.GetPlant(plantID);
			plant && plant->IsActive() && !plant->IsSquished()) {
			plant->ResetUnyieldingRootsForRound();
		}
	}

	// 推进轮次并重置本轮波次状态（轮次单列在 mSurvivalRound）
	mSurvivalRound++;
	mCurrentWave = 0;
	mMaxWave = SURVIVAL_WAVES_PER_ROUND;
	mZombieCountDown = 10.0f;
	mTrophySpawned = false;
	mTrophy.reset();
	mHasHugeWaveSound = false;
	mHasHugeWaveMusicBurst = false;
	mHugeWaveCountDown = 0.0f;
	mNextWaveSpawnZombieHP = 0;
	mCurrectWaveZombieHP = 0;
	mTotalZombieHP = 0;
	mEliteDancersSpawnedThisWave = 0;
	mReinforcedDoorsSpawnedThisWave = 0;
	mElitePolevaultersSpawnedThisWave = 0;
	mGildedZambonisSpawnedThisWave = 0;
	mEliteDolphinRidersSpawnedThisWave = 0;
	mEliteJackInTheBoxesSpawnedThisWave = 0;
	mEliteDiggersSpawnedThisWave = 0;
	mElitePogosSpawnedThisWave = 0;
	mEliteLaddersSpawnedThisWave = 0;
	mEliteCatapultsSpawnedThisWave = 0;
	mRedeyeGargantuarsSpawnedThisWave = 0;
	mInsulatorsSpawnedThisWave = 0;
	mHijackersSpawnedThisWave = 0;
	mGroundingZombiesSpawnedThisWave = 0;
	mBobsledTeamsSpawnedThisWave = 0;
	mIceWallEngineersSpawnedThisWave = 0;
	mIceCrackDrillsSpawnedThisWave = 0;
	mWeatherJammersSpawnedThisWave = 0;
	mIceStatueExecutionersSpawnedThisWave = 0;
	mSnowBurrowsSpawnedThisWave = 0;
	mAdaptiveHelmetsSpawnedThisWave = 0;
	mAdaptiveHelmetTutorialWaveSpawned = false;
	mThermalSnipersSpawnedThisWave = 0;
	mThermalSniperTutorialSpawned = false;
	RefreshZombieWeatherSpeeds();

	// 重算难度（解锁更强僵尸）+ 刷新关卡名
	BuildSurvivalSpawnList(mSurvivalRound);
	UpdateSurvivalLevelName();

	// 回到选卡：暂停波次推进
	mBoardState = BoardState::CHOOSE_CARD;

	// 通知场景：先结算两次成对词条机会（每次可选或放弃），然后再链式进入选卡。
	if (mPresentation)
		mPresentation->BeginSurvivalPerkSelect();
}

/** AutoTest 直接定位无尽轮次，并同步所有由轮次派生的天气速度状态。 */
bool Board::SetSurvivalRoundForTesting(int round)
{
	if (!mIsSurvival || round < 1) return false;
	mSurvivalRound = round;
	BuildSurvivalSpawnList(round);
	UpdateSurvivalLevelName();
	RefreshZombieWeatherSpeeds();
	return true;
}

/** 按无尽轮次、地形和类型资格重建本轮可见僵尸卡池。 */
void Board::BuildSurvivalSpawnList(int round)
{
	mSpawnZombieList.clear();
	mSpawnZombieList.push_back(ZombieType::ZOMBIE_NORMAL);   // 普通：每轮必有，先固定放入

	// 1) 收集本轮合格候选(排除普通)。旗数递减下调每种僵尸的"有效最早轮次"。
	std::vector<ZombieType> candidates;
	for (int i = 0; i < static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES); ++i)
	{
		const ZombieType t = static_cast<ZombieType>(i);
		if (t == ZombieType::ZOMBIE_NORMAL) continue;
		if (CanZombieTypeEnterSurvivalPool(t, round)) candidates.push_back(t);
	}

	// 2) 第 1~2 轮：候选全放(确定性)→ 自然得到 {普通} / {普通,路障}
	if (round < SURVIVAL_RANDOM_POOL_START_ROUND)
	{
		for (ZombieType t : candidates) mSpawnZombieList.push_back(t);
		return;
	}

	// 3) 第 3 轮起：基础总种类缓慢增长并先钳到 8，再随机 ±1~2 种。
	//    深轮正波动留在上限，负波动形成 6~7 种，避免所有已实现类型每轮固定全上。
	const int baseExtra = SURVIVAL_POOL_BASE_EXTRA
		+ (round - SURVIVAL_RANDOM_POOL_START_ROUND) / SURVIVAL_POOL_GROWTH_EVERY;
	const int maxTypes = std::min(SURVIVAL_POOL_MAX_TYPES,
		static_cast<int>(candidates.size()) + 1);
	const int baseTypes = std::min(1 + baseExtra, maxTypes);
	const int jitterMagnitude = GameRandom::Range(SURVIVAL_POOL_JITTER_MIN, SURVIVAL_POOL_JITTER_MAX);
	const int jitter = GameRandom::Range(0, 1) == 0 ? -jitterMagnitude : jitterMagnitude;
	const int minTypes = std::min(2, maxTypes); // 第3轮起有候选时至少保留普通+1种
	const int targetTypes = std::clamp(baseTypes + jitter, minTypes, maxTypes);
	const int extra = targetTypes - 1;

	// 预筛合格者后做部分 Fisher-Yates，无放回且无需重抽循环。
	for (int k = 0; k < extra; ++k)
	{
		int j = GameRandom::Range(k, static_cast<int>(candidates.size()) - 1);
		std::swap(candidates[k], candidates[j]);
		mSpawnZombieList.push_back(candidates[k]);
	}
}

/** 统一无尽僵尸卡池资格；直造、冒险出怪和其他召唤来源不受这里影响。 */
bool Board::CanZombieTypeEnterSurvivalPool(ZombieType type, int round) const
{
	const int typeIndex = static_cast<int>(type);
	if (!mIsSurvival || round < 1 || typeIndex < 0
		|| typeIndex >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)) return false;
	if (type == ZombieType::ZOMBIE_NORMAL) return true;
	if (type == ZombieType::ZOMBIE_GILDED_ZAMBONI || type == ZombieType::ZOMBIE_SUN_THIEF) return false;

	// 粉色橄榄球是黑夜专属变体；冒险关仍只由 spawnlists.json 控制。
	if (type == ZombieType::ZOMBIE_PINK_FOOTBALL
		&& !GameAPP::GetInstance().GetBackgroundIsNight(mBackGround)) return false;
	const auto& gameData = GameDataManager::GetInstance();
	const int baseRound = gameData.GetZombieSurvivalRound(type);
	if (baseRound < 1 || gameData.GetZombieWeight(type) <= 0) return false;

	bool hasCompatibleRow = false;
	for (int row = 0; row < mRows; ++row) {
		if (!IsSpawnRowCompatible(type, row)) continue;
		hasCompatibleRow = true;
		break;
	}
	if (!hasCompatibleRow) return false;

	const int flagsCompleted = round - 1;
	const int reduction = SurvivalCurveLerp(SURVIVAL_UNLOCK_REDUCE_START_FLAG,
		SURVIVAL_UNLOCK_REDUCE_END_FLAG, flagsCompleted, 0, SURVIVAL_UNLOCK_REDUCE_MAX);
	return std::max(baseRound - reduction, 1) <= round;
}

void Board::UpdateSurvivalLevelName()
{
	const auto* definition = FindSurvivalEndlessDefinition(mLevel);
	const std::string label = definition ? definition->label : u8"未知无尽";
	mLevelName = std::string(u8"生存模式：") + label + u8" 第"
		+ std::to_string(mSurvivalRound) + u8"轮";
}

bool Board::ConsumePlantDamageEchoHit()
{
	if (!mPerkManager.HasPlantDamageEcho()) {
		mPlantDamageEchoHitCounter = 0;
		return false;
	}
	constexpr int kDamageEchoHitInterval = 10; // 每十次实际植物伤害命中触发一次同目标回响
	++mPlantDamageEchoHitCounter;
	if (mPlantDamageEchoHitCounter < kDamageEchoHitInterval) return false;
	mPlantDamageEchoHitCounter = 0;
	return true;
}

void Board::LoadSpawnListFromJson()
{
	nlohmann::json data;
	if (!FileManager::LoadJsonFile("./resources/spawnlists.json", data)) return;
	if (!data.is_array()) return;

	for (auto& entry : data)
	{
		if (!entry.contains("level") || !entry.contains("zombies")) continue;
		if (entry["level"].get<int>() != mLevel) continue;

		mSpawnZombieList.clear();
		std::unordered_set<int> seen;
		for (auto& v : entry["zombies"])
		{
			int val = v.get<int>();
			if (val < 0 || val >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)) continue;
			if (seen.count(val)) continue;
			seen.insert(val);
			mSpawnZombieList.push_back(static_cast<ZombieType>(val));
		}
		if (entry.contains("waves"))
		{
			int waves = entry["waves"].get<int>();
			if (waves > 0)
				mMaxWave = waves;
		}
		// 可选：每关初始阳光（如黑夜收官关 18 给 1500）。只在开新关生效——
		// 读档路径在本函数之后由 GameInfoSaver 还原存档里的 mSun，天然覆盖。
		if (entry.contains("sun"))
		{
			int sun = entry["sun"].get<int>();
			if (sun > 0)
				mSun = sun;
		}
		return;
	}
	// 没找到对应关卡配置，保持默认 ZOMBIE_NORMAL（不清空）
}

Plant* Board::CreatePlantWithID(PlantType type, int row, int col, int id) {
	return CreatePlantWithIDInternal(type, type, row, col, id);
}

Plant* Board::CreateImitaterPlantWithID(
	PlantType targetType, int row, int col, int id)
{
	if ((targetType == PlantType::PLANT_IMITATER || targetType == PlantType::PLANT_CARRYVINE)
		|| IsUpgradePlantType(targetType)
		|| !GameDataManager::GetInstance().HasPlant(targetType)) {
		return nullptr;
	}
	return CreatePlantWithIDInternal(
		PlantType::PLANT_IMITATER, targetType, row, col, id);
}

Plant* Board::CreatePlantWithIDInternal(PlantType actualType,
	PlantType placementType, int row, int col, int id)
{
	Cell* cell = GetCell(row, col);
	const bool isUnderPlant = IsUnderPlantLayerType(placementType);
	const bool isPumpkinPlant = placementType == PlantType::PLANT_PUMPKINSHELL;
	const bool isOverlayPlant = placementType == PlantType::PLANT_INSTANT_COFFEE;
	if (cell && (isUnderPlant
		? cell->GetUnderPlantID()
		: (isPumpkinPlant ? cell->GetPumpkinPlantID()
			: (isOverlayPlant ? cell->GetOverlayPlantID() : cell->GetNormalPlantID()))) != NULL_PLANT_ID) {
		return nullptr;
	}
	if (!isUnderPlant && !isPumpkinPlant && !isOverlayPlant) {
		const PlantFootprint footprint = GetPlantFootprint(placementType);
		for (std::size_t i = 0; i < footprint.count; ++i) {
			Cell* occupiedCell = GetCell(row + footprint.cells[i].rowOffset,
				col + footprint.cells[i].columnOffset);
			if (!occupiedCell || occupiedCell->GetNormalPlantID() != NULL_PLANT_ID) {
				return nullptr;
			}
		}
	}
	// 走 GameApp 工厂拿 shared_ptr 用于 EntityRegistry 注册
	if (row < 0 || row >= mRows || col < 0 || col >= mColumns) {
		LOG_ERROR("Board") << "无效的行列位置: (" << row << ", " << col << ")";
		return nullptr;
	}
	std::shared_ptr<Plant> plant = GameAPP::GetInstance().InstantiatePlant(
		actualType, this, row, col, false);
	if (auto imitater = std::dynamic_pointer_cast<Imitater>(plant)) {
		imitater->SetImitaterTarget(placementType);
	}
	if (plant) {
		mEntityRegistry.AddPlantWithID(plant, id);
		if (cell) {
			if (isUnderPlant) cell->SetUnderPlantID(id);
			else if (isPumpkinPlant) cell->SetPumpkinPlantID(id);
			else if (isOverlayPlant) cell->SetOverlayPlantID(id);
			else if (!OccupyPlantFootprint(placementType, row, col, id)) {
				plant->Die();
				return nullptr;
			}
			const PlantFootprint footprint = GetPlantFootprint(placementType);
			for (std::size_t i = 0; i < footprint.count; ++i) {
				if (Cell* occupiedCell = GetCell(row + footprint.cells[i].rowOffset,
					col + footprint.cells[i].columnOffset)) {
					RefreshPlantStackRenderOrder(occupiedCell);
				}
			}
		}
		if (placementType == PlantType::PLANT_PLANTERN) {
			mActivePlanternID = id;
		}
	}
	return plant.get();
}

Zombie* Board::CreateZombieWithID(ZombieType type, int row, float x, int id) {
	// y 由持久化的 row + x 重建，屋顶不需要新增坐标字段即可恢复连续坡面。
	float y = GetZombieSpawnY(row, x);
	if (y < 0.0f) y = 0.0f;

	std::shared_ptr<Zombie> zombie = GameAPP::GetInstance().InstantiateZombie
	(type, this, x, y, row, false);
	if (!zombie) return nullptr;
	mZombieNumber++;
	mEntityRegistry.AddZombieWithID(zombie, id);
	zombie->mSpawnWave = this->mCurrentWave;
	return zombie.get();
}

Zombie* Board::ReplaceDyingZombieWithID(Zombie* dyingZombie, ZombieType type,
	int row, float x, int id)
{
	if (!dyingZombie || dyingZombie != mEntityRegistry.GetZombie(id)
		|| !dyingZombie->IsActive() || !dyingZombie->IsDying()
		|| dyingZombie->mZombieID != id
		|| dyingZombie->mZombieType != type) return nullptr;

	float y = GetZombieSpawnY(row, x);
	if (y < 0.0f) y = 0.0f;
	std::shared_ptr<Zombie> replacement = GameAPP::GetInstance().InstantiateZombie(
		type, this, x, y, row, false);
	if (!replacement) return nullptr;
	replacement->mSpawnWave = mCurrentWave;

	// 替身尚未登记也未计数，旧壳仍可通过自身稳定 ID 安全清理派生状态。
	// 退役成功后直接把旧壳原有的那一份计数所有权交给替身，不经过 +1/-1 窗口。
	if (!dyingZombie->RetireForTemporalReplacement()) {
		GameObjectManager::GetInstance().DestroyGameObject(replacement);
		return nullptr;
	}
	mEntityRegistry.AddZombieWithID(replacement, id);
	return replacement.get();
}

Bullet* Board::CreateBulletWithID(BulletType type, int row, const Vector& pos, int id) {
	BulletPool* bulletPool = GameObjectManager::GetInstance().GetBulletPool();
	if (!bulletPool) {
		LOG_ERROR("Board") << "CreateBulletWithID 对象池未初始化";
		return nullptr;
	}
	std::shared_ptr<Bullet> bullet = bulletPool->AcquireShared(this, type, row, Vector(10, 10), pos);
	if (bullet) {
		mEntityRegistry.AddBulletWithID(bullet, id);
	}
	return bullet.get();
}

Sun* Board::CreateSunWithID(const Vector& pos, bool fromSky, int id) {
	auto sun = GameObjectManager::GetInstance().CreateGameObjectAsShared<Sun>
		(LAYER_GAME_COIN, this, pos, 0.85f, "Sun",
			fromSky, true);
	if (sun) {
		mEntityRegistry.AddCoinWithID(sun, id);
	}
	return sun.get();
}

SmallSun* Board::CreateSmallSunWithID(const Vector& pos, bool fromSky, int id) {
	auto sun = GameObjectManager::GetInstance().CreateGameObjectAsShared<SmallSun>
		(LAYER_GAME_COIN, this, pos, 0.6f, "SmallSun",
			fromSky, true);
	if (sun) {
		mEntityRegistry.AddCoinWithID(sun, id);
	}
	return sun.get();
}

std::weak_ptr<Shovel> Board::CreateShovel() {
	if (!mShovel.expired())
		return mShovel;

	auto shovel = GameObjectManager::GetInstance().CreateGameObjectImmediateAsShared<Shovel>(LAYER_UI, this);
	mShovel = shovel;
	return mShovel;
}

void Board::ActivateShovel()
{
	if (auto shovel = mShovel.lock()) {
		mCursorObjectManager.Activate(CursorObjectType::SHOVEL, [shovel]() {
			shovel->ReturnHome();
			});
		shovel->Activate();
	}
}

bool Board::CanHaveMowerInRow(int row) const
{
	return row >= 0 && row < mRows
		&& (!SupportsWinterTemperature() || row >= 2);
}

/** 创建当前地形允许的小推车；冬日花园温室遮挡行直接返回空。 */
Mower* Board::CreateMower(MowerType type, int row)
{
	if (!CanHaveMowerInRow(row)) return nullptr;
	if (IsRoofBackground()) type = MowerType::ROOF;
	float x = 160.0f;
	float y = GetMowerTerrainY(row, x + 40.0f);
	const AnimationType animType = type == MowerType::WATER
		? AnimationType::ANIM_POOL_CLEANER
		: (type == MowerType::ROOF
			? AnimationType::ANIM_ROOF_CLEANER : AnimationType::ANIM_LAWNMOWER);
	const float scale = type == MowerType::WATER ? 0.8f : 0.85f;

	auto mower = GameObjectManager::GetInstance().CreateGameObjectImmediateAsShared<Mower>(
		LAYER_GAME_OBJECT, this, type, animType, x, y, row, scale);

	if (mower) {
		mEntityRegistry.AddMower(mower);
	}
	return mower.get();
}

Mower* Board::CreateMowerWithID(MowerType type, int row, float x, float y, int id)
{
	// 旧冬日花园存档可能仍含前两行小推车；加载时按当前地形契约静默丢弃。
	if (!CanHaveMowerInRow(row)) return nullptr;
	// 屋顶开发期旧存档曾把清洁车保存成 LAWN；按当前地图规范化即可无版本迁移兼容。
	if (IsRoofBackground()) {
		type = MowerType::ROOF;
		y = GetMowerTerrainY(row, x + 40.0f);
	}
	const AnimationType animType = type == MowerType::WATER
		? AnimationType::ANIM_POOL_CLEANER
		: (type == MowerType::ROOF
			? AnimationType::ANIM_ROOF_CLEANER : AnimationType::ANIM_LAWNMOWER);
	const float scale = type == MowerType::WATER ? 0.8f : 0.85f;
	auto mower = GameObjectManager::GetInstance().CreateGameObjectImmediateAsShared<Mower>(
		LAYER_GAME_OBJECT, this, type, animType, x, y, row, scale);

	if (mower) {
		mEntityRegistry.AddMowerWithID(mower, id);
	}
	return mower.get();
}

void Board::InitializeMowers()
{
	for (int row = 0; row < mRows; row++) {
		if (!CanHaveMowerInRow(row)) continue;
		CreateMower(IsRoofBackground()
			? MowerType::ROOF
			: (IsPoolRow(row) ? MowerType::WATER : MowerType::LAWN), row);
	}
}

/** 复制 ID 后逐一销毁其他小推车，避免 Die() 延迟回收期间修改遍历来源。 */
void Board::RemoveOtherMowersWithoutTrigger(int preservedMowerID)
{
	const std::vector<int> mowerIDs = mEntityRegistry.GetAllMowerIDs();
	for (int id : mowerIDs) {
		if (id == preservedMowerID) continue;
		Mower* mower = mEntityRegistry.GetMower(id);
		if (!mower) continue;
		SetRowLoseMower(mower->mRow);
		mower->Die();
	}
}

float Board::GetZombieCollisionY(int row) const
{
	return GetZombieCollisionY(row, CELL_INITALIZE_POS_X);
}

float Board::GetZombieCollisionY(int row, float worldX) const
{
	if (row < 0 || row >= mRows) {
		LOG_INFO("Board") << "GetZombieCollisionY: 无效的行索引: " << row;
		return -1.0f;
	}

	// 碰撞始终锚定网格逻辑行；第三大关公共地图修正仍属于整张棋盘的基线。
	const float mapAlignmentOffset = mBackGround == Background::WATER_POOL
		? kThirdAreaZombieAlignmentOffsetY
		: 0.0f;
	return GetRowCenterYAtX(row, worldX) + kZombieSpawnBaseOffsetY + mapAlignmentOffset
		+ (IsRoofBackground() ? kRoofZombieAlignmentOffsetY : 0.0f)
		+ (IsPoolBackground()
			? kPoolBackgroundZombieSpawnYOffset
			: 0.0f);
}

float Board::GetZombieSpawnY(int row) const
{
	return GetZombieSpawnY(row, CELL_INITALIZE_POS_X);
}

float Board::GetZombieSpawnY(int row, float worldX) const
{
	const float collisionY = GetZombieCollisionY(row, worldX);
	if (collisionY < 0.0f) return collisionY;

	// 水路身体需要继续保持主人校对过的下沉画面，但该偏移只影响生成/绘制基准。
	return collisionY + (IsPoolRow(row) ? kPoolRowZombieSpawnYOffset : 0.0f);
}
