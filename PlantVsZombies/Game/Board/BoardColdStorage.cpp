#include "Board.h"
#include "BoardPresentation.h"
#include "Game/AdventureProgression.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/CardSlotManager.h"
#include "Game/Card.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
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
	constexpr float kHabitDecay = 0.97f; // 每次正式落种衰减旧画像，防止长期偏好锁死

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

int Board::GetZombieIceCost(ZombieType type) const
{
	using Z = ZombieType;
	switch (type) {
	case Z::ZOMBIE_NORMAL: case Z::ZOMBIE_TRAFFIC_CONE: case Z::ZOMBIE_NEWSPAPER: return 4;
	case Z::ZOMBIE_BUCKET: case Z::ZOMBIE_DOOR: case Z::ZOMBIE_POLEVAULTER: return 5;
	case Z::ZOMBIE_GARGANTUAR: case Z::ZOMBIE_FOOTBALL: case Z::ZOMBIE_DANCER: return 6;
	case Z::ZOMBIE_REDEYE_GARGANTUAR: case Z::ZOMBIE_PINK_FOOTBALL:
	case Z::ZOMBIE_ELITE_JACK_IN_THE_BOX: case Z::ZOMBIE_HEALER: return 8;
	case Z::ZOMBIE_AURORA_PRIEST: case Z::ZOMBIE_POLAR_CLOCKMAKER:
	case Z::ZOMBIE_ELITE_DANCER: return 10;
	default: return 7;
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

/** 从当前防线、在途资源及应急牌比较组合，短时估计突破与击杀收益后提交队伍。 */
void Board::PlanColdStorageAttack()
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || !mColdStorage.pending.empty()) return;
	PlantDefenseMonteCarlo::Snapshot snapshot;
	BuildMonteCarloCombatSnapshot(snapshot, false, false);
	std::array<float, 6> dps{}, value{}, armor{}, support{}, splash{}, control{};
	std::array<int, 6> committed{}, waveRows{}, healers{};
	for (const auto& p : snapshot.plants) {
		value[p.row] += 1.0f + p.strategicValue / 100.0f;
		armor[p.row] += p.health / 1000.0f;
		for (int row = std::max(0, p.row - p.attackRowRadius); row <= std::min(mRows - 1, p.row + p.attackRowRadius); ++row) {
			dps[row] += p.attackDps;
			control[row] += p.slowApplicationsPerSecond + p.frozenApplicationsPerSecond;
			if (p.attackRowRadius > 0) splash[row] += p.attackDps;
		}
	}
	for (const auto& z : snapshot.zombies) if (!z.mindControlled && z.row >= 0 && z.row < mRows) {
		support[z.row] += z.bodyHealth + z.helmHealth + z.shieldHealth;
		++committed[z.row];
	}
	// 辅助沿己方队伍跟进；记录实体身份，避免同一路反复叠医生却没有进攻单位。
	for (int id : mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* z = mEntityRegistry.GetZombie(id);
		if (z && z->IsActive() && !z->IsPreview() && !z->IsDying() && !z->IsMindControlled()
			&& z->mRow >= 0 && z->mRow < mRows && z->mZombieType == ZombieType::ZOMBIE_HEALER) ++healers[z->mRow];
	}
	// 读取实际应急卡冷却和资源；订单尚未完成时只算预计会在时域内到账的冰。
	float readyBlast = 0.0f;
	const int futureIce = mColdStorage.playerIce + (mColdStorage.orderRemaining < kForecastHorizon ? mColdStorage.orderIce : 0);
	if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
		if (!card) continue;
		const auto type = card->GetGameplayPlantType();
		if (PlantStrategy(type)[1] > 0 && card->IsReady() && mSun >= card->GetSunCost()
			&& futureIce >= GetPlantIceCost(type)) readyBlast += 1.0f;
	}
	const int stage = std::clamp(AdventureProgression::GetLevelNumberInArea(mLevel), 1, 9);
	const int slots = mColdStorage.elapsed < 120 ? 2 : mColdStorage.elapsed < 240 ? 4 : (stage <= 5 ? 8 : 11);
	std::array<int, static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)> chosen{};
	mColdStorage.candidatesEvaluated = 0;
	for (int slot = 0; slot < slots; ++slot) {
		float best = std::numeric_limits<float>::lowest();
		ZombieType selected = ZombieType::NUM_ZOMBIE_TYPES;
		int selectedRow = -1;
		for (ZombieType type : mSpawnZombieList) {
			const int cost = GetZombieIceCost(type);
			if (cost > mColdStorage.enemyIce
				|| GameDataManager::GetInstance().GetZombieAppearWave(type) > mColdStorage.decisions + 1) continue;
			const auto profile = Assault(type);
			// 开局先给经济布阵空间；之后不靠每波三只红眼上限稀释进攻。
			if (mColdStorage.elapsed < 80 && cost >= 8) continue;
			for (int row = 0; row < mRows; ++row) {
				if (!IsSpawnRowCompatible(type, row)) continue;
				if (type == ZombieType::ZOMBIE_HEALER && (support[row] < 1200 || healers[row] > 0)) continue;
				// 玩家仍有可支付的爆炸牌时分路，不把整波堆进同一枚炸弹的范围。
				if (readyBlast > 0 && waveRows[row] >= (slots + mRows - 1) / mRows) continue;
				++mColdStorage.candidatesEvaluated;
				float score = 0;
				for (int sample = 0; sample < kForecastSamples; ++sample) {
					const float fire = dps[row] * (0.85f + 0.05f * sample);
					const float exposure = kForecastHorizon / profile.speed * (1.0f + std::min(0.7f, control[row] * 0.2f));
					const float shield = std::min(support[row] * 0.35f, fire * exposure * 0.7f);
					const float remaining = profile.health - std::max(0.0f, fire * exposure - shield);
					score += std::clamp(remaining / profile.health, -1.5f, 1.0f) * 4.0f;
				}
				score /= kForecastSamples;
				score += 1.5f * std::log1p(profile.health / cost / 100.0f) + value[row] * 0.12f;
				// 直通房屋优先于刷击杀收入，避免为了经济奖励放过完全空出的路线。
				score += 3.0f * profile.speed / (1.0f + armor[row]);
				if (profile.bypass) score += std::min(3.0f, armor[row] * 0.5f) + mColdStorage.habits[0] + mColdStorage.habits[3];
				if (type == ZombieType::ZOMBIE_ADAPTIVE_HELMET) score += mColdStorage.habits[2];
				if (profile.support) score += std::min(4.0f, support[row] / 1200.0f) - (support[row] < 500 ? 3.0f : 0.0f);
				if (profile.splash) score += std::min(2.0f, value[row] * 0.15f);
				score -= committed[row] * (0.25f + std::min(1.0f, splash[row] / 100.0f) + readyBlast * 0.35f + mColdStorage.habits[1]);
				score -= chosen[static_cast<int>(type)] * 1.25f;
				// 没有现成解围牌时让首个突破点获得少量后援；保留其他路的火力与拥挤惩罚。
				if (readyBlast == 0 && waveRows[row] > 0) score += 0.9f;
				score += GameRandom::Range(0.0f, 0.65f);
				if (score > best) { best = score; selected = type; selectedRow = row; }
			}
		}
		if (selectedRow < 0) break;
		if (!QueueColdStorageZombie(selected, selectedRow, 1.0f + slot * kDeploySpacing)) break;
		mColdStorage.lastAttackRow = selectedRow;
		mColdStorage.lastBestScore = best;
		++chosen[static_cast<int>(selected)];
		++committed[selectedRow];
		++waveRows[selectedRow];
		if (selected == ZombieType::ZOMBIE_HEALER) ++healers[selectedRow];
		support[selectedRow] += Assault(selected).health;
	}
	// 一批正式出兵就是一波；只有成功提交才加波，缺资源、队列或同时名额不能空刷解锁。
	if (!mColdStorage.pending.empty()) mCurrentWave = ++mColdStorage.decisions;
}

void Board::UpdateColdStorage(float dt)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || dt <= 0) return;
	auto& s = mColdStorage;
	s.battleStarted = true;
	s.elapsed += dt;
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
		s.decisionRemaining = s.elapsed < 120 ? kOpeningDecisionSeconds
			: s.elapsed < 240 ? kDevelopingDecisionSeconds : kDecisionSeconds;
	}
}

nlohmann::json Board::SaveColdStorage() const
{
	const auto& s = mColdStorage;
	nlohmann::json j{{"playerIce",s.playerIce},{"enemyIce",s.enemyIce},{"initialEnemyIce",s.initialEnemyIce},
		{"difficulty",s.difficulty},{"orderIce",s.orderIce},{"orderRemaining",s.orderRemaining},
		{"supplyRemaining",s.supplyRemaining},{"decisionRemaining",s.decisionRemaining},{"elapsed",s.elapsed},
		{"spent",s.spent},{"supplied",s.supplied},{"killIncome",s.killIncome},{"playerKillIncome",s.playerKillIncome},{"deployments",s.deployments},
		{"decisions",s.decisions},{"lastAttackRow",s.lastAttackRow},{"battleStarted",s.battleStarted},
		{"habits",s.habits},{"pending",nlohmann::json::array()}};
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
	s.spent = integer("spent", 0, 0, kMaxIce);
	s.supplied = integer("supplied", 0, 0, kMaxIce);
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
			if (id > 0 && cost > 0 && cost <= 10) s.refundableCosts.emplace(id, cost);
		}
	if (j.contains("habits") && j["habits"].is_array() && j["habits"].size() == 4)
		for (int i=0; i<4; ++i) { const float v=j["habits"][i].get<float>(); s.habits[i]=std::isfinite(v)?std::clamp(v,0.0f,1.0f):0; }
	s.pending.clear();
	if (j.contains("pending") && j["pending"].is_array()) for (const auto& p : j["pending"]) {
		const int type=p.value("type",-1), row=p.value("row",-1);
		const float remaining=p.value("remaining",0.0f);
		if (type<0 || type>=static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES) || row<0 || row>=mRows
			|| !std::isfinite(remaining) || s.pending.size()>=kMaxSimultaneous) continue;
		s.pending.push_back({static_cast<ZombieType>(type), row, std::clamp(p.value("cost",0),0,10),std::clamp(remaining,0.0f,60.0f)});
	}
}
