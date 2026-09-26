#include "Board.h"
#include "BoardPresentation.h"
#include "Game/AdventureProgression.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/AI/ColdStorageStrategy.h"
#include "Game/AI/ColdStoragePolicy.h"
#include "Game/CardSlotManager.h"
#include "Game/Card.h"
#include "Game/LawnMower.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/IceWorkerZombie.h"
#include "Game/Zombie/GargantuarZombie.h"
#include "Game/Plant/IceMint.h"
#include "Game/Plant/Squash.h"
#include "Game/Plant/DawnLotus.h"
#include "Game/Plant/PlantUpgradeRules.h"
#include "GameApp.h"
#include "DeltaTime.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
	constexpr std::array<int, 9> kOpeningIce{350, 400, 450, 500, 550, 1500, 1650, 1800, 2000}; // 各关难度1初始敌方冰块；前段削减囤兵，后段保留长流程
	constexpr float kSupplySeconds = 30.0f; // 固定敌方补给间隔，游戏秒
	constexpr int kSupplyIce = 20; // 每次补给冰块，不随难度再放大，给库存消耗留出空间
	constexpr int kLargeOrderSun = 225; // 大额购冰的阳光价格，同时供双方资产预测折算
	constexpr int kLargeOrderIce = 100; // 大额购冰的实际到货量
	constexpr int kSmallOrderSun = 100, kSmallOrderIce = 40; // 小额购冰的阳光价格和实际到货量
	constexpr float kLargeOrderDelay = 10, kSmallOrderDelay = 5; // 商店订单从付款到到货的游戏秒
	constexpr int kMaxIce = 1000000; // 存档与长期对局资源安全上限，避免整数溢出
	constexpr int kMaxSimultaneous = 64; // 正式出兵的敌对同时容量，包含在途；技能召唤沿用自身上限
	constexpr float kDeploySpacing = 0.65f; // 同一队伍逐只入场间隔，游戏秒
	constexpr float kDecisionSeconds = 12.0f; // 常规指挥决策间隔，游戏秒
	constexpr float kOpeningDecisionSeconds = 18.0f; // 前两分钟两波之间留出经济恢复时间，游戏秒
	constexpr float kDevelopingDecisionSeconds = 15.0f; // 两至四分钟的过渡出兵间隔，游戏秒
	constexpr int kForecastSamples = 8; // 每候选短时压力推演的采样次数
	constexpr float kForecastHorizon = 20.0f; // 防线火力与候选生存推演时域，游戏秒
	constexpr float kEconomyHorizon = 60.0f; // 经济评估时域，游戏秒；覆盖新工人首次满产
	constexpr float kInvestmentMargin = 8.0f; // 预计净收入超过此冰量才追加经济投资
	constexpr float kWorkerEntryDelay = 6.0f; // 护卫先入场后制冰工跟进的最小间隔，游戏秒
	constexpr float kWorkerLateEntryDelay = 12.0f; // 经营组合可选的较晚跟进间隔，游戏秒
	constexpr float kEscortForecastStep = 0.5f; // 护卫位置与承伤预测步长，游戏秒
	constexpr float kForecastContactDistance = 55.0f; // 接近可啃食植物时的预测停步距离，像素
	constexpr float kWorkerForecastSpeed = 20.0f; // 新工人的保守水平移速估计，像素/游戏秒
	constexpr float kEconomySplashReach = 100.0f; // 含碰撞箱的溅射接近窗口，像素；不把整路都当成命中
	constexpr float kUnusedBlastRisk = 0.22f; // 未提交炸弹的基础投资折损权重，不等同于确定命中
	constexpr float kCrowdedBlastRisk = 0.10f; // 爆区每多一个可同时命中的敌人，增加的风险权重
	constexpr float kMaximumBlastRisk = 0.80f; // 仅卡槽可用时的最高折损，已提交爆炸另按实际范围截断
	constexpr float kHabitDecay = 0.97f; // 每次正式落种衰减旧画像，防止长期偏好锁死
	constexpr float kReserveFraction = 0.12f; // 常规储备占初始库存比例，总攻可动用一半
	constexpr float kRapidDecisionSeconds = 9.0f; // 抢攻时的重评间隔，游戏秒，仍遵循付费队列与按波解锁
	constexpr float kAssaultCooldownSeconds = 48.0f; // 两次总攻之间的最短重组时间，秒
	constexpr float kMaxObserveSeconds = 24.0f; // 场上兵力充足时最多连续观望的游戏秒
	constexpr float kExhaustionGraceSeconds = 180.0f; // 开战后最早允许失去作战能力判负的游戏秒
	constexpr float kExhaustionIdleSeconds = 90.0f; // 无击杀且经营不盈利的滚动窗口，游戏秒
	constexpr size_t kMaxIncomeWindowRecords = 4096; // 读档收支记录上限，高于满场工人在窗口内的合法事务数量
	constexpr float kResponseLookaheadSeconds = 18.0f; // 判断炸弹即将恢复的观察窗，秒
	constexpr float kAssaultOpportunityThreshold = 2.4f; // 已有前锋可利用的突破评分下限
	constexpr float kSupportHealthRequired = 1200.0f; // 派治疗或钟匠前，同路有效前锋的最低生命
	constexpr int kNormalEarlyBudget = 72; // 前五关每波常规冰块预算上限，可容纳进攻队伍与跟进工人
	constexpr int kNormalLateBudget = 96; // 后四关每波常规冰块预算上限
	constexpr int kAssaultEarlyBudget = 192; // 前五关一次集中进攻冰块预算上限，不是额外收入
	constexpr int kAssaultLateBudget = 256; // 后四关一次集中进攻冰块预算上限
	constexpr float kCollateralScorePerIce = 0.4f; // 邻路额外损失每冰折算的出兵评分，可被突破价值抵消
	constexpr float kEscortIncomeShare = 0.5f; // 工人预期收入分配给护卫的保护价值权重
	constexpr float kStagingRecheck = 3.0f; // 暂缓后续梯队后重新看战况的间隔，游戏秒
	constexpr float kBlastStakeMinimum = 24.0f; // 爆炸牌可用时仍允许投入的小队风险额度，冰块
	constexpr float kBlastStakeMaximum = 40.0f; // 库存充裕时单个爆区的常规最高投资损失，冰块
	constexpr float kBlastStakeFraction = 0.06f; // 库存折算为可承受爆区风险的比例
	constexpr float kBlastScorePerIce = 0.15f; // 候选新增爆炸损失折算的评分惩罚
	constexpr float kAshForecastDamage = 1800.0f; // 樱桃/辣椒/毁灭菇正式灰烬伤害的预测值
	constexpr float kBreachFrontHealth = 2000.0f; // 当前最前格达到此耐久时，快速跟进需等待破障前锋

	/** 统一计划与预算采用的决策间隔，避免两处调整后预算速度失配。 */
	float DecisionInterval(float elapsed, bool rapid = false)
	{
		if (rapid && elapsed >= 80.0f) return kRapidDecisionSeconds;
		return elapsed < 120 ? kOpeningDecisionSeconds
			: elapsed < 240 ? kDevelopingDecisionSeconds : kDecisionSeconds;
	}

	/** 预测和正式击杀共用返冰舍入规则；预测收入不直接计入库存。 */
	int PlantKillIce(int plantCost, int difficulty)
	{
		return plantCost * (difficulty + 2) / 4;
	}

	/** 紫卡炮台的建造卡不是一次性爆炸牌；其已部署技能从战场快照读取。 */
	bool IsInstantBlast(PlantType type)
	{
		return type == PlantType::PLANT_CHERRYBOMB || type == PlantType::PLANT_JALAPENO
			|| type == PlantType::PLANT_DOOMSHROOM;
	}

	/** 同一植物可同时具有多项战略标签，画像记录实际使用而非选卡次数。 */
	std::array<float, 4> PlantStrategy(PlantType type)
	{
		const auto& p = GameDataManager::GetInstance().GetPlantSimulationProfile(type);
		const bool explosive = type == PlantType::PLANT_CHERRYBOMB || type == PlantType::PLANT_DOOMSHROOM
			|| type == PlantType::PLANT_JALAPENO || type == PlantType::PLANT_COBCANNON;
		return {p.sunPerSecond > 0 ? 1.0f : 0.0f, explosive ? 1.0f : 0.0f,
			p.slowApplicationsPerSecond + p.frozenApplicationsPerSecond > 0 ? 1.0f : 0.0f,
			p.baseHealth >= 2000 || type == PlantType::PLANT_PUMPKINSHELL ? 1.0f : 0.0f};
	}

	/** 已种下的爆炸只威胁其当前范围；卡槽威胁记录实际可用格位与最早就绪时间。 */
	struct EconomyBlast {
		PlantType type;
		int row;
		float x;
		float ready;
		bool committed;
		float radius = 0.0f;
		int rowRadius = 0;
		float damage = kAshForecastDamage;
	};

	/** 返回对应结算几何在目标行的水平半径；负值表示该行不会命中。 */
	float EconomyBlastReach(const EconomyBlast& blast, int row)
	{
		const int rows = std::abs(blast.row - row);
		if (blast.type == PlantType::PLANT_COBCANNON) return rows <= blast.rowRadius ? blast.radius : -1.0f;
		if (blast.type == PlantType::PLANT_JALAPENO) return rows == 0 ? 10000.0f : -1.0f;
		if (blast.type == PlantType::PLANT_CHERRYBOMB) return rows <= 1 ? 130.0f : -1.0f;
		if (blast.type == PlantType::PLANT_SQUASH) return rows == 0 ? 50.0f : -1.0f;
		// 与 CreateDoomBoom 的圆/碰撞矩形纵向口径一致，用当前棋盘行距换算。
		const float dy = std::max(0.0f, rows * static_cast<float>(CELL_COLLIDER_SIZE_Y)
			- (row > blast.row ? 65.0f : 35.0f));
		return dy <= 250.0f ? 25.0f + std::sqrt(250.0f * 250.0f - dy * dy) : -1.0f;
	}

	struct AssaultProfile { float health; float speed; bool bypass; bool support; bool splash; float smashSeconds = 0.0f; };
	/** 只供经营预测的运动状态；出生前、定身和啃食期间不前进。 */
	struct EconomyMover {
		float x = 0.0f, speed = 0.0f, health = 0.0f;
		float spawnAt = 0.0f, stopped = 0.0f, slow = 0.0f, eating = 0.0f;
		float stopX = 0.0f, futureSlowFactor = 1.0f;
		float slowFactor = 0.3f; // 未生成普通单位按动画 0.6 × 位移时间 0.5 估计；已有实体读取品种接口
	};

	/** 将现有控制按剩余时间积分；新护卫进场后才受到防线持续减速。 */
	float EconomyPosition(const EconomyMover& mover, float time)
	{
		const float pause = mover.stopped + mover.eating;
		const float active = std::max(0.0f, time - mover.spawnAt - pause);
		const float slowed = std::clamp(mover.slow - pause, 0.0f, active);
		const float distance = mover.speed * (active - slowed * (1.0f - mover.slowFactor));
		const float outside = std::max(0.0f, mover.x - SCENE_WIDTH);
		// 已有减速与后续持续减速取较慢者，不能重复叠乘同一种减速。
		const float futureDistance = std::min(mover.speed * active, outside)
			+ std::max(0.0f, mover.speed * active - outside) * mover.futureSlowFactor;
		return std::max(mover.stopX, mover.x - std::min(distance, futureDistance));
	}

	struct EconomySurvival {
		float life = 0.0f, cover = 0.0f;
		std::array<float, static_cast<int>(kEconomyHorizon / kEscortForecastStep) + 1> positions{};
		float PositionAt(float time) const {
			return positions[std::clamp(static_cast<int>(time / kEscortForecastStep), 0, static_cast<int>(positions.size()) - 1)];
		}
	};

	/** 在决策时域内逐段消耗实际位于工人前方的护卫；追过、死亡或尚未出生均不挡伤害。 */
	EconomySurvival ForecastEconomySurvival(const EconomyMover& worker, std::vector<EconomyMover> guards,
		float directFire, float adjacentFire, float splashFire, float slowDuty, float splashSlowDuty, float slowDuration)
	{
		float health = worker.health;
		float workerX = worker.x, futureSlowUntil = 0.0f, futureSlowDuty = 0.0f;
		EconomySurvival result;
		result.positions.fill(worker.x);
		for (float time = 0.0f; time < kEconomyHorizon; time += kEscortForecastStep) {
			const float dt = std::min(kEscortForecastStep, kEconomyHorizon - time);
			const float sample = time + dt * 0.5f;
			if (sample >= worker.spawnAt + worker.stopped + worker.eating) {
				const float currentSlow = sample < worker.spawnAt + worker.slow ? worker.slowFactor : 1.0f;
				const float futureSlow = sample < futureSlowUntil ? 1.0f - (1.0f - worker.slowFactor) * futureSlowDuty : 1.0f;
				workerX = std::max(worker.stopX, workerX - worker.speed * std::min(currentSlow, futureSlow) * dt);
			}
			result.positions[static_cast<int>((time + dt) / kEscortForecastStep)] = workerX;
			const bool present = time + dt > worker.spawnAt;
			EconomyMover* front = nullptr;
			float frontPosition = present ? workerX : std::numeric_limits<float>::max();
			for (auto& guard : guards) {
				if (guard.health <= 0.0f || sample < guard.spawnAt) continue;
				const float x = EconomyPosition(guard, sample);
				if (x < frontPosition) { front = &guard; frontPosition = x; }
			}
			float protectedTime = 0.0f;
			if (front && frontPosition <= SCENE_WIDTH) {
				protectedTime = directFire > 0.0f ? std::min(dt, front->health / directFire) : dt;
				front->health = std::max(0.0f, front->health - directFire * dt);
			}
			else if (front) protectedTime = dt;
			if (!present) continue; // 等工人期间，前排仍会承伤；未出生工人不生产。
			const float aliveStep = std::min(dt, time + dt - worker.spawnAt);
			float damage = 0.0f;
			if (workerX <= SCENE_WIDTH) {
				// 冰西瓜溅到后排时，工人也会被减速；不能假定它始终全速冲过同样变慢的巨人。
				const bool splashed = front && workerX - frontPosition < kEconomySplashReach;
				const float appliedSlow = front ? (splashed ? splashSlowDuty : 0.0f) : slowDuty;
				if (appliedSlow > 0.0f) {
					futureSlowUntil = sample + slowDuration;
					futureSlowDuty = std::clamp(appliedSlow, 0.0f, 1.0f);
				}
				const float leak = front ? splashFire / 3.0f
					* std::clamp(1.0f - (workerX - frontPosition) / kEconomySplashReach, 0.0f, 1.0f) : 0.0f;
				damage = adjacentFire * aliveStep + leak * protectedTime
					+ directFire * std::max(0.0f, aliveStep - protectedTime);
			}
			const float fraction = damage > health && damage > 0.0f ? health / damage : 1.0f;
			result.life += aliveStep * fraction;
			result.cover += std::min(aliveStep, protectedTime) * fraction;
			health -= damage;
			if (health <= 0.0f) break;
		}
		return result;
	}

	/** 候选战术画像只服务排序；实体出生仍使用原品种生命、技能和动作。 */
	AssaultProfile Assault(ZombieType type)
	{
		using Z = ZombieType;
		switch (type) {
		case Z::ZOMBIE_REDEYE_GARGANTUAR: return {6000, 1, false, false, true, 4.0f};
		case Z::ZOMBIE_GARGANTUAR: return {3000, 1, false, false, true, 4.0f};
		case Z::ZOMBIE_PINK_FOOTBALL: return {2600, 2, false, false, false};
		case Z::ZOMBIE_FOOTBALL: return {1700, 2, false, false, false};
		case Z::ZOMBIE_HEALER: return {800, 1, false, true, false};
		case Z::ZOMBIE_DANCER: case Z::ZOMBIE_ELITE_DANCER: return {1500, 1, false, true, false};
		case Z::ZOMBIE_AURORA_PRIEST: return {1800, 1, true, true, false};
		case Z::ZOMBIE_POLAR_CLOCKMAKER: return {1500, 1, false, true, false};
		case Z::ZOMBIE_DIGGER: case Z::ZOMBIE_ELITE_DIGGER: return {900, 1.5f, true, false, true};
		case Z::ZOMBIE_POLEVAULTER: case Z::ZOMBIE_ELITE_POLEVAULTER:
		case Z::ZOMBIE_POGO: case Z::ZOMBIE_ELITE_POGO: return {700, 1.8f, true, false, false};
		case Z::ZOMBIE_JACK_IN_THE_BOX: case Z::ZOMBIE_ELITE_JACK_IN_THE_BOX: return {800, 1.4f, false, false, true};
		case Z::ZOMBIE_ZAMBONI: case Z::ZOMBIE_GILDED_ZAMBONI: return {1500, 1.2f, false, false, true};
		case Z::ZOMBIE_CATAPULT: return {1200, 1, true, false, false};
		case Z::ZOMBIE_BUCKET: case Z::ZOMBIE_FASTBUCKET: case Z::ZOMBIE_ADAPTIVE_HELMET:
		case Z::ZOMBIE_REINFORCED_DOOR: return {1500, 1, false, false, false};
		case Z::ZOMBIE_ICE_WORKER: return {IceProduction::WorkerHealth, 1, false, true, false};
		case Z::ZOMBIE_NORMAL: return {270, 1, false, false, false};
		default: return {700, 1.2f, false, false, false};
		}
	}
}

int Board::GetPlantIceCost(PlantType type) const
{
	using P = PlantType;
	switch (type) {
	case P::PLANT_PUFFSHROOM: case P::PLANT_POTATOMINE: case P::PLANT_LILYPAD:
	case P::PLANT_FLOWERPOT: case P::PLANT_INSTANT_COFFEE: return 5;
	case P::PLANT_CARRYVINE: return 0; // 搬运既有植物不重复收取种植冰块
	case P::PLANT_WALLNUT: case P::PLANT_REPEATER: case P::PLANT_FUMESHROOM:
	case P::PLANT_TORCHWOOD: case P::PLANT_MAGNETSHROOM: return 15;
	case P::PLANT_CHERRYBOMB: case P::PLANT_JALAPENO: case P::PLANT_SNOWPEA:
	case P::PLANT_PUMPKINSHELL: case P::PLANT_TALLNUT: case P::PLANT_ICESHROOM:
	case P::PLANT_TWINSUNFLOWER: case P::PLANT_THREEPEATER: return 20;
	case P::PLANT_MELONPULT: case P::PLANT_WINTERMELON: case P::PLANT_GLOOMSHROOM:
	case P::PLANT_DOOMSHROOM: case P::PLANT_GATLINGPEA: case P::PLANT_ELITE_SCAREDYSHROOM: return 30;
	case P::PLANT_COBCANNON: return 40;
	default: return 10;
	}
}

/** 按兵种威胁分档定价；重装和支援必须消耗足够库存，击杀回收沿用实际支付价。 */
int Board::GetZombieIceCost(ZombieType type) const
{
	using Z = ZombieType;
	switch (type) {
	case Z::ZOMBIE_ICE_WORKER: return IceProduction::WorkerCost;
	case Z::ZOMBIE_NORMAL: return 4; // 保留低库存收尾时可派出的基础兵
	case Z::ZOMBIE_TRAFFIC_CONE: case Z::ZOMBIE_NEWSPAPER: return 6;
	case Z::ZOMBIE_BUCKET: case Z::ZOMBIE_DOOR: case Z::ZOMBIE_POLEVAULTER: return 8;
	case Z::ZOMBIE_DANCER: return 12;
	case Z::ZOMBIE_FOOTBALL: case Z::ZOMBIE_GARGANTUAR: case Z::ZOMBIE_HEALER: return 16;
	case Z::ZOMBIE_PINK_FOOTBALL: return 22;
	case Z::ZOMBIE_ELITE_JACK_IN_THE_BOX:
	case Z::ZOMBIE_POLAR_CLOCKMAKER: return 18;
	case Z::ZOMBIE_REDEYE_GARGANTUAR: return 24;
	case Z::ZOMBIE_AURORA_PRIEST:
	case Z::ZOMBIE_ELITE_DANCER: return 24;
	default: return 12;
	}
}

bool Board::CanAffordPlantIce(PlantType type) const
{
	return !IsColdStorage() || (GameAPP::mDevelopMode && GameAPP::mDevFreePlant)
		|| mColdStorage.playerIce >= GetPlantIceCost(type);
}

void Board::InitializeColdStorage()
{
	if (!IsColdStorage()) return;
	mColdStorage = {};
	mMaxWave = 0; // 冷藏站没有最终波；波号仍用于逐步解锁兵种，胜利由冰块破产与清场判定。
	mColdStorage.difficulty = std::clamp(GameAPP::GetInstance().Difficulty, 1, 4);
	const int stage = std::clamp(AdventureProgression::GetLevelNumberInArea(mLevel) - 1, 0, 8);
	mColdStorage.initialEnemyIce = MiniGame::IsBrawl(mLevel) ? MiniGame::BRAWL_ENEMY_ICE
		: kOpeningIce[stage] * (4 + mColdStorage.difficulty - 1) / 4;
	mColdStorage.enemyIce = mColdStorage.initialEnemyIce;
	mColdStorage.habits = GameAPP::GetInstance().mColdStorageHabits;
}

bool Board::BuyColdStorageIce(bool large)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned
		|| DeltaTime::IsPaused() || mColdStorage.orderIce > 0) return false;
	const int price = large ? kLargeOrderSun : kSmallOrderSun;
	if (mSun < price) return false;
	SubSun(price);
	mColdStorage.orderIce = large ? kLargeOrderIce : kSmallOrderIce;
	mColdStorage.orderRemaining = large ? kLargeOrderDelay : kSmallOrderDelay;
	return true;
}

void Board::CreditProducedIce(bool player, int amount, int sourceWave)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || amount <= 0) return;
	int& balance = player ? mColdStorage.playerIce : mColdStorage.enemyIce;
	int& income = player ? mColdStorage.playerProductionIncome : mColdStorage.workerIncome;
	const int accepted = std::min(amount, kMaxIce - balance);
	balance += accepted;
	income = std::min(kMaxIce, income + accepted);
	if (!player && accepted > 0) {
		mColdStorage.incomeIdleSeconds = 0;
		mColdStorage.incomeWindow.push_back({mColdStorage.elapsed,accepted,0});
		if (GameAPP::mAutoTestMode) {
			auto& events = mColdStorage.productionEvents;
			events.push_back({mColdStorage.elapsed,sourceWave,accepted});
			while (events.size() > 4096 || (!events.empty() && events.front().at < mColdStorage.elapsed - 120)) events.pop_front();
		}
	}
}

void Board::CommitColdStoragePlant(PlantType type)
{
	if (!IsColdStorage()) return;
	if (!(GameAPP::mDevelopMode && GameAPP::mDevFreePlant))
		mColdStorage.playerIce = std::max(0, mColdStorage.playerIce - GetPlantIceCost(type));
	const auto use = PlantStrategy(type);
	for (std::size_t i = 0; i < use.size(); ++i)
		mColdStorage.habits[i] = mColdStorage.habits[i] * kHabitDecay + use[i] * (1.0f - kHabitDecay);
	// 小游戏内仍逐步适应本局布阵，但不把试玩画像写回冒险模式。
	if (!MiniGame::IsBrawl(mLevel))
		GameAPP::GetInstance().mColdStorageHabits = mColdStorage.habits;
}

void Board::RewardColdStoragePlantKill(PlantType type)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned) return;
	// 难度1～4分别返还植物冰价的0.75/1/1.25/1.5倍，抑制破阵后的资源滚雪球。
	const int reward = PlantKillIce(GetPlantIceCost(type), mColdStorage.difficulty);
	const int accepted = std::min(reward, kMaxIce - mColdStorage.enemyIce);
	mColdStorage.enemyIce += accepted;
	mColdStorage.killIncome = std::min(kMaxIce, mColdStorage.killIncome + accepted);
	if (accepted > 0) mColdStorage.incomeIdleSeconds = 0;
	// 实际破阵本身就有进展，零冰价植物被消灭也重置；与生产账本分开。
	mColdStorage.plantKillIdleSeconds = 0;
}

void Board::SettleColdStorageZombieDeath(const Zombie& zombie)
{
	if (!IsColdStorage()) return;
	auto& s = mColdStorage;
	const auto it = s.refundableCosts.find(zombie.mZombieID);
	if (it == s.refundableCosts.end()) return;
	const int reward = it->second * 3 / 4;
	// 先关闭资格再发放；即使库存已满也不能留到复活后再次结算。
	s.refundableCosts.erase(it);
	if (zombie.IsPreview() || zombie.IsMindControlled() || mBoardState != BoardState::GAME) return;
	const int accepted = std::min(reward, kMaxIce - s.playerIce);
	s.playerIce += accepted;
	s.playerKillIncome = std::min(kMaxIce, s.playerKillIncome + accepted);
}

int Board::GetColdStorageHostileCount() const
{
	int count = 0;
	for (int id : mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* z = mEntityRegistry.GetZombie(id);
		if (z && z->IsActive() && !z->IsPreview() && !z->IsDying()
			&& !z->IsMindControlled()) ++count;
	}
	return count;
}

/** 清场且无在途援军时判定破产或长期无破阵且经营不盈利的低库存败局，不中断仍在作战的部队。 */
bool Board::IsColdStorageCleared() const
{
	if (!IsColdStorage() || !mColdStorage.battleStarted || mTrophySpawned
		|| !mColdStorage.pending.empty() || !mPendingSnowHoleSpawns.empty()
		|| !mPendingAuroraRifts.empty() || GetColdStorageHostileCount() != 0) return false;
	// 钟匠已经提交的复活同样属于在途兵力；施法者死亡不能让本局提前结束。
	for (const auto& anchor : mTemporalAnchors)
		for (const auto& target : anchor.targets)
			if (!target.irreversible) return false;
	if (mColdStorage.elapsed >= kExhaustionGraceSeconds
		&& mColdStorage.plantKillIdleSeconds >= kExhaustionIdleSeconds
		&& mColdStorage.enemyIce < ColdStorageState::RecoveryReserveIce) {
		// 只看完整的最近窗口，少量亏本产冰不能为每轮送兵重新续命。
		long long net = 0;
		for (const auto& flow : mColdStorage.incomeWindow)
			if (flow.at >= mColdStorage.elapsed - kExhaustionIdleSeconds)
				net += static_cast<long long>(flow.production) - flow.spent;
		if (net <= 0) return true;
	}
	for (ZombieType type : mSpawnZombieList)
		if (mColdStorage.enemyIce >= GetZombieIceCost(type)) return false;
	return true;
}

bool Board::QueueColdStorageZombie(ZombieType type, int row, float delay)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned
		|| row < 0 || row >= mRows || !IsSpawnRowCompatible(type, row)
		|| (!(GameAPP::mAutoTestMode && ColdStoragePolicy::AllUnits())
			&& GameDataManager::GetInstance().GetZombieAppearWave(type) > mColdStorage.decisions + 1)
		|| std::find(mSpawnZombieList.begin(), mSpawnZombieList.end(), type) == mSpawnZombieList.end()
		|| GetColdStorageHostileCount() + static_cast<int>(mColdStorage.pending.size()) >= kMaxSimultaneous) return false;
	const int cost = GetZombieIceCost(type);
	if (mColdStorage.enemyIce < cost || !std::isfinite(delay)) return false;
	// 先登记再扣款，只有成功提交的事务能改变余额；免费技能召唤不经过这里。
	mColdStorage.pending.push_back({type, row, cost, std::clamp(delay, 0.0f, 60.0f), mColdStorage.decisions});
	mColdStorage.enemyIce -= cost;
	mColdStorage.spent = std::min(kMaxIce, mColdStorage.spent + cost);
	mColdStorage.incomeWindow.push_back({mColdStorage.elapsed,0,cost});
	return true;
}

/** 先决定是否值得增援及本波预算，再在合法候选中组织队伍；未派兵不推进波号。 */
void Board::PlanColdStorageAttack()
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned) return;
	const auto* learnedWeights = GameAPP::GetInstance().mEnableMonteCarloAI ? ColdStoragePolicy::Get(mLevel) : nullptr;
	if (!mColdStorage.pending.empty() && !learnedWeights) return;
	auto& s = mColdStorage;
	const bool allUnitsUnlocked = ColdStoragePolicy::AllUnits();
	const auto isUnlocked = [&](ZombieType type) {
		return allUnitsUnlocked || GameDataManager::GetInstance().GetZombieAppearWave(type) <= s.decisions + 1;
	};
	s.commanderMode = "pressure";
	s.commanderBudget = s.commanderSpent = s.commanderReserve = 0;
	s.commanderFocusRow = -1;
	s.candidatesEvaluated = 0;
	s.predictedProduction = 0.0f;
	s.economyValue = 0.0f;
	s.economyRow = -1;
	s.economicFollowups = 0;
	s.predictedKillIncome = 0.0f;
	s.raidNetByRow.fill(0.0f);
	s.splashRiskByRow.fill(0.0f);
	s.attackDeferred = false;
	s.formationBlastLoss = 0.0f;
	s.economyNetByRow.fill(0.0f);
	s.economyBlastLossByRow.fill(0.0f);
	s.economyGuardCostByRow.fill(0);
	s.economyEntryDelayByRow.fill(0.0f);
	s.economyCoverByRow.fill(0.0f);
	s.searchCommittedCount = s.searchQueueEvaluated = s.searchQueueChanged = 0;
	s.searchQueueBeforeScore = s.searchQueueAfterScore = 0;
	PlantDefenseMonteCarlo::Snapshot snapshot;
	if (!BuildMonteCarloCombatSnapshot(snapshot, false, false)) return;
	std::array<float, 6> directDps{}, directSplash{}, slowDuty{}, splashSlowDuty{}, slowDuration{}, dps{}, value{}, armor{}, frontArmor{}, escort{}, splash{}, control{}, frontX{};
	std::array<int, 6> committed{}, frontlineCount{}, waveRows{}, healers{}, specialists{};
	std::array<int, 6> frontColumn;
	frontColumn.fill(-1);
	std::array<float, 6> frontHealth{};
	for (int row = 0; row < mRows; ++row) frontX[row] = GetCellCenterPosition(row, 0).x;
	for (const auto& p : snapshot.plants) {
		if (p.canBeEaten && p.column >= frontColumn[p.row]) {
			if (p.column > frontColumn[p.row]) frontHealth[p.row] = 0;
			frontColumn[p.row] = p.column;
			frontHealth[p.row] += p.health;
		}
		directDps[p.row] += p.attackDps;
		slowDuty[p.row] += p.slowApplicationsPerSecond * p.slowDuration;
		slowDuration[p.row] = std::max(slowDuration[p.row], p.slowDuration);
		if (p.attackRowRadius > 0) splashSlowDuty[p.row] += p.slowApplicationsPerSecond * p.slowDuration;
		if (p.attackRowRadius > 0) directSplash[p.row] += p.attackDps;
		value[p.row] += 1.0f + p.strategicValue / 100.0f;
		armor[p.row] += p.health / 1000.0f;
		frontX[p.row] = std::max(frontX[p.row], p.x);
		if (p.column >= 3) frontArmor[p.row] += p.health;
		for (int row = std::max(0, p.row - p.attackRowRadius); row <= std::min(mRows - 1, p.row + p.attackRowRadius); ++row) {
			dps[row] += p.attackDps;
			control[row] += p.slowApplicationsPerSecond + p.frozenApplicationsPerSecond;
			if (p.attackRowRadius > 0) splash[row] += p.attackDps;
		}
	}
	float totalHealth = 0.0f, totalDps = 0.0f, totalArmor = 0.0f;
	bool likelyBreach = false;
	for (int row = 0; row < mRows; ++row) { totalDps += dps[row]; totalArmor += armor[row] * 1000.0f; }
	for (const auto& z : snapshot.zombies) if (!z.mindControlled && z.row >= 0 && z.row < mRows) {
		const float health = z.bodyHealth + z.helmHealth + z.shieldHealth;
		const bool economicUnit = dynamic_cast<const IceWorkerZombie*>(mEntityRegistry.GetZombie(z.id)) != nullptr;
		if (!economicUnit) { totalHealth += health; ++frontlineCount[z.row]; }
		++committed[z.row];
		// 太深入的旧部队不能替刚从右侧入场的支援挡伤害。
		if (z.x >= frontX[z.row] - 100.0f
			&& !dynamic_cast<const IceWorkerZombie*>(mEntityRegistry.GetZombie(z.id))) escort[z.row] += health;
		float blocking = 0.0f;
		for (const auto& p : snapshot.plants) if (p.row == z.row && p.canBeEaten && p.x < z.x) blocking += p.health;
		const float travel = std::max(0.0f, z.x - GetCellCenterPosition(z.row, 0).x + 150.0f) / std::max(5.0f, z.moveSpeed);
		if (z.x < GetCellCenterPosition(z.row, 1).x && blocking < 200.0f
			&& health > dps[z.row] * travel * 1.2f) {
			// 空路不等于已经获胜：主人仍可能用可支付的南瓜或坚果临时堵住漏怪。
			bool canIntercept = false;
			if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
				if (!card || card->GetCooldownTimer() > travel || card->GetSunCost() > mSun) continue;
				const auto type = card->GetGameplayPlantType();
				if (GetPlantIceCost(type) > s.playerIce
					|| GameDataManager::GetInstance().GetPlantSimulationProfile(type).baseHealth < 2000) continue;
				for (int col = 0; col < mColumns && !canIntercept; ++col)
					if (GetCellCenterPosition(z.row, col).x <= z.x + 30.0f && CanPlantAt(type, z.row, col)) canIntercept = true;
			}
			if (!canIntercept) likelyBreach = true;
		}
	}
	// 每次决策一次全场身份采样；正式同时容量有限，不放进逐帧能力路径。
	for (int id : mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* z = mEntityRegistry.GetZombie(id);
		if (!z || !z->IsActive() || z->IsPreview() || z->IsDying() || z->IsMindControlled()
			|| z->mRow < 0 || z->mRow >= mRows) continue;
		if (z->mZombieType == ZombieType::ZOMBIE_HEALER) ++healers[z->mRow];
		if (z->mZombieType == ZombieType::ZOMBIE_HEALER || z->mZombieType == ZombieType::ZOMBIE_POLAR_CLOCKMAKER
			|| z->mZombieType == ZombieType::ZOMBIE_AURORA_PRIEST) ++specialists[z->mRow];
	}

	// 读取真正可支付的反制。毁灭菇卡还需要咖啡，已种下的睡菇不受毁灭菇卡冷却限制。
	std::vector<EconomyBlast> economyBlasts;
	float responseWindow = 60.0f, coffeeWait = 60.0f;
	int coffeeSun = 0;
	bool hasCoffee = false;
	const int futureIce = s.playerIce + (s.orderRemaining <= kResponseLookaheadSeconds ? s.orderIce : 0);
	if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
		if (card && card->GetGameplayPlantType() == PlantType::PLANT_INSTANT_COFFEE) {
			hasCoffee = true;
			coffeeWait = std::min(coffeeWait, card->GetCooldownTimer());
			coffeeSun = card->GetSunCost();
		}
	}
	if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
		if (!card) continue;
		const auto type = card->GetGameplayPlantType();
		if (!IsInstantBlast(type)) continue;
		const bool doom = type == PlantType::PLANT_DOOMSHROOM;
		if (doom && !hasCoffee) continue;
		const int sunCost = card->GetSunCost() + (doom ? coffeeSun : 0);
		const int iceCost = GetPlantIceCost(type) + (doom ? GetPlantIceCost(PlantType::PLANT_INSTANT_COFFEE) : 0);
		if (mSun < sunCost || futureIce < iceCost) continue;
		bool legal = false;
		const float ready = std::max(card->GetCooldownTimer(), doom ? coffeeWait : 0.0f);
		for (int row = 0; row < mRows; ++row)
			for (int col = 0; col < mColumns; ++col) if (CanPlantAt(type, row, col)) {
				legal = true;
				economyBlasts.push_back({type, row, GetCellCenterPosition(row, col).x, ready, false});
			}
		if (legal) responseWindow = std::min(responseWindow, std::max(card->GetCooldownTimer(), doom ? coffeeWait : 0.0f));
	}
	for (const auto& p : snapshot.plants) {
		const Plant* plant = mEntityRegistry.GetPlant(p.id);
		if (!plant) continue;
		if (IsInstantBlast(plant->GetPlacementType())) {
			if (!plant->GetSleepState()) {
				responseWindow = 0.0f;
				economyBlasts.push_back({plant->GetPlacementType(), p.row, p.x, 1.0f, true});
			}
			else if (hasCoffee && mSun >= coffeeSun && futureIce >= GetPlantIceCost(PlantType::PLANT_INSTANT_COFFEE))
			{
				responseWindow = std::min(responseWindow, coffeeWait);
				economyBlasts.push_back({plant->GetPlacementType(), p.row, p.x, coffeeWait + 1.5f, false});
			}
		}
		if (p.cobBlastDamage > 0.0f) {
			responseWindow = std::min(responseWindow, p.abilityCooldownRemaining);
			for (const auto& cell : snapshot.cells) economyBlasts.push_back({PlantType::PLANT_COBCANNON,
				cell.row, cell.x, p.abilityCooldownRemaining + 4.0f, false, p.cobBlastRadius, p.cobBlastRowRadius, p.cobBlastDamage});
		}
	}
	for (const auto& blast : snapshot.pendingCobBlasts) economyBlasts.push_back({PlantType::PLANT_COBCANNON,
		blast.targetRow, blast.x, blast.resolveSeconds, true, blast.radius, blast.rowRadius, blast.damage});
	s.responseWindow = responseWindow;
	const bool blastSoon = responseWindow <= kResponseLookaheadSeconds;

	// 经济预测只发生在指挥边沿；逐只按真实位置累计前方护卫，后方血量不能挡枪。
	std::array<float, 6> workerValue{}, investment{}, guardNeed{};
	std::array<int, 6> workers{};
	float playerProduction = 0.0f;
	for (const auto& p : snapshot.plants) {
		if (dynamic_cast<const IceMint*>(mEntityRegistry.GetPlant(p.id))) {
			playerProduction += IceProduction::MintYield * kEconomyHorizon / IceProduction::MintInterval;
			value[p.row] += 2.0f; // 可被摧毁的生产设施也构成进攻路线价值
		}
	}
	// 用真实实体的稳态走路速度，避免啃食/定身动画的零位移被当成永久静止。
	auto makeMover = [&](const PlantDefenseMonteCarlo::ZombieSnapshot& z, bool guard) {
		EconomyMover mover;
		mover.x = z.x;
		mover.health = z.bodyHealth + z.helmHealth + z.shieldHealth;
		const Zombie* entity = mEntityRegistry.GetZombie(z.id);
		mover.speed = entity ? entity->GetMineSimulationMoveSpeed() : z.moveSpeed;
		mover.slowFactor = entity ? entity->GetSimulationSlowMoveMultiplier() : 0.3f;
		mover.slow = z.slowRemaining;
		mover.stopped = std::max({z.frozenRemaining, z.butterRemaining, z.paralysisRemaining});
		if (guard && z.canBeChilled && z.slowImmunityRemaining < kEconomyHorizon)
			mover.futureSlowFactor = 1.0f - (1.0f - mover.slowFactor) * std::clamp(slowDuty[z.row], 0.0f, 1.0f);
		for (const auto& plant : snapshot.plants) {
			if (plant.row != z.row || !plant.canBeEaten) continue;
			if (z.isEating && plant.id == z.eatingPlantId) {
				// 正在吃的目标用剩余生命估算停留；控制到期后可继续，不永远锁在原地。
				const float biteDps = std::max(1.0f, entity ? entity->GetMineSimulationAttackDps() : z.attackDamage);
				mover.eating = plant.health / biteDps;
				mover.eating += std::min(mover.eating, std::max(0.0f, mover.slow - mover.stopped)) * 0.5f;
			}
			else if (plant.x + kForecastContactDistance < z.x)
				mover.stopX = std::max(mover.stopX, plant.x + kForecastContactDistance);
		}
		return mover;
	};
	std::array<std::vector<EconomyMover>, 6> rowGuards;
	for (const auto& z : snapshot.zombies) {
		const Zombie* entity = mEntityRegistry.GetZombie(z.id);
		if (z.mindControlled || !entity || !entity->HasHead() || dynamic_cast<const IceWorkerZombie*>(entity)
			|| z.x < frontX[z.row] - 100.0f) continue;
		rowGuards[z.row].push_back(makeMover(z, true));
	}
	// 先预测双方位置与承伤，再对存活期间的收入计入可支付爆炸风险。
	auto expectedIncome = [&](int row, const EconomyMover& worker, const std::vector<EconomyMover>& guards,
		float remainingProduction, float amount, int addedGuards, int workerID, float* blastLoss, float* cover) {
		float adjacentFire = 0.0f;
		for (int otherRow = std::max(0, row - 1); otherRow <= std::min(mRows - 1, row + 1); ++otherRow) {
			if (otherRow == row) continue;
			const bool nearbyTarget = std::any_of(snapshot.zombies.begin(), snapshot.zombies.end(), [&](const auto& z) {
				return !z.mindControlled && z.row == otherRow && std::abs(z.x - worker.x) < kEconomySplashReach;
			});
			if (nearbyTarget) adjacentFire += directSplash[otherRow] / 3.0f;
		}
		const auto survival = ForecastEconomySurvival(worker, guards, directDps[row], adjacentFire, directSplash[row],
			slowDuty[row], splashSlowDuty[row], slowDuration[row]);
		if (cover) *cover = survival.cover;
		auto incomeUntil = [&](float time) {
			return static_cast<float>(IceProduction::Forecast(remainingProduction, amount,
				std::max(0.0f, time - worker.stopped)));
		};
		const float income = incomeUntil(survival.life);
		float worstLoss = 0.0f;
		for (const auto& blast : economyBlasts) {
			const float reach = EconomyBlastReach(blast, row);
			if (reach < 0.0f || (blast.committed && blast.ready < worker.spawnAt)) continue;
			float impact = std::max(0.0f, blast.ready - worker.spawnAt) + (blast.committed ? 0.0f : 1.0f);
			// 跟进延迟、减速和啃食也会改变进入爆区的时间，不再按固定速度外推。
			if (!blast.committed) while (impact < survival.life
				&& std::abs(survival.PositionAt(worker.spawnAt + impact) - blast.x) > reach)
				impact += kEscortForecastStep;
			if (impact >= survival.life || std::abs(survival.PositionAt(worker.spawnAt + impact) - blast.x) > reach) continue;
			const float safeIncome = incomeUntil(impact);
			int collateral = addedGuards;
			for (const auto& z : snapshot.zombies) {
				if (z.id == workerID || z.mindControlled || EconomyBlastReach(blast, z.row) < 0.0f) continue;
				const float projectedX = std::max(frontX[z.row], z.x - z.moveSpeed * (worker.spawnAt + impact));
				if (std::abs(projectedX - blast.x) <= EconomyBlastReach(blast, z.row)) ++collateral;
			}
			const float risk = blast.committed ? 1.0f
				: std::min(kMaximumBlastRisk, kUnusedBlastRisk + collateral * kCrowdedBlastRisk);
			worstLoss = std::max(worstLoss, (income - safeIncome) * risk);
		}
		if (blastLoss) *blastLoss = worstLoss;
		return income - worstLoss;
	};
	for (const auto& z : snapshot.zombies) {
		const auto* worker = dynamic_cast<const IceWorkerZombie*>(mEntityRegistry.GetZombie(z.id));
		if (!worker || z.mindControlled) continue;
		float cover = 0.0f;
		const float income = expectedIncome(z.row, makeMover(z, false), rowGuards[z.row],
			worker->GetIceRemaining(), worker->GetNextIceYield(), 0, z.id, nullptr, &cover);
		s.predictedProduction += income;
		workerValue[z.row] += income;
		++workers[z.row];
		// 老工人的产能只有在前排撑得住时可兑现，不能一味追加新工人。
		guardNeed[z.row] += std::max(0.0f, dps[z.row] * (25.0f - cover))
			* worker->GetNextIceYield() / IceProduction::MaximumYield;
	}
	const bool workerUnlocked = std::find(mSpawnZombieList.begin(), mSpawnZombieList.end(),
		ZombieType::ZOMBIE_ICE_WORKER) != mSpawnZombieList.end()
		&& isUnlocked(ZombieType::ZOMBIE_ICE_WORKER);
	ColdStorageStrategy::SplashField splashField;
	splashField.directDps = directDps;
	splashField.directSlowDuty = slowDuty;
	splashField.slowDuration = slowDuration;
	splashField.targetRight = SCENE_WIDTH;
	// 只有同路索敌、跨路溅射的西瓜会产生这里的引火外部损失；不能把三线射手也当成西瓜。
	for (const auto& p : snapshot.plants) {
		const Plant* plant = mEntityRegistry.GetPlant(p.id);
		if (!plant) continue;
		const auto type = plant->GetPlacementType();
		if (type != PlantType::PLANT_MELONPULT && type != PlantType::PLANT_WINTERMELON) continue;
		splashField.melonDps[p.row] += p.attackDps;
		splashField.melonSlowDuty[p.row] += p.slowApplicationsPerSecond * p.slowDuration;
		if (splashField.plantX[p.row] == 0 || p.x < splashField.plantX[p.row]) splashField.plantX[p.row] = p.x;
	}
	std::vector<ColdStorageStrategy::SplashUnit> formation;
	for (const auto& z : snapshot.zombies) {
		const Zombie* entity = mEntityRegistry.GetZombie(z.id);
		if (z.mindControlled || !entity || !entity->HasHead()) continue;
		const auto mover = makeMover(z, false);
		ColdStorageStrategy::SplashUnit u;
		u.row = z.row; u.x = mover.x; u.speed = mover.speed; u.health = mover.health;
		u.stopped = mover.stopped; u.eating = mover.eating; u.slow = mover.slow;
		u.slowFactor = mover.slowFactor; u.stopX = mover.stopX;
		u.boundsOffset = z.bounds.x - z.x; u.boundsWidth = z.bounds.width;
		u.canBeChilled = z.canBeChilled; u.slowImmunity = z.slowImmunityRemaining;
		u.value = static_cast<float>(GetZombieIceCost(entity->mZombieType));
		u.purchaseCost = u.value;
		u.smashSeconds = Assault(entity->mZombieType).smashSeconds;
		u.blastAnchorOffset = entity->GetPosition().x - z.x;
		u.economic = entity->mZombieType == ZombieType::ZOMBIE_ICE_WORKER;
		if (const auto* worker = dynamic_cast<const IceWorkerZombie*>(entity))
			u.value += expectedIncome(z.row, mover, rowGuards[z.row], worker->GetIceRemaining(), worker->GetNextIceYield(), 0, z.id, nullptr, nullptr);
		else if (z.x >= frontX[z.row] - 100.0f)
			u.value += kEscortIncomeShare * workerValue[z.row] * u.health / std::max(1.0f, escort[z.row]);
		formation.push_back(u);
	}
	// 待购单位沿用经营推演的速度/接触近似；每次付款后立即加入基线，后续选兵不会忽略队友。
	auto newSplashUnit = [&](ZombieType type, int row, float delay) {
		ColdStorageStrategy::SplashUnit u;
		u.row = row; u.x = SCENE_WIDTH + 40.0f;
		u.speed = kWorkerForecastSpeed * Assault(type).speed;
		u.health = Assault(type).health; u.value = static_cast<float>(GetZombieIceCost(type));
		u.purchaseCost = u.value;
		u.smashSeconds = Assault(type).smashSeconds;
		u.economic = type == ZombieType::ZOMBIE_ICE_WORKER;
		u.spawnAt = delay; u.stopX = frontX[row] + kForecastContactDistance;
		if (type == ZombieType::ZOMBIE_ICE_WORKER)
			u.value += IceProduction::Forecast(IceProduction::Interval, IceProduction::InitialYield, kEconomyHorizon);
		return u;
	};
	auto collateral = [&](const std::vector<ColdStorageStrategy::SplashUnit>& additions) {
		return ColdStorageStrategy::ForecastSplashExternality(splashField, formation, additions);
	};
	std::vector<ColdStorageStrategy::BlastThreat> blastThreats;
	for (const auto& blast : economyBlasts) {
		ColdStorageStrategy::BlastThreat threat;
		threat.x = blast.x; threat.ready = blast.ready; threat.damage = blast.damage;
		threat.committed = blast.committed;
		threat.usesObjectX = blast.type != PlantType::PLANT_DOOMSHROOM;
		threat.reach.fill(-1.0f);
		for (int row = 0; row < mRows; ++row) threat.reach[row] = EconomyBlastReach(blast, row);
		blastThreats.push_back(threat);
	}
	auto blastRisk = [&](const std::vector<ColdStorageStrategy::SplashUnit>& additions) {
		auto combined = formation;
		combined.insert(combined.end(), additions.begin(), additions.end());
		return ColdStorageStrategy::ForecastBlastRisk(combined, formation.size(), blastThreats, slowDuty, SCENE_WIDTH);
	};
	// 学习分支只搜索当前可以买到的自由序列；所有扣款/出生仍提交正式 Board 队列。
	if (const auto* weights = learnedWeights) {
		ColdStorageSearch::Snapshot search;
		const int requestedVersion = ColdStoragePolicy::SearchVersion();
		search.searchVersion = requestedVersion;
		search.productionCalibration = ColdStoragePolicy::ProductionModel();
		search.stateModel = ColdStoragePolicy::AdaptiveModel();
		search.netEconomy = ColdStoragePolicy::NetEconomy();
		search.anticipateEconomy = ColdStoragePolicy::AnticipateEconomy();
		search.opponentWeight = ColdStoragePolicy::OpponentWeight();
		search.sunIceValue = static_cast<float>(kLargeOrderIce)/kLargeOrderSun;
		auto plantCapital = [&](const Plant* entity) {
			const auto type = entity->GetPlacementType();
			const float price = GetPlantIceCost(type)+std::max(0,GameDataManager::GetInstance().GetPlantSunCost(type))*search.sunIceValue;
			return price*std::clamp(static_cast<float>(entity->mPlantHealth)/std::max(1,entity->mPlantMaxHealth),0.0f,1.0f);
		};
		search.noProgressSeconds = s.plantKillIdleSeconds;
		search.budget = s.enemyIce;
		search.recoveryReserve = ColdStorageState::RecoveryReserveIce;
		search.capacity = std::max(0, kMaxSimultaneous - GetColdStorageHostileCount() - static_cast<int>(s.pending.size()));
		// 观望时长不能把负收益方案变成必选项，否则成型防线会诱发周期性单兵送死。
		// 唯一例外是下方确有后续兵种可解锁、且能支付整条解锁路径的合法小额探路。
		search.allowWait = true;
		// 波次由付费派兵推进。只评当前卡池会在弱兵被克制时永久观望，错失下一档能力。
		// 解锁可能相隔不止一波；把到下一档的最低出兵成本算全，避免开局停在空白解锁波。
		s.unlockProbe = false;
		if (!allUnitsUnlocked && s.pending.empty() && GetColdStorageHostileCount() == 0) {
			int probeCost = kMaxIce;
			for (auto type : mSpawnZombieList)
				if (GameDataManager::GetInstance().GetZombieAppearWave(type) <= s.decisions + 1)
					probeCost = std::min(probeCost,GetZombieIceCost(type));
			if (probeCost > 0) for (auto type : mSpawnZombieList) {
				const int wavesNeeded = GameDataManager::GetInstance().GetZombieAppearWave(type) - (s.decisions + 1);
				if (wavesNeeded > 0 && static_cast<long long>(wavesNeeded)*probeCost + GetZombieIceCost(type)
					+ ColdStorageState::RecoveryReserveIce <= s.enemyIce) s.unlockProbe = true;
			}
			if (s.unlockProbe) search.allowWait = false;
		}
		search.rightEdge = SCENE_WIDTH;
		// 清洁车是共同战斗规则，不能因候选搜索版本不同而从局面中消失。
		for (int id : mEntityRegistry.GetAllMowerIDs()) {
			const Mower* mower = mEntityRegistry.GetMower(id);
			if (!mower || !mower->IsActive()) continue;
			const auto bounds = mower->GetColliderComponent()->GetBoundingBox();
			search.mowers.push_back({mower->mRow,bounds.x,bounds.w,mower->mSpeed,mower->mState == MowerState::MOVING});
		}
		search.houseX = GetCellCenterPosition(0, 0).x - 120;
		search.playerSun = mSun; search.playerIce = s.playerIce;
		search.playerSunLimit = MAX_SUN; search.playerIceLimit = kMaxIce;
		search.incomingIce = s.orderIce; search.incomingIceAt = s.orderRemaining;
		// 只从真实卡槽和当前合法格读取周转能力；不偷看陪练策略，也不给正式玩家免费资源。
		if (search.anticipateEconomy) {
			search.shop = {{kSmallOrderSun,kSmallOrderIce,kSmallOrderDelay},{kLargeOrderSun,kLargeOrderIce,kLargeOrderDelay}};
			if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
				if (!card || card->GetGameplayPlantType() != PlantType::PLANT_MARIGOLD || card->GetSunCost() >= 0) continue;
				ColdStorageSearch::SunExchange exchange;
				exchange.sunGain = -card->GetSunCost(); exchange.iceCost = GetPlantIceCost(card->GetGameplayPlantType());
				exchange.ready = card->GetCooldownTimer(); exchange.recharge = card->GetCooldownTime();
				for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col)
					if (CanPlantAt(card->GetGameplayPlantType(),row,col)) exchange.cells.push_back({row,col});
				if (!exchange.cells.empty()) search.exchanges.push_back(std::move(exchange));
			}
		}
		// 一个卡槽只代表一张可用反制牌，合法格位是替代落点，不能凭空复制次数。
		int source = 0;
		auto addCounter = [&](const EconomyBlast& blast, int id, int sun, int ice, float recharge, bool targeted) {
			ColdStorageSearch::Counter counter;
			counter.source = id; counter.sunCost = sun; counter.iceCost = ice;
			counter.recharge = recharge; counter.targeted = targeted;
			counter.windup = blast.type == PlantType::PLANT_COBCANNON ? 4.0f : targeted ? 1.7f : 1.0f;
			counter.blast.x = blast.x; counter.blast.ready = blast.ready; counter.blast.damage = blast.damage;
			counter.blast.committed = blast.committed;
			counter.blast.usesObjectX = blast.type != PlantType::PLANT_DOOMSHROOM;
			counter.blast.reach.fill(-1);
			for (int row = 0; row < mRows; ++row) counter.blast.reach[row] = EconomyBlastReach(blast,row);
			search.counters.push_back(counter);
		};
		if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
			if (!card) continue;
			const auto type = card->GetGameplayPlantType();
			if (!IsInstantBlast(type) && type != PlantType::PLANT_SQUASH) continue;
			const bool doom = type == PlantType::PLANT_DOOMSHROOM;
			if (doom && !hasCoffee) continue;
			const int id = source++;
			for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col) if (CanPlantAt(type,row,col)) {
				addCounter({type,row,GetCellCenterPosition(row,col).x,std::max(card->GetCooldownTimer(),doom ? coffeeWait : 0.0f),false},
					id,card->GetSunCost() + (doom ? coffeeSun : 0),GetPlantIceCost(type) + (doom ? GetPlantIceCost(PlantType::PLANT_INSTANT_COFFEE) : 0),
					card->GetCooldownTime(),type == PlantType::PLANT_SQUASH);
				if (ColdStoragePolicy::AnticipateBuilding()) {
					search.counters.back().cellRow = row; search.counters.back().cellColumn = col;
				}
			}
		}
		// 从当前实战卡槽采集可能的建设，不读取陪练脚本或未来随机结果。
		// 初版只表示单格普通株/外壳及曙光莲；不假造紫卡前置株、地面陷阱、累计配额或循环铲种。
		if (ColdStoragePolicy::AnticipateBuilding() && mCardSlotManager) {
			int source = 0;
			for (const Card* card : mCardSlotManager->GetCards()) {
				if (!card) continue;
				const auto type = card->GetGameplayPlantType();
				const auto& profile = GameDataManager::GetInstance().GetPlantSimulationProfile(type);
				if (!profile.persistent || !profile.futurePlantable || profile.supportOnly || IsUpgradePlantType(type)
					|| type == PlantType::PLANT_ELITE_SCAREDYSHROOM || type == PlantType::PLANT_SPIKEWEED
					|| card->GetSunCost() < 0 || (profile.daytimeDormant && !GameAPP::GetInstance().GetBackgroundIsNight(mBackGround))) continue;
				const bool lotus = type == PlantType::PLANT_DAWNLOTUS;
				if (!lotus && profile.attackDps <= 0 && profile.sunPerSecond <= 0 && profile.baseHealth < 1000) continue;
				const int id = source++;
				for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col) if (CanPlantAt(type,row,col)) {
					ColdStorageSearch::Construction future;
					future.source = id; future.sunCost = card->GetSunCost(); future.iceCost = GetPlantIceCost(type);
					future.ready = card->GetCooldownTimer(); future.recharge = card->GetCooldownTime();
					future.firstSunDelay = profile.firstSunDelay;
					auto& p = future.plant;
					p.row = row; p.column = col; p.x = GetCellCenterPosition(row,col).x;
					p.layer = type == PlantType::PLANT_PUMPKINSHELL ? 2 : 1;
					p.health = static_cast<float>(profile.baseHealth); p.dps = profile.attackDps; p.sunPerSecond = profile.sunPerSecond;
					p.assetValue = future.iceCost+future.sunCost*search.sunIceValue;
					// 新版完整预测这笔交易：玩家付费造出且随后被消灭，才在推演中结算预期返冰。
					// 这不会提前增加实际余额或允许预支购买；旧配置保留原来的零收益近似。
					p.reward = search.searchVersion == 2 ? static_cast<float>(PlantKillIce(GetPlantIceCost(type),s.difficulty)) : 0;
					p.rowRadius = profile.attackRowRadius; p.multiTarget = profile.mineMultiTarget;
					p.around = profile.mineAttackShape == 2;
					p.range = CELL_COLLIDER_SIZE_X*(p.around ? 1.5f : static_cast<float>(profile.mineAttackRange));
					p.melon = type == PlantType::PLANT_MELONPULT;
					p.slowRate = profile.slowApplicationsPerSecond; p.slowDuration = profile.slowDuration;
					p.stopDuty = profile.frozenApplicationsPerSecond*profile.frozenDuration + profile.butterApplicationsPerSecond*profile.butterDuration;
					if (lotus) {
						future.strike.ready = future.strike.recharge = DawnLotusRules::MaxEnergy/DawnLotusRules::NormalEnergyRate;
						future.strike.damage = DawnLotusRules::Damage; future.strike.splashDamage = DawnLotusRules::SplashDamage;
						future.strike.radius = CELL_COLLIDER_SIZE_X*DawnLotusRules::SplashRadiusCells;
					}
					search.construction.push_back(future);
				}
			}
		}
		for (const auto& p : snapshot.plants) {
			const Plant* entity = mEntityRegistry.GetPlant(p.id);
			if (!entity) continue;
			if (const auto* lotus = dynamic_cast<const DawnLotus*>(entity)) {
				if (!lotus->IsShutdown() && !lotus->IsActionPaused() && !lotus->IsBungeeTargeted())
					search.rowStrikes.push_back({p.id,lotus->GetChargeSecondsRemaining(),
						DawnLotusRules::MaxEnergy/lotus->GetEnergyRate(),static_cast<float>(DawnLotusRules::Damage),
						static_cast<float>(DawnLotusRules::SplashDamage),CELL_COLLIDER_SIZE_X*DawnLotusRules::SplashRadiusCells});
			}
			if (const auto* squash = dynamic_cast<const Squash*>(entity)) {
				if (squash->HasAppliedDamage()) continue;
				const Zombie* target = mEntityRegistry.GetZombie(squash->GetTargetZombieID());
				addCounter({PlantType::PLANT_SQUASH,p.row,target ? target->GetPosition().x : p.x,target ? 1.0f : 0.0f,target != nullptr},source++,0,0,10000,target == nullptr);
			} else if (IsInstantBlast(entity->GetPlacementType())) {
				if (!entity->GetSleepState()) addCounter({entity->GetPlacementType(),p.row,p.x,1,true},source++,0,0,10000,false);
				else if (hasCoffee) addCounter({entity->GetPlacementType(),p.row,p.x,coffeeWait,false},source++,coffeeSun,GetPlantIceCost(PlantType::PLANT_INSTANT_COFFEE),10000,false);
			} else if (p.cobBlastDamage > 0) {
				const int id = source++;
				for (const auto& cell : snapshot.cells) addCounter({PlantType::PLANT_COBCANNON,cell.row,cell.x,p.abilityCooldownRemaining,false,p.cobBlastRadius,p.cobBlastRowRadius,p.cobBlastDamage},id,0,0,p.cobBlastCooldown,false);
			}
		}
		for (const auto& blast : snapshot.pendingCobBlasts)
			addCounter({PlantType::PLANT_COBCANNON,blast.targetRow,blast.x,blast.resolveSeconds,true,blast.radius,blast.rowRadius,blast.damage},source++,0,0,10000,false);
		for (const auto& body : formation) search.current.push_back({body});
		// 生产进度使用同一实体，不以成熟工人的默认第一批代替实际收入。
		size_t index = 0;
		for (const auto& z : snapshot.zombies) {
			const Zombie* entity = mEntityRegistry.GetZombie(z.id);
			if (z.mindControlled || !entity || !entity->HasHead()) continue;
			auto& unit = search.current[index++];
			unit.id = z.id;
			unit.biteDps = entity->GetMineSimulationAttackDps();
			unit.mowerImmune = !entity->CanBeKilledByMower(); unit.consumesOtherMowers = entity->ConsumesOtherMowersOnContact();
			if (const auto* giant = dynamic_cast<const GargantuarZombie*>(entity); giant && giant->HasImp() && !giant->HasReleasedImp()) {
				unit.throwHealth = entity->mBodyMaxHealth*.5f;
				unit.throwAnchorX = GetCellCenterPosition(z.row,std::min(5,mColumns-1)).x;
			}
			if (const auto paid = s.refundableCosts.find(z.id); paid != s.refundableCosts.end())
				unit.playerRefund = static_cast<float>(paid->second * 3 / 4);
			if (const auto* worker = dynamic_cast<const IceWorkerZombie*>(entity)) {
				unit.productionRemaining = worker->GetIceRemaining(); unit.nextYield = worker->GetNextIceYield();
				unit.productionStopHealth = entity->mBodyMaxHealth / 3;
			}
		}
		for (const auto& p : snapshot.plants) {
			ColdStorageSearch::Plant plant;
			plant.row = p.row; plant.column = p.column; plant.layer = p.eatingLayerPriority;
			plant.id = p.id;
			plant.x = p.x; plant.health = p.health; plant.dps = p.attackDps;
			plant.sunPerSecond = p.sunPerSecond;
			plant.rowRadius = p.attackRowRadius; plant.edible = p.canBeEaten;
			plant.slowRate = p.slowApplicationsPerSecond; plant.slowDuration = p.slowDuration;
			plant.stopDuty = p.frozenApplicationsPerSecond * p.frozenDuration + p.butterApplicationsPerSecond * p.butterDuration;
			if (const Plant* entity = mEntityRegistry.GetPlant(p.id)) {
				const auto type = entity->GetPlacementType();
				if (IsInstantBlast(type) || type == PlantType::PLANT_SQUASH) continue;
				plant.reward = static_cast<float>(PlantKillIce(GetPlantIceCost(type), s.difficulty));
				plant.assetValue = plantCapital(entity);
				const auto& profile = GameDataManager::GetInstance().GetPlantSimulationProfile(type);
				plant.multiTarget = profile.mineMultiTarget;
				plant.around = profile.mineAttackShape == 2;
				plant.range = static_cast<float>(CELL_COLLIDER_SIZE_X) * (plant.around ? 1.5f : static_cast<float>(profile.mineAttackRange));
				plant.melon = type == PlantType::PLANT_MELONPULT || type == PlantType::PLANT_WINTERMELON;
			}
			search.plants.push_back(plant);
		}
		// 支撑层同样会阻挡、受击和产生返冰，不能在预测中凭空消失。
		for (const auto& p : snapshot.supports) {
			ColdStorageSearch::Plant plant;
			plant.row = p.row; plant.column = p.column; plant.layer = 0;
			plant.x = p.x; plant.health = p.health; plant.edible = p.canBeEaten;
			if (const Plant* entity = mEntityRegistry.GetPlant(p.id)) {
				plant.reward = static_cast<float>(PlantKillIce(GetPlantIceCost(entity->GetPlacementType()), s.difficulty));
				plant.assetValue = plantCapital(entity);
			}
			search.plants.push_back(plant);
		}
		// 新购与已付款单位共用能力投影；已有队列的成交价不能被当前价格覆盖。
		auto purchaseUnit = [&](ZombieType type, int row, int cost, float delay) {
			ColdStorageSearch::Unit unit;
			unit.body = newSplashUnit(type,row,delay);
			unit.body.purchaseCost = static_cast<float>(cost);
			unit.playerRefund = static_cast<float>(cost * 3 / 4);
			unit.mowerImmune = type == ZombieType::ZOMBIE_ROOF_MARSHAL;
			unit.consumesOtherMowers = type == ZombieType::ZOMBIE_ELITE_DANCER;
			if (type == ZombieType::ZOMBIE_GARGANTUAR || type == ZombieType::ZOMBIE_REDEYE_GARGANTUAR) {
				unit.throwHealth = unit.body.health*.5f;
				unit.throwAnchorX = GetCellCenterPosition(row,std::min(5,mColumns-1)).x;
			}
			return unit;
		};
		for (const auto& paid : s.pending) {
			ColdStorageSearch::CommittedUnit committed;
			committed.unit = static_cast<int>(search.current.size());
			for (int row = 0; row < mRows; ++row) committed.legalRows[row] = IsSpawnRowCompatible(paid.type,row);
			search.committed.push_back(committed);
			search.current.push_back(purchaseUnit(paid.type,paid.row,paid.cost,paid.remaining));
		}
		for (ZombieType type : mSpawnZombieList) {
			if (!isUnlocked(type)) continue;
			// 特殊能力的收益由真实对局训练的局势偏好补充，不排除支援或绕后兵种。
			for (int row = 0; row < mRows; ++row) if (IsSpawnRowCompatible(type, row)) {
				ColdStorageSearch::Option option;
				option.type = static_cast<int>(type); option.row = row; option.cost = GetZombieIceCost(type);
				option.unit = purchaseUnit(type,row,option.cost,0);
				option.preference = ColdStoragePolicy::UnitPreference(type);
				search.options.push_back(option);
			}
		}
		for (int row = 0; row < mRows; ++row) {
			auto& context = search.context[row]; context[0] = 1;
			float maximum = 0, health = 0;
			for (const auto& z : snapshot.zombies) if (!z.mindControlled && z.row == row) {
				maximum += z.bodyMaxHealth + z.helmMaxHealth + z.shieldMaxHealth;
				health += z.bodyHealth + z.helmHealth + z.shieldHealth;
				context[2] += 1.0f / 3;
				const Zombie* entity = mEntityRegistry.GetZombie(z.id);
				if (entity && entity->mZombieType == ZombieType::ZOMBIE_ICE_WORKER) context[6] += 1;
			}
			context[1] = maximum > 0 ? 1 - health / maximum : 0;
			context[3] = frontHealth[row] / 4000;
			context[4] = std::clamp(slowDuty[row], 0.0f, 1.0f);
			context[5] = directDps[row] / 60;
			for (const auto& p : snapshot.plants) if (p.row == row && p.column <= 2 && p.pumpkinShell)
				context[7] += p.health / 4000;
		}
		const auto seed = 0xC01D1234u + static_cast<unsigned>(s.decisions * 31) + static_cast<unsigned>(s.elapsed);
		const size_t paidCount = s.pending.size();
		const auto revision = ColdStorageSearch::ReplanCommitted(search,*weights,seed ^ 0x91A7u);
		s.searchCommittedCount = static_cast<int>(paidCount);
		s.searchQueueEvaluated = revision.evaluated; s.searchQueueChanged = revision.changed;
		s.searchQueueBeforeScore = revision.beforeScore; s.searchQueueAfterScore = revision.afterScore;
		for (size_t i = 0; i < paidCount; ++i) {
			const auto& body = search.current[search.committed[i].unit].body;
			s.pending[i].row = body.row;
			s.pending[i].remaining = std::min(s.pending[i].remaining,body.spawnAt);
		}
		auto result = ColdStorageSearch::Search(search, *weights,seed);
		result.expandedForecast |= requestedVersion == 1 && !search.committed.empty();
		s.commanderStrategy = "learned_search";
		s.commanderMode = result.regrouping ? "regroup" : result.actions.empty() ? "observe" : s.unlockProbe ? "unlock" : "search";
		s.commanderBudget = search.budget; s.candidatesEvaluated = result.evaluated;
		s.lastBestScore = result.score; s.searchPreferenceScore = result.preferenceScore;
		s.searchOpponentAssets = result.opponentAssets; s.searchBaselineOpponentAssets = result.baselineOpponentAssets;
		s.searchOpponentWeight = search.opponentWeight; s.searchOpponentScore = result.opponentScore;
		s.searchAnticipateEconomy = search.anticipateEconomy;
		s.searchExchangeCards = static_cast<int>(search.exchanges.size());
		s.searchExchanges = result.construction.exchanges; s.searchOrders = result.construction.orders;
		s.searchExchangeSun = result.construction.exchangeSun; s.searchExchangeIce = result.construction.exchangeIce;
		s.searchOrderSun = result.construction.orderSun; s.searchOrderIce = result.construction.orderIce;
		s.searchPendingIce = result.construction.pendingIce;
		s.searchStateInputs = result.stateInputs; s.searchEffectiveWeights = result.effectiveWeights;
		s.searchVersion = requestedVersion; s.searchLargestPlan = result.largestPlan;
		s.searchAdaptive = search.stateModel != nullptr;
		s.searchExpandedForecast = result.expandedForecast;
		s.searchNetEconomy = search.netEconomy;
		s.searchAnticipateBuilding = ColdStoragePolicy::AnticipateBuilding();
		s.searchConstructionOptions = static_cast<int>(search.construction.size());
		s.searchPredictedPlantings = result.construction.planted;
		s.searchFeatures = result.features; s.searchBaselineFeatures = result.baselineFeatures; ++s.searchSerial;
		s.searchElapsed = s.elapsed; s.searchRawProduction = result.rawProduction;
		s.searchCounterHoldSeconds = result.counterHoldSeconds;
		s.searchRowStrikeCount = static_cast<int>(search.rowStrikes.size());
		s.searchFormationBaseScore = result.formationBaseScore; s.searchFormationScores = result.formationScores;
		s.searchFormationTested = result.formationTested; s.searchFormationRejected = result.formationRejected;
		s.searchFormationChosenRow = result.formationChosenRow;
		s.searchProductionInputs = result.productionInputs;
		s.formationBlastLoss = result.blastLoss;
		s.predictedProduction = result.features[4]; s.predictedKillIncome = result.features[0];
		const int before = s.enemyIce;
		for (const auto& action : result.actions) {
			const auto& option = search.options[action.option];
			if (QueueColdStorageZombie(static_cast<ZombieType>(option.type), option.row, action.delay)) {
				s.commanderFocusRow = option.row;
				if (option.unit.body.economic) ++s.economicFollowups;
			}
		}
		s.commanderSpent = before - s.enemyIce; s.commanderReserve = s.enemyIce;
		s.attackDeferred = true; // 队列兑现期间也定期观察；灰烬、前排损失与新收入都会进入下一次快照。
		if (s.pending.size() > paidCount) {
			mCurrentWave = ++s.decisions; s.dispatchQuietSeconds = 0;
			for (size_t i = paidCount; i < s.pending.size(); ++i) s.pending[i].wave = s.decisions;
		}
		return;
	}
	// 对每条路线比较裸投与各个已解锁护卫，护卫是否值得买由净收益决定。
	// 普通僵尸也可作早期护卫；不改其他关卡的兵种解锁波数，不强制先凑重甲血量。
	std::array<ZombieType, 6> economyGuards;
	economyGuards.fill(ZombieType::NUM_ZOMBIE_TYPES);
	for (int row = 0; row < mRows; ++row) {
		investment[row] = -1000.0f;
		auto evaluateGuard = [&](ZombieType guard, float entryDelay) {
			const bool buyingGuard = guard != ZombieType::NUM_ZOMBIE_TYPES;
			const int charge = buyingGuard ? GetZombieIceCost(guard) : 0;
			if (!workerUnlocked || charge + IceProduction::WorkerCost > s.enemyIce) return;
			auto guards = rowGuards[row];
			EconomyMover worker;
			worker.x = SCENE_WIDTH + 40.0f;
			worker.speed = kWorkerForecastSpeed;
			worker.health = IceProduction::WorkerHealth;
			worker.spawnAt = 1.0f + (buyingGuard ? kDeploySpacing + entryDelay : 0.0f);
			worker.stopX = frontX[row] + kForecastContactDistance;
			if (buyingGuard) {
				EconomyMover newGuard = worker;
				newGuard.speed *= Assault(guard).speed;
				newGuard.health = Assault(guard).health;
				newGuard.spawnAt = 1.0f;
				newGuard.futureSlowFactor = 1.0f - (1.0f - newGuard.slowFactor) * std::clamp(slowDuty[row], 0.0f, 1.0f);
				guards.push_back(newGuard);
			}
			float loss = 0.0f, cover = 0.0f;
			const float income = expectedIncome(row, worker, guards, IceProduction::Interval,
				IceProduction::InitialYield, buyingGuard ? 1 : 0, -1, &loss, &cover);
			std::vector<ColdStorageStrategy::SplashUnit> additions{newSplashUnit(ZombieType::ZOMBIE_ICE_WORKER, row, worker.spawnAt)};
			if (buyingGuard) additions.push_back(newSplashUnit(guard, row, 1.0f));
			const float net = income - IceProduction::WorkerCost - charge
				- workers[row] * (6.0f + directSplash[row] * 0.05f) - std::max(0.0f, collateral(additions));
			if (net > investment[row]) {
				investment[row] = net;
				economyGuards[row] = guard;
				s.economyBlastLossByRow[row] = loss;
				s.economyGuardCostByRow[row] = charge;
				s.economyEntryDelayByRow[row] = entryDelay;
				s.economyCoverByRow[row] = cover;
			}
		};
		evaluateGuard(ZombieType::NUM_ZOMBIE_TYPES, 0.0f);
		for (ZombieType type : mSpawnZombieList) {
			const auto profile = Assault(type);
			if (type == ZombieType::ZOMBIE_ICE_WORKER || profile.support || profile.bypass
				|| !IsSpawnRowCompatible(type, row)
				|| (s.elapsed < 80 && GetZombieIceCost(type) >= 8)
				|| !isUnlocked(type)) continue;
			evaluateGuard(type, kWorkerEntryDelay);
			evaluateGuard(type, kWorkerLateEntryDelay);
		}
		s.economyNetByRow[row] = investment[row];
		if (investment[row] > s.economyValue) { s.economyValue = investment[row]; s.economyRow = row; }
	}


	// 战略层按实战目标读取模仿者，不把其代理卡身份当成没有生产/攻击能力。
	std::vector<PlantDefenseMonteCarlo::CardSnapshot> growthCards;
	if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
		if (!card) continue;
		const auto type = card->GetGameplayPlantType();
		const auto& profile = GameDataManager::GetInstance().GetPlantSimulationProfile(type);
		if (card->GetSunCost() >= 0 && (profile.attackDps <= 0.0f || !profile.persistent || profile.daytimeDormant)) continue;
		PlantDefenseMonteCarlo::CardSnapshot future;
		future.typeKey = static_cast<int>(type);
		future.cost = card->GetSunCost();
		future.cooldownRemaining = card->GetCooldownTimer();
		future.cooldownTime = card->GetCooldownTime();
		future.attackDps = profile.attackDps;
		for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col)
			if (CanPlantAt(type, row, col)) future.legalCellMask |= std::uint64_t{1} << (row * mColumns + col);
		if (future.legalCellMask) growthCards.push_back(future);
	}
	// 只按当前卡槽、冷却和资源估计补阵能力；双金盏花是两张独立经济卡，共享冰块预算。
	float futureSun = static_cast<float>(mSun), growthIce = static_cast<float>(s.playerIce + s.orderIce) + playerProduction;
	for (const auto& plant : snapshot.plants)
		if (!dynamic_cast<const IceMint*>(mEntityRegistry.GetPlant(plant.id)))
			futureSun += std::max(0.0f, plant.sunPerSecond) * kEconomyHorizon;
	auto cardUses = [&](const auto& card) {
		return card.cooldownRemaining > kEconomyHorizon ? 0
			: 1 + static_cast<int>((kEconomyHorizon - card.cooldownRemaining) / std::max(1.0f, card.cooldownTime));
	};
	for (const auto& card : growthCards) if (card.cost < 0) {
		const int iceCost = std::max(1, GetPlantIceCost(static_cast<PlantType>(card.typeKey)));
		const int uses = std::min(cardUses(card), static_cast<int>(growthIce) / iceCost);
		futureSun -= card.cost * uses;
		growthIce -= iceCost * uses;
	}
	std::vector<const PlantDefenseMonteCarlo::CardSnapshot*> attacks;
	for (const auto& card : growthCards) if (card.attackDps > 0 && card.cost > 0) attacks.push_back(&card);
	std::stable_sort(attacks.begin(), attacks.end(), [](const auto* a, const auto* b) {
		return a->attackDps / a->cost > b->attackDps / b->cost;
	});
	std::uint64_t builtCells = 0;
	s.playerGrowthDps = 0.0f;
	for (const auto* card : attacks) {
		const int iceCost = GetPlantIceCost(static_cast<PlantType>(card->typeKey));
		for (int use = 0; use < cardUses(*card) && futureSun >= card->cost && growthIce >= iceCost; ++use) {
			int bestCell = -1;
			float gain = 0.0f;
			for (int cell = 0; cell < mRows * mColumns; ++cell) {
				const auto bit = std::uint64_t{1} << cell;
				if (!(card->legalCellMask & bit) || (builtCells & bit)) continue;
				float oldDps = 0.0f;
				for (const auto& plant : snapshot.plants)
					if (plant.row * mColumns + plant.column == cell) oldDps += plant.attackDps;
				if (card->attackDps - oldDps > gain) { gain = card->attackDps - oldDps; bestCell = cell; }
			}
			if (bestCell < 0) break;
			builtCells |= std::uint64_t{1} << bestCell;
			futureSun -= card->cost;
			growthIce -= iceCost;
			s.playerGrowthDps += gain;
		}
	}

	const int stage = MiniGame::IsBrawl(mLevel) ? 9
		: std::clamp(AdventureProgression::GetLevelNumberInArea(mLevel), 1, 9);
	const int assaultCap = stage <= 5 ? kAssaultEarlyBudget : kAssaultLateBudget;
	const int availableSlots = std::max(0, kMaxSimultaneous - GetColdStorageHostileCount());
	struct RaidChoice {
		ZombieType type = ZombieType::NUM_ZOMBIE_TYPES;
		ColdStorageStrategy::RaidResult forecast;
		float score = -100000.0f;
		int count = 0, cost = 0;
		std::vector<ZombieType> troops;
	};
	std::array<RaidChoice, 6> raids;
	int raidRow = -1;
	float bestRaidScore = -100000.0f, totalDirectDps = 0.0f;
	for (int row = 0; row < mRows; ++row) {
		totalDirectDps += directDps[row];
		ColdStorageStrategy::Raid raid;
		for (int col = mColumns - 1; col >= 0; --col) {
			ColdStorageStrategy::Target target;
			target.x = GetCellCenterPosition(row, col).x;
			for (const auto& plant : snapshot.plants) if (plant.row == row && plant.column == col && plant.canBeEaten) {
				target.health += plant.health;
				target.attackDps += plant.attackDps;
				if (const Plant* entity = mEntityRegistry.GetPlant(plant.id))
					target.reward += PlantKillIce(GetPlantIceCost(entity->GetPlacementType()), s.difficulty);
			}
			if (target.health > 0.0f) raid.targets.push_back(target);
		}
		raid.spawnX = SCENE_WIDTH + 40.0f;
		raid.guardHealth = escort[row];
		for (const auto& unit : formation) if (unit.row == row && unit.smashSeconds > 0 && unit.x >= frontX[row] - 100.0f) {
			raid.guardSmashSeconds = unit.smashSeconds;
			raid.guardSpeed = unit.speed;
		}
		raid.directDps = directDps[row] + std::max(0.0f, dps[row] - directDps[row]) / 3.0f;
		raid.splashDps = directSplash[row];
		raid.slowFactor = 1.0f - 0.7f * std::clamp(slowDuty[row], 0.0f, 1.0f);
		auto evaluateRaid = [&](const std::vector<ZombieType>& troops) {
			raid.members.clear();
			std::vector<ColdStorageStrategy::SplashUnit> additions;
			int cost = 0;
			for (size_t i = 0; i < troops.size(); ++i) {
				const auto profile = Assault(troops[i]);
				const int price = GetZombieIceCost(troops[i]);
				cost += price;
				raid.members.push_back({profile.health, kWorkerForecastSpeed * profile.speed, static_cast<float>(price), profile.smashSeconds});
				additions.push_back(newSplashUnit(troops[i], row, 1.0f + i * kDeploySpacing));
			}
			if (cost > std::min(s.enemyIce, assaultCap)) return;
			const auto risk = blastRisk(additions);
			raid.blastTime = risk.time; raid.guardBlastDamage = 0;
			raid.blastDamage.assign(troops.size(), 0);
			if (!risk.damageByUnit.empty()) {
				for (size_t i = 0; i < formation.size(); ++i)
					if (formation[i].row == row && !formation[i].economic) raid.guardBlastDamage += std::min(formation[i].health, risk.damageByUnit[i]);
				for (size_t i = 0; i < troops.size(); ++i) raid.blastDamage[i] = risk.damageByUnit[formation.size() + i];
			}
			auto forecast = ColdStorageStrategy::ForecastRaid(raid);
			forecast.net -= std::max(0.0f, collateral(additions));
			const float score = forecast.net + (forecast.cellsBroken > 0 ? forecast.cellsBroken * 60.0f : -1000.0f)
				+ std::min(24.0f, forecast.remainingHealth / 500.0f);
			if (score > raids[row].score)
				raids[row] = {troops.front(), forecast, score, static_cast<int>(troops.size()), cost, troops};
		};
		for (ZombieType type : mSpawnZombieList) {
			const auto profile = Assault(type);
			if (profile.support || profile.bypass || !IsSpawnRowCompatible(type, row)
				|| (s.elapsed < 80 && GetZombieIceCost(type) >= 8)
				|| !isUnlocked(type)) continue;
			const int maxCount = std::min({stage <= 5 ? 12 : 16, availableSlots,
				std::min(s.enemyIce, assaultCap) / GetZombieIceCost(type)});
			for (int count = 1; count <= maxCount; ++count) {
				evaluateRaid(std::vector<ZombieType>(count, type));
				// 同时比较破障在前、快兵在后的组合，避免只能在纯巨人与纯橄榄中二选一。
				if (type == ZombieType::ZOMBIE_FOOTBALL && count >= 3) {
					const auto heavy = ZombieType::ZOMBIE_GARGANTUAR;
					if (std::find(mSpawnZombieList.begin(), mSpawnZombieList.end(), heavy) != mSpawnZombieList.end()
						&& IsSpawnRowCompatible(heavy, row) && isUnlocked(heavy)) {
						auto mixed = std::vector<ZombieType>(count, type);
						mixed[0] = mixed[1] = heavy;
						evaluateRaid(mixed);
					}
				}
			}
		}
		s.raidNetByRow[row] = raids[row].forecast.net;
		if (raids[row].score > bestRaidScore) { bestRaidScore = raids[row].score; raidRow = row; }
	}
	ColdStorageStrategy::Situation situation;
	situation.rows = mRows;
	situation.defenseDps = totalDirectDps;
	situation.defenseHealth = totalArmor;
	situation.playerGrowthDps = s.playerGrowthDps;
	situation.productionIncome = s.predictedProduction;
	situation.investmentNet = s.economyValue;
	if (raidRow >= 0) {
		situation.raidNet = raids[raidRow].forecast.net;
		situation.raidCellsBroken = raids[raidRow].forecast.cellsBroken;
		s.predictedKillIncome = raids[raidRow].forecast.income;
	}
	const auto policy = ColdStorageStrategy::Choose(situation);
	s.commanderStrategy = policy.name;
	s.spendingHorizon = policy.spendingHorizon;
	int reserve = s.enemyIce < 64 ? 0 : static_cast<int>(std::min(s.enemyIce / 3.0f,
		std::clamp(s.initialEnemyIce * kReserveFraction, 24.0f, 120.0f)));
	if (policy.racePlayer) reserve /= 3;
	const int normalCap = stage <= 5 ? kNormalEarlyBudget : kNormalLateBudget;
	const float forecastIncome = s.predictedProduction + (policy.racePlayer ? s.predictedKillIncome : 0.0f);
	const int baseBudget = std::clamp(static_cast<int>(std::ceil((s.enemyIce / policy.spendingHorizon
		+ static_cast<float>(kSupplyIce) / kSupplySeconds + forecastIncome / kEconomyHorizon)
		* DecisionInterval(s.elapsed, policy.racePlayer))), s.elapsed < 120 ? 8 : 16, normalCap);
	int budget = baseBudget;
	int slots = s.elapsed < 120 ? 2 : s.elapsed < 240 ? 4 : (stage <= 5 ? 8 : 11);

	float bestOpportunity = -1.0f;
	int focus = -1;
	for (int row = 0; row < mRows; ++row) {
		// 有前锋且屏障薄弱才值得总攻；空场并不是花光库存的理由。
		if (escort[row] < kSupportHealthRequired || frontlineCount[row] < 2) continue;
		const float pressure = escort[row] / std::max(1000.0f, dps[row] * kForecastHorizon + frontArmor[row] * 0.5f);
		const float opportunity = pressure + std::max(0.0f, 2.0f - frontArmor[row] / 2000.0f);
		if (opportunity > bestOpportunity) { bestOpportunity = opportunity; focus = row; }
	}
	const bool saturated = GetColdStorageHostileCount() >= 12
		&& totalHealth > totalDps * kForecastHorizon + totalArmor * 0.6f + 4000.0f;
	if (((likelyBreach && !blastSoon) || saturated) && s.dispatchQuietSeconds < kMaxObserveSeconds) {
		s.commanderMode = "observe";
		budget = 0;
	}
	else if (focus >= 0 && bestOpportunity >= kAssaultOpportunityThreshold && !blastSoon
		&& s.elapsed >= 120 && s.assaultCooldown <= 0.0f) {
		const float fastestArrival = (static_cast<float>(SCENE_WIDTH) + 40.0f - frontX[focus]) / 40.0f + 4.0f;
		if (responseWindow > fastestArrival) {
			s.commanderMode = "assault";
			budget = std::min(assaultCap, std::max(96, baseBudget * 2));
			slots = stage <= 5 ? 12 : 16;
			reserve /= 2;
			s.commanderFocusRow = focus;
		}
	}
	// 玩家发展更快或需要集中攻坚时，可主动组织整批进攻；不再要求已有两只前锋才敢发起。
	if (s.commanderMode != "observe" && (policy.racePlayer || policy.siege) && raidRow >= 0
		&& raids[raidRow].forecast.cellsBroken > 0 && s.elapsed >= 80.0f && s.assaultCooldown <= 0.0f) {
		s.commanderMode = "assault";
		s.commanderFocusRow = raidRow;
		budget = std::min(assaultCap, std::max({baseBudget * 2, raids[raidRow].cost, policy.siege ? 96 : 128}));
		slots = stage <= 5 ? 12 : 16;
		reserve = policy.racePlayer ? 0 : reserve / 2;
	}
	if (s.commanderMode == "pressure" && blastSoon) {
		s.commanderMode = "probe";
		budget = std::max(8, baseBudget * 2 / 3);
		slots = std::min(slots, 3);
	}
	// 有明确突破机会时优先猛攻；否则比较可兑现的经济收益与玩家扩张速度。
	// 生产收入已经进入基础预算，不会因为赚到更多冰仍只按固定补给花钱。
	if (workerUnlocked && s.commanderMode != "assault" && s.commanderMode != "observe"
		&& !policy.racePlayer && !policy.siege
		&& s.economyRow >= 0 && s.economyValue > kInvestmentMargin) {
		s.commanderMode = "economy";
		s.commanderFocusRow = s.economyRow;
		const int packageCost = IceProduction::WorkerCost + s.economyGuardCostByRow[s.economyRow];
		reserve = std::min(reserve, std::max(0, s.enemyIce - packageCost));
		budget = std::min(normalCap, std::max(baseBudget, packageCost));
		slots = std::max(slots, 2);
	}
	if (s.commanderMode == "observe" && !likelyBreach && workerUnlocked && s.economyRow >= 0
		&& s.economyValue > kInvestmentMargin && s.economyGuardCostByRow[s.economyRow] == 0) {
		s.commanderMode = "harvest";
		budget = IceProduction::WorkerCost;
		reserve = 0;
		slots = 0;
	}
	// 余额很低时主动花完，不靠保留一只普通僵尸的钱维持永不结束的补给循环。
	if (s.enemyIce <= 24 && s.commanderMode != "observe" && s.commanderMode != "economy" && s.commanderMode != "harvest") {
		s.commanderMode = "last_stand";
		budget = s.enemyIce;
		reserve = 0;
	}
	if (s.commanderMode == "assault" && s.commanderFocusRow >= 0) {
		const auto& core = raids[s.commanderFocusRow];
		if (core.forecast.cellsBroken > 0) {
			// 为预计核心预留足够预算；实际付款仍受下方爆区和梯队门禁约束。
			reserve = std::min(reserve, std::max(0, s.enemyIce - core.cost));
			budget = std::max(budget, core.cost);
		}
	}
	budget = std::min(budget, std::max(0, s.enemyIce - reserve));
	s.commanderBudget = budget;
	s.commanderReserve = reserve;
	if (budget <= 0) return;

	bool heavyUnlocked = false;
	for (ZombieType type : mSpawnZombieList)
		if (Assault(type).smashSeconds > 0.0f && isUnlocked(type))
			heavyUnlocked = true;
	const int attackRow = s.commanderMode == "assault" ? s.commanderFocusRow : -1;
	const int requiredFront = attackRow >= 0 && raids[attackRow].forecast.cellsBroken > 0 ? raids[attackRow].count : 0;
	const int requiredFrontCost = requiredFront > 0 ? raids[attackRow].cost : 0;
	// 进攻预算中预留一名工人的价款；最终仍按实际已购买前排算边际收益，不满足就保留库存。
	const int followupReserve = workerUnlocked && (s.commanderMode == "harvest"
		|| (s.commanderMode != "economy" && s.commanderMode != "observe" && budget >= 72
			&& (heavyUnlocked || totalHealth >= 3000.0f) && budget - IceProduction::WorkerCost >= requiredFrontCost))
		? IceProduction::WorkerCost : 0;
	budget -= followupReserve;
	std::array<float, 6> lastGuardSpawn{};
	const bool investing = s.commanderMode == "economy";
	const ZombieType economyGuard = investing ? economyGuards[s.economyRow] : ZombieType::NUM_ZOMBIE_TYPES;
	bool guardCommitted = economyGuard == ZombieType::NUM_ZOMBIE_TYPES;
	std::array<int, static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)> chosen{};
	const int initialIce = s.enemyIce;
	const float blastStake = std::max(investing ? static_cast<float>(IceProduction::WorkerCost + s.economyGuardCostByRow[s.economyRow]) : 0.0f,
		std::clamp(16.0f + initialIce * kBlastStakeFraction, kBlastStakeMinimum, kBlastStakeMaximum));
	bool riskBlocked = false;
	for (int slot = 0; slot < slots; ++slot) {
		const bool formingAttack = requiredFront > 0 && waveRows[attackRow] < requiredFront;
		float best = std::numeric_limits<float>::lowest();
		ZombieType selected = ZombieType::NUM_ZOMBIE_TYPES;
		int selectedRow = -1;
		float selectedDelay = 0, selectedBlastLoss = 0;
		for (ZombieType type : mSpawnZombieList) {
			const int cost = GetZombieIceCost(type);
			if (cost > budget || cost > s.enemyIce || !isUnlocked(type)) continue;
			const auto profile = Assault(type);
			const bool economicUnit = type == ZombieType::ZOMBIE_ICE_WORKER;
			if (economicUnit && s.commanderMode != "economy") continue;
			if (s.elapsed < 80 && cost >= 8 && !economicUnit) continue;
			for (int row = 0; row < mRows; ++row) {
				if (!IsSpawnRowCompatible(type, row)) continue;
				// 战略收益来自完整编队，不能在实际提交时把核心拆散成各路少量送兵。
				if (formingAttack && (row != attackRow || type != raids[attackRow].troops[waveRows[attackRow]])) continue;
				if (economicUnit && (row != s.economyRow || !guardCommitted
					|| investment[row] <= kInvestmentMargin)) continue;
				// 新经济组合先买护卫且保留工人成交价，避免预算被其他兵种抢走。
				if (investing && !guardCommitted
					&& (type != economyGuard || row != s.economyRow
						|| budget < cost + IceProduction::WorkerCost)) continue;
				const bool specialist = type == ZombieType::ZOMBIE_HEALER || type == ZombieType::ZOMBIE_POLAR_CLOCKMAKER
					|| type == ZombieType::ZOMBIE_AURORA_PRIEST;
				if (specialist && escort[row] < kSupportHealthRequired) continue;
				if (type == ZombieType::ZOMBIE_HEALER && healers[row] > 0) continue;
				// 炸弹风险已按经营组合整体估价；允许已选护卫后跟一名工人，其他兵力仍分路。
				if (blastSoon && !formingAttack && waveRows[row] >= (slots + mRows - 1) / mRows
					&& !(economicUnit && chosen[static_cast<int>(type)] == 0)) continue;
				++s.candidatesEvaluated;
				float score = 0.0f;
				for (int sample = 0; sample < kForecastSamples; ++sample) {
					const float fire = dps[row] * (0.85f + 0.05f * sample);
					const float exposure = kForecastHorizon / profile.speed * (1.0f + std::min(0.7f, control[row] * 0.2f));
					const float shield = std::min(escort[row] * 0.35f, fire * exposure * 0.7f);
					const float remainingHealth = profile.health - std::max(0.0f, fire * exposure - shield);
					score += std::clamp(remainingHealth / profile.health, -1.5f, 1.0f) * 4.0f;
				}
				score /= kForecastSamples;
				score += 1.5f * std::log1p(profile.health / cost / 100.0f) + value[row] * 0.12f;
				score += 3.0f * profile.speed / (1.0f + armor[row]);
				if (profile.bypass) score += std::min(3.0f, armor[row] * 0.5f) + s.habits[0] + s.habits[3];
				if (type == ZombieType::ZOMBIE_ADAPTIVE_HELMET) score += s.habits[2];
				if (profile.support) score += std::min(4.0f, escort[row] / 1200.0f) - (escort[row] < 500 ? 3.0f : 0.0f);
				if (specialist) score += std::min(3.0f, escort[row] / 1800.0f) - specialists[row] * 2.5f;
				if (profile.splash) score += std::min(2.0f, value[row] * 0.15f);
				score -= committed[row] * (0.25f + std::min(1.0f, splash[row] / 100.0f) + (blastSoon ? 0.35f : 0.0f) + s.habits[1]);
				score -= chosen[static_cast<int>(type)] * (s.commanderMode == "assault" ? 0.3f : 1.25f);
				if (profile.smashSeconds > 0.0f) score += std::min(7.0f, armor[row] * 0.6f);
				if (s.commanderMode == "assault" && raids[row].type == type) score += 4.0f;
				if (s.commanderFocusRow == row) score += 2.5f;
				else if (!blastSoon && waveRows[row] > 0) score += 0.9f;
				// 慢速重装抵达前炸弹会转好时，不把当前冷却误认为安全的集结窗口。
				const float arrival = (static_cast<float>(SCENE_WIDTH) + 40.0f - frontX[row]) / (20.0f * profile.speed);
				if (responseWindow <= arrival && profile.health >= 1500.0f) score -= 0.8f + waveRows[row] * 0.5f;
				if (economicUnit) score += 15.0f + std::min(8.0f, investment[row] * 0.05f) - chosen[static_cast<int>(type)] * 10.0f;
				else if (!profile.support && !profile.bypass && profile.health >= 1000.0f) {
					// 为成熟且缺掩护的工人补肉盾，也适用于暂停经济扩张的回合。
					score += std::min(10.0f, workerValue[row] / 15.0f) * std::min(1.0f, guardNeed[row] / 1000.0f);
				}
				float candidateDelay = 1.0f + slot * kDeploySpacing
					+ (economicUnit && economyGuard != ZombieType::NUM_ZOMBIE_TYPES ? s.economyEntryDelayByRow[row] : 0.0f);
				if (economicUnit && economyGuard != ZombieType::NUM_ZOMBIE_TYPES)
					candidateDelay = std::max(candidateDelay, lastGuardSpawn[row] + s.economyEntryDelayByRow[row]);
				if (profile.speed > 1.5f && frontHealth[row] >= kBreachFrontHealth) {
					float firstBreach = std::numeric_limits<float>::max();
					const float slow = 1.0f - 0.7f * std::clamp(slowDuty[row], 0.0f, 1.0f);
					for (const auto& front : formation) if (front.row == row && front.smashSeconds > 0 && front.health > 0) {
						const float arrival = front.spawnAt + front.stopped + front.eating
							+ std::max(0.0f, front.x - frontX[row] - kForecastContactDistance) / std::max(1.0f, front.speed * slow);
						firstBreach = std::min(firstBreach, arrival + front.smashSeconds / std::max(0.5f, slow));
					}
					if (firstBreach < std::numeric_limits<float>::max()) {
						const float travel = std::max(0.0f, SCENE_WIDTH + 40.0f - frontX[row] - kForecastContactDistance)
							/ (kWorkerForecastSpeed * profile.speed * slow);
						if (firstBreach - travel > candidateDelay + kStagingRecheck) { riskBlocked = true; continue; }
						candidateDelay = std::max(candidateDelay, firstBreach - travel);
					}
				}
				float safeDelay = candidateDelay;
				auto exposure = blastRisk({newSplashUnit(type, row, safeDelay)});
				// 短暂错开能真正拉开爆区时才预付；前队被堵住时，钱留在库存，下一轮再看战况。
				while (exposure.loss > blastStake && safeDelay < candidateDelay + kStagingRecheck) {
					safeDelay += 1.0f;
					exposure = blastRisk({newSplashUnit(type, row, safeDelay)});
				}
				if (exposure.loss > blastStake) { riskBlocked = true; continue; }
				score -= exposure.addedLoss * kBlastScorePerIce;
				const float loss = collateral({newSplashUnit(type, row, safeDelay)});
				s.splashRiskByRow[row] = std::max(s.splashRiskByRow[row], loss);
				// 总攻核心已按整批损益选定；零散增援则须靠自身战术收益覆盖引火损失。
				const bool formingEconomy = investing && (!guardCommitted || (economicUnit && chosen[static_cast<int>(type)] == 0));
				if (!formingAttack && !formingEconomy) {
					score -= std::max(0.0f, loss) * kCollateralScorePerIce;
					if (loss > 0.0f && score <= 0.0f) continue;
				}
				score += GameRandom::Range(0.0f, 0.65f);
				if (score > best) {
					best = score; selected = type; selectedRow = row;
					selectedDelay = safeDelay; selectedBlastLoss = exposure.loss;
				}
			}
		}
		if (selectedRow < 0) break;
		const bool buyingWorker = selected == ZombieType::ZOMBIE_ICE_WORKER;
		const float delay = selectedDelay;
		if (!QueueColdStorageZombie(selected, selectedRow, delay)) break;
		s.formationBlastLoss = std::max(s.formationBlastLoss, selectedBlastLoss);
		formation.push_back(newSplashUnit(selected, selectedRow, delay));
		if (investing && selected == economyGuard && selectedRow == s.economyRow) guardCommitted = true;
		budget -= GetZombieIceCost(selected);
		s.lastAttackRow = selectedRow;
		s.lastBestScore = best;
		++chosen[static_cast<int>(selected)];
		++committed[selectedRow];
		++waveRows[selectedRow];
		if (selected == ZombieType::ZOMBIE_HEALER) ++healers[selectedRow];
		if (selected == ZombieType::ZOMBIE_HEALER || selected == ZombieType::ZOMBIE_POLAR_CLOCKMAKER
			|| selected == ZombieType::ZOMBIE_AURORA_PRIEST) ++specialists[selectedRow];
		else if (!buyingWorker) {
			escort[selectedRow] += Assault(selected).health;
			if (!Assault(selected).support && !Assault(selected).bypass) {
				EconomyMover guard;
				guard.x = SCENE_WIDTH + 40.0f;
				guard.speed = kWorkerForecastSpeed * Assault(selected).speed;
				guard.health = Assault(selected).health;
				guard.spawnAt = delay;
				guard.stopX = frontX[selectedRow] + kForecastContactDistance;
				guard.futureSlowFactor = 1.0f - (1.0f - guard.slowFactor) * std::clamp(slowDuty[selectedRow], 0.0f, 1.0f);
				rowGuards[selectedRow].push_back(guard);
				lastGuardSpawn[selectedRow] = delay;
			}
			guardNeed[selectedRow] = std::max(0.0f, guardNeed[selectedRow] - Assault(selected).health);
		}
		if (buyingWorker) { ++workers[selectedRow]; investment[selectedRow] -= 12.0f; }
	}
	if (followupReserve > 0 && s.enemyIce >= IceProduction::WorkerCost) {
		int followRow = -1;
		float followDelay = 0.0f, bestNet = kInvestmentMargin;
		for (int row = 0; row < mRows; ++row) {
			for (float spacing : {kWorkerEntryDelay, kWorkerLateEntryDelay}) {
				EconomyMover worker;
				worker.x = SCENE_WIDTH + 40.0f;
				worker.speed = kWorkerForecastSpeed;
				worker.health = IceProduction::WorkerHealth;
				worker.stopX = frontX[row] + kForecastContactDistance;
				worker.spawnAt = lastGuardSpawn[row] > 0.0f ? lastGuardSpawn[row] + spacing : 1.0f;
				if (blastRisk({newSplashUnit(ZombieType::ZOMBIE_ICE_WORKER, row, worker.spawnAt)}).loss > blastStake) {
					riskBlocked = true;
					continue;
				}
				const int freshGuards = static_cast<int>(std::count_if(rowGuards[row].begin(), rowGuards[row].end(),
					[](const auto& guard) { return guard.spawnAt > 0.0f; }));
				const float income = expectedIncome(row, worker, rowGuards[row], IceProduction::Interval,
					IceProduction::InitialYield, freshGuards, -1, nullptr, nullptr);
				// 前排已为进攻付款，跟进仅比较工人新增费用；仍扣同路堆积与溅射风险。
				const float net = income - IceProduction::WorkerCost - workers[row] * (6.0f + directSplash[row] * 0.05f)
					- std::max(0.0f, collateral({newSplashUnit(ZombieType::ZOMBIE_ICE_WORKER, row, worker.spawnAt)}));
				if (net > bestNet) { bestNet = net; followRow = row; followDelay = worker.spawnAt; }
			}
		}
		if (followRow >= 0 && QueueColdStorageZombie(ZombieType::ZOMBIE_ICE_WORKER, followRow, followDelay)) {
			s.formationBlastLoss = std::max(s.formationBlastLoss,
				blastRisk({newSplashUnit(ZombieType::ZOMBIE_ICE_WORKER, followRow, followDelay)}).loss);
			++s.economicFollowups;
		}
	}
	s.commanderSpent = initialIce - s.enemyIce;
	s.attackDeferred = s.enemyIce > 0 && ((riskBlocked && s.commanderSpent < s.commanderBudget) || s.commanderMode == "probe");
	if (!s.pending.empty()) {
		mCurrentWave = ++s.decisions;
		for (auto& paid : s.pending) paid.wave = s.decisions;
		s.dispatchQuietSeconds = 0.0f;
		if (s.commanderMode == "assault" && !s.attackDeferred) s.assaultCooldown = kAssaultCooldownSeconds;
	}
}

void Board::UpdateColdStorage(float dt)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || dt <= 0) return;
	auto& s = mColdStorage;
	s.battleStarted = true;
	s.elapsed += dt;
	s.incomeIdleSeconds = std::min(kExhaustionIdleSeconds, s.incomeIdleSeconds + dt);
	s.plantKillIdleSeconds = std::min(kExhaustionIdleSeconds, s.plantKillIdleSeconds + dt);
	while (!s.incomeWindow.empty() && s.incomeWindow.front().at < s.elapsed - kExhaustionIdleSeconds)
		s.incomeWindow.pop_front();
	s.assaultCooldown = std::max(0.0f, s.assaultCooldown - dt);
	s.dispatchQuietSeconds = std::min(kMaxObserveSeconds + 1.0f, s.dispatchQuietSeconds + dt);
	if (s.orderIce > 0) {
		s.orderRemaining = std::max(0.0f, s.orderRemaining - dt);
		if (s.orderRemaining <= 0) { s.playerIce = std::min(kMaxIce, s.playerIce + s.orderIce); s.orderIce = 0; }
	}
	// 清场先于本步补给：最后一只死后已破产就结束，不用新补给复活已结束的战争。
	if (IsColdStorageCleared()) { CreateTrophy(GetCellCenterPosition(2, 4)); return; }
	s.supplyRemaining -= dt;
	while (s.supplyRemaining <= 0) {
		s.supplyRemaining += kSupplySeconds;
		const int amount = std::min(kSupplyIce, kMaxIce - s.enemyIce);
		s.enemyIce += amount;
		s.supplied = std::min(kMaxIce, s.supplied + amount);
	}
	if (GameAPP::mDevSpawnPaused) return;
	for (auto it = s.pending.begin(); it != s.pending.end();) {
		it->remaining -= dt;
		if (it->remaining > 0) { ++it; continue; }
		Zombie* z = CreateResolvedWaveZombie(it->type, it->row, static_cast<float>(SCENE_WIDTH) + 40.0f);
		if (!z) { it->remaining = 1.0f; ++it; continue; }
		s.refundableCosts.emplace(z->mZombieID, it->cost);
		z->mSpawnWave = it->wave;
		++s.deployments;
		++s.deploymentTypes[it->type];
		it = s.pending.erase(it);
		UpdateZombieMetrics();
	}
	s.decisionRemaining -= dt;
	// 学习分支将已付队列作为未来友军重新推演；旧 AI 仍等队列兑现，避免漏算其承诺。
	if (s.decisionRemaining <= 0 && (s.pending.empty()
		|| (GameAPP::GetInstance().mEnableMonteCarloAI && ColdStoragePolicy::Get(mLevel)))) {
		PlanColdStorageAttack();
		s.decisionRemaining = s.attackDeferred ? kStagingRecheck : DecisionInterval(s.elapsed, s.commanderStrategy == "short_game");
	}
}

nlohmann::json Board::SaveColdStorage() const
{
	const auto& s = mColdStorage;
	nlohmann::json j{{"playerIce",s.playerIce},{"enemyIce",s.enemyIce},{"initialEnemyIce",s.initialEnemyIce},
		{"difficulty",s.difficulty},{"orderIce",s.orderIce},{"orderRemaining",s.orderRemaining},
		{"supplyRemaining",s.supplyRemaining},{"decisionRemaining",s.decisionRemaining},{"elapsed",s.elapsed},
		{"incomeIdleSeconds",s.incomeIdleSeconds},
		{"plantKillIdleSeconds",s.plantKillIdleSeconds},
		{"workerIncome",s.workerIncome},{"playerProductionIncome",s.playerProductionIncome},
		{"spent",s.spent},{"supplied",s.supplied},{"killIncome",s.killIncome},{"playerKillIncome",s.playerKillIncome},{"deployments",s.deployments},
		{"decisions",s.decisions},{"lastAttackRow",s.lastAttackRow},{"battleStarted",s.battleStarted},
		{"habits",s.habits},{"assaultCooldown",s.assaultCooldown},{"dispatchQuietSeconds",s.dispatchQuietSeconds},
		{"pending",nlohmann::json::array()}};
	for (const auto& p : s.pending) j["pending"].push_back({{"type",static_cast<int>(p.type)},
		{"row",p.row},{"cost",p.cost},{"remaining",p.remaining},{"wave",p.wave}});
	j["refundableCosts"] = nlohmann::json::array();
	for (const auto& [id, cost] : s.refundableCosts)
		j["refundableCosts"].push_back({{"id",id},{"cost",cost}});
	j["incomeWindow"] = nlohmann::json::array();
	long long production = 0, spent = 0;
	for (const auto& flow : s.incomeWindow) if (flow.at >= s.elapsed - kExhaustionIdleSeconds) {
		j["incomeWindow"].push_back({{"at",flow.at},{"production",flow.production},{"spent",flow.spent}});
		production += flow.production; spent += flow.spent;
	}
	j["incomeWindowProduction"] = production; j["incomeWindowSpent"] = spent;
	return j;
}

void Board::LoadColdStorage(const nlohmann::json& j)
{
	if (!IsColdStorage() || !j.is_object() || j.empty()) return;
	auto integer = [&](const char* key, int fallback, int low, int high) {
		return std::clamp(j.value(key, fallback), low, high);
	};
	auto seconds = [&](const char* key, float fallback, float high) {
		const float value = j.value(key, fallback);
		return std::isfinite(value) ? std::clamp(value, 0.0f, high) : fallback;
	};
	auto& s = mColdStorage;
	s.playerIce = integer("playerIce", 200, 0, kMaxIce);
	s.enemyIce = integer("enemyIce", s.enemyIce, 0, kMaxIce);
	s.initialEnemyIce = integer("initialEnemyIce", s.initialEnemyIce, 1, kMaxIce);
	s.difficulty = integer("difficulty", 1, 1, 4);
	s.orderIce = integer("orderIce", 0, 0, 100);
	s.orderRemaining = seconds("orderRemaining", 0, 10);
	s.supplyRemaining = seconds("supplyRemaining", 30, 30);
	s.decisionRemaining = seconds("decisionRemaining", 12, 60);
	s.elapsed = seconds("elapsed", 0, 10000000);
	// 旧档没有可核实的收入时间，给予完整恢复窗口，不能用总对局时间追溯判负。
	s.incomeIdleSeconds = seconds("incomeIdleSeconds", 0, kExhaustionIdleSeconds);
	s.plantKillIdleSeconds = seconds("plantKillIdleSeconds", 0, kExhaustionIdleSeconds);
	s.incomeWindow.clear();
	// 只有完整保存了滚动账本的新档才能沿用判负计时；旧档给予一个完整窗口。
	if (j.contains("incomeWindow") && j["incomeWindow"].is_array()
		&& j["incomeWindow"].size() <= kMaxIncomeWindowRecords) {
		float previousAt = 0;
		for (const auto& entry : j["incomeWindow"]) {
			const float at = entry.value("at", -1.0f);
			const int production = entry.value("production", -1), spent = entry.value("spent", -1);
			if (!std::isfinite(at) || at < previousAt || at > s.elapsed
				|| production < 0 || production > kMaxIce || spent < 0 || spent > kMaxIce) {
				s.incomeWindow.clear(); s.plantKillIdleSeconds = 0; break;
			}
			previousAt = at;
			if (at >= s.elapsed - kExhaustionIdleSeconds) s.incomeWindow.push_back({at,production,spent});
		}
	} else s.plantKillIdleSeconds = 0;
	s.assaultCooldown = seconds("assaultCooldown", 0, kAssaultCooldownSeconds);
	s.dispatchQuietSeconds = seconds("dispatchQuietSeconds", 0, kMaxObserveSeconds + 1.0f);
	s.spent = integer("spent", 0, 0, kMaxIce);
	s.supplied = integer("supplied", 0, 0, kMaxIce);
	s.workerIncome = integer("workerIncome", 0, 0, kMaxIce);
	s.playerProductionIncome = integer("playerProductionIncome", 0, 0, kMaxIce);
	s.killIncome = integer("killIncome", 0, 0, kMaxIce);
	s.playerKillIncome = integer("playerKillIncome", 0, 0, kMaxIce);
	s.deployments = integer("deployments", 0, 0, kMaxIce);
	s.decisions = integer("decisions", 0, 0, kMaxIce);
	mCurrentWave = s.decisions;
	mMaxWave = 0;
	s.lastAttackRow = integer("lastAttackRow", -1, -1, mRows - 1);
	s.battleStarted = j.value("battleStarted", false);
	// 旧档没有付费身份，不能靠品种或波号猜测（召唤物也可能继承波号）。
	// 已在途的付款事务保留，之后入场会正常登记；已有实体不追溯补发。
	s.refundableCosts.clear();
	if (j.contains("refundableCosts") && j["refundableCosts"].is_array())
		for (const auto& record : j["refundableCosts"]) {
			const int id = record.value("id", 0), cost = record.value("cost", 0);
			// 保存的是成交价，不用当前价表或旧最高价截断，保证调价前后读档的返冰一致。
			if (id > 0 && cost > 0 && cost <= kMaxIce) s.refundableCosts.emplace(id, cost);
		}
	if (j.contains("habits") && j["habits"].is_array() && j["habits"].size() == 4)
		for (int i=0; i<4; ++i) { const float v=j["habits"][i].get<float>(); s.habits[i]=std::isfinite(v)?std::clamp(v,0.0f,1.0f):0; }
	s.pending.clear();
	if (j.contains("pending") && j["pending"].is_array()) for (const auto& p : j["pending"]) {
		const int type=p.value("type",-1), row=p.value("row",-1);
		const float remaining=p.value("remaining",0.0f);
		if (type<0 || type>=static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES) || row<0 || row>=mRows
			|| !std::isfinite(remaining) || s.pending.size()>=kMaxSimultaneous) continue;
		s.pending.push_back({static_cast<ZombieType>(type), row, std::clamp(p.value("cost",0),0,kMaxIce),std::clamp(remaining,0.0f,60.0f),
			std::clamp(p.value("wave",s.decisions),0,s.decisions)});
	}
}
