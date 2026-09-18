#pragma once
#include "../MiniGameDefinition.h"
#ifndef _BOARD_H
#define _BOARD_H
#include "Game/Cell.h"
#include "Game/Board/MineGrid.h"
#include "Game/GameObject.h"
#include "Game/GameObjectManager.h"
#include "Game/Ladder.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieType.h"
#include "Game/Bullet/BulletType.h"
#include "Game/EntityRegistry.h"
#include "Game/CursorObjectManager.h"
#include "Game/Perk/SurvivalPerkManager.h"
#include "Game/WeatherTypes.h"
#include <vector>
#include <memory>
#include <string>
#include <array>
#include <functional>
#include <unordered_map>

class GameInfoSaver;
class BoardPresentation;
class CardSlotManager;
class Graphics;
class Sun;
class SmallSun;
class Coin;
class Plant;
class Imitater;
class Plantern;
class Zombie;
class HijackerZombie;
class Bullet;
class Trophy;
class Crater;
class IceWall;
class GroundRift;
class Shovel;
class Mower;
struct MagneticItem;
struct WinterGroundImpactResponse;
enum class DamageSource;
enum class MowerType;
enum class PlanternGear : int;
enum class WinterGroundImpactKind;
enum class ZombieJumpType;
namespace PlantDefenseMonteCarlo { struct Config; struct Snapshot; }

struct MonteCarloTargetStats {
	int rolloutCount = 0;
	int candidateCount = 0;
	int sampledZombieCount = 0;
	int sampledPlantCount = 0;
	int supportPlantCount = 0;
	int cardCount = 0;
	float bestScore = 0.0f;
	float coordinationLoss = 0.0f;
};

enum class MonteCarloTreatmentAction {
	AREA,
	FOCUSED,
	WAIT,
};

struct MonteCarloTreatmentRequest {
	int sourceZombieID = -1;
	std::vector<int> areaTargetIDs;
	std::vector<int> focusedTargetIDs;
	float areaRadius = 0.0f;
	float focusedRadius = 0.0f;
	float areaHealAmount = 0.0f;
	float focusedHealAmount = 0.0f;
	float castSeconds = 0.0f;
	float waitSeconds = 0.0f;
	bool allowWait = false;
};

struct MonteCarloTreatmentDecision {
	MonteCarloTreatmentAction action = MonteCarloTreatmentAction::AREA;
	int targetZombieID = -1;
};

enum class Background {
	GROUND_DAY,
	GROUND_NIGHT,
	WATER_POOL,
	NIGHT_WATER_POOL,
	ROOF,
	NIGHT_ROOF,
	WINTER_GARDEN,
	POLAR_NIGHT_SNOWFIELD,
	GLOOMCRYSTAL_MINE
};

inline constexpr int SURVIVAL_ENDLESS_LEVEL = 1000; // 白天无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_NIGHT_LEVEL = 1001; // 黑夜无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_POOL_LEVEL = 1002; // 泳池无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_NIGHT_POOL_LEVEL = 1003; // 黑夜泳池无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_ROOF_LEVEL = 1004; // 白天屋顶无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_NIGHT_ROOF_LEVEL = 1005; // 黑夜屋顶无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_WINTER_LEVEL = 1006; // 雪原无尽专用 level 号
inline constexpr int SURVIVAL_ENDLESS_POLAR_LEVEL = 1007; // 极夜无尽专用 level 号

struct SurvivalEndlessDefinition {
	int level;
	Background background;
	const char* label;
	int requiredAdventureArea;
};

// 无尽模式的关卡号、背景、显示名和解锁大关只有这一处真源，新增地形时选关与 Board 自动同步。
inline constexpr std::array<SurvivalEndlessDefinition, 8> SURVIVAL_ENDLESS_DEFINITIONS{ {
	{ SURVIVAL_ENDLESS_LEVEL, Background::GROUND_DAY, u8"白天无尽", 1 },
	{ SURVIVAL_ENDLESS_NIGHT_LEVEL, Background::GROUND_NIGHT, u8"黑夜无尽", 2 },
	{ SURVIVAL_ENDLESS_POOL_LEVEL, Background::WATER_POOL, u8"泳池无尽", 3 },
	{ SURVIVAL_ENDLESS_NIGHT_POOL_LEVEL, Background::NIGHT_WATER_POOL, u8"黑夜泳池无尽", 4 },
	{ SURVIVAL_ENDLESS_ROOF_LEVEL, Background::ROOF, u8"白天屋顶无尽", 5 },
	{ SURVIVAL_ENDLESS_NIGHT_ROOF_LEVEL, Background::NIGHT_ROOF, u8"黑夜屋顶无尽", 6 },
	{ SURVIVAL_ENDLESS_WINTER_LEVEL, Background::WINTER_GARDEN, u8"雪原无尽", 7 },
	{ SURVIVAL_ENDLESS_POLAR_LEVEL, Background::POLAR_NIGHT_SNOWFIELD, u8"极夜无尽", 8 },
} };

/** 查找无尽关卡定义；普通冒险关返回 nullptr。 */
inline constexpr const SurvivalEndlessDefinition* FindSurvivalEndlessDefinition(int level)
{
	for (const auto& definition : SURVIVAL_ENDLESS_DEFINITIONS) {
		if (definition.level == level) return &definition;
	}
	return nullptr;
}

/** 判断关卡号是否属于任一无尽模式。 */
inline constexpr bool IsSurvivalEndlessLevel(int level)
{
	return FindSurvivalEndlessDefinition(level) != nullptr;
}

struct RowInfo {
	int rowIndex = 0;
	float weight = 1.0f;
	float smoothWeight = 1.0f;
	int loseMower = -3;   // 割草机丢失时的波次（-3 使第1波权重正常为1.0）
	int lastPicked = 0;    // 上次被选中后经过的僵尸数
	int secondLastPicked = 0;
};

namespace {
	constexpr int MAX_SUN = 9990;
	constexpr float NEXTWAVE_COUNT_MAX = 25.0f;
	constexpr float SPAWN_SUN_TIME = 14.0f;       // 日间天降普通阳光的生成间隔，单位：游戏秒
	constexpr float POOL_SUN_SPAWN_TIME = 14.0f;  // 日间泳池水面小阳光的生成间隔，单位：游戏秒
	constexpr int MAX_ZOMBIES_PER_WAVE = 250;	// 一波最大僵尸数量

	// ===== 生存模式设置 =====
	constexpr int   SURVIVAL_WAVES_PER_ROUND = 10;   // 每轮（每面旗）波数，10 与"第10波=一大波"逻辑对齐
	constexpr float SURVIVAL_BUDGET_GROWTH = 0.55f;  // 每轮单波点数预算增长系数
	constexpr float SURVIVAL_HP_GROWTH = 0.05f;  // 每轮僵尸全局血量倍率线性增长系数（可调）：mult = 1 + x*(轮次-1)
	// 生存模式出怪池子组装(见 BuildSurvivalSpawnList)
	constexpr int SURVIVAL_RANDOM_POOL_START_ROUND = 3;   // 第几轮起改为"普通+随机子集"
	constexpr int SURVIVAL_POOL_BASE_EXTRA = 1;   // 第3轮的随机种类数(除普通外)
	constexpr int SURVIVAL_POOL_GROWTH_EVERY = 2;   // 每多少轮 +1 种(缓慢增长)
	constexpr int SURVIVAL_POOL_MAX_TYPES = 8;   // 最终池子上限（包含必出的普通僵尸）
	constexpr int SURVIVAL_POOL_JITTER_MIN = 1;  // 基础种类数的随机波动幅度
	constexpr int SURVIVAL_POOL_JITTER_MAX = 2;
	// 旗数递减(复刻原版 TodAnimateCurve(18,50,flags,0,15))：深局提前解锁强僵尸；
	// 当前阵容(survivalRound 最高7、18旗才起步)下休眠，为未来高 survivalRound 僵尸预留。
	constexpr int SURVIVAL_UNLOCK_REDUCE_START_FLAG = 18;
	constexpr int SURVIVAL_UNLOCK_REDUCE_END_FLAG = 50;
	constexpr int SURVIVAL_UNLOCK_REDUCE_MAX = 15;
	// 杂兵稀释(复刻原版 Normal→base/10、Cone→base/4，TodAnimateCurve(10,50,...))：仅作用于抽中权重。
	constexpr int SURVIVAL_DILUTE_START_FLAG = 10;
	constexpr int SURVIVAL_DILUTE_END_FLAG = 50;
}

enum class BoardState {
	CHOOSE_CARD,
	GAME,
	LOSE_GAME,
	WIN,
	NONE,
};

class Board {
public:
	friend GameInfoSaver;

	BoardState mBoardState = BoardState::CHOOSE_CARD;
	std::string mLevelName = "关卡 1-1";
	int mLevel = -1;	// 冒险模式当前关卡序号
	Background mBackGround = Background::GROUND_DAY; // 背景图
	int mRows = 5;	// 行数
	int mColumns = 8; // 列数
	std::weak_ptr<Shovel> mShovel;
	CursorObjectManager mCursorObjectManager;
	int mSun = 50;
	bool mHxyModeEnabled = false; // 本局锁定的 HXY专属规则，随关卡保存，读档不跟随菜单开关变化
	float mSunCountDown = 5.0f;
	float mPoolSunCountDown = POOL_SUN_SPAWN_TIME;
	EntityRegistry mEntityRegistry;
	int mCurrentWave = 0;			// 当前波
	int mMaxWave = 10;		// 关卡总波数
	float mZombieCountDown = 20.0f;		// 下一波僵尸倒计时
	int64_t mTotalZombieHP = 0;		// 在场全部僵尸血量
	int64_t mCurrectWaveZombieHP = 0;	// 本波僵尸血量
	int64_t mNextWaveSpawnZombieHP = 0;		// 下一波僵尸刷新血量

	int mZombieNumber = 0;

	// 全局节拍帧计数（随游戏时间推进：60Hz 基准，暂停冻结、倍速等比加速，入存档）。
	// 用途：舞王/伴舞全队共舞节拍源——所有舞者从同一计数推导动作，不互相通信也能整齐划一。
	int mBoardFrame = 0;
	float mBoardFrameAccum = 0.0f;	// 游戏时间→节拍帧的亚帧余量（不入存档，读档损失<1帧无感）

	// 舞蹈节拍帧 0~22 循环，每拍 12 逻辑步(0.2s)，等价原版 100Hz 的 20cs/拍、23 拍一循环。
	// 0~11 = 舞步段(anim_walk)，12~22 = 举手段(anim_armraise)；补充召唤只在节拍==12 触发。
	int GetDanceBeatFrame() const { return mBoardFrame % (12 * 23) / 12; }

	/** 返回最近一次僵尸指标采样得到的敌对、有头、未垂死僵尸数，供动态音乐与 AutoTest 使用。 */
	int GetHostileZombieCountForMusic() const { return mHostileZombieCountForMusic; }

	bool mTrophySpawned = false;  // 防止重复生成
	std::weak_ptr<Trophy> mTrophy;  // 每关至多一个；所有权在 GameObjectManager，此处仅供存档定位

	// 毁灭菇弹坑：所有权在 GameObjectManager（到时自毁），此处 weak_ptr 仅供寻址/存档
	std::vector<std::weak_ptr<Crater>> mCraters;
	// 已放置扶梯：所有权在 GameObjectManager；Board 只保留格子查询与存档所需的弱引用。
	std::vector<std::weak_ptr<Ladder>> mLadders;
	// 冰墙工程师建筑：全场至多一堵；所有权在 GameObjectManager，Board 只做 O(1) 寻址与存档。
	std::weak_ptr<IceWall> mIceWall;
	// 冰裂钻机提交的同行地裂：所有权在 GameObjectManager，Board 只做存档与测试寻址。
	std::vector<std::weak_ptr<GroundRift>> mGroundRifts;

	bool mIsSurvival = false;     // 是否为生存模式（无尽）
	int  mSurvivalRound = 1;      // 当前第几面旗（轮次，从 1 起）
	SurvivalPerkManager mPerkManager;   // 生存模式词条（非生存关恒空）
	int mPlantDamageEchoHitCounter = 0; // 火力回响：本局累计到下一次回响的实际植物命中数

	Vector mSpawnZombiePos1 = Vector(1180, 85);			// 左上角坐标
	Vector mSpawnZombiePos2 = Vector(1500, 581);		// 右下角坐标

	std::vector<Zombie*> mPreviewZombieList;  // 预览僵尸（选卡阶段）；Board 显式管理生命周期，Die() 后立刻 clear()

	// 外层表示行（rows） 内层columns。Cell 所有权在 GameObjectManager；这里仅做格子寻址
	std::vector<std::vector<Cell*>> mCells;
	std::array<float, 8> mIceMinX{};      // 每行冰道最靠房屋的世界 X；最多覆盖当前六行地图
	std::array<float, 8> mIceTimer{};     // 每行冰道剩余寿命，单位秒
	std::array<float, 8> mGoldenIceMinX{}; // 每行黄色冰道最靠房屋的世界 X；与普通冰道独立共存
	std::array<float, 8> mGoldenIceTimer{}; // 每行黄色冰道剩余寿命，单位秒

private:
	/** 雪穴预警期间尚未创建的正式波次僵尸；提交前清穴只改回右侧入口。 */
	struct PendingSnowHoleSpawn {
		ZombieType type = ZombieType::NUM_ZOMBIE_TYPES;
		int row = -1;
		int holeColumn = -1;
		int spawnWave = 0;
		float timer = 0.0f;
		bool tutorialSnowBurrow = false;
	};

	/** 极光祭司提交后独立存在的裂隙出生事务。 */
	struct PendingAuroraRift {
		ZombieType type = ZombieType::NUM_ZOMBIE_TYPES;
		int row = -1;
		int column = -1;
		int spawnWave = 0;
		float timer = 0.0f;
		int transactionID = 0;
		int ownerZombieID = -1;
	};

	/** 时间锚对单只僵尸保存的可回溯核心状态。 */
	struct TemporalTargetSnapshot {
		int zombieID = -1;
		ZombieType type = ZombieType::NUM_ZOMBIE_TYPES;
		int row = -1;
		float x = 0.0f;
		float mineRowOffset = 0.0f; // 矿道连续换行相对行基线的 Y 偏移，旧档默认为已到行
		int mineTargetCell = -1; // 矿道已承诺节点，随回位一起恢复
		bool mineTargetReturning = false; // 与节点一起恢复，避免把回溯误判为再次掉头
		int bodyHealth = 0;
		HelmType helmType = HelmType::HELMTYPE_NONE;
		int helmHealth = 0;
		ShieldType shieldType = ShieldType::SHIELDTYPE_NONE;
		int shieldHealth = 0;
		float slowTimer = 0.0f;
		float frozenTimer = 0.0f;
		float butterTimer = 0.0f;
		float paralysisTimer = 0.0f;
		bool hasHead = true;
		bool hasArm = true;
		bool irreversible = false;
		bool restoreHelm = true;
		bool restoreShield = true;
		bool specialActionSubmitted = false;
		bool abilityStateValid = false;
		int abilityPhase = -1;
		float abilityRemaining = 0.0f;
		int abilityReleaseCount = 0;
	};

	/** 钟匠提交后与来源生命周期解耦的六秒时间锚。 */
	struct TemporalAnchor {
		int ownerZombieID = -1;
		float timer = 0.0f;
		float visualPulseTimer = 0.0f; // 仅驱动时间锚可读性，不参与存档与回溯语义
		std::vector<TemporalTargetSnapshot> targets;
	};

	BoardPresentation* mPresentation = nullptr; // 非拥有；宿主场景的生命周期覆盖 Board
	CardSlotManager* mCardSlotManager = nullptr; // 非拥有；由 GameScene 的场景控制器绑定
	/** 采集推演共用的植物、僵尸、卡槽和格子纯数值快照。 */
	bool BuildMonteCarloCombatSnapshot(
		PlantDefenseMonteCarlo::Snapshot& snapshot, bool mindControlledFaction,
		bool includeNightRoofChargeDetails = false);
	/** 用核心文件的通用调参填充蒙特卡洛战斗配置，供拆分后的环境决策复用。 */
	void ConfigureMonteCarloCombatConfig(
		PlantDefenseMonteCarlo::Config& config,
		int rolloutCount, float horizonSeconds) const;
	/** 追加植物范围冲击配置，并复用核心南瓜保护参数。 */
	void ConfigureMonteCarloPlantImpactConfig(
		PlantDefenseMonteCarlo::Config& config,
		int rolloutCount, float horizonSeconds,
		int damage, float radius) const;
	/** 返回屋顶坡段包含的逻辑列数；环境文件不得复制核心几何常量。 */
	int GetRoofSlopeColumnCount() const;
	/** 为急救员推演投影当前劫持者处决倒计时和生存模式生命线。 */
	void PopulateNightRoofHijackerTreatmentForecast(
		int lockedHijackerID, float& executionSeconds,
		float& survivalExecutionLineCap) const;
	int mMonteCarloHealerDecisionCooldownSteps = 0; // 下次急救员推演前需经过的固定逻辑步数，不入存档
	int mMonteCarloBungeeDecisionCooldownSteps = 0; // 下次蹦极选点推演前需经过的固定逻辑步数，不入存档
	std::vector<ZombieType> mSpawnZombieList;	// 本关出怪表
	float mHugeWaveCountDown = 0.0f;	// 一大波倒计时
	float mUpdateZombieMetricsTimer = 0.0f;	// 僵尸血量与音乐敌对数的合并采样计时器
	int mHostileZombieCountForMusic = 0;	// 每 0.5 游戏秒刷新，避免动态音乐每帧重复扫描全部僵尸
	float mPlantRegenTimer = 0.0f;	// 词条③：全场植物回血脉冲计时器
	bool mHasHugeWaveSound = false;		// 有无放过一大波音乐
	bool mHasHugeWaveMusicBurst = false;	// 本次一大波警告是否已强制加入鼓组
	bool mIsLoadSave = false;	// 是否正在加载存档
	/** 用独立的实体身份和落种语义创建植物；普通入口传入相同类型。 */
	Plant* CreatePlantInternal(PlantType actualType, PlantType placementType,
		int row, int column, bool skipsettings, bool isPreview,
		bool playerDeployment = false);
	/** 成功完成卡片落种后向同行僵尸发布一次原子部署事件。 */
	void NotifyPlayerPlantDeployed(const Plant& plant, PlantType placementType);
	/** 读档版双类型创建；调用方必须先从模仿者 extraData 取得目标。 */
	Plant* CreatePlantWithIDInternal(PlantType actualType, PlantType placementType,
		int row, int col, int id);

	// 黑夜随机天气。weatherTimer 在 CLEAR 时表示距下一场雨，在下雨时表示本场剩余时间。
	RainIntensity mRainIntensity = RainIntensity::CLEAR;
	RainIntensity mPreviousRainIntensity = RainIntensity::CLEAR; // 两秒平滑过渡开始前的雨势
	RainIntensity mForecastRainIntensity = RainIntensity::CLEAR; // 已发布预警对应的下一天气
	RainIntensity mActualForecastRainIntensity = RainIntensity::CLEAR; // 预警发布时锁定的真实下一天气
	float mWeatherTimer = 0.0f;
	float mWeatherTransitionTimer = 0.0f; // 当前天气过渡的剩余时间（秒，随游戏速度缩放）
	float mLightningTimer = 0.0f;
	float mRainSplashTimer = 0.0f;       // 距下一次地面水花的秒数；瞬态视觉无需写入存档
	bool mWeatherInitialized = false;   // 旧档缺天气字段时由 StartGame 首次初始化
	bool mRainCanIntensify = false;     // 仅初始小雨可增强；首次切档后永久转入衰减链
	bool mRainCanHold = false;          // 新雨首段为中/大雨时允许一次同档续期，避免无限维持
	bool mWeatherForecastReady = false; // true 表示公开预报与真实下一天气均已锁定、等待揭晓
	bool mWeatherForecastDisrupted = false; // 当前雨雪及待生效台风预警已被隐藏；真实锁定结果仍保留
	float mWeatherPanelInterferenceTimer = 0.0f; // 整个气象栏目主动黑障的可叠加剩余游戏秒；期间新广播也会被截获
	bool mStormyNightInitialized = false; // 4-9 第 23 波暴风雨夜是否已经正式初始化，防读档重置阵风额度
	int mStormyNightFlashPattern = 0;   // 原版 4-10 三种闪光节奏（1～3）；0 表示尚未启用
	float mStormyNightFlashTimer = 0.0f; // 当前闪光节奏剩余游戏秒；黑屏等待也包含在此计时内
	bool mWinterTemperatureInitialized = false; // 冬日花园寒潮状态是否已初始化；旧档由 StartGame 补齐
	bool mOpeningColdWavePlanInitialized = false; // 7-8/7-9 开幕寒潮是否已锁定，读档后不得重复注入
	ColdWavePhase mColdWavePhase = ColdWavePhase::CALM; // 当前寒潮阶段，与雨势完全正交
	float mColdWaveTimer = 0.0f;        // 当前寒潮阶段剩余游戏秒
	float mAmbientTemperatureC = 6.0f;  // 当前环境温度，单位摄氏度；只由寒潮状态机写入
	ColdWaveStrength mColdWaveStrength = ColdWaveStrength::STRONG; // 已锁定寒潮强度；预报与揭晓共用
	float mColdWaveTargetTemperatureC = -12.0f; // 已锁定最低温，单位摄氏度
	float mColdWaveCoolingDuration = 20.0f; // 已锁定降温总时长，单位游戏秒
	float mColdWaveHoldDuration = 57.5f; // 已锁定低温维持总时长，单位游戏秒
	float mColdWaveThawDuration = 32.0f; // 已锁定回暖总时长，单位游戏秒
	bool mColdWaveForecastDisrupted = false; // 本轮准确预报是否已被干扰；下一轮重新开放
	int mWinterFrostVariant = 0;       // 本轮寒潮稳定使用的冻融线轮廓变体（0～2）

	// 极夜雪原环境导演独立于雨势和寒潮；所有随机量在计划开始时一次性锁定。
	bool mPolarNightInitialized = false; // 新局初始化或读档恢复后保持，不允许 StartGame 重抽
	PolarNightPhase mPolarNightPhase = PolarNightPhase::DORMANT; // 当前连续曲线或白毛风阶段
	bool mPolarPlanIsWhiteout = false; // 当前计划是否保证三红并提交白毛风
	bool mPolarLastPlanWasFalse = false; // 假信号最多连续一次的持久约束
	bool mPolarFirstWhiteoutCompleted = false; // 8-3 固定首轮是否已经完整消费
	int mPolarDangerMask = 0;          // bit0 低温、bit1 高湿、bit2 强风
	float mPolarTemperatureC = -14.0f; // 仪表权威温度，单位摄氏度
	float mPolarHumidityPercent = 58.0f; // 仪表权威相对湿度，百分比
	float mPolarWindSpeedMps = 8.0f;  // 仪表权威风速，单位米/秒
	float mPolarStartTemperatureC = -14.0f; // 当前曲线起点温度
	float mPolarStartHumidityPercent = 58.0f; // 当前曲线起点湿度
	float mPolarStartWindSpeedMps = 8.0f; // 当前曲线起点风速
	float mPolarTargetTemperatureC = -14.0f; // 当前计划锁定目标温度
	float mPolarTargetHumidityPercent = 58.0f; // 当前计划锁定目标湿度
	float mPolarTargetWindSpeedMps = 8.0f; // 当前计划锁定目标风速
	float mPolarPhaseTimer = 0.0f;     // 当前阶段已经推进的游戏秒
	float mPolarPhaseDuration = 0.0f;  // 当前阶段锁定总时长，单位游戏秒
	float mPolarAllDangerTimer = 0.0f; // 三项连续危险累计；归零代表尚未提交
	float mPolarHighHumidityTimer = 0.0f; // 当前高湿段连续累计，3 秒提交雪穴
	bool mPolarHumidityEpisodeConsumed = false; // 同一连续高湿段只提交一批
	bool mPolarTutorialHoleBatchConsumed = false; // 8-1 全关只允许首批雪穴
	VerticalWindDirection mPolarVerticalWindDirection = VerticalWindDirection::NONE; // 强风段锁定吹向
	float mPolarWhiteoutTimer = 0.0f;  // 雪盲或淡出阶段剩余游戏秒
	float mPolarFluctuationTimer = 0.0f; // 危险保持期当前微波动段已经推进的游戏秒
	float mPolarFluctuationDuration = 0.0f; // 当前微波动段锁定总时长，单位游戏秒
	float mPolarWindParticleTimer = 0.0f; // 纯视觉风雪粒子重发计时，不进入存档
	float mPolarWindVisualStrength = 0.0f; // 纯视觉强风淡入强度，不进入存档
	bool mPolarFinalWaveUpgradeApplied = false; // 8-9 最终波平滑升级只提交一次
	std::array<SnowHoleState, 5> mSnowHoles{}; // 五行雪穴槽；每行最多一个
	int mLastSnowHoleBatchCreated = 0; // 最近一批实际形成数量，仅供观测与测试
	std::vector<PendingSnowHoleSpawn> mPendingSnowHoleSpawns; // 已扣波次预算、等待雪雾预警结束的出生事务
	std::vector<PendingAuroraRift> mPendingAuroraRifts; // 已提交、等待展开的裂隙出生事务
	std::vector<TemporalAnchor> mTemporalAnchors; // 已提交、等待回溯的独立时间锚
	int mNextDiscontinuousTransactionID = 1; // 非连续入场事务稳定排序 ID
	int mAuroraPriestsSpawnedThisWave = 0; // 本波累计创建的敌对极光祭司
	int mClockmakersSpawnedThisWave = 0; // 本波累计创建的敌对极夜钟匠
	int mSunThievesSpawnedThisWave = 0; // 本波累计盗晶；读档与正式生成共用
	int mCrystalDrummersSpawnedThisWave = 0; // 本波累计鼓手；最多一只，场上同时最多两只
	int mCrystalMinersSpawnedThisWave = 0; // 本波累计晶角矿工，限制为一只
	bool mAuroraPriestGuaranteeConsumed = false; // 8-7 第三波保底已提交
	bool mClockmakerGuaranteeConsumed = false; // 8-8 第二波保底已提交
	float mDawnNavigationTimer = 0.0f; // 曙光莲强风模块全场导航剩余游戏秒
	float mRoofRunoffCharge = 0.0f;     // 昼夜屋顶坡面径流积累值（0～100）
	float mRoofRunoffRetainedCharge = 0.0f; // 本次冲刷结束后兑现的预抽残留湿度（30～60）
	RoofRunoffPhase mRoofRunoffPhase = RoofRunoffPhase::IDLE; // 当前径流所处的待机、预警或冲刷阶段
	float mRoofRunoffPhaseTimer = 0.0f; // 当前预警或冲刷阶段剩余游戏秒
	int mRoofRunoffRowMask = 0;         // 已锁定的冲刷行 bitmask；待机阶段为 0
	float mNightRoofCharge = 0.0f;      // 黑夜屋顶独立雷荷积累值（0～100）
	float mNightRoofOvercharge = 0.0f;  // 满电活动阶段截留、放电结束后兑现的余电（0～25）
	NightRoofChargePhase mNightRoofChargePhase = NightRoofChargePhase::CHARGING; // 当前积累、预警或放电阶段
	float mNightRoofChargePhaseTimer = 0.0f; // 当前预警或放电阶段剩余游戏秒
	int mNightRoofChargeRow = -1;       // 满电后一次锁定的导电瓦路行；积累阶段为 -1
	bool mNightRoofChargeGuided = false; // 本轮是否锁定为接地僵尸引导路线；锁定后不因实体失效改写
	int mNightRoofChargeGuideID = NULL_ZOMBIE_ID; // 本轮接地路线锁定的稳定实体 ID
	bool mNightRoofChargeRouteUsedMonteCarlo = false; // 最近一次满电路线是否由统一短视推演选出，仅供观测
	MonteCarloTargetStats mNightRoofChargeRouteStats; // 最近一次满电路线推演规模与得分，仅供观测
	int mNightRoofChargeRouteDecisionMicros = 0; // 最近一次满电路线选择的墙钟微秒，仅供性能观测
	bool mNightRoofHijackerSelectionAttempted = false; // 本轮是否已经在 75% 边沿完成唯一一次候选选择
	int mNightRoofHijackerID = NULL_ZOMBIE_ID; // 本轮被锁定的劫持者稳定实体 ID；取消后保持空且不补选
	bool mNightRoofHijackerWarningExtended = false; // 满电时是否由仍有效的劫持者把预警扩展到七秒
	bool mNightRoofHijackerFinalizing = false; // 本轮劫持者是否已进入最后一秒的停走充能阶段

	// 夜间泳池迷雾是独立于雨势的环境层：基础雾、增强雾势、预报与台风驱散均由 Board 持有。
	FogWeatherIntensity mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	FogWeatherIntensity mForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	FogWeatherIntensity mActualForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	float mFogWeatherTimer = 0.0f;      // DEFAULT 时距下一次增强雾势，其余档位为本次事件剩余游戏秒
	float mFogDispersal = 0.0f;         // 台风累积驱散比例（0～1）；停风后缓慢回落，基础雾随之回流
	float mFogVisualOffsetX = 0.0f;     // 随实时风向平滑漂移的纯视觉横向偏移，单位像素
	float mFogAnimationTime = 0.0f;     // 雾纹理轻微呼吸的游戏秒计时；纯视觉，不入存档
	bool mFogWeatherInitialized = false; // 旧档缺雾势字段时由 StartGame 首次初始化
	bool mFogWeatherForecastReady = false; // 已锁定公开/真实下一雾势，等待独立雾势倒计时揭晓
	bool mFogWeatherForecastDisrupted = false; // 当前雾势预报已被隐藏；真实下一雾势仍按原计时揭晓
	std::vector<float> mFogCellAlpha;   // 逐格平滑后的最终 alpha；行数为泳池六行再加一条底部收边
	int mActivePlanternID = NULL_PLANT_ID; // 当前唯一可用路灯花 ID；由实体创建/死亡派生，不单独入存档
	float mMistFuelDropAccumulator = 0.0f; // 正式波次雾火随机的保底累计值；影响未来抽取，进入存档
	int mMistFuelAssignedThisWave = 0; // 当前波已分配的雾火总量；只作预算闸门与观测

	bool mPendingHeavyTyphoonPrepared = false; // 已锁定公开大雨警报等级；真实新大雨时同为待生效台风初态
	bool mPendingHeavyTyphoonOpeningProtected = false; // 本次等级因开局保护锁定为无台风；真实消费时不累计落空保底
	TyphoonStrength mPendingHeavyTyphoonStrength = TyphoonStrength::NONE; // 警报等级；真实新大雨时也是待生效台风等级
	WindDirection mPendingHeavyWindDirection = WindDirection::NONE; // 锁定等级配套风向；真实新大雨时待生效
	float mPendingHeavyTyphoonStrengthTimer = 0.0f; // 锁定等级首档时长；真实新大雨时待生效（游戏秒）
	float mPendingHeavyWindDirectionTimer = 0.0f; // 锁定等级首次重抽风向倒计时（游戏秒）
	float mPendingHeavyWindGustTimer = 0.0f; // 锁定等级首次阵风倒计时（游戏秒）
	int mPendingHeavyTyphoonGustsRemaining = 0; // 锁定等级首档阵风预算；真实新大雨时待生效
	int mPendingHeavyRainPromptVariant = 0; // 同等级三句古风警报中的锁定编号（0～2）
	bool mHeavyRainPromptShown = false; // 当前公开大雨预报是否已经弹出过提前 5 秒警报
	bool mRainVisualActive = false;     // 纯运行期标记，防读档/生存轮间重复发射同一场雨
	std::string mRainVisualEffectName;  // 当前雨丝特效名；风向切换时只停止旧雨而不清空其他粒子
	float mWindParticleTimer = 0.0f;    // 距下一批风线粒子的游戏秒数；瞬态视觉不入存档
	TyphoonStrength mTyphoonStrength = TyphoonStrength::NONE; // 大雨附加台风；离开大雨立即清空
	WindDirection mWindDirection = WindDirection::NONE;       // 当前风实际吹向，台风期间分段独立重抽
	float mTyphoonStrengthTimer = 0.0f; // 当前台风强度距下一档衰减的游戏秒数
	float mWindDirectionTimer = 0.0f;   // 距下一次风向独立重抽的游戏秒数
	float mWindGustTimer = 0.0f;        // 非阵风期间距下一次阵风开始的游戏秒数
	int mTyphoonGustsRemaining = 0;     // 本次台风阶段尚可触发的阵风次数
	bool mTyphoonGustActive = false;    // true 时锁定本次阵风强度/风向并连续吹动僵尸
	TyphoonStrength mActiveGustStrength = TyphoonStrength::NONE; // 阵风开始时锁定的强度，避免中途衰减突变
	WindDirection mActiveGustDirection = WindDirection::NONE;    // 阵风开始时锁定的吹向，避免中途翻转
	float mActiveGustDuration = 0.0f;   // 当前阵风完整持续时间（游戏秒）
	float mActiveGustTimer = 0.0f;      // 当前阵风剩余时间（游戏秒）
	float mActiveGustPlantMoveTimer = 0.0f; // 距本次阵风植物整格结算的游戏秒数
	bool mActiveGustPlantMoved = false; // 本次阵风是否已结算植物，防读档后重复移动
	int mWeakWeatherPhasesSinceHeavy = 0; // 后期连续非大雨新天气数；达到上限后下轮保底大雨并进入存档
	int mHeavyPhasesWithoutTyphoon = 0; // 连续未命中台风的新大雨阶段数；用于保底并进入存档
	int mEliteDancersSpawnedThisWave = 0; // 当前波已生成的精英舞王数量；用于每波上限并进入存档
	int mReinforcedDoorsSpawnedThisWave = 0; // 当前波正式生成的加固铁门数量；上限计数并进入存档
	int mElitePolevaultersSpawnedThisWave = 0; // 当前波正式生成的精英撑杆数量；上限计数并进入存档
	int mGildedZambonisSpawnedThisWave = 0; // 当前波正式生成的鎏金冰车数量；每波至多一只并进入存档
	int mEliteDolphinRidersSpawnedThisWave = 0; // 当前波正式生成的精英海豚数量；每波至多一只并进入存档
	int mEliteJackInTheBoxesSpawnedThisWave = 0; // 当前波正式生成的精英小丑数量；每波至多一只并进入存档
	int mEliteDiggersSpawnedThisWave = 0; // 当前波正式生成的爆破工头数量；每波至多一只并进入存档
	int mElitePogosSpawnedThisWave = 0; // 当前波正式生成的精英跳跳数量；每波至多一只并进入存档
	int mEliteLaddersSpawnedThisWave = 0; // 当前波正式生成的精英扶梯数量；每波至多一只并进入存档
	int mEliteCatapultsSpawnedThisWave = 0; // 当前波正式生成的导流投篮车数量；每波至多一只并进入存档
	int mRedeyeGargantuarsSpawnedThisWave = 0; // 当前冒险波正式生成的红眼巨人数量；每波至多三只并进入存档，无尽不消费
	int mInsulatorsSpawnedThisWave = 0; // 当前波正式生成的绝缘僵尸数量；所有正式波次统一至多两只并进入存档
	int mHijackersSpawnedThisWave = 0; // 当前波正式生成的劫持者数量；所有正式波次统一至多两只并进入存档
	int mHijackerSpawnCooldownWavesRemaining = 0; // 当前波之后仍需封锁劫持者刷新的完整正式波次数
	bool mHijackerSpawnBlockedThisWave = false; // 当前波是否承接成功处决后的刷新封锁；与剩余波数一起入档
	int mGroundingZombiesSpawnedThisWave = 0; // 当前波正式生成的接地僵尸数量；所有正式波次统一至多两只并进入存档
	int mBobsledTeamsSpawnedThisWave = 0; // 当前波正式生成的雪橇车队数量；跟随者不重复计数
	int mIceWallEngineersSpawnedThisWave = 0; // 当前波正式生成的冰墙工程师数量；所有正式波次统一至多一只
	int mIceCrackDrillsSpawnedThisWave = 0; // 当前波正式生成的冰裂钻机数量；所有正式波次统一至多三只
	int mWeatherJammersSpawnedThisWave = 0; // 当前波正式生成的气象干扰僵尸数量；所有正式波次统一至多一只
	int mIceStatueExecutionersSpawnedThisWave = 0; // 当前波正式生成的冰像处刑者数量；所有正式波次统一至多一只
	int mSnowBurrowsSpawnedThisWave = 0; // 当前波已接受的潜雪僵尸数量；8-1 上限一只，其后上限两只
	bool mSnowBurrowTutorialHoleSpawnConsumed = false; // 8-1 是否已有一只潜雪僵尸真正从雪穴提交出生
	int mAdaptiveHelmetsSpawnedThisWave = 0; // 当前波已接受的适应头盔僵尸数量；8-3 起每波至多四只
	bool mAdaptiveHelmetTutorialWaveSpawned = false; // 8-3 首轮白毛风结束后的独立教学出场是否已提交
	int mThermalSnipersSpawnedThisWave = 0; // 当前波已接受的热感狙击僵尸数量；8-5/8-6 上限分别为三/四只
	bool mThermalSniperTutorialSpawned = false; // 当前关要求的独立保底是否已经提交
	int mEliteScaredyShroomsPlanted = 0; // 本关累计种下的精英胆小菇数量；死亡或铲除不返还次数
	int mLastTyphoonMovedPlants = 0;    // 最近一次阵风移动的植物数，仅供观测和测试
	int mLastTyphoonLostPlants = 0;     // 最近一次阵风吹出棋盘或吹入弹坑的植物数，仅供观测和测试
	int mLastTyphoonBlockedPlantSteps = 0; // 最近一次阵风被锚定植物直接挡下的植物格步数，仅供观测和测试

	std::vector<RowInfo> mRowInfos;
	static constexpr float ROW_WEIGHT_THRESHOLD = 1e-6f;
	float mCellInitialY = CELL_INITALIZE_POS_Y;
	float mCellHeight = CELL_COLLIDER_SIZE_Y;

	// 屏幕抖动状态（见 ShakeBoard）。timer 递减到 0 即结束；重复触发直接覆盖重置
	float mShakeTimer = 0.0f;         // 剩余秒数，<=0 = 未抖动
	float mShakeDuration = 0.12f;     // 本次抖动总时长（秒）
	float mShakeAmountX = 0.0f;       // 峰值位移（原版符号约定）
	float mShakeAmountY = 0.0f;
	int   mShakeOscillations = 1;     // 1=原版三角弹跳；>1=衰减正弦来回甩
	int mTargetingCobCannonID = NULL_PLANT_ID; // 当前等待玩家指定落点的加农炮；纯 UI 瞬态不入存档

	void LoadSpawnListFromJson();
	/** 按底座、本体、南瓜、覆盖层排列同格绘制号，并同步转移号的分配所有权。 */
	void RefreshPlantStackRenderOrder(Cell* cell);
	/** 校验双玉米投手、两格外壳与边界，供加农炮放置和卡片可用性共用。 */
	bool IsValidCobCannonAnchor(int row, int anchorColumn) const;
	/** 把同一普通层植物 ID 原子写入其全部排他占格；任一格冲突时不修改棋盘。 */
	bool OccupyPlantFootprint(PlantType type, int row, int anchorColumn, int plantID,
		const std::vector<int>& replacePlantIDs = {});
	void InitializeRows();
	/** 按地形与行平滑权重选择正式出生行；没有合法行时返回 -1。 */
	inline int SelectSpawnRow(ZombieType type, int wave = -1);
	bool IsSpawnRowCompatible(ZombieType type, int row) const;
	bool IsNaturalWaveSpawnRowCompatible(ZombieType type, int row) const;
	inline ZombieType PickZombieType(int remainingPoints, int wave = -1);
	inline ZombieType GetWeightedRandomZombie();
	inline ZombieType GetCheapestZombie();
	void InitializeWeather();
	void UpdateWeather(float deltaTime);
	/** 建立极夜雪原独立初态；新局锁定首轮计划，读档不得覆盖。 */
	void InitializePolarNightEnvironment();
	/** 一次性锁定下一轮真假信号、危险组合、目标实数与阶段时长。 */
	void RollNextPolarNightPlan();
	/** 推进三仪表、阈值提交、白毛风与雪穴形成状态。 */
	void UpdatePolarNightEnvironment(float deltaTime);
	/** 8-3 首轮白毛风结束后，从右侧独立保底一只适应头盔僵尸。 */
	void TrySpawnAdaptiveHelmetTutorialWave();
	/** 锁定下一段不跨危险线的邻近仪表目标；波动本身不改变计划提交状态。 */
	void BeginPolarGaugeFluctuation();
	/** 平滑推进当前仪表微波动段，到点后再一次性抽取下一段。 */
	void UpdatePolarGaugeFluctuation(float deltaTime);
	/** 高湿提交时均匀锁定最多两个不同行的合法空格。 */
	void CommitSnowHoleBatch();
	/** 只推进已经锁定的雪穴形成计时，不重新抽取格位。 */
	void UpdateSnowHoles(float deltaTime);
	/** 推进正式波次雪穴预警；到提交边沿时按雪穴是否仍在决定近路或右侧出生。 */
	void UpdatePendingSnowHoleSpawns(float deltaTime);
	/** 周期发射与锁定垂直方向一致的分层风雪粒子。 */
	void UpdatePolarNightWindVisual(float deltaTime);
	/** 创建正式波次僵尸，或在同行活动雪穴处锁定一次延迟出生事务。 */
	bool CreateOrQueueWaveZombie(ZombieType actualType, int row, float rightEdgeX,
		bool tutorialSnowBurrow = false);
	/** 返回活动实体与已锁定雪穴事务中指定类型的敌对数量。 */
	int CountActiveOrPendingZombieType(ZombieType type) const;
	/** 最终波警告开始时从当前实数平滑补齐三项危险，不重新触发已有白毛风。 */
	void BeginPolarFinalWavePrelude();
	/** 开发者直调跳过最终波警告时，立即提交 8-9 最终白毛风。 */
	void ActivatePolarFinalWaveImmediately();
	void UpdateWeatherPanelInterference(float deltaTime);
	/** 初始化并推进冬日花园独立寒潮；雨势和台风不得改写温度。 */
	void InitializeWinterTemperature();
    /** 锁定 7-8/7-9 的固定开幕强寒潮；新开实战或旧档恢复尚未消费时调用。 */
    void InitializeOpeningColdWavePlan();
	/** 一次性抽取并锁定下一场寒潮的强度、最低温、阶段时长与冻融线外观。 */
	void RollNextColdWave();
	void UpdateWinterTemperature(float deltaTime);
	/** 推进昼夜屋顶雨水积累、锁行预警与短时冲刷状态机。 */
	void UpdateRoofRunoff(float deltaTime);
	/** 从存档恢复已经判定的积累值、阶段、锁定行、残留湿度与剩余时间，不重新抽取。 */
	void RestoreRoofRunoffState(float charge, RoofRunoffPhase phase,
		int rowMask, float phaseTimer, float retainedCharge);
	/** 推进黑夜屋顶独立雷荷的积累、锁行预警与基础放电状态机。 */
	void UpdateNightRoofCharge(float deltaTime);
	/** 统一接收正向雷荷；积累阶段跨阈值的溢出和活动阶段新增量都转入余电。 */
	void AddNightRoofCharge(float amount);
	/** 满电后抽取并锁定本次导电瓦路；锁定结果在活动阶段保持不变。 */
	void BeginNightRoofChargeWarning();
	/** 用统一短视推演比较全部普通行与现存接地候选；失败时走正式 RNG 回退。 */
	void ChooseNightRoofChargeRoute(float warningSeconds);
	/** 在本轮雷荷首次达到 75% 时从专用弱索引锁定唯一候选；本轮取消后不补选。 */
	void TryLockNightRoofHijacker();
	/** 返回仍能执行本轮能力的已锁定劫持者；只做稳定 ID 的 O(1) 查询。 */
	HijackerZombie* GetValidNightRoofHijacker() const;
	/** 在普通放电之前按同一帧快照批量处决，并先清除施法者权威引用以禁止连锁。 */
	void ResolveNightRoofHijackerExecution();
	/** O(1) 查询目标同格承载层是否提供雷荷停机保护；不做逐帧全场遍历。 */
	Plant* GetNightRoofChargeSupportProtector(const Plant* target) const;
	/** O(1) 查询目标同格承载层是否提供劫持者处决豁免。 */
	Plant* GetNightRoofHijackerSupportProtector(const Plant* target) const;
	/** 放电周期结束或场景不支持时清理本轮锁定状态。 */
	void ResetNightRoofHijackerCycle();
	/** 在预警转放电的唯一边沿快照目标，并结算通用停机、伤害与麻痹。 */
	void ResolveNightRoofChargeDischarge();
	/** 从存档恢复雷荷积累、余电、阶段、锁定行和倒计时，不重新抽取路线。 */
	void RestoreNightRoofChargeState(float charge, NightRoofChargePhase phase,
		int row, float phaseTimer, float overcharge, bool hijackerSelectionAttempted,
		int hijackerID, bool hijackerWarningExtended, bool hijackerFinalizing,
		bool guided, int guideID);
	void InitializeFogWeather();
	void UpdateFog(float deltaTime);
	void UpdateFogWeather(float deltaTime);
	void UpdateFogDispersal(float deltaTime);
	void UpdateFogCellAlpha(float deltaTime, bool snapToTarget);
	FogWeatherIntensity RollNextFogWeather(int forcedRoll = 0);
	void PrepareFogWeatherForecast(int fogRoll = 0);
	void ConsumeFogWeatherForecast();
	void BeginFogWeather(FogWeatherIntensity intensity, float duration);
	void EndFogWeather(float defaultDuration);
	float RandomDefaultFogWeatherDuration() const;
	void ClearFogWeatherForecast();
	void RestoreFogState(bool initialized, FogWeatherIntensity intensity,
		FogWeatherIntensity forecast, FogWeatherIntensity actual,
		float timer, bool forecastReady, float dispersal, float visualOffsetX);
	float GetWeatherLateGameFactor() const;
	float GetWeatherDirectorFactor() const;
	float GetWeatherTransitionProgress() const;
	float GetRainAudioVolume() const;
	void BeginWeatherTransition(RainIntensity target);
	void UpdateWeatherTransition(float deltaTime);
	void FinishWeatherTransitionImmediately();
	void RestoreWeatherTransition(RainIntensity previous, float remaining);
	/** 将当前普通出波倒计时夹紧到暴风雨夜上限；波次调度常量仍由 Board 核心唯一持有。 */
	void ClampStormyNightWaveCountdown();
	/** 使用 Board 核心持有的统一音量播放一次天气雷声。 */
	void PlayWeatherThunder();
	void ActivateStormyNight();
	void EnforceStormyNightWeather();
	void UpdateStormyNightFlash(float deltaTime);
	void ScheduleNextStormyNightFlash();
	void GetStormyNightOverlayAlphas(float& blackAlpha, float& whiteAlpha) const;
	int GetNextWeatherRollTotal() const;
	bool ShouldForceHeavyWeather() const;
	void RecordNewWeatherOutcome(RainIntensity next);
	RainIntensity RollNextWeather(int forcedRoll = 0);
	void PrepareWeatherForecast(int weatherRoll = 0);
	void ConsumeWeatherForecast();
	bool NeedsPendingHeavyForecastState() const;
	void PreparePendingHeavyTyphoon(int chanceRoll = 0, int strengthRoll = 0);
	void ClearPendingHeavyRainWarning();
	void MaybeShowHeavyRainPrompt();
	void BeginRain(RainIntensity intensity, float duration, bool canIntensify, bool canHold,
		bool allowTyphoonRoll = true);
	// 结束当前雨段：按固定权重落点决定放晴或进入一个不可再增强的尾雨段。
	void FinishRainPhase(int transitionRoll);
	void EndRain();
	void StartTyphoonForHeavyPhase(int chanceRoll = 0, int strengthRoll = 0,
		WindDirection forcedDirection = WindDirection::NONE);
	void ConsumePendingHeavyTyphoon();
	void StopTyphoon();
	void RestoreWeakWeatherPity(int weakWeatherPhases);
	void RestoreTyphoonPity(int missedHeavyPhases);
	void RestorePendingHeavyTyphoon(bool prepared, bool openingProtected,
		TyphoonStrength strength,
		WindDirection direction, float strengthTimer, float gustTimer,
		float directionTimer, int gustsRemaining, int promptVariant);
	void RestoreEliteDancerWaveSpawnCount(int count);
	void RestoreReinforcedDoorWaveSpawnCount(int count);
	void RestoreElitePolevaulterWaveSpawnCount(int count);
	void RestoreGildedZamboniWaveSpawnCount(int count);
	void RestoreEliteDolphinRiderWaveSpawnCount(int count);
	void RestoreEliteJackInTheBoxWaveSpawnCount(int count);
	void RestoreEliteDiggerWaveSpawnCount(int count);
	void RestoreElitePogoWaveSpawnCount(int count);
	void RestoreEliteLadderWaveSpawnCount(int count);
	void RestoreEliteCatapultWaveSpawnCount(int count);
	void RestoreRedeyeGargantuarWaveSpawnCount(int count);
	void RestoreInsulatorWaveSpawnCount(int count);
	void RestoreHijackerWaveSpawnCount(int count);
	void RestoreHijackerSpawnCooldown(int wavesRemaining, bool blockedThisWave);
	void RestoreGroundingZombieWaveSpawnCount(int count);
	void RestoreBobsledTeamWaveSpawnCount(int count);
	void RestoreIceWallEngineerWaveSpawnCount(int count);
	void RestoreIceCrackDrillWaveSpawnCount(int count);
	void RestoreWeatherJammerWaveSpawnCount(int count);
	void RestoreIceStatueExecutionerWaveSpawnCount(int count);
	/** 成功处决后立即封锁本波后续候选，并预留后续完整波次的刷新冷却。 */
	void BeginHijackerSpawnCooldown();
	/** 新波开始时把一份未来冷却转为覆盖整个当前波的封锁。 */
	void AdvanceHijackerSpawnCooldownForNewWave();
	void RestoreTyphoonState(TyphoonStrength strength, WindDirection direction,
		float strengthTimer, float gustTimer, float directionTimer, int gustsRemaining);
	void RestoreActiveTyphoonGust(bool active, TyphoonStrength strength,
		WindDirection direction, float duration, float remaining,
		float plantMoveRemaining, bool plantMoved);
	void UpdateTyphoon(float deltaTime);
	void UpdateTyphoonWindVisual(float deltaTime);
	void WeakenTyphoon();
	/** 到达维持时限后独立重抽风向；directionRoll=0 使用正式随机，1/2 供确定性测试。 */
	void RerollWindDirection(int directionRoll = 0);
	bool BeginTyphoonGust(bool consumeBudget, float forcedPlantMoveIn = -1.0f);
	void UpdateActiveTyphoonGust(float deltaTime);
	void EndTyphoonGust();
	void TriggerTyphoonPlantMove(TyphoonStrength strength, WindDirection direction);
	void EmitRainEffect(float duration);
	void RestartRainVisualForWindChange();
	void UpdateRainGroundSplash(float deltaTime);
	void TriggerRainGroundSplash();
	void StartRainAudio();
	void StopRainAudio();
	void RefreshZombieWeatherSpeeds();
	/** 大雨阶段触发一次同步的程序化闪电与雷声。 */
	void TriggerLightning();
	void AssignMistFuelReward(Zombie* zombie);
	// 生存模式"抽中权重"：对 NORMAL/CONE 随轮稀释(仅供 GetWeightedRandomZombie；成本侧仍用 GetZombieWeight)
	inline int GetSurvivalPickWeight(ZombieType type) const;

public:
	// 该行僵尸的视觉落脚 y（由地图行几何与地形美术偏移派生）。公开原因：伴舞出土裁剪要用
	// “行地面线”而非僵尸自身动态坐标定裁剪底边——换新地图/行高时自动适配（主人指示）。
	float GetZombieSpawnY(int row) const;
	/** 返回指定世界 X 上的僵尸视觉落脚 Y；屋顶斜坡必须使用此重载。 */
	float GetZombieSpawnY(int row, float worldX) const;
	/**
	 * 返回该行僵尸参与碰撞的逻辑基线 Y。
	 * 水路美术下沉不进入此坐标，避免子弹、植物和小推车判定随贴图对齐量漂移。
	 */
	float GetZombieCollisionY(int row) const;
	/** 返回指定世界 X 上的僵尸碰撞基线；屋顶斜坡必须使用此重载。 */
	float GetZombieCollisionY(int row, float worldX) const;

	Board(BoardPresentation* presentation, Background background, int level);
	~Board();

	/** 返回非拥有的场景展示端口；用于存档恢复 UI 瞬态。 */
	BoardPresentation* GetPresentation() const { return mPresentation; }
	/** 绑定当前场景卡槽，供 Board 级轻量推演读取玩家实际已选卡与冷却。 */
	void BindCardSlotManager(CardSlotManager* manager) { mCardSlotManager = manager; }
	/**
	 * @brief 从当前实体和实际卡槽构建快照，用蒙特卡洛短视推演选择植物爆区。
	 *
	 * 算法只推进紧凑数值副本，不创建 GameObject，也不消费 GameRandom。
	 */
	bool PickMonteCarloPlantBlastTarget(
		int minRow, int maxRow, int damage, float radius, int sourceZombieID,
		int& targetRow, Vector& targetPosition,
		MonteCarloTargetStats* stats = nullptr,
		const std::vector<int>* removalPlantIDs = nullptr,
		int* selectedRemovalPlantID = nullptr,
		float removalStrikeInterval = 0.0f,
		int removalStrikeDamage = 0,
		const std::vector<int>* removalStrikeCounts = nullptr,
		int maxCandidateRolloutEvaluations = 0);
	/**
	 * @brief 用共享短视推演从给定植物 ID 中选择立即移除后对玩家损失最大的目标。
	 */
	bool PickMonteCarloPlantRemovalTarget(
		const std::vector<int>& eligiblePlantIDs, int sourceZombieID,
		int& targetPlantID, MonteCarloTargetStats* stats = nullptr,
		float strikeInterval = 0.0f, int strikeDamage = 0,
		const std::vector<int>* strikeCounts = nullptr,
		int maxCandidateRolloutEvaluations = 0);
	/**
	 * @brief 比较急救员当前全部群疗、单疗与一次延迟分支；魅惑侧不适用时返回 false。
	 *
	 * Board 是唯一读取实体、卡槽、待结算治疗与雷荷锁定状态的边界。
	 */
	bool PickMonteCarloZombieTreatment(
		const MonteCarloTreatmentRequest& request,
		MonteCarloTreatmentDecision& decision,
		MonteCarloTargetStats* stats = nullptr);
	/**
	 * @brief 领取按固定逻辑步限流的急救员推演名额。
	 *
	 * 名额尚未恢复时调用者保持治疗就绪，后续按实体 ID 顺序尝试。
	 */
	bool TryClaimMonteCarloHealerDecisionSlot();
	/**
	 * @brief 领取按固定逻辑步限流的蹦极选点推演名额。
	 *
	 * 名额尚未恢复时调用者停留在屏幕外等待，避免同波蹦极把多次长时域推演叠到一帧。
	 */
	bool TryClaimMonteCarloBungeeDecisionSlot();
	/** 完成一次读档恢复，并在实体全部还原后立即同步派生的逐格迷雾。 */
	void CompleteLoadRestore();
	/** 返回 Board 是否仍处于关卡存档恢复生命周期。 */
	bool IsLoadRestoreActive() const { return mIsLoadSave; }

	/** 按模式结算阳光收益；最后的家底不接受任何补给。 */
	inline void AddSun(int amount)
	{
		if (MiniGame::IsMiniGame(mLevel)) return;
		// 只缩放正常收益入口；开局阳光、AutoTest set_sun 与花费均不走这里。
		const int scaledAmount = mPerkManager.ScaleSunIncome(amount);
		if (scaledAmount > MAX_SUN - mSun)
		{
			mSun = MAX_SUN;
			return;
		}
		mSun += scaledAmount;
	}

	inline void SubSun(int amount)
	{
		mSun -= amount;
	}

	inline int GetSun() { return mSun; }

	void SetZombieSpawnList(std::vector<ZombieType>& zombieTypeList) {
		this->mSpawnZombieList = zombieTypeList;
	}

	const std::vector<ZombieType>& GetSpawnZombieList() const { return mSpawnZombieList; }
	/** 判断指定类型在当前无尽地形与轮次能否进入本轮僵尸候选池。 */
	bool CanZombieTypeEnterSurvivalPool(ZombieType type, int round) const;

	/** 当前雨势对僵尸 Animator extra 层的倍率。 */
	float GetZombieRainSpeedMultiplier() const;
	/** 独立天气压力进度（0～1）；供后期玩法倍率与天气导演共同使用。 */
	float GetWeatherPressureFactor() const;
	/** 台风对僵尸水平移动的额外倍率；返回值以当前雨天速度为 1。 */
	float GetZombieWindMoveMultiplier(bool movingTowardFront) const;
	/** 台风对轻型植物子弹水平速度的派生倍率；不修改子弹基础速度。 */
	float GetPlantBulletWindSpeedMultiplier(bool movingTowardFront) const;
	/** 台风对轻型植物子弹命中伤害的派生倍率；不修改子弹基础伤害。 */
	float GetPlantBulletWindDamageMultiplier(bool movingTowardFront) const;
	/** 当前雨势对植物攻击、生产、成长和恢复计时的倍率。 */
	float GetPlantRainActionSpeedMultiplier() const;
	/** 屋顶冲刷时，花盆上的目标行坡面植物暂停行动；花盆本体保持工作。 */
	bool IsPlantPausedByRoofRunoff(const Plant* plant) const;
	/** 世界层蓝灰暗幕的 alpha（0..255）；UI 在暗幕之后绘制，不受影响。 */
	float GetRainOverlayAlpha() const;
	/** 4-9 第 22 波显示暴风雨预报，但尚不改变玩法天气。 */
	bool IsStormyNightForecastActive() const;
	/** 4-9 第 23～30 波固定启用暴风雨夜。 */
	bool IsStormyNightActive() const;
	bool IsStormyNightInitialized() const { return mStormyNightInitialized; }
	int GetStormyNightFlashPattern() const { return mStormyNightFlashPattern; }
	float GetStormyNightFlashTimer() const { return mStormyNightFlashTimer; }
	bool IsStormyNightFlashOn() const;
	float GetStormyNightBlackAlpha() const;
	float GetStormyNightWhiteAlpha() const;
	RainIntensity GetRainIntensity() const { return mRainIntensity; }
	RainIntensity GetPreviousRainIntensity() const { return mPreviousRainIntensity; }
	/**
	 * 屋脊督军直接提升当前雨势；不会降级更强天气，是否延长同档由调用方显式决定。
	 * @return 本次确实改变档位或延长持续时间时返回 true。
	 */
	bool TriggerRoofMarshalWeather(RainIntensity target, float duration,
		bool extendSameIntensity = false);
	float GetWeatherTimer() const { return mWeatherTimer; }
	float GetWeatherTransitionTimer() const { return mWeatherTransitionTimer; }
	bool IsWeatherTransitionActive() const { return mWeatherTransitionTimer > 0.0f; }
	float GetLightningTimer() const { return mLightningTimer; }
	/** 冬日花园独立温度与冻融线查询；非冬日地图始终返回温暖、中性结果。 */
	bool SupportsWinterTemperature() const { return mBackGround == Background::WINTER_GARDEN; }
	/** 极夜环境与旧天气完全正交，只由专用背景启用。 */
	bool SupportsPolarNightEnvironment() const {
		return mBackGround == Background::POLAR_NIGHT_SNOWFIELD;
	}
	/** 矿场地形、施工与查询只有 Board 拥有；预览只读副本不提交地形。 */
	bool IsMineBackground() const { return mBackGround == Background::GLOOMCRYSTAL_MINE; }
	MineGrid mMineGrid;
	static constexpr float kMineFogDuration = 90.0f; // 各色矿雾单次总时长，游戏秒，包含渐入渐退
	float mMineFogElapsed = -1.0f; // -1 表示无雾，其余为本次雾潮已过游戏秒
	int mMineFogNextWave = 10; // 完全散尽后按所在波次加三；初始化时按关卡设定首次波次
	bool mMineFogTutorialSeen = false;
	float mMineFogNoticeRemaining = 0.0f; // 首次说明的剩余游戏秒
	/** 首次矿雾波次；收官提前，其余关卡仍保留原教学窗口。 */
	int GetMineFogOpeningWave() const { return mLevel == 81 ? 5 : 10; }
	/** 矿雾独立于普通雾，不接入照明、驱散和索敌遮挡。 */
	bool SupportsMineFog() const { return IsMineBackground() && (mLevel >= 75 && mLevel <= 81); }
	bool HasPurpleMineFog() const { return SupportsMineFog() && mLevel >= 77 && mLevel <= 78; }
	bool HasGoldenMineFog() const { return SupportsMineFog() && mLevel >= 79; }
	int GetMineFogFirstColumn() const { return HasGoldenMineFog() ? 3 : HasPurpleMineFog() ? 4 : 5; }
	float GetMineFogReduction() const { return HasGoldenMineFog() ? 0.75f : HasPurpleMineFog() ? 0.5f : 0.25f; }
	float GetMineFogStrength() const;
	struct SunTheftRecord { int stolen = 0; int carried = 0; bool escaped = false; bool disabled = false; };
	static constexpr int kSunTheftCapacity = 375; // 单只盗晶僵尸本关累计盗取上限；账本、满载与读档共用，不随回溯返还
	// 经济账本独立于可被时间锚回溯的实体；死亡返款后仍保留同 ID 记录。
	std::unordered_map<int, SunTheftRecord> mSunTheftLedger;
	SunTheftRecord GetSunTheftRecord(int zombieID) const;
	/** 原子扣余额、累计盗款并增加携款；前摇不预扣。 */
	int CommitSunTheft(int zombieID, int requested);
	/** 死亡/魅惑返还剩余携款，逃走则结清为损失；重复调用不重复返钱。 */
	void CloseSunTheft(int zombieID, bool escaped);
	void DisableSunTheft(int zombieID);
	int GetSunThievesSpawnedThisWave() const { return mSunThievesSpawnedThisWave; }
	int GetCrystalMinersSpawnedThisWave() const { return mCrystalMinersSpawnedThisWave; }
	int GetCrystalDrummersSpawnedThisWave() const { return mCrystalDrummersSpawnedThisWave; }
	/** 取得雾区及其右侧场外敌方僵尸的减伤比例；魅惑与棱镜标记目标不受保护。 */
	float GetMineFogProtection(const Zombie* zombie) const;
	int ScaleMineFogDamage(int damage, const Zombie* zombie) const;
	void UpdateMineFog(float delta);
	void DrawMineFog(Graphics* g) const;
	struct EchoWave {
		int row = 0, column = 0;
		float elapsed = 0.0f;
		bool hitIceWall = false; // 全场唯一冰墙在单轮声波中仅承伤一次
		std::vector<int> distances; // 发射时冻结的最短路，-1 表示本轮不可达
		std::vector<int> hitIDs; // 稳定实体 ID，同一波最多结算一次
	};
	std::vector<EchoWave> mEchoWaves;
	/** 发射时锁定可通行图；在途不再读取会变化的岩壁布局。 */
	EchoWave BuildEchoWave(int row, int column) const;
	bool HasEchoTarget(int row, int column);
	void EmitEchoWave(int row, int column);
	void UpdateEchoWaves(float delta);
	void DrawEchoWaves(Graphics* g) const;
	std::array<int, MineGrid::Count> mMineWallOwners{}; // 预留索引由有效个体派生，读档时由个体重建
	/** 取得仍有效的施工预留者；失效 ID 不阻止重新选墙。 */
	Zombie* GetMineWallOwner(int cell) const;
	/** 为开凿者查找并原子预留墙体；不会预留玩家已经施工的墙。 */
	bool ReserveMineExcavation(Zombie* zombie, int start, int& wall, int& stand);
	/** 统一永久开墙；僵尸抢先完成时退还同格玩家施工费。 */
	bool CompleteMineExcavation(int wall, bool byZombie);
	int mMinePlannedWave = -1;
	std::vector<std::pair<ZombieType, int>> mMineWavePlan;
	/** 提前锁定下一波的正常点数选池结果，入口预报只投影该计划，不增兵。 */
	void PrepareMineWave();
	/** 在原抽取预算内组织多方向队伍；只在新矿道预报提交时执行，读档不重抽。 */
	void ArrangeMineWave();
	/** 返回已锁定下一波计划实际使用的入口行位掩码，不表示兵力数量。 */
	int GetMineForecastEntranceMask() const;
	/** 冻结预报中占至少四成点数的最重入口；均衡波次返回0，不透露精确阵容/数量。 */
	int GetMineForecastMainEntranceMask() const;
	int mMineDigCell = -1;
	float mMineDigRemaining = 0.0f;
	int mMineLastDugCell = -1;
	float mMineRubbleTimer = 0.0f;
	bool mMineToolActive = false;
	bool mMineRoutesVisible = false;
	int mMinePreviewCell = -1;
	bool mMineTutorialSeen = false;
	/** 按真实格边界判断直线遮挡，角点相切也算命中岩体。 */
	bool MineBlocksSegment(const Vector& from, const Vector& to) const;
	bool CanPlantOnMineCell(int row, int col) const;
	/** 正式扣费/取消/完工事务；不允许预览或暂停输入绕过状态闸门。 */
	bool BeginMineExcavation(int row, int col);
	bool CancelMineExcavation();
	/** 按游戏时间推进单处施工，完成时原子提交地形并刷新路径。 */
	void UpdateMine(float deltaTime);
	/** 仅正式战斗接受镐子/矿道图输入；共用全局手持物互斥管理器。 */
	void UpdateMineInput();
	/** 绘制真实格界、入口预报、种植射界与开凿前后的最短路径。 */
	void DrawMineGround(Graphics* g);
	/** 用正式选敌格范围与视线规则绘制棱光花落点预览，适用于所有地图。 */
	void DrawPrismRangePreview(Graphics* g);
	/** 绘制岩壁、遮挡淡化与施工碎落；调用位置必须早于选卡和其他 UI。 */
	void DrawMineWalls(Graphics* g);
	/** 绘制开战后工具底座、手持镐子、路线按钮及施工说明。 */
	void DrawMineUI(Graphics* g);
	/** 将根运动映射到矿道；返程意图改变时立即反走当前边，节点和意图共同保存以稳定续行。 */
	void AdvanceMineZombie(Zombie* zombie, float distance);
	bool IsPolarNightInitialized() const { return mPolarNightInitialized; }
	PolarNightPhase GetPolarNightPhase() const { return mPolarNightPhase; }
	float GetPolarTemperatureC() const { return mPolarTemperatureC; }
	float GetPolarHumidityPercent() const { return mPolarHumidityPercent; }
	float GetPolarWindSpeedMps() const { return mPolarWindSpeedMps; }
	float GetPolarTargetTemperatureC() const { return mPolarTargetTemperatureC; }
	float GetPolarTargetHumidityPercent() const { return mPolarTargetHumidityPercent; }
	float GetPolarTargetWindSpeedMps() const { return mPolarTargetWindSpeedMps; }
	/** 白毛风音画强度；爬升 0～1、雪盲 1、淡出 1～0。 */
	float GetPolarWhiteoutVisualStrength() const;
	/** 强风雪带的独立淡入强度；可在玩法危险线前给出连续的视觉预兆。 */
	float GetPolarWindVisualStrength() const { return mPolarWindVisualStrength; }
	VerticalWindDirection GetPolarWindVisualDirection() const {
		return mPolarWindVisualStrength > 0.0f ? mPolarVerticalWindDirection
			: VerticalWindDirection::NONE;
	}
	bool IsPolarTemperatureDangerous() const { return mPolarTemperatureC <= -18.0f; }
	bool IsPolarHumidityDangerous() const { return mPolarHumidityPercent >= 85.0f; }
	bool IsPolarWindDangerous() const { return mPolarWindSpeedMps >= 18.0f; }
	bool IsPolarSnowBlindActive() const { return mPolarNightPhase == PolarNightPhase::SNOW_BLIND; }
	bool IsPolarWhiteoutCommitted() const {
		return mPolarNightPhase == PolarNightPhase::WHITEOUT_RAMP
			|| mPolarNightPhase == PolarNightPhase::SNOW_BLIND
			|| mPolarNightPhase == PolarNightPhase::FADE;
	}
	VerticalWindDirection GetPolarVerticalWindDirection() const {
		return IsPolarWindDangerous() ? mPolarVerticalWindDirection
			: VerticalWindDirection::NONE;
	}
	float GetPolarWhiteoutTimer() const { return mPolarWhiteoutTimer; }
	float GetPolarAllDangerTimer() const { return mPolarAllDangerTimer; }
	float GetPolarFluctuationRemaining() const {
		return std::max(0.0f,
			mPolarFluctuationDuration - mPolarFluctuationTimer);
	}
	int GetActiveSnowHoleCount() const;
	int GetLastSnowHoleBatchCreated() const { return mLastSnowHoleBatchCreated; }
	int GetPendingSnowHoleSpawnCount() const {
		return static_cast<int>(mPendingSnowHoleSpawns.size());
	}
	bool IsPolarFinalWaveUpgradeApplied() const { return mPolarFinalWaveUpgradeApplied; }
	bool HasSnowHoleInRow(int row) const;
	bool HasSnowHoleAt(int row, int col) const;
	int GetSnowHoleColumn(int row) const;
	SnowHolePhase GetSnowHolePhase(int row) const {
		return row >= 0 && row < static_cast<int>(mSnowHoles.size())
			? mSnowHoles[row].phase : SnowHolePhase::NONE;
	}
	float GetSnowHoleTimer(int row) const {
		return row >= 0 && row < static_cast<int>(mSnowHoles.size())
			? mSnowHoles[row].timer : 0.0f;
	}
	/** 专用封穴语义与小推车共用；普通伤害和铲子不得调用。 */
	bool SealSnowHole(int row);
	/** 发射时锁定垂直风切变；返回 false 表示越界落空。 */
	bool ApplyPolarLobbedWind(int sourceRow, int& landingRow, Vector& target,
		bool guided = false) const;
	/** 提交固定终点与召唤类型，裂隙随后独立展开；至少提交一个事务才返回 true。 */
	bool CommitAuroraPriestRitual(int ownerZombieID, int sourceRow, bool whiteout);
	/** 极夜钟匠提交本行及相邻行的六秒可回溯快照。 */
	void CommitPolarClockAnchor(int ownerZombieID, int sourceRow);
	/** 推进裂隙、时间锚与曙光导航；游戏暂停时由 deltaTime=0 自然冻结。 */
	void UpdatePolarFinaleRituals(float deltaTime);
	/** 选择唯一界碑提供者并原子消费碎片；true 表示终点被拒绝。 */
	bool TryRejectDiscontinuousZombieEntry(int row, int column);
	/** 把指定时间锚目标标为不可逆，魅惑、小推车与成功吞食统一调用。 */
	void MarkTemporalTargetIrreversible(int zombieID);
	/** 磁力抽取后禁止时间锚恢复相应装备生命层。 */
	void MarkTemporalTargetEquipmentExtracted(int zombieID, bool shieldLayer);
	/** 始终逐行重击最高威胁目标并溅射；极夜红色湿度/风速分别附加清穴/导航。 */
	bool ActivateDawnLotus(int sourcePlantID, int dangerMask);
	/** 无选卡点击格子的交互入口；只有该格满能曙光莲会消费点击。 */
	bool ActivateDawnLotusAt(int row, int column);
	float GetDawnNavigationTimer() const { return mDawnNavigationTimer; }
	int GetPendingAuroraRiftCount() const {
		return static_cast<int>(mPendingAuroraRifts.size());
	}
	int GetTemporalAnchorCount() const {
		return static_cast<int>(mTemporalAnchors.size());
	}
	int GetTemporalAnchorTargetCount() const {
		std::size_t count = 0;
		for (const TemporalAnchor& anchor : mTemporalAnchors) count += anchor.targets.size();
		return static_cast<int>(count);
	}
	int GetAuroraPriestsSpawnedThisWave() const { return mAuroraPriestsSpawnedThisWave; }
	int GetClockmakersSpawnedThisWave() const { return mClockmakersSpawnedThisWave; }
	bool HasConsumedAuroraPriestGuarantee() const { return mAuroraPriestGuaranteeConsumed; }
	bool HasConsumedClockmakerGuarantee() const { return mClockmakerGuaranteeConsumed; }
	/** 为一次即将提交的抛射动作取得现有领域或按稳定规则消费最近就绪北极星花。 */
	bool PreparePolarLobbedNavigation(const Plant* plant);
	/** AutoTest 固定三仪表和风向；生产逻辑只走已锁环境计划。 */
	bool SetPolarNightEnvironmentForTesting(float temperatureC, float humidityPercent,
		float windSpeedMps, VerticalWindDirection direction,
		PolarNightPhase phase = PolarNightPhase::DANGER_HOLD,
		float phaseRemaining = -1.0f);
	/** AutoTest 固定单行雪穴状态；生产选点仍只走高湿提交。 */
	bool SetSnowHoleForTesting(int row, int column, SnowHolePhase phase,
		float timer = 0.0f);
	bool IsWinterTemperatureInitialized() const { return mWinterTemperatureInitialized; }
	bool IsOpeningColdWavePlanInitialized() const { return mOpeningColdWavePlanInitialized; }
	ColdWavePhase GetColdWavePhase() const { return mColdWavePhase; }
	ColdWaveStrength GetColdWaveStrength() const { return mColdWaveStrength; }
	float GetColdWaveTimer() const { return mColdWaveTimer; }
	float GetColdWaveTargetTemperatureC() const { return mColdWaveTargetTemperatureC; }
	float GetColdWaveCoolingDuration() const { return mColdWaveCoolingDuration; }
	float GetColdWaveHoldDuration() const { return mColdWaveHoldDuration; }
	float GetColdWaveThawDuration() const { return mColdWaveThawDuration; }
	int GetWinterFrostVariant() const { return mWinterFrostVariant; }
	float GetAmbientTemperatureC() const { return mAmbientTemperatureC; }
	/** 把当前温度映射到温度计液柱比例；0 为最低温，1 为最高温。 */
	float GetWinterTemperatureGaugeRatio() const;
	float GetWinterMinimumTemperatureC() const;
	float GetWinterMaximumTemperatureC() const;
	float GetWinterFreezingTemperatureC() const;
	/** 寒潮进入降温前的固定准确预报；它不参与雨势预报的误报与失败判定。 */
	bool HasColdWaveForecast() const;
	bool IsColdWaveForecastDisrupted() const { return mColdWaveForecastDisrupted; }
	/** 当前寒潮阶段仍处于预报窗口但栏目已经被干扰，供 UI 显示统一故障文案。 */
	bool IsColdWaveForecastDisruptionVisible() const;
	/** 使当前已公开寒潮预报失效，并通知依赖预报的植物清除准备状态。 */
	bool DisruptColdWaveForecast();
	bool IsColdWaveActive() const;
	int GetFrozenColumnCount() const;
	/** 返回当前准确寒潮预报在最低温时会冻结的列数；无有效预报时返回 0。 */
	int GetForecastFrozenColumnCount() const;
	/** 返回从僵尸侧连续推进的视觉霜冻列数；玩法冻结资格仍使用整数列。 */
	float GetWinterFrostVisualColumnCount() const;
	int GetFirstFrozenColumn() const;
	bool IsCellFrozen(int row, int col) const;
	/** 任一占地格位于冻土即返回 true；多格植物不得只检查锚点。 */
	bool IsPlantFootprintFrozen(PlantType type, int row, int anchorColumn) const;
	/** 用共享短视推演选择处决目标；推演不可用时退回战略价值和稳定 ID。 */
	Plant* SelectIceStatueExecutionTarget(
		int sourceZombieID, float strikeInterval, int strikeDamage,
		MonteCarloTargetStats* stats = nullptr);
	/** 按稳定植物 ID 请求 3x3 内提供者阻止目标建立一次冰像封存。 */
	bool TryPreventIceExecutionSeal(Plant& target) const;
	/** 当前准确预报的最低温是否会覆盖该格；预报干扰后立即返回 false。 */
	bool IsCellInColdWaveForecast(int row, int col) const;
	/** 当前低温会把同一雨势的视觉改为雪；雨势强度和玩法倍率保持原值。 */
	bool IsWinterPrecipitationSnow() const;
	/** AutoTest 固定一轮完整寒潮计划；生产逻辑只走 RollNextColdWave 与 UpdateWinterTemperature。 */
	bool SetWinterTemperatureForTesting(float temperatureC, ColdWavePhase phase,
		float remaining = 30.0f,
		ColdWaveStrength strength = ColdWaveStrength::STRONG,
		float targetTemperatureC = -12.0f,
		float coolingDuration = 20.0f,
		float holdDuration = 57.5f,
		float thawDuration = 32.0f,
		int frostVariant = 0);
	bool IsWeatherInitialized() const { return mWeatherInitialized; }
	bool CanRainIntensify() const { return mRainCanIntensify; }
	bool CanRainHold() const { return mRainCanHold; }
	bool HasWeatherForecast() const {
		return mWeatherForecastReady && !mWeatherForecastDisrupted;
	}
	bool IsWeatherForecastDisrupted() const {
		return mWeatherForecastReady && mWeatherForecastDisrupted;
	}
	RainIntensity GetForecastRainIntensity() const { return mForecastRainIntensity; }
	RainIntensity GetActualForecastRainIntensity() const { return mActualForecastRainIntensity; }
	FogWeatherIntensity GetFogWeatherIntensity() const { return mFogWeatherIntensity; }
	FogWeatherIntensity GetForecastFogWeatherIntensity() const {
		return mForecastFogWeatherIntensity;
	}
	FogWeatherIntensity GetActualForecastFogWeatherIntensity() const {
		return mActualForecastFogWeatherIntensity;
	}
	float GetFogWeatherTimer() const { return mFogWeatherTimer; }
	float GetFogDispersal() const { return mFogDispersal; }
	float GetFogVisualOffsetX() const { return mFogVisualOffsetX; }
	float GetFogAnimationTime() const { return mFogAnimationTime; }
	bool IsFogWeatherInitialized() const { return mFogWeatherInitialized; }
	bool HasFogWeatherForecast() const {
		return mFogWeatherForecastReady && !mFogWeatherForecastDisrupted;
	}
	bool IsFogWeatherForecastDisrupted() const {
		return mFogWeatherForecastReady && mFogWeatherForecastDisrupted;
	}
	/** 是否至少存在一项仍公开、可由气象干扰窗口截获的独立预报。 */
	bool HasDisruptibleWeatherForecast() const;
	/** 原子隐藏当前全部公开预报；返回 bit0 雨雪、bit1 寒潮、bit2 雾势。 */
	int DisruptWeatherForecastPanel();
	/** 当前地图是否拥有可被气象干扰设备遮蔽的天气栏目。 */
	bool SupportsWeatherPanelInterference() const;
	/** 当前地图支持整栏黑障，且累计剩余时长尚未达到安全上限时返回 true。 */
	bool CanBeginWeatherPanelInterference() const;
	/** 将 duration 追加到整栏主动黑障并截获当前公开预报；成功结果额外包含 bit3。 */
	int BeginWeatherPanelInterference(float duration);
	/** 多次提交后允许同时保留的最大黑障剩余游戏秒数。 */
	float GetMaximumWeatherPanelInterferenceDuration() const;
	/** 主动黑障是否仍在生效。 */
	bool IsWeatherPanelInterferenceActive() const {
		return mWeatherPanelInterferenceTimer > 0.0f;
	}
	float GetWeatherPanelInterferenceTimer() const {
		return mWeatherPanelInterferenceTimer;
	}
	bool IsDenseFogWeather() const {
		return mFogWeatherIntensity == FogWeatherIntensity::DENSE;
	}
	/** 当前雾势的渲染层数：默认 1 层，小雾/普通迷雾 2 层，大雾 3 层。 */
	int GetFogLayerCount() const;
	/** 当前基础迷雾最左列；无迷雾关卡返回棋盘列数。 */
	int GetBaseFogLeftColumn() const;
	/** 合并当前雾势扩展后的目标最左列，不受短时台风透明度影响。 */
	int GetEffectiveFogLeftColumn() const;
	/** 当前雾场用于绘制的行数；战场行外再保留一条底部收边。 */
	int GetFogDrawRowCount() const { return SupportsStageFog() ? mRows + 1 : 0; }
	/** 返回指定雾格平滑后的 alpha（0～255）。 */
	float GetFogCellAlpha(int row, int col) const;
	/** 返回目标当前位置是否仍处在索敌阈值以上的浓雾中。 */
	bool IsZombieObscuredByFog(const Zombie* zombie) const;
	/** 4-2 起启用路灯花燃料、照明、产光加速与雾中远程索敌限制。 */
	bool SupportsPlanternMechanics() const;
	/** 返回当前未压扁的唯一路灯花；ID 失效时返回空。 */
	Plantern* GetActivePlantern() const;
	/** 返回指定格受到的路灯花照明比例（0～1）；所有雾玩法消费同一形状。 */
	float GetPlanternIllumination(int row, int col) const;
	/** 远程植物的统一雾中索敌许可；可见边界外一格薄雾与近身目标仍可感知。 */
	bool CanPlantAcquireZombie(const Plant* plant, const Zombie* zombie);
	/** 周围产光植物的局部效率倍率；三档峰值依次为 1.10/1.20/1.35。 */
	float GetPlanternSunProductionMultiplier(const Plant* producer) const;
	float GetPlanternFuel() const;
	float GetPlanternFuelRatio() const;
	int GetPlanternGearValue() const;
	float GetPlanternFuelFullHintTimer() const;
	float GetMistFuelDropAccumulator() const { return mMistFuelDropAccumulator; }
	int GetMistFuelAssignedThisWave() const { return mMistFuelAssignedThisWave; }
	/** 返回首波 0 到最终波 1 的雾火紧缩进度；不复用天气导演压力。 */
	float GetMistFuelScarcityFactor() const;
	/** 返回当前波每只携带者的整数雾火价值。 */
	int GetMistFuelRewardAmount() const;
	/** 返回当前波最多预分配的雾火总量。 */
	int GetMistFuelWaveBudget() const;
	/** 返回当前波普通耐久僵尸加入跨波保底累计器的基础份额。 */
	float GetMistFuelBaseCarrierChance() const;
	/** 返回当前波高耐久僵尸按耐久比例最多追加的保底份额。 */
	float GetMistFuelHeavyCarrierBonus() const;
	void SetPlanternGear(PlanternGear gear);
	void NotifyPlanternRemoved(int plantID);
	void TogglePlanternGearMenu();
	/** 僵尸死亡的唯一雾火结算入口；无路灯花或满仓时直接丢弃。 */
	void CollectMistFuelFromZombie(Zombie* zombie);
	/** 生存亡者接力：把一次免伤交给同排更靠近房屋的敌对前锋。 */
	void RelayZombieDeathWard(const Zombie* source);
	bool SetPlanternFuelForTesting(float fuel);
	bool AwardPlanternFuelForTesting(float amount);
	/** 返回稳定的 0～7 贴图变体；不消费玩法随机数。 */
	int GetFogTileVariant(int row, int col) const;
	/** 返回当前场景换算后的雾格左上角；包含风向漂移但不硬编码原版 800 宽坐标。 */
	Vector GetFogTilePosition(int row, int col) const;
	int GetVisibleFogCellCount() const;
	int GetMaximumFogAlpha() const;
	/** 当前天气导演下进入大雾的概率（百分比）；只用于夜间泳池独立雾势抽取。 */
	int GetDenseFogChancePercent() const;
	bool HasPendingHeavyTyphoon() const { return mPendingHeavyTyphoonPrepared; }
	bool IsPendingHeavyTyphoonOpeningProtected() const {
		return mPendingHeavyTyphoonOpeningProtected;
	}
	TyphoonStrength GetPendingHeavyTyphoonStrength() const { return mPendingHeavyTyphoonStrength; }
	int GetPendingHeavyRainPromptVariant() const { return mPendingHeavyRainPromptVariant; }
	bool HasShownHeavyRainPrompt() const { return mHeavyRainPromptShown; }
	/** 当前天气预报准确率（百分比）；随导演强度成长但最高不超过 90%。 */
	int GetCurrentWeatherForecastAccuracyPercent() const;
	/** 当前连续非大雨新天气次数；只在后期天气导演启用时累计。 */
	int GetWeakWeatherPhasesSinceHeavy() const { return mWeakWeatherPhasesSinceHeavy; }
	/** 后期弱天气保底是否已经要求下一轮新天气为大雨。 */
	bool IsHeavyWeatherForced() const { return ShouldForceHeavyWeather(); }
	/** 当前天气导演下新天气的原始相对权重，不含弱天气保底覆盖。 */
	int GetCurrentNewWeatherWeight(RainIntensity intensity) const;
	bool HasTyphoon() const { return mTyphoonStrength != TyphoonStrength::NONE; }
	/** 冬日花园明确禁用台风；其大雪不复用风向、阵风或位移机制。 */
	bool SupportsTyphoon() const;
	/** 玩家开关开启时，首局或生存首轮第 1～5 波禁止新大雨附加台风。 */
	bool IsOpeningTyphoonProtectionActive() const;
	int GetCurrentTyphoonChancePercent() const;
	TyphoonStrength GetTyphoonStrength() const { return mTyphoonStrength; }
	WindDirection GetWindDirection() const { return mWindDirection; }
	/**
	 * 三叶草改写当前台风方向；活动阵风同步转向，但强度、预算和迷雾驱散量保持不变。
	 * @return 当前确有台风且方向合法时返回 true。
	 */
	bool RedirectTyphoonFromBlover(WindDirection direction);
	float GetTyphoonStrengthTimer() const { return mTyphoonStrengthTimer; }
	float GetWindDirectionTimer() const { return mWindDirectionTimer; }
	float GetWindGustTimer() const { return mWindGustTimer; }
	int GetTyphoonGustsRemaining() const { return mTyphoonGustsRemaining; }
	bool IsTyphoonGustActive() const { return mTyphoonGustActive; }
	TyphoonStrength GetActiveGustStrength() const { return mActiveGustStrength; }
	WindDirection GetActiveGustDirection() const { return mActiveGustDirection; }
	float GetActiveGustDuration() const { return mActiveGustDuration; }
	float GetActiveGustTimer() const { return mActiveGustTimer; }
	float GetActiveGustPlantMoveTimer() const { return mActiveGustPlantMoveTimer; }
	bool HasActiveGustMovedPlants() const { return mActiveGustPlantMoved; }
	/** 返回当前阵风施加给全部存活僵尸的有符号水平漂移速度，正值吹向前线（像素/游戏秒）。 */
	float GetZombieGustDriftVelocity() const;
	/** 返回僵尸被阵风吹向前线时的最大世界横坐标，避免越过既有出生侧清理线。 */
	float GetZombieGustFrontLimit() const;
	int GetHeavyPhasesWithoutTyphoon() const { return mHeavyPhasesWithoutTyphoon; }
	int GetEliteDancersSpawnedThisWave() const { return mEliteDancersSpawnedThisWave; }
	int GetReinforcedDoorsSpawnedThisWave() const { return mReinforcedDoorsSpawnedThisWave; }
	int GetElitePolevaultersSpawnedThisWave() const { return mElitePolevaultersSpawnedThisWave; }
	int GetGildedZambonisSpawnedThisWave() const { return mGildedZambonisSpawnedThisWave; }
	int GetEliteDolphinRidersSpawnedThisWave() const { return mEliteDolphinRidersSpawnedThisWave; }
	int GetEliteJackInTheBoxesSpawnedThisWave() const {
		return mEliteJackInTheBoxesSpawnedThisWave;
	}
	int GetEliteDiggersSpawnedThisWave() const { return mEliteDiggersSpawnedThisWave; }
	int GetElitePogosSpawnedThisWave() const { return mElitePogosSpawnedThisWave; }
	int GetEliteLaddersSpawnedThisWave() const { return mEliteLaddersSpawnedThisWave; }
	int GetEliteCatapultsSpawnedThisWave() const { return mEliteCatapultsSpawnedThisWave; }
	int GetRedeyeGargantuarsSpawnedThisWave() const {
		return mRedeyeGargantuarsSpawnedThisWave;
	}
	int GetInsulatorsSpawnedThisWave() const { return mInsulatorsSpawnedThisWave; }
	int GetHijackersSpawnedThisWave() const { return mHijackersSpawnedThisWave; }
	int GetHijackerSpawnCooldownWavesRemaining() const {
		return mHijackerSpawnCooldownWavesRemaining;
	}
	bool IsHijackerSpawnBlockedThisWave() const { return mHijackerSpawnBlockedThisWave; }
	bool IsHijackerSpawnBlocked() const {
		return mHijackerSpawnBlockedThisWave
			|| mHijackerSpawnCooldownWavesRemaining > 0;
	}
	int GetGroundingZombiesSpawnedThisWave() const {
		return mGroundingZombiesSpawnedThisWave;
	}
	int GetBobsledTeamsSpawnedThisWave() const { return mBobsledTeamsSpawnedThisWave; }
	int GetIceWallEngineersSpawnedThisWave() const {
		return mIceWallEngineersSpawnedThisWave;
	}
	int GetIceCrackDrillsSpawnedThisWave() const {
		return mIceCrackDrillsSpawnedThisWave;
	}
	int GetWeatherJammersSpawnedThisWave() const {
		return mWeatherJammersSpawnedThisWave;
	}
	int GetIceStatueExecutionersSpawnedThisWave() const {
		return mIceStatueExecutionersSpawnedThisWave;
	}
	int GetSnowBurrowsSpawnedThisWave() const { return mSnowBurrowsSpawnedThisWave; }
	bool HasConsumedSnowBurrowTutorialHoleSpawn() const {
		return mSnowBurrowTutorialHoleSpawnConsumed;
	}
	int GetAdaptiveHelmetsSpawnedThisWave() const {
		return mAdaptiveHelmetsSpawnedThisWave;
	}
	bool HasSpawnedAdaptiveHelmetTutorialWave() const {
		return mAdaptiveHelmetTutorialWaveSpawned;
	}
	int GetThermalSnipersSpawnedThisWave() const { return mThermalSnipersSpawnedThisWave; }
	bool HasSpawnedThermalSniperTutorial() const { return mThermalSniperTutorialSpawned; }
	/** 恢复热感狙击僵尸的当前波上限计数与本关保底提交状态。 */
	void RestoreThermalSniperSpawnState(int waveCount, bool tutorialSpawned);
	int GetLastTyphoonMovedPlants() const { return mLastTyphoonMovedPlants; }
	int GetLastTyphoonLostPlants() const { return mLastTyphoonLostPlants; }
	int GetLastTyphoonBlockedPlantSteps() const { return mLastTyphoonBlockedPlantSteps; }
	bool IsTyphoonGustWarning() const;
	/** 当前公开预报是否属于此天气阶段真实允许出现的下一档。 */
	bool IsWeatherForecastPlausible() const;
	/** 当前雨势对应的粒子发射器是否仍在工作。 */
	bool IsRainEffectEmitting() const;
	/** 当前实际发射的雨丝配置名，供 AutoTest 精确断言风向切换。 */
	const std::string& GetRainVisualEffectName() const { return mRainVisualEffectName; }

	// AutoTest 专用：固定雨势并重启对应粒子，真实游戏只走随机天气状态机。
	void SetRainForTesting(RainIntensity intensity, float duration = 30.0f, bool canIntensify = false);
	// AutoTest 专用：固定小雾/普通迷雾/大雾并清除预报。
	bool SetFogWeatherForTesting(FogWeatherIntensity intensity, float duration = 30.0f);
	// AutoTest 专用：固定公开与真实雾势预报，并在指定游戏秒后揭晓。
	bool SetFogWeatherForecastForTesting(FogWeatherIntensity forecast,
		FogWeatherIntensity actual, float revealIn = 1.0f);
	// AutoTest 专用：固定台风驱散进度，供存档往返与边界断言使用。
	bool SetFogDispersalForTesting(float dispersal);
	// AutoTest 专用：定位黑夜无尽轮次，并刷新所有由轮次派生的天气速度状态。
	bool SetSurvivalRoundForTesting(int round);
	// AutoTest 专用：固定公开预报与真实天气，并把当前阶段倒计时改为指定揭晓时间。
	bool SetWeatherForecastForTesting(RainIntensity forecast, RainIntensity actual, float revealIn = 1.0f);
	// AutoTest 专用：覆盖已锁定大雨的待生效台风等级，验证四档预警文案与切档消费。
	bool SetPendingHeavyTyphoonForTesting(TyphoonStrength strength, int promptVariant = -1);
	// AutoTest 专用：在晴天用固定权重落点走正式新天气抽取，并发布必定准确的锁定预报。
	bool PrepareWeatherForecastForTesting(int weatherRoll, float revealIn = 0.1f);
	// AutoTest 专用：用固定权重落点结束当前雨段，覆盖增强、衰减和放晴分支。
	bool AdvanceRainPhaseForTesting(int transitionRoll);
	// AutoTest 专用：仅大雨允许触发，返回是否真正闪电。
	bool TriggerLightningForTesting();
	// AutoTest 专用：固定大雨附加台风、风向和计时，真实游戏只走阶段开始时的一次随机判定。
	bool SetTyphoonForTesting(TyphoonStrength strength, WindDirection direction,
		float gustIn = 30.0f, float directionIn = 30.0f, int gustsRemaining = 1,
		float decayIn = 30.0f);
	// AutoTest 专用：用固定二选一点数走正式风向重抽，覆盖保持同向与切换方向。
	bool RerollWindDirectionForTesting(int directionRoll);
	// AutoTest 专用：用固定概率点数和强度点数走正式台风判定，覆盖连续落空保底。
	bool RollTyphoonForTesting(int chanceRoll, int strengthRoll, WindDirection direction);
	// AutoTest 专用：启动一次当前强度的阵风，不消费自动预算；可固定植物结算时刻。
	bool TriggerTyphoonGustForTesting(float plantMoveIn = 0.0f);
	/** AutoTest 专用：固定径流积累、活动阶段和冲刷后的残留湿度。 */
	bool SetRoofRunoffForTesting(float charge, RoofRunoffPhase phase,
		int rowMask = 0, float phaseTimer = 0.0f, float retainedCharge = 45.0f);
	/** AutoTest 专用：固定黑夜屋顶雷荷、余电、活动阶段、锁定行和剩余时间。 */
	bool SetNightRoofChargeForTesting(float charge, NightRoofChargePhase phase,
		int row = -1, float phaseTimer = 0.0f, float overcharge = 0.0f);
	/** 立即生成一次地面雨滴水花，供不同地形的落点闭环测试。 */
	void TriggerRainGroundSplashForTesting();
	/** 正式波次与 AutoTest 共用的天气变异入口；mutationRoll=0 时随机，超额成功变异返回 NUM_ZOMBIE_TYPES。 */
	ZombieType ResolveRainMutationType(ZombieType selected, int mutationRoll = 0);
	/** 正式波次总解析入口；超过类型上限返回 NUM_ZOMBIE_TYPES，调用方必须跳过候选。 */
	ZombieType ResolveWaveZombieType(ZombieType selected, int mutationRoll = 0);
	/** 从存档恢复潜雪僵尸本波计数与 8-1 雪穴保底提交状态。 */
	void RestoreSnowBurrowSpawnState(int count, bool tutorialHoleSpawnConsumed);
	/** 从存档恢复适应头盔的波次预算与白毛风教学提交状态。 */
	void RestoreAdaptiveHelmetSpawnState(int waveCount, bool tutorialWaveSpawned);
	/** 创建已解析的正式波次类型；成功后记录依赖实际出生的永久遭遇。 */
	Zombie* CreateResolvedWaveZombie(ZombieType actualType, int row, float x);

	// 初始化格子 默认5行9列
	void InitializeCell(int rows = 4, int cols = 8);
	/** 当前地图是否使用泳池地形与六行网格。 */
	bool IsPoolBackground() const;
	/** 当前地图是否使用屋顶五行网格与连续斜坡。 */
	bool IsRoofBackground() const;
	/** 返回屋顶斜坡与平台交界的世界 X；非屋顶也返回同一网格派生位置。 */
	float GetRoofSlopeEndX() const;
	/** 返回指定行在任意世界 X 上的地面中心 Y，供所有僵尸品种复用地形。 */
	float GetRowCenterYAtX(int row, float worldX) const;
	/** 返回清扫车前缘探针在指定行的逻辑 Y；屋顶包含 RoofCleaner 资源原点校准。 */
	float GetMowerTerrainY(int row, float worldX) const;
	/** 正式冒险从第二大关启用旧天气；极夜无尽改用专属环境。 */
	bool SupportsWeather() const;
	/** 昼夜屋顶场景共用坡面径流，不依赖关卡编号。 */
	bool SupportsRoofRunoff() const;
	float GetRoofRunoffCharge() const { return mRoofRunoffCharge; }
	float GetRoofRunoffChargeRatio() const { return mRoofRunoffCharge / 100.0f; }
	float GetRoofRunoffRetainedCharge() const { return mRoofRunoffRetainedCharge; }
	RoofRunoffPhase GetRoofRunoffPhase() const { return mRoofRunoffPhase; }
	float GetRoofRunoffPhaseTimer() const { return mRoofRunoffPhaseTimer; }
	/** 正式冲刷从 0 到 1 的归一化进度；非冲刷阶段返回 0。 */
	float GetRoofRunoffFlowProgress() const;
	/** 返回本次锁定行集合的 bitmask；待机阶段为 0。 */
	int GetRoofRunoffRowMask() const { return mRoofRunoffRowMask; }
	/** 判断指定逻辑行是否属于当前锁定行组。 */
	bool IsRoofRunoffRowSelected(int row) const {
		return row >= 0 && row < mRows && (mRoofRunoffRowMask & (1 << row)) != 0;
	}
	/** 返回当前锁定的不重复行数。 */
	int GetRoofRunoffRowCount() const;
	/** 返回当前最靠房屋且能参与下一次自然锁行的导流僵尸行；没有候选时返回 -1。 */
	int GetRoofRunoffGuideCandidateRow() const;
	bool IsRoofRunoffWarning() const {
		return mRoofRunoffPhase == RoofRunoffPhase::WARNING;
	}
	bool IsRoofRunoffFlowing() const {
		return mRoofRunoffPhase == RoofRunoffPhase::FLOWING;
	}
	/** 返回目标行坡段地面僵尸承受的有符号径流速度；负值表示顺坡冲向屋檐/房屋。 */
	float GetRoofRunoffZombieDriftVelocity(int row, float worldX) const;
	/** 黑夜屋顶场景启用独立雷荷，不依赖关卡编号。 */
	bool SupportsNightRoofCharge() const;
	float GetNightRoofCharge() const { return mNightRoofCharge; }
	float GetNightRoofChargeRatio() const { return mNightRoofCharge / 100.0f; }
	float GetNightRoofOvercharge() const { return mNightRoofOvercharge; }
	float GetNightRoofOverchargeRatio() const { return mNightRoofOvercharge / 100.0f; }
	NightRoofChargePhase GetNightRoofChargePhase() const { return mNightRoofChargePhase; }
	float GetNightRoofChargePhaseTimer() const { return mNightRoofChargePhaseTimer; }
	/** 返回当前由场上有效劫持者提供的雨中雷荷固定增量，晴天恒为零。 */
	float GetNightRoofHijackerRainChargeBonusPerSecond() const;
	int GetNightRoofChargeRow() const { return mNightRoofChargeRow; }
	bool IsNightRoofChargeGuided() const { return mNightRoofChargeGuided; }
	int GetNightRoofChargeGuideID() const { return mNightRoofChargeGuideID; }
	bool DidNightRoofChargeRouteUseMonteCarlo() const {
		return mNightRoofChargeRouteUsedMonteCarlo;
	}
	const MonteCarloTargetStats& GetNightRoofChargeRouteStats() const {
		return mNightRoofChargeRouteStats;
	}
	int GetNightRoofChargeRouteDecisionMicros() const {
		return mNightRoofChargeRouteDecisionMicros;
	}
	/** 仅视觉读取已锁定引雷附件位置；实体失效时返回 false，绝不重选。 */
	bool TryGetNightRoofChargeGuideAnchor(Vector& anchor) const;
	bool IsNightRoofChargeWarning() const {
		return mNightRoofChargePhase == NightRoofChargePhase::WARNING;
	}
	bool IsNightRoofChargeDischarging() const {
		return mNightRoofChargePhase == NightRoofChargePhase::DISCHARGING;
	}
	/** 当前被锁定且仍有效的劫持者 ID；没有候选或已取消时为 NULL_ZOMBIE_ID。 */
	int GetNightRoofHijackerID() const { return mNightRoofHijackerID; }
	bool HasNightRoofHijackerSelectionAttempted() const {
		return mNightRoofHijackerSelectionAttempted;
	}
	bool IsNightRoofHijackerWarningExtended() const {
		return mNightRoofHijackerWarningExtended;
	}
	bool IsNightRoofHijackerFinalizing() const { return mNightRoofHijackerFinalizing; }
	/** 返回 UI 与目标描边使用的实时处决线；生存模式封顶 1200。 */
	int GetNightRoofExecutionLine() const;
	/** 返回僵尸是否属于当前处决快照阈值；只读取锁定 ID 与当前生命，不遍历实体。 */
	bool IsZombieThreatenedByNightRoofHijacker(const Zombie* zombie) const;
	/** 返回植物是否属于当前逻辑格处决组；普通层和南瓜合并计血，承载层免疫。 */
	bool IsPlantThreatenedByNightRoofHijacker(const Plant* plant) const;
	/** 返回紫色脉冲透明度；最后一秒自动提高亮度。 */
	float GetNightRoofHijackerPulseAlpha() const;
	/** 死亡或掉头从实体侧取消锁定；不重选，也不改写普通放电倒计时。 */
	void CancelNightRoofHijacker(int zombieID);
	/** 实体读档全部完成后校验交叉引用并恢复声音、预警和最终动画。 */
	void FinalizeNightRoofHijackerLoad();
	/** 全部植物和僵尸读档后双向校验冰像封存关系并清除孤儿状态。 */
	void FinalizeIceStatueExecutionerLoad();
	/** 返回指定僵尸是否站在任一活动植物声明的接地范围内。 */
	bool IsNightRoofChargeProtectionSuppressed(const Zombie* zombie) const;
	/** 返回本次基础放电的 0～1 进度；非放电阶段为 0。 */
	float GetNightRoofChargeDischargeProgress() const;
	/** 当前关卡是否拥有不依赖雨势的基础迷雾；额外固定关由 AdventureProgression 集中登记。 */
	bool SupportsStageFog() const;
	/** 当前基础迷雾关卡是否抽取独立增强雾势。 */
	bool SupportsFogWeather() const;
	bool IsPoolRow(int row) const;
	bool IsPoolSquare(int row, int col) const;
	bool IsPoolWorldPosition(int row, float x) const;
	/** 水路出怪扩展点：集中列出冰车、跳跳等禁水类型。 */
	bool CanZombieTypeSpawnInPool(ZombieType type) const;
	Vector GetCellCenterPosition(int row, int col) const;
	/** 四邻接连通距离；矿雾辅助效果绕弯但不穿岩壁，普通地图按曼哈顿距离。 */
	bool IsCellWithinConnectedRange(int sourceRow, int sourceColumn, int row, int column, int range) const;
	/** 合并活动植物提供的地面移动减速，只取最强来源；不改啃食和技能时间。 */
	float GetGroundSlowFactor(const Zombie& zombie) const;
	float GetCellHeight() const { return mCellHeight; }
	/** 冰车用车辆前缘把指定行冰道向房屋方向延伸，并刷新原版 30 秒寿命。 */
	void ExtendIceTrail(int row, float frontX);
	/** 鎏金冰车把指定陆地行黄色冰道向房屋方向延伸；水路拒绝铺设。 */
	void ExtendGoldenIceTrail(int row, float frontX);
	/** 火爆辣椒等效果把指定行普通与黄色冰道剩余时间压到给定上限，单位秒。 */
	void ShortenIceTrail(int row, float maxRemainingSeconds);
	/** 返回指定格当前是否被任一冰道覆盖；场外冰道不占格，覆盖格禁止种植。 */
	bool IsIceAt(int row, int col) const;
	/** 返回指定世界点是否处在活动黄色冰道上，供僵尸速度层边沿检测。 */
	bool IsGoldenIceAtWorld(int row, float worldX) const;
	float GetIceTrailMinX(int row) const;
	float GetIceTrailTimeRemaining(int row) const;
	float GetGoldenIceTrailMinX(int row) const;
	float GetGoldenIceTrailTimeRemaining(int row) const;
	/** 返回冰道主体的固定右边界；冰道从车辆处一直铺到逻辑屏幕最右端。 */
	float GetIceTrailRightX() const;
	/** 在背景和游戏对象之间绘制全部活动冰道；重叠区黄色冰道覆盖普通冰道。 */
	void DrawIceTrails(Graphics* g) const;
	/** 在冬日花园草坪实体下方绘制当前冻土覆盖，以及本轮锁定的不规则冻融边界。 */
	void DrawWinterFrost(Graphics* g) const;
	/** 在手绘雪格上绘制与逻辑 Cell 同源的雪穴形成和活动状态。 */
	void DrawPolarNightGround(Graphics* g) const;

	/** UI 与测试共用的正式种植判定，不含阳光与卡片冷却。 */
	bool CanPlantAt(PlantType type, int row, int col);
	/** 解析可搬组合的稳定锚点 ID；多格植物任一占格均归到同一锚点。 */
	int GetRelocationSourceID(int row, int col);
	/** 检查已有整组植物能否原样移入完整空占地，不应用新种配额或升级前置条件。 */
	bool CanRelocatePlantGroup(int sourceID, int row, int col);
	/** 校验后原子移动植物、承载和扶梯，并解除旧啃食关系；失败不修改状态。 */
	bool RelocatePlantGroup(int sourceID, int row, int col);
	/**
	 * 把点击格解析为植物左侧逻辑锚点；普通植物原样返回，双格升级可从任一基础植物点击。
	 */
	bool ResolvePlantPlacementAnchor(PlantType type, int row, int col,
		int& anchorRow, int& anchorColumn) const;
	/** 返回空手状态下指定格的加农炮是否已装填并可进入落点模式。 */
	bool CanBeginCobCannonTargeting(int row, int col) const;
	/** 点击任一占格选择一株已装填加农炮，并进入独占落点模式。 */
	bool BeginCobCannonTargeting(int row, int col);
	/**
	 * 让当前已选加农炮向点击世界点开火；屋顶只按该 X 的连续坡面推导逻辑行，
	 * 不会把权威像素落点吸附到格子或坡面。成功或目标失效都会退出落点模式。
	 */
	bool FireTargetedCobCannonAt(const Vector& target);
	/** 植物死亡或场景切换时按稳定 ID 取消仍指向它的落点模式。 */
	void CancelCobCannonTargeting(int plantID);
	bool IsCobCannonTargeting() const {
		return mCursorObjectManager.IsActive(CursorObjectType::COB_CANNON_TARGET)
			&& mTargetingCobCannonID != NULL_PLANT_ID;
	}
	int GetTargetingCobCannonID() const { return mTargetingCobCannonID; }
	/** 返回该植物是否仍有本关种植次数；无限制的植物恒为 true。 */
	bool HasPlantingQuota(PlantType type) const;
	/** 返回升级卡等额外在场种植前提是否满足；普通植物恒为 true。 */
	bool HasPlantingRequirement(PlantType type) const;
	int GetEliteScaredyShroomPlantLimit() const;
	int GetEliteScaredyShroomsPlanted() const { return mEliteScaredyShroomsPlanted; }
	/** 返回格子最上层战斗目标：南瓜层、普通层、承载层依次优先；铲子另按点击区域选层。 */
	Plant* GetTopPlantAt(int row, int col) const;
	/** 返回投篮车篮球在指定格应命中的层：飞行覆盖、普通、南瓜、承载依次优先。 */
	Plant* GetCatapultTargetPlantAt(int row, int col) const;
	/** 返回能保护指定格免受空中威胁的最早种下植物；重叠范围只触发一株。 */
	Plant* FindAirborneThreatProtector(int row, int col) const;
	/** 返回指定格中最上层能阻挡该类跳跃的植物；非阻拦外壳不会遮蔽内层高坚果。 */
	Plant* GetJumpBlockingPlantAt(int row, int col, ZombieJumpType jumpType) const;
	/** 返回指定格承载层；当前可能是睡莲或花盆。 */
	Plant* GetUnderPlantAt(int row, int col) const;
	/** 返回指定格普通植物层；不存在或 ID 已失效时返回空。 */
	Plant* GetNormalPlantAt(int row, int col) const;
	/** 返回指定格南瓜外壳层；不存在或 ID 已失效时返回空。 */
	Plant* GetPumpkinAt(int row, int col) const;
	/** 返回指定格短时飞行覆盖层；当前由咖啡豆占用，不参与顶层啃食或铲子选择。 */
	Plant* GetOverlayPlantAt(int row, int col) const;
	/**
	 * 按飞行覆盖、南瓜、普通、承载层顺序访问指定格全部活动植物。
	 *
	 * 访问前会快照实体 ID，并在每次回调前重新验证实体；回调可使植物死亡或释放格位，
	 * 但本次遍历不会纳入回调期间新进入该格的植物。
	 */
	void ForEachActivePlantInCell(int row, int col,
		const std::function<void(Plant&)>& action);
	/**
	 * 对指定格全部活动植物结算一次冬季地面冲击，并返回首个拦截响应。
	 *
	 * 先选定传播响应，再让同格各层共享调用时给定的伤害；响应倍率由调用方从后续格应用。
	 */
	WinterGroundImpactResponse ApplyWinterGroundImpactToCell(int row, int col,
		WinterGroundImpactKind kind, int damage, DamageSource source);
	/**
	 * 返回能替目标植物承受僵尸范围爆炸的南瓜头。
	 *
	 * 保护范围为逻辑九宫格；优先正交近邻，再按行、列和实体 ID 稳定打破并列。
	 * 南瓜头只为自身承伤，不会由相邻南瓜继续转移伤害。
	 */
	Plant* FindPumpkinAreaProtector(const Plant& plant) const;
	/**
	 * 对命中范围内的植物结算可被南瓜头拦截的僵尸范围伤害。
	 *
	 * 每个实际命中的植物层先在自身九宫格内选择一枚活动南瓜头；同一次爆炸对同一南瓜
	 * 只结算一次默认 5 倍基础伤害。没有保护者时仍由原命中植物层承受基础伤害。
	 */
	void ApplyPumpkinProtectedZombieAreaDamage(int baseDamage,
		const std::function<bool(const Plant&)>& overlapsArea);
	/** 使用调用方指定的正倍率结算南瓜拦截，供拥有独立平衡值的范围攻击复用同一归并规则。 */
	void ApplyPumpkinProtectedZombieAreaDamage(int baseDamage,
		int pumpkinDamageMultiplier,
		const std::function<bool(const Plant&)>& overlapsArea);
	/** 将基础僵尸按所选行解析为泳池表现变体；不改变波次成本。 */
	ZombieType ResolveTerrainZombieType(ZombieType selected, int row) const;
	bool CanSpawnZombieInRow(ZombieType type, int row) const {
		return IsSpawnRowCompatible(type, row);
	}

	// 获取格子。返回原始指针：Cell 所有权在 GameObjectManager，调用方仅做非所有 view
	Cell* GetCell(int row, int col) {
		if (row >= 0 && row < mRows && col >= 0 && col < mColumns) {
			return mCells[row][col];
		}
		return nullptr;
	}

	// 创建僵尸：x 为任意像素横坐标，y 始终由 row 决定（不可自定义）。
	// 需要自由摆放（任意 y）的预览/UI 僵尸请改用 GameAPP::InstantiateZombieFree。
	Zombie* CreateZombie(ZombieType zombieType, int row, float x, bool skipsettings = false, bool isPreview = false);

	// 创建太阳
	Sun* CreateSun(const Vector& position, bool needAnimation = false);

	// 创建太阳
	Sun* CreateSun(float x, float y, bool needAnimation = false);

	// 创建小阳光（阳光菇幼年产物：缩放 0.6 / 价值 15，行为同 Sun）
	SmallSun* CreateSmallSun(const Vector& position, bool needAnimation = false);
	SmallSun* CreateSmallSun(float x, float y, bool needAnimation = false);

	// 创建奖杯
	void CreateTrophy(const Vector& position);

	// 创建植物
	Plant* CreatePlant(PlantType plantType, int row, int column, bool skipsettings = false, bool isPreview = false);
	/** 玩家卡片和 AutoTest 的正式落种入口；预览、初始布置与读档不得调用。 */
	Plant* CreatePlayerPlant(PlantType plantType, int row, int column);
	/** 创建按目标占层/占格的模仿者占位；卡槽只在目标有效时调用。 */
	Plant* CreateImitaterPlant(PlantType targetType, int row, int column);
	/** 把已完成 anim_explode 的模仿者原子替换为同 ID 的泛白目标。 */
	Plant* MorphImitater(Imitater* imitater);

	// 创建铲子（保持 weak_ptr：mShovel 是跨帧成员，需要悬垂检测）
	std::weak_ptr<Shovel> CreateShovel();

	// 激活铲子
	void ActivateShovel();

	// 创建子弹
	Bullet* CreateBullet(BulletType plantType, int row, const Vector& position, bool skipsettings = false);
	/** 创建并标记原发射植物谱系；火炬树桩只改变弹种表现。 */
	Bullet* CreatePlantBullet(BulletType bulletType, int row, const Vector& position,
		PlantType originPlant);

	// 创建樱桃爆炸效果；纵向范围按植物逻辑行覆盖相邻三行，避免泳池美术下沉干扰命中。
	void CreateBoom(const Vector& position, int plantRow, int damage = 1800);

	// 毁灭菇爆炸：半径 250 圆形判定、波及全部行、跳过魅惑僵尸；Charred 阈值逻辑同 CreateBoom
	void CreateDoomBoom(const Vector& position, int plantRow, int damage = 1800);
	/** 玉米加农炮落点爆炸：半径 115px、目标行上下各一行，只命中合法地面敌人。 */
	void CreateCobCannonExplosion(const Vector& position, int targetRow, int damage = 1800);

	// 添加/查询弹坑（毁灭菇）。AddCrater 由爆炸与读档共用；timeLeft 读档时传剩余值
	Crater* AddCrater(int row, int column, float timeLeft);
	bool HasCraterAt(int row, int column);   // 顺带惰性清理已消散的 weak_ptr
	/** 在格子上添加唯一扶梯；已有扶梯时直接返回原对象。 */
	Ladder* AddLadder(int row, int column,
		LadderStyle style = LadderStyle::CLASSIC);
	/** 查询格子上的活动扶梯，并顺带清理失效弱引用。 */
	Ladder* GetLadderAt(int row, int column);
	bool HasLadderAt(int row, int column) { return GetLadderAt(row, column) != nullptr; }
	/** 移除指定格扶梯；返回是否确实移除。 */
	bool RemoveLadderAt(int row, int column);
	/** 创建或恢复全场唯一冰墙；未完成墙必须携带有效施工者 ID。 */
	IceWall* AddIceWall(int row, float centerX,
		int health = 1800, int maxHealth = 1800,
		float thawDamageRemainder = 0.0f,
		bool constructionComplete = true,
		int builderZombieID = NULL_ZOMBIE_ID);
	/** 返回活动冰墙并惰性清理失效弱引用。 */
	IceWall* GetIceWall();
	IceWall* GetIceWallInRow(int row);
	bool HasIceWall() { return GetIceWall() != nullptr; }
	/** 只允许目标墙回收自身；不会误删后来创建的新墙。 */
	bool RemoveIceWall(IceWall* wall);
	/** 创建或恢复一条已提交地裂；nextColumn 是下一待结算列。 */
	GroundRift* AddGroundRift(int row, float frontX, int nextColumn,
		float downstreamDamageMultiplier = 1.0f);
	/** 返回全部活动地裂并惰性清理失效弱引用。 */
	std::vector<GroundRift*> GetGroundRifts();
	/** 只允许目标地裂回收自身，不影响同排其他裂缝。 */
	bool RemoveGroundRift(GroundRift* rift);
	/** 移除指定行全部扶梯，返回移除数量。 */
	int RemoveLaddersInRow(int row);
	/** 按爆心所在格的方形格范围移除扶梯；范围口径与原版 KillAllZombiesInRadius 一致。 */
	int RemoveLaddersInBlastSquare(const Vector& position, int centerRow, int cellRange);
	/** 按 C# 两格评分选择并移除最近扶梯，构造磁力菇离体物。 */
	bool ExtractNearestLadderForMagnet(int plantRow, int plantColumn, MagneticItem& item);

	// 屏幕抖动（移植原版 Board::ShakeBoard）。amountX/amountY 为峰值位移（像素，
	// 符号约定同原版：正 amountX 向左、正 amountY 向下）；oscillations=1 时为原版
	// 单次三角弹跳（0→满幅→0），>1 时改为衰减正弦来回甩（毁灭菇用）。
	// 计时随 dt 推进（暂停冻结）；纯视觉瞬态，不入存档。
	void ShakeBoard(float amountX, float amountY, float durationSeconds = 0.12f, int oscillations = 1);
	// 当前帧的抖动位移，GameScene::Draw 用它整体平移全部绘制命令；未抖动时恒 (0,0)
	Vector GetShakeOffset() const;

	// 带指定 ID 创建实体（用于读档）
	Plant* CreatePlantWithID(PlantType type, int row, int col, int id);
	/** 读档恢复按目标占层/占格的模仿者占位。 */
	Plant* CreateImitaterPlantWithID(PlantType targetType, int row, int col, int id);
	Zombie* CreateZombieWithID(ZombieType type, int row, float x, int id);
	/** 用未计数的新对象接管仍在死亡动画中的同 ID 旧壳，整个操作不改变 mZombieNumber。 */
	Zombie* ReplaceDyingZombieWithID(Zombie* dyingZombie, ZombieType type,
		int row, float x, int id);
	Bullet* CreateBulletWithID(BulletType type, int row, const Vector& pos, int id);
	Sun* CreateSunWithID(const Vector& pos, bool fromSky, int id);
	SmallSun* CreateSmallSunWithID(const Vector& pos, bool fromSky, int id);

	// 更新关卡
	void UpdateLevel();

	// 清理删除的对象
	inline void CleanupExpiredObjects();

	// 从所有Cell中清除指定植物ID
	inline void CleanPlantFromCells(int plantID);

	inline void UpdateSunFalling(float deltaTime);
	/** 仅日间泳池调用：按独立节奏在随机水路位置生成 15 点小阳光。 */
	inline void UpdatePoolSunFalling(float deltaTime);

	/** 一次遍历刷新僵尸血量汇总与动态音乐所需的敌对僵尸数。 */
	void UpdateZombieMetrics();
	/** 本波候选全部兑现后，用完整实体血量锁定提前出波阈值；预警期间暂缓。 */
	void FinalizeWaveSpawnThreshold();
	/** 推进逐行冰道寿命并在到期后恢复空状态。 */
	void UpdateIceTrails(float deltaTime);

	// 尝试生成本波僵尸
	inline void TrySummonZombie();
	/** 在正式冒险 BOSS 波按槽位额外创建唯一首领；普通关和非最终波无操作。 */
	void TrySummonAdventureBoss();

	// 计算当前波的总点数
	inline int CalculateWaveZombiePoints(int wave = -1) const;
	/** 返回当前波正式生成使用的点数预算，供状态投影和回归测试读取。 */
	int GetCurrentWaveZombiePoints() const;

	// 推进并生成下一波（Update 倒计时归零与开发者面板「下一波」共用入口）
	void SummonNextWave();

	// 选好卡，开始游戏
	void StartGame();
	/** 新开屋顶关时按 C# 关卡规则与列优先顺序生成初始花盆；读档路径不得调用。 */
	void InitializeStartingFlowerPots();

	// 游戏结束
	void GameOver();

	// 根据场景播放音乐
	/** 选卡期间后台预构建当前地形的动态音乐，避免 StartGame 同步解析 MO3。 */
	void PrepareBackgroundMusic();
	void PlayBackgroundMusic();

	// 生存模式：一轮（一面旗）清空后推进到下一轮并回到选卡
	void OnSurvivalRoundClear();

	// 生存模式：根据轮次重建出怪表
	void BuildSurvivalSpawnList(int round);

	// 生存模式：根据当前轮次刷新关卡名（"生存模式：无尽 第N面旗"）
	void UpdateSurvivalLevelName();

	// 僵尸全局血量倍率聚合点：默认 1，按当前生效的难度来源叠乘。
	// 目前唯一来源是生存模式（线性 1 + SURVIVAL_HP_GROWTH*(轮次-1)）；
	// 未来其他模式（困难冒险、悬赏关等）若需血量倍率，在此处继续叠乘即可，调用方无需改动。
	// 新波次僵尸生成时(CreateZombie)对其 body/头盔/护盾血量整体乘此系数；读档(CreateZombieWithID)不乘，避免二次叠加。
	SurvivalPerkManager&       GetPerkManager()       { return mPerkManager; }
	const SurvivalPerkManager& GetPerkManager() const { return mPerkManager; }
	/** 记录一次实际植物伤害命中；返回 true 表示该次应额外回响同额伤害。 */
	bool ConsumePlantDamageEchoHit();
	int GetPlantDamageEchoHitCounter() const { return mPlantDamageEchoHitCounter; }

	double GetZombieHpMultiplier() const {
		double multiplier = 1.0;
		if (mIsSurvival)
			multiplier *= (1.0 + SURVIVAL_HP_GROWTH * static_cast<double>(mSurvivalRound - 1));
		multiplier *= mPerkManager.GetZombieHealthMultiplier();   // 词条：僵尸血量（空时=1.0）
		return multiplier;
	}

	void Update();

	// 创建预览僵尸
	void CreatePreviewZombies();

	// 销毁所有预览僵尸
	void DestroyPreviewZombies();

	// 割草机触发时调用（预留接口）
	void SetRowLoseMower(int row);

	// 小推车
	/** 查询该逻辑行是否允许拥有小推车；冬日花园前两行由温室实体阻挡。 */
	bool CanHaveMowerInRow(int row) const;
	Mower* CreateMower(MowerType type, int row);
	Mower* CreateMowerWithID(MowerType type, int row, float x, float y, int id);
	void InitializeMowers();
	/** 保留指定小推车，静默移除其他现存小推车，不播放其启动动画或音效。 */
	void RemoveOtherMowersWithoutTrigger(int preservedMowerID);
};
#endif
