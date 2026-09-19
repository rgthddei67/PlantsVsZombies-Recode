#include "Board.h"
#include "BoardPresentation.h"
#include "Game/AdventureProgression.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/CardSlotManager.h"
#include "Game/Card.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/IceWorkerZombie.h"
#include "Game/Plant/IceMint.h"
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
	constexpr float kWorkerForecastSpeed = 20.0f; // 新工人的保守水平移速估计，像素/游戏秒
	constexpr float kEconomySplashReach = 100.0f; // 含碰撞箱的溅射接近窗口，像素；不把整路都当成命中
	constexpr float kUnusedBlastRisk = 0.22f; // 未提交炸弹的基础投资折损权重，不等同于确定命中
	constexpr float kCrowdedBlastRisk = 0.10f; // 爆区每多一个可同时命中的敌人，增加的风险权重
	constexpr float kMaximumBlastRisk = 0.80f; // 仅卡槽可用时的最高折损，已提交爆炸另按实际范围截断
	constexpr float kHabitDecay = 0.97f; // 每次正式落种衰减旧画像，防止长期偏好锁死
	constexpr std::array<float, 9> kEconomyTargetSeconds{360,390,420,450,480,720,750,780,810}; // 库存分配参考时长，秒；不是强制结束时间
	constexpr float kReserveFraction = 0.12f; // 常规储备占初始库存比例，总攻可动用一半
	constexpr float kReserveReleaseSeconds = 90.0f; // 接近参考时长时逐步释放储备，避免永久囤冰
	constexpr float kAssaultCooldownSeconds = 48.0f; // 两次总攻之间的最短重组时间，秒
	constexpr float kMaxObserveSeconds = 24.0f; // 场上兵力充足时最多连续观望的游戏秒
	constexpr float kResponseLookaheadSeconds = 18.0f; // 判断炸弹即将恢复的观察窗，秒
	constexpr float kAssaultOpportunityThreshold = 2.4f; // 已有前锋可利用的突破评分下限
	constexpr float kSupportHealthRequired = 1200.0f; // 派治疗或钟匠前，同路有效前锋的最低生命
	constexpr int kNormalEarlyBudget = 48; // 前五关每波常规冰块预算上限
	constexpr int kNormalLateBudget = 72; // 后四关每波常规冰块预算上限
	constexpr int kAssaultEarlyBudget = 96; // 前五关一次总攻冰块预算上限
	constexpr int kAssaultLateBudget = 144; // 后四关一次总攻冰块预算上限

	/** 统一计划与预算采用的决策间隔，避免两处调整后预算速度失配。 */
	float DecisionInterval(float elapsed)
	{
		return elapsed < 120 ? kOpeningDecisionSeconds
			: elapsed < 240 ? kDevelopingDecisionSeconds : kDecisionSeconds;
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
	};

	/** 返回对应结算几何在目标行的水平半径；负值表示该行不会命中。 */
	float EconomyBlastReach(const EconomyBlast& blast, int row)
	{
		const int rows = std::abs(blast.row - row);
		if (blast.type == PlantType::PLANT_COBCANNON) return rows <= blast.rowRadius ? blast.radius : -1.0f;
		if (blast.type == PlantType::PLANT_JALAPENO) return rows == 0 ? 10000.0f : -1.0f;
		if (blast.type == PlantType::PLANT_CHERRYBOMB) return rows <= 1 ? 130.0f : -1.0f;
		// 与 CreateDoomBoom 的圆/碰撞矩形纵向口径一致，用当前棋盘行距换算。
		const float dy = std::max(0.0f, rows * static_cast<float>(CELL_COLLIDER_SIZE_Y)
			- (row > blast.row ? 65.0f : 35.0f));
		return dy <= 250.0f ? 25.0f + std::sqrt(250.0f * 250.0f - dy * dy) : -1.0f;
	}

	struct AssaultProfile { float health; float speed; bool bypass; bool support; bool splash; };
	/** 候选战术画像只服务排序；实体出生仍使用原品种生命、技能和动作。 */
	AssaultProfile Assault(ZombieType type)
	{
		using Z = ZombieType;
		switch (type) {
		case Z::ZOMBIE_REDEYE_GARGANTUAR: return {6000, 1, false, false, true};
		case Z::ZOMBIE_GARGANTUAR: return {3000, 1, false, false, true};
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
	mMaxWave = 0; // 此地图没有最终波；波号只用于兵种解锁，胜利由冰块破产与清场判定。
	mColdStorage.difficulty = std::clamp(GameAPP::GetInstance().Difficulty, 1, 4);
	const int stage = std::clamp(AdventureProgression::GetLevelNumberInArea(mLevel) - 1, 0, 8);
	mColdStorage.initialEnemyIce = kOpeningIce[stage] * (4 + mColdStorage.difficulty - 1) / 4;
	mColdStorage.enemyIce = mColdStorage.initialEnemyIce;
	mColdStorage.habits = GameAPP::GetInstance().mColdStorageHabits;
}

bool Board::BuyColdStorageIce(bool large)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned
		|| DeltaTime::IsPaused() || mColdStorage.orderIce > 0) return false;
	const int price = large ? 225 : 100;
	if (mSun < price) return false;
	SubSun(price);
	mColdStorage.orderIce = large ? 100 : 40;
	mColdStorage.orderRemaining = large ? 10.0f : 5.0f;
	return true;
}

void Board::CreditProducedIce(bool player, int amount)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || amount <= 0) return;
	int& balance = player ? mColdStorage.playerIce : mColdStorage.enemyIce;
	int& income = player ? mColdStorage.playerProductionIncome : mColdStorage.workerIncome;
	const int accepted = std::min(amount, kMaxIce - balance);
	balance += accepted;
	income = std::min(kMaxIce, income + accepted);
}

void Board::CommitColdStoragePlant(PlantType type)
{
	if (!IsColdStorage()) return;
	if (!(GameAPP::mDevelopMode && GameAPP::mDevFreePlant))
		mColdStorage.playerIce = std::max(0, mColdStorage.playerIce - GetPlantIceCost(type));
	const auto use = PlantStrategy(type);
	for (std::size_t i = 0; i < use.size(); ++i)
		mColdStorage.habits[i] = mColdStorage.habits[i] * kHabitDecay + use[i] * (1.0f - kHabitDecay);
	// 玩家存档仍由正常保存点写盘；AutoTest 只更新进程内值，不污染玩家档。
	GameAPP::GetInstance().mColdStorageHabits = mColdStorage.habits;
}

void Board::RewardColdStoragePlantKill(PlantType type)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned) return;
	// 难度1～4分别返还植物冰价的0.75/1/1.25/1.5倍，抑制破阵后的资源滚雪球。
	const int reward = GetPlantIceCost(type) * (3 + mColdStorage.difficulty - 1) / 4;
	const int accepted = std::min(reward, kMaxIce - mColdStorage.enemyIce);
	mColdStorage.enemyIce += accepted;
	mColdStorage.killIncome = std::min(kMaxIce, mColdStorage.killIncome + accepted);
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

/** 余额、场上敌人及两类已提交援军同时清空才算破产，不把瞬时出兵冷却当胜利。 */
bool Board::IsColdStorageCleared() const
{
	if (!IsColdStorage() || !mColdStorage.battleStarted || mTrophySpawned
		|| !mColdStorage.pending.empty() || !mPendingSnowHoleSpawns.empty()
		|| !mPendingAuroraRifts.empty() || GetColdStorageHostileCount() != 0) return false;
	// 钟匠已经提交的复活同样属于在途兵力；施法者死亡不能让本局提前结束。
	for (const auto& anchor : mTemporalAnchors)
		for (const auto& target : anchor.targets)
			if (!target.irreversible) return false;
	for (ZombieType type : mSpawnZombieList)
		if (mColdStorage.enemyIce >= GetZombieIceCost(type)) return false;
	return true;
}

bool Board::QueueColdStorageZombie(ZombieType type, int row, float delay)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned
		|| row < 0 || row >= mRows || !IsSpawnRowCompatible(type, row)
		|| GameDataManager::GetInstance().GetZombieAppearWave(type) > mColdStorage.decisions + 1
		|| std::find(mSpawnZombieList.begin(), mSpawnZombieList.end(), type) == mSpawnZombieList.end()
		|| GetColdStorageHostileCount() + static_cast<int>(mColdStorage.pending.size()) >= kMaxSimultaneous) return false;
	const int cost = GetZombieIceCost(type);
	if (mColdStorage.enemyIce < cost || !std::isfinite(delay)) return false;
	// 先登记再扣款，只有成功提交的事务能改变余额；免费技能召唤不经过这里。
	mColdStorage.pending.push_back({type, row, cost, std::clamp(delay, 0.0f, 60.0f)});
	mColdStorage.enemyIce -= cost;
	mColdStorage.spent = std::min(kMaxIce, mColdStorage.spent + cost);
	return true;
}

/** 先决定是否值得增援及本波预算，再在合法候选中组织队伍；未派兵不推进波号。 */
void Board::PlanColdStorageAttack()
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || !mColdStorage.pending.empty()) return;
	auto& s = mColdStorage;
	s.commanderMode = "pressure";
	s.commanderBudget = s.commanderSpent = s.commanderReserve = 0;
	s.commanderFocusRow = -1;
	s.candidatesEvaluated = 0;
	s.predictedProduction = 0.0f;
	s.economyValue = 0.0f;
	s.economyRow = -1;
	s.economyNetByRow.fill(0.0f);
	s.economyBlastLossByRow.fill(0.0f);
	s.economyGuardCostByRow.fill(0);
	PlantDefenseMonteCarlo::Snapshot snapshot;
	if (!BuildMonteCarloCombatSnapshot(snapshot, false, false)) return;
	std::array<float, 6> directDps{}, directSplash{}, dps{}, value{}, armor{}, frontArmor{}, escort{}, splash{}, control{}, frontX{};
	std::array<int, 6> committed{}, frontlineCount{}, waveRows{}, healers{}, specialists{};
	for (int row = 0; row < mRows; ++row) frontX[row] = GetCellCenterPosition(row, 0).x;
	for (const auto& p : snapshot.plants) {
		directDps[p.row] += p.attackDps;
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
				cell.row, cell.x, p.abilityCooldownRemaining + 4.0f, false, p.cobBlastRadius, p.cobBlastRowRadius});
		}
	}
	for (const auto& blast : snapshot.pendingCobBlasts) economyBlasts.push_back({PlantType::PLANT_COBCANNON,
		blast.targetRow, blast.x, blast.resolveSeconds, true, blast.radius, blast.rowRadius});
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
	// 经济预测保留爆炸发生前已经产生的收入；卡片就绪只是风险，不直接判为必死。
	auto expectedIncome = [&](int row, float x, float health, float guardHealth, float guardGap,
		float remainingProduction, float amount, float stoppedSeconds, float spawnDelay, int addedGuards, int workerID,
		float* blastLoss) {
		float adjacentFire = 0.0f;
		for (int otherRow = std::max(0, row - 1); otherRow <= std::min(mRows - 1, row + 1); ++otherRow) {
			if (otherRow == row) continue;
			// 邻路西瓜必须先有靠近工人的命中目标，才按次要伤害计入；不会凭空穿过空路溅射。
			const bool nearbyTarget = std::any_of(snapshot.zombies.begin(), snapshot.zombies.end(), [&](const auto& z) {
				return !z.mindControlled && z.row == otherRow && std::abs(z.x - x) < kEconomySplashReach;
			});
			if (nearbyTarget) adjacentFire += directSplash[otherRow] / 3.0f;
		}
		const float fire = std::max(1.0f, directDps[row] + adjacentFire);
		const float shieldSeconds = guardHealth / fire;
		const float splashLeak = adjacentFire + directSplash[row] / 3.0f
			* std::clamp(1.0f - guardGap / kEconomySplashReach, 0.0f, 1.0f);
		const float sheltered = splashLeak > 0.0f ? std::min(shieldSeconds, health / splashLeak) : shieldSeconds;
		const float remainingHealth = std::max(0.0f, health - sheltered * splashLeak);
		const float offscreenSeconds = std::clamp((x - SCENE_WIDTH) / kWorkerForecastSpeed, 0.0f, 4.0f);
		const float life = std::clamp(offscreenSeconds + sheltered + remainingHealth / fire - stoppedSeconds,
			0.0f, kEconomyHorizon);
		const float income = static_cast<float>(IceProduction::Forecast(remainingProduction, amount, life));
		float worstLoss = 0.0f;
		for (const auto& blast : economyBlasts) {
			const float reach = EconomyBlastReach(blast, row);
			if (reach < 0.0f || (blast.committed && blast.ready < spawnDelay)) continue;
			const float ready = std::max(0.0f, blast.ready - spawnDelay);
			const float enter = std::max(0.0f, (x - blast.x - reach) / kWorkerForecastSpeed);
			const float exit = (x - blast.x + reach) / kWorkerForecastSpeed;
			const float impact = blast.committed ? ready : std::max(ready, enter) + 1.0f;
			if (impact < enter || impact > exit || impact >= life) continue;
			const float safeIncome = static_cast<float>(IceProduction::Forecast(remainingProduction, amount, impact));
			int collateral = addedGuards;
			for (const auto& z : snapshot.zombies) {
				if (z.id == workerID || z.mindControlled || EconomyBlastReach(blast, z.row) < 0.0f) continue;
				const float projectedX = std::max(frontX[z.row], z.x - z.moveSpeed * (spawnDelay + impact));
				if (std::abs(projectedX - blast.x) <= EconomyBlastReach(blast, z.row)) ++collateral;
			}
			const float risk = blast.committed ? 1.0f
				: std::min(kMaximumBlastRisk, kUnusedBlastRisk + collateral * kCrowdedBlastRisk);
			// 多个合法种植格是同一张卡的不同选择，不重复累计成必杀。
			worstLoss = std::max(worstLoss, (income - safeIncome) * risk);
		}
		if (blastLoss) *blastLoss = worstLoss;
		return income - worstLoss;
	};
	for (const auto& z : snapshot.zombies) {
		const auto* worker = dynamic_cast<const IceWorkerZombie*>(mEntityRegistry.GetZombie(z.id));
		if (!worker || z.mindControlled) continue;
		float guards = 0.0f, guardGap = kEconomySplashReach;
		for (const auto& other : snapshot.zombies) {
			if (other.id == z.id || other.row != z.row || other.mindControlled || other.x >= z.x
				|| other.x < frontX[z.row] - 100.0f
				|| dynamic_cast<const IceWorkerZombie*>(mEntityRegistry.GetZombie(other.id))) continue;
			guardGap = std::min(guardGap, z.x - other.x);
			guards += other.bodyHealth + other.helmHealth + other.shieldHealth;
		}
		const float income = expectedIncome(z.row, z.x, z.bodyHealth, guards, guardGap,
			worker->GetIceRemaining(), worker->GetNextIceYield(),
			std::max(z.frozenRemaining, std::max(z.butterRemaining, z.paralysisRemaining)), 0.0f, 0, z.id, nullptr);
		s.predictedProduction += income;
		workerValue[z.row] += income;
		++workers[z.row];
		// 老工人的产能只有在前排撑得住时可兑现，不能一味追加新工人。
		guardNeed[z.row] += std::max(0.0f, dps[z.row] * 25.0f - guards)
			* worker->GetNextIceYield() / IceProduction::MaximumYield;
	}
	const bool workerUnlocked = std::find(mSpawnZombieList.begin(), mSpawnZombieList.end(),
		ZombieType::ZOMBIE_ICE_WORKER) != mSpawnZombieList.end()
		&& GameDataManager::GetInstance().GetZombieAppearWave(ZombieType::ZOMBIE_ICE_WORKER) <= s.decisions + 1;
	// 对每条路线比较裸投与各个已解锁护卫，护卫是否值得买由净收益决定。
	// 普通僵尸也可作早期护卫；不改其他关卡的兵种解锁波数，不强制先凑重甲血量。
	std::array<ZombieType, 6> economyGuards;
	economyGuards.fill(ZombieType::NUM_ZOMBIE_TYPES);
	for (int row = 0; row < mRows; ++row) {
		investment[row] = -1000.0f;
		auto evaluateGuard = [&](ZombieType guard) {
			const bool buyingGuard = guard != ZombieType::NUM_ZOMBIE_TYPES;
			const int charge = buyingGuard ? GetZombieIceCost(guard) : 0;
			if (!workerUnlocked || charge + IceProduction::WorkerCost > s.enemyIce) return;
			float loss = 0.0f;
			const float guardHealth = escort[row] + (buyingGuard ? Assault(guard).health : 0.0f);
			const float income = expectedIncome(row, SCENE_WIDTH + 40.0f, IceProduction::WorkerHealth,
				guardHealth, kWorkerEntryDelay * kWorkerForecastSpeed, IceProduction::Interval,
				IceProduction::InitialYield, 0.0f, buyingGuard ? kWorkerEntryDelay : 0.0f, buyingGuard ? 1 : 0, -1, &loss);
			const float net = income - IceProduction::WorkerCost - charge
				- workers[row] * (6.0f + directSplash[row] * 0.05f);
			if (net > investment[row]) {
				investment[row] = net;
				economyGuards[row] = guard;
				s.economyBlastLossByRow[row] = loss;
				s.economyGuardCostByRow[row] = charge;
			}
		};
		evaluateGuard(ZombieType::NUM_ZOMBIE_TYPES);
		for (ZombieType type : mSpawnZombieList) {
			const auto profile = Assault(type);
			if (type == ZombieType::ZOMBIE_ICE_WORKER || profile.support || profile.bypass
				|| !IsSpawnRowCompatible(type, row)
				|| (s.elapsed < 80 && GetZombieIceCost(type) >= 8)
				|| GameDataManager::GetInstance().GetZombieAppearWave(type) > s.decisions + 1) continue;
			evaluateGuard(type);
		}
		s.economyNetByRow[row] = investment[row];
		if (investment[row] > s.economyValue) { s.economyValue = investment[row]; s.economyRow = row; }
	}


	const int stage = std::clamp(AdventureProgression::GetLevelNumberInArea(mLevel), 1, 9);
	const float remaining = std::max(30.0f, kEconomyTargetSeconds[stage - 1] - s.elapsed);
	const float reserveFade = std::clamp((kEconomyTargetSeconds[stage - 1] - s.elapsed) / kReserveReleaseSeconds, 0.0f, 1.0f);
	int reserve = s.enemyIce < 64 ? 0 : static_cast<int>(std::min(s.enemyIce / 3.0f,
		std::clamp(s.initialEnemyIce * kReserveFraction, 24.0f, 120.0f)) * reserveFade);
	const int normalCap = stage <= 5 ? kNormalEarlyBudget : kNormalLateBudget;
	const int baseBudget = std::clamp(static_cast<int>(std::ceil((s.enemyIce / remaining
		+ static_cast<float>(kSupplyIce) / kSupplySeconds + s.predictedProduction / kEconomyHorizon) * DecisionInterval(s.elapsed))), s.elapsed < 120 ? 8 : 16, normalCap);
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
			budget = std::min(stage <= 5 ? kAssaultEarlyBudget : kAssaultLateBudget, baseBudget * 2);
			reserve /= 2;
			s.commanderFocusRow = focus;
		}
	}
	if (s.commanderMode == "pressure" && blastSoon) {
		s.commanderMode = "probe";
		budget = std::max(8, baseBudget * 2 / 3);
		slots = std::min(slots, 3);
	}
	// 有明确突破机会时优先猛攻；否则比较可兑现的经济收益与玩家扩张速度。
	// 生产收入已经进入基础预算，不会因为赚到更多冰仍只按固定补给花钱。
	if (workerUnlocked && s.commanderMode != "assault" && s.commanderMode != "observe"
		&& s.economyRow >= 0 && s.economyValue > kInvestmentMargin
		&& !(playerProduction > s.predictedProduction + 30.0f && bestOpportunity >= 1.5f)) {
		s.commanderMode = "economy";
		s.commanderFocusRow = s.economyRow;
		const int packageCost = IceProduction::WorkerCost + s.economyGuardCostByRow[s.economyRow];
		reserve = std::min(reserve, std::max(0, s.enemyIce - packageCost));
		budget = std::min(normalCap, std::max(baseBudget, packageCost));
		slots = std::max(slots, 2);
	}
	// 余额很低时主动花完，不靠保留一只普通僵尸的钱维持永不结束的补给循环。
	if (s.enemyIce <= 24 && s.commanderMode != "observe" && s.commanderMode != "economy") {
		s.commanderMode = "last_stand";
		budget = s.enemyIce;
		reserve = 0;
	}
	budget = std::min(budget, std::max(0, s.enemyIce - reserve));
	s.commanderBudget = budget;
	s.commanderReserve = reserve;
	if (budget <= 0) return;

	const bool investing = s.commanderMode == "economy";
	const ZombieType economyGuard = investing ? economyGuards[s.economyRow] : ZombieType::NUM_ZOMBIE_TYPES;
	bool guardCommitted = economyGuard == ZombieType::NUM_ZOMBIE_TYPES;
	std::array<int, static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)> chosen{};
	const int initialIce = s.enemyIce;
	for (int slot = 0; slot < slots; ++slot) {
		float best = std::numeric_limits<float>::lowest();
		ZombieType selected = ZombieType::NUM_ZOMBIE_TYPES;
		int selectedRow = -1;
		for (ZombieType type : mSpawnZombieList) {
			const int cost = GetZombieIceCost(type);
			if (cost > budget || cost > s.enemyIce || GameDataManager::GetInstance().GetZombieAppearWave(type) > s.decisions + 1) continue;
			const auto profile = Assault(type);
			const bool economicUnit = type == ZombieType::ZOMBIE_ICE_WORKER;
			if (economicUnit && s.commanderMode != "economy") continue;
			if (s.elapsed < 80 && cost >= 8 && !economicUnit) continue;
			for (int row = 0; row < mRows; ++row) {
				if (!IsSpawnRowCompatible(type, row)) continue;
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
				if (blastSoon && waveRows[row] >= (slots + mRows - 1) / mRows
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
				score -= chosen[static_cast<int>(type)] * 1.25f;
				if (s.commanderFocusRow == row) score += 2.5f;
				else if (!blastSoon && waveRows[row] > 0) score += 0.9f;
				// 慢速重装抵达前炸弹会转好时，不把当前冷却误认为安全的集结窗口。
				const float arrival = (static_cast<float>(SCENE_WIDTH) + 40.0f - frontX[row]) / (20.0f * profile.speed);
				if (responseWindow <= arrival && profile.health >= 1500.0f) score -= 0.8f + waveRows[row] * 0.5f;
				if (economicUnit) score += 15.0f + investment[row] * 0.15f - chosen[static_cast<int>(type)] * 6.0f;
				else if (!profile.support && !profile.bypass && profile.health >= 1000.0f) {
					// 为成熟且缺掩护的工人补肉盾，也适用于暂停经济扩张的回合。
					score += std::min(10.0f, workerValue[row] / 15.0f) * std::min(1.0f, guardNeed[row] / 1000.0f);
				}
				score += GameRandom::Range(0.0f, 0.65f);
				if (score > best) { best = score; selected = type; selectedRow = row; }
			}
		}
		if (selectedRow < 0) break;
		const bool buyingWorker = selected == ZombieType::ZOMBIE_ICE_WORKER;
		const float delay = 1.0f + slot * kDeploySpacing
			+ (buyingWorker && economyGuard != ZombieType::NUM_ZOMBIE_TYPES ? kWorkerEntryDelay : 0.0f);
		if (!QueueColdStorageZombie(selected, selectedRow, delay)) break;
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
			guardNeed[selectedRow] = std::max(0.0f, guardNeed[selectedRow] - Assault(selected).health);
		}
		if (buyingWorker) { ++workers[selectedRow]; investment[selectedRow] -= 12.0f; }
	}
	s.commanderSpent = initialIce - s.enemyIce;
	if (!s.pending.empty()) {
		mCurrentWave = ++s.decisions;
		s.dispatchQuietSeconds = 0.0f;
		if (s.commanderMode == "assault") s.assaultCooldown = kAssaultCooldownSeconds;
	}
}

void Board::UpdateColdStorage(float dt)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || dt <= 0) return;
	auto& s = mColdStorage;
	s.battleStarted = true;
	s.elapsed += dt;
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
		z->mSpawnWave = s.decisions;
		++s.deployments;
		it = s.pending.erase(it);
		UpdateZombieMetrics();
	}
	s.decisionRemaining -= dt;
	if (s.decisionRemaining <= 0) {
		PlanColdStorageAttack();
		s.decisionRemaining = DecisionInterval(s.elapsed);
	}
}

nlohmann::json Board::SaveColdStorage() const
{
	const auto& s = mColdStorage;
	nlohmann::json j{{"playerIce",s.playerIce},{"enemyIce",s.enemyIce},{"initialEnemyIce",s.initialEnemyIce},
		{"difficulty",s.difficulty},{"orderIce",s.orderIce},{"orderRemaining",s.orderRemaining},
		{"supplyRemaining",s.supplyRemaining},{"decisionRemaining",s.decisionRemaining},{"elapsed",s.elapsed},
		{"workerIncome",s.workerIncome},{"playerProductionIncome",s.playerProductionIncome},
		{"spent",s.spent},{"supplied",s.supplied},{"killIncome",s.killIncome},{"playerKillIncome",s.playerKillIncome},{"deployments",s.deployments},
		{"decisions",s.decisions},{"lastAttackRow",s.lastAttackRow},{"battleStarted",s.battleStarted},
		{"habits",s.habits},{"assaultCooldown",s.assaultCooldown},{"dispatchQuietSeconds",s.dispatchQuietSeconds},
		{"pending",nlohmann::json::array()}};
	for (const auto& p : s.pending) j["pending"].push_back({{"type",static_cast<int>(p.type)},
		{"row",p.row},{"cost",p.cost},{"remaining",p.remaining}});
	j["refundableCosts"] = nlohmann::json::array();
	for (const auto& [id, cost] : s.refundableCosts)
		j["refundableCosts"].push_back({{"id",id},{"cost",cost}});
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
		s.pending.push_back({static_cast<ZombieType>(type), row, std::clamp(p.value("cost",0),0,kMaxIce),std::clamp(remaining,0.0f,60.0f)});
	}
}
