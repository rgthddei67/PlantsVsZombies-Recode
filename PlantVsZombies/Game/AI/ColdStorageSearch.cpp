#include "ColdStorageSearch.h"
#include "Game/Board/IceProduction.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/Zombie/CrystalDrummerRules.h"
#include "Game/Zombie/AuroraPriestRules.h"
#include "Game/Plant/BoundaryFlowerRules.h"
#include "Game/Plant/DawnLotusRules.h"
#include "Game/Zombie/ImpThrowRules.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace ColdStorageSearch {
namespace {
struct SearchCancelled {}; // 取消只由拥有任务的 Planner 捕获，不作为可提交结果
constexpr float kHorizon = 60; // 推演覆盖的游戏秒，实际对局评测负责检验更长期收益
constexpr float kStep = 0.5f; // 仅候选预测的积分步长；真实比赛仍使用正式固定步
constexpr int kTrials = 96; // 自由搜索的评估数，之后最多补六次同编队逐行比较
constexpr int kMaxActions = 8; // 小队搜索阶段的单位上限；升级后的完整编队搜索使用正式容量
constexpr float kMaxDelay = 12; // 新队员最迟出生时间，游戏秒
constexpr int kAdaptiveActions = 32; // 新模型可比较的最大编队，能力上限而非最低出兵数
constexpr float kAdaptiveDelay = 30; // 新模型可搜索的分批出生时域，游戏秒
constexpr float kAdaptiveHorizon = 90; // 新模型多观察一个后续交战窗口，游戏秒；产冰统计仍固定 60 秒
constexpr float kContact = 55; // 接触植物的预测距离，像素
constexpr float kAreaCounterStake = 24; // 玩家倾向对至少此冰价的集中兵力使用大范围清场；危及房屋时不等待
constexpr float kTargetCounterStake = 8; // 窄范围反制允许用于较小目标，冰价
constexpr float kSquashTriggerRange = 125; // 倭瓜候选种植点可触发近邻目标的预测距离，像素
constexpr float kSquashImpactRange = 50; // 倭瓜落点的窄范围碰撞近似，像素
constexpr float kSquashFlightSeconds = 0.6f; // Squash 起跳后上升/下落时长的近似，游戏秒；此阶段不再追踪
constexpr float kSquashLeadSeconds = 0.3f; // 对齐 Squash::StartRising 起跳时的目标运动预判，游戏秒
constexpr float kRecoveryReturnFraction = 0.5f; // 低库存增援至少应换回半数冰价的预测收入或有效削血价值
constexpr float kConstructionInterval = 2.0f; // 玩家模型两次建设决策间隔，游戏秒
constexpr int kConstructionPlantLimit = 128; // 单次推演的植物容量，含已毁植物，约束新增对象开销
constexpr int kPortfolioActions = 64; // 完整编队覆盖正式可用容量，仍受当前预算和剩余名额限制
constexpr int kPortfolioTrials = 192; // 第二版固定评估预算，含不同规模、同类/混编和错峰方案
constexpr float kPortfolioDelay = 60; // 第二版允许跨过一轮反制冷却的出生时域，游戏秒
constexpr float kPortfolioHorizon = 120; // 最晚队员也有完整交战窗口，游戏秒；产冰仍只计前 60 秒
constexpr int kForecastSummonLimit = 64; // 一次推演新增小鬼数量上限，不改变正式召唤上限
constexpr float kForecastImpWalkSpeed = 20; // 小鬼落地后的保守移动近似，像素/游戏秒
constexpr float kForecastImpLanding = .5f; // 落地动作阻止攻击/行走的近似时长，游戏秒
constexpr float kUnpricedCounterStake = 1; // 免费召唤的最低反制威胁，仅用于选灰烬落点，不计购买资产/返冰
constexpr float kEconomyClearSeconds = 2; // 返阳光卡铲除腾出周转格的保守预测耗时，游戏秒
constexpr int kInvestmentTrials = 32; // 大兵池额外经营对照的单阶段上限；不增加原攻击搜索预算或强制采购
constexpr int kSupportTrials = 32; // 护卫/光环协同额外对照上限，不强制购买
constexpr int kPrecisionTargets = 12; // 技能优先候选数，每个最多比较两个编队
constexpr int kQueueTrials = 40; // 已付队列每次滚动重评的候选预算，不改变单位数或购买预算
constexpr float kPatientCounterSeconds = 8; // 对手等聚团再交灰烬的一种预测习惯，游戏秒；与即时反制共同取保守结果
constexpr float kLargeInvestmentFraction = .5f; // 一次投入超过现有库存一半时，必须证明增量回报能覆盖费用
constexpr float kCapitalBlastFraction = .35f; // 爆区预计折损超过现有库存此比例时，不能依赖削血/位移维持大额采购
constexpr float kStoredCounterSeconds = 32; // 预存一次性清场可等后续部队聚集的保守对照，游戏秒；不延迟已提交爆炸

/** 采购先预留本案技能费；所有维修仍与部队、技能共用同一个钱包。 */
int PurchaseBudget(const Snapshot& s) { return s.budget-(s.precisionTargetID > 0 ? ColdStorageSkillRules::StrikeIceCost : 0); }
/** 只延续已经激活的优惠，不假设玩家将来必定续券；整数费用向上取整。 */
float PlayerIceCost(const Snapshot& s, float time, float cost) {
	return time < s.discountRemaining && cost > 0 ? std::ceil(cost/ColdStorageSkillRules::DiscountDivisor) : cost;
}

/** 维护“总剩余生命 + 其中护盾”的双层投影；穿透同时扣两层，剩余门不能救活已死本体。 */
float ApplyDamage(Unit& unit, float damage, bool penetrate = false, bool bypass = false, bool discardOverflow = false, PlantDamageOrigin origin = {}) {
	if (origin.IsValid() && unit.adaptedOrigin.IsValid() && origin == unit.adaptedOrigin) return 0;
	if (unit.adaptiveHelmet > 0 && origin.IsValid() && damage >= unit.adaptiveHelmet) {
		const float lost = unit.adaptiveHelmet;
		unit.body.health -= lost; unit.adaptiveHelmet = 0; unit.adaptedOrigin = origin;
		return lost; // 首次击穿整击只提交适应，不允许溢入本体。
	}
	auto& health = unit.body.health;
	const float before = health;
	const float shield = std::clamp(unit.shieldHealth,0.0f,std::max(0.0f,health));
	const float shieldLoss = bypass ? 0 : std::min(shield,damage);
	const float vitalLoss = bypass || penetrate ? damage : discardOverflow && shield > 0 ? 0 : damage-shieldLoss;
	const float vital = health-shield-vitalLoss;
	unit.shieldHealth = shield-shieldLoss;
	// 西瓜和大喷穿的是二类门盾；一类冰盾仍先于本体承受 vitalLoss。
	unit.adaptiveHelmet = std::max(0.0f,unit.adaptiveHelmet-std::max(0.0f,vitalLoss));
	unit.ritual.armor = std::max(0.0f,unit.ritual.armor-std::max(0.0f,vitalLoss));
	unit.repair.health = std::max(0.0f,unit.repair.health-std::max(0.0f,vitalLoss));
	health = vital > 0 ? vital+unit.shieldHealth : 0;
	return before-health;
}
struct PlantHit { float rate; bool penetrate, bypass, discardOverflow; };
/** 把单击修正换算回持续火力；西瓜溅射先分配伤害预算，再应用目标自身每击上限。 */
PlantHit DescribePlantHit(const Unit& unit, const Plant& plant, float fraction = 1) {
	const bool shielded = unit.shieldHealth > 0;
	const bool blocksFume = shielded && unit.blocksFumePiercing && plant.fume;
	const bool bypass = plant.bypassShield && !(shielded && unit.blocksShieldBypass);
	const float multiplier = plant.fume ? unit.fumeMultiplier : 1;
	const float hit = plant.hitDamage*fraction*multiplier;
	const float cap = shielded && !bypass ? unit.shieldedHitCap : 0;
	const float reduction = cap > 0 && hit > 0 ? std::min(1.0f,cap/hit) : 1;
	return {plant.dps*fraction*multiplier*reduction,plant.melon || (plant.fume && !blocksFume),bypass,blocksFume};
}
/** 已提交大招逐击结算；普通植物能力和灰烬分别使用目标自己的上限。 */
float ApplyDiscreteHit(Unit& unit, float damage, bool ash) {
	// 祭司沿用普通化灰入口，灰烬达到本体生命便直接死亡，仪器不能冒充冷链护盾保命。
	if (ash && unit.ritual.present && damage >= unit.body.health-unit.ritual.armor) {
		const float lost = unit.body.health; unit.body.health = 0; unit.ritual.armor = 0; return lost;
	}
	const float cap = unit.shieldHealth > 0 ? (ash ? unit.shieldedAshCap : unit.shieldedHitCap) : 0;
	return ApplyDamage(unit,cap > 0 ? std::min(damage,cap) : damage,false,false,false,
		ash ? PlantDamageOrigin::Ash() : PlantDamageOrigin::FromPlant(PlantType::PLANT_DAWNLOTUS));
}

/** 返回本次搜索阶段的容量；升级阶段可以比较整队，但不设置最低购买量。 */
int ActionLimit(const Snapshot& s) {
	return std::max(0,std::min(s.capacity,s.searchVersion == 2 ? kPortfolioActions : s.stateModel ? kAdaptiveActions : kMaxActions));
}
float DelayLimit(const Snapshot& s) { return s.searchVersion == 2 ? kPortfolioDelay : s.stateModel ? kAdaptiveDelay : kMaxDelay; }
/** 已付长队列需要完整战斗预测，但不因此跳过新购物车的小队搜索阶段。 */
bool FullForecast(const Snapshot& s) { return s.searchVersion == 2 || !s.committed.empty(); }
float Horizon(const Snapshot& s) { return FullForecast(s) ? kPortfolioHorizon : s.stateModel ? kAdaptiveHorizon : kHorizon; }

/** 先提交清洁车触发和扫过区间，再判断进屋；未出生部队不能被提前清掉，也不能重复使用同一辆车。 */
void AdvanceMowers(std::vector<Mower>& mowers, std::vector<Unit>& units, float time, float rightEdge) {
	for (auto& mower : mowers) {
		if (!mower.active) continue;
		const float left = mower.x;
		const float movement = mower.moving ? mower.speed*kStep : 0;
		const float right = mower.x+mower.width+movement;
		for (auto& unit : units) {
			auto& u = unit.body;
			if (u.health <= 0 || u.spawnAt > time || u.row != mower.row
				|| u.x+u.boundsOffset+u.boundsWidth < left || u.x+u.boundsOffset > right) continue;
			mower.moving = true;
			if (unit.consumesOtherMowers) for (auto& other : mowers) if (&other != &mower) other.active = false;
			if (!unit.mowerImmune) u.health = 0;
		}
		mower.x += movement;
		if (mower.x > rightEdge+100) mower.active = false;
	}
}

/** 独立采样完整编队，跨过单只亏损的局部最优；只使用合法选项，不指定任何兵种或必攻路线。 */
std::vector<Action> SamplePortfolio(const Snapshot& s, std::mt19937& rng, int trial) {
	std::vector<Action> plan;
	const int limit = ActionLimit(s);
	if (limit == 0 || s.options.empty()) return plan;
	const int count = trial % 4 == 0 ? limit : 1 + rng() % limit;
	const int groups = 1 + rng() % 3;
	const bool sameType = rng() % 2 == 0, sameRow = rng() % 2 == 0;
	const int base = rng() % s.options.size();
	const float duration = static_cast<float>(rng() % (static_cast<int>(DelayLimit(s)*2)+1)) * .5f;
	const bool gradual = rng() % 2 == 0;
	for (int group = 0; group < groups; ++group) {
		const int choice = group == 0 ? base : static_cast<int>(rng() % s.options.size());
		const auto& type = s.options[sameType ? base : choice];
		const int row = s.options[sameRow ? base : choice].row;
		const auto option = std::find_if(s.options.begin(),s.options.end(),[&](const auto& o) {
			return o.type == type.type && o.cost == type.cost && o.row == row;
		});
		if (option == s.options.end()) continue;
		for (int i = group*count/groups; i < (group+1)*count/groups; ++i) {
			const float delay = gradual ? duration*i/std::max(1,count-1) : duration*group/std::max(1,groups-1);
			plan.push_back({static_cast<int>(option-s.options.begin()),delay});
		}
	}
	int totalCost = 0;
	for (const auto& action : plan) totalCost += s.options[action.option].cost;
	if (totalCost > PurchaseBudget(s) && !plan.empty()) {
		// 按总预算缩小整案，不让后续 Repair 总是先买光前一批、再删掉后面的配合兵力。
		const int count = std::max(1,static_cast<int>(plan.size())*PurchaseBudget(s)/totalCost);
		std::vector<Action> affordable;
		for (int i = 0; i < count; ++i) affordable.push_back(plan[i*plan.size()/count]);
		plan = std::move(affordable);
	}
	return plan;
}

/** 分层抽样先给各可支付兵种一个合法落点，避免大兵池把低频经济/协同能力淹没；不按角色加权。 */
std::vector<int> SampleTypeCoverage(const Snapshot& s, std::mt19937& rng, int limit) {
	std::vector<std::vector<int>> types;
	for (int i=0; i<static_cast<int>(s.options.size()); ++i) {
		const auto& option = s.options[i];
		if (option.cost <= 0 || option.cost > PurchaseBudget(s)) continue;
		const auto group = std::find_if(types.begin(),types.end(),[&](const auto& entries) {
			return s.options[entries.front()].type == option.type;
		});
		if (group == types.end()) types.push_back({i});
		else group->push_back(i);
	}
	// 小兵池沿用已验证的随机流和搜索过程；只修复宽于小队容量的兵池覆盖稀释。
	if (types.size() <= kMaxActions) return {};
	std::shuffle(types.begin(),types.end(),rng);
	std::vector<int> result;
	for (const auto& type : types) {
		if (static_cast<int>(result.size()) >= limit) break;
		result.push_back(type[rng()%type.size()]);
	}
	return result;
}

/** 将一个新兵种试入已有优案；先腾出预算和容量，不能让合法候选在 Repair 中总被队尾截掉。 */
std::vector<Action> IntroduceOption(const Snapshot& s, std::vector<Action> plan, int option, std::mt19937& rng) {
	int cost = s.options[option].cost;
	for (const auto& action : plan) cost += s.options[action.option].cost;
	while (!plan.empty() && (static_cast<int>(plan.size()) >= ActionLimit(s) || cost > PurchaseBudget(s))) {
		const auto at = rng()%plan.size();
		cost -= s.options[plan[at].option].cost;
		plan.erase(plan.begin()+at);
	}
	// 无队友时从立即行动起评；已有编队仍自由比较分批时机，生产单位没有固定保护模板。
	const float delay = plan.empty() ? 0 : (rng()%(static_cast<int>(DelayLimit(s)*2)+1))*.5f;
	plan.push_back({option,delay});
	return plan;
}

/** 修复变异后的越界和超预算动作，稳定排序保留同一时刻的提交次序。 */
void Repair(const Snapshot& s, std::vector<Action>& actions) {
	int spent = 0;
	actions.erase(std::remove_if(actions.begin(), actions.end(), [&](Action& a) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) return true;
		const int cost = s.options[a.option].cost;
		if (cost <= 0 || spent + cost > PurchaseBudget(s)) return true;
		spent += cost;
		a.delay = std::clamp(a.delay, 0.0f, DelayLimit(s));
		return false;
	}), actions.end());
	const int limit = ActionLimit(s);
	if (actions.size() > static_cast<size_t>(limit)) actions.resize(limit);
	std::stable_sort(actions.begin(), actions.end(), [](auto a, auto b) { return a.delay < b.delay; });
}
float Score(const Weights& f, const Weights& w) {
	float value = 0;
	for (int i = 0; i < FeatureCount; ++i) value += f[i] * w[i];
	return value;
}

/** 汇总工人存活条件，只读当前快照和候选，不读取未来玩家动作。 */
ProductionFeatures ProductionInputs(const Snapshot& s, const std::vector<Action>& plan, float income) {
	ProductionFeatures f{}; f[0] = std::log1p(std::max(0.0f,income)); f[9] = s.budget / 100.0f;
	float count = 0;
	auto worker = [&](const Unit& unit, float entry) {
		++count; f[3] += unit.body.health / IceProduction::WorkerHealth;
		const auto& context = s.context[unit.body.row];
		f[6] += context[5]; f[7] += context[4]; f[8] += context[3];
		for (const auto& guard : s.current)
			if (!guard.body.economic && guard.body.health > 0 && guard.body.row == unit.body.row
				&& guard.body.spawnAt <= entry && (guard.body.x < unit.body.x
					|| (guard.body.spawnAt < entry && guard.body.speed > 0 && guard.body.x <= unit.body.x)))
				f[4] += guard.body.health / 3000.0f;
	};
	for (const auto& unit : s.current) if (unit.body.economic && unit.body.health > 0) { ++f[1]; worker(unit,unit.body.spawnAt); }
	for (const auto& action : plan) {
		const auto& option = s.options[action.option];
		if (option.unit.body.economic) {
			++f[2]; worker(option.unit,action.delay);
			for (const auto& escort : plan) {
				const auto& guard = s.options[escort.option];
				if (!guard.unit.body.economic && guard.row == option.row && escort.delay < action.delay)
					f[5] += guard.cost / 48.0f;
			}
		}
	}
	if (count > 0) for (int i = 3; i <= 8; ++i) f[i] /= count;
	return f;
}

/** 保留未经校准的训练输入，再用已验证的小模型折减经济预期。 */
void Calibrate(const Snapshot& s, Result& result) {
	result.rawProduction = result.features[4];
	result.productionInputs = ProductionInputs(s,result.actions,result.rawProduction);
	if (s.productionCalibration && result.rawProduction > 0)
		result.features[4] *= s.productionCalibration->Predict(result.productionInputs);
}

/** 对自由搜索与逐行对照使用同一收益、校准和兵种经验，避免比较时遗漏评分项。 */
Result EvaluatePlan(const Snapshot& s, const Weights& weights, std::vector<Action> plan, const Weights& baseline, float baselineOpponentAssets) {
	Result candidate;
	candidate.actions = std::move(plan);
	candidate.precisionTargetID = s.precisionTargetID;
	candidate.features = Evaluate(s, candidate.actions, &candidate.construction);
	Calibrate(s, candidate);
	candidate.baselineFeatures = baseline;
	candidate.score = Score(candidate.features, weights);
	candidate.opponentAssets = candidate.construction.opponentAssets;
	candidate.baselineOpponentAssets = baselineOpponentAssets;
	candidate.opponentScore = s.opponentWeight*(baselineOpponentAssets-candidate.opponentAssets);
	candidate.score += candidate.opponentScore;
	// 不把玩家一定会尽早交牌当成进攻收益。比较完整且各自合法的一次推演，
	// 不能逐项拼接两个世界的最坏损失，也不能让玩家凭空多出冷却或资源。
	float storedHold = kStoredCounterSeconds;
	// 长队列还能分批出生到一分钟之后。等待对照随已知的己方出生计划延长，
	// 避免新兵仅靠晚于固定等待窗口就被误判为已经骗掉了预存毁灭。
	for (const auto& unit : s.current) storedHold = std::max(storedHold,unit.body.spawnAt);
	for (const auto& action : candidate.actions) storedHold = std::max(storedHold,action.delay);
	for (float hold : {kPatientCounterSeconds,storedHold}) {
		if (std::none_of(s.counters.begin(),s.counters.end(),[&](const auto& counter) {
			return !counter.blast.committed && (hold == kPatientCounterSeconds || counter.stored);
		})) continue;
		Result patient;
		patient.actions = candidate.actions;
		patient.precisionTargetID = s.precisionTargetID;
		patient.features = Evaluate(s,patient.actions,&patient.construction,kPatientCounterSeconds,
			hold == kPatientCounterSeconds ? 0 : storedHold);
		Calibrate(s,patient);
		patient.baselineFeatures = baseline;
		patient.opponentAssets = patient.construction.opponentAssets;
		patient.baselineOpponentAssets = baselineOpponentAssets;
		patient.opponentScore = s.opponentWeight*(baselineOpponentAssets-patient.opponentAssets);
		patient.score = Score(patient.features,weights)+patient.opponentScore;
		patient.counterHoldSeconds = hold;
		if (patient.score < candidate.score) candidate = std::move(patient);
	}
	for (const auto& action : candidate.actions) {
		const auto& option = s.options[action.option];
		auto context = s.context[option.row];
		context[5] *= option.firePreferenceScale; // 盾牌偏好只受它实际能削弱的火力激励，不能把穿盾火力也算成优势。
		context[0] = 1;
		// 先前已付款且会先到场的队友也是协作背景，不能因其还在队列中而漏掉护卫。
		for (const auto& paid : s.committed) {
			const auto& ally = s.current[paid.unit].body;
			if (ally.row == option.row && ally.spawnAt <= action.delay) {
				context[2] += 1.0f / 3;
				if (ally.economic) context[6] += 1;
			}
		}
		// 计划中的同行队友也算协作背景，允许发现尚未出生的支援组合。
		for (const auto& other : candidate.actions) if (&action != &other) {
			const auto& ally = s.options[other.option];
			if (ally.row == option.row) { context[2] += 1.0f / 3; if (ally.unit.body.economic) context[6] += 1; }
		}
		for (int feature = 0; feature < ContextCount; ++feature) {
			const float value = option.preference[feature] * std::clamp(context[feature], 0.0f, 3.0f);
			candidate.score += value; candidate.preferenceScore += value;
		}
	}
	candidate.blastLoss = candidate.features[6];
	return candidate;
}

/** 只更换计划中尚未购买单位的行；缺少同兵种同费用选项时整案无效，不偷偷删兵或换兵。 */
bool Concentrate(const Snapshot& s, std::vector<Action>& plan, int row) {
	for (auto& action : plan) {
		const auto& original = s.options[action.option];
		if (original.row == row) continue;
		const auto match = std::find_if(s.options.begin(), s.options.end(), [&](const auto& option) {
			return option.row == row && option.type == original.type && option.cost == original.cost;
		});
		if (match == s.options.end()) return false;
		action.option = static_cast<int>(match - s.options.begin());
	}
	return true;
}

struct PendingCounter {
	ColdStorageStrategy::BlastThreat blast;
	float at;
	int target = -1;
	float lastTargetX = 0;
	bool targetLocked = false;
	int plantID = 0;
	float invulnerableAt = 0;
};

/** 模拟已经种下的逐行主动打击：按生命和推进择敌，前排位置本身不能替工人挡主伤害。 */
void AdvanceRowStrikes(const Snapshot& state, const std::vector<RowStrike>& strikes, float time, const std::vector<Plant>& plants,
	std::vector<Unit>& units, const std::vector<float>& initialHealth, std::vector<float>& ready, Weights& features) {
	for (size_t ability = 0; ability < strikes.size(); ++ability) {
		const auto& strike = strikes[ability];
		if (time < ready[ability] || std::none_of(plants.begin(),plants.end(),[&](const auto& plant) {
			return plant.id == strike.plantID && plant.health > 0;
		})) continue;
		bool used = false;
		for (int row = 0; row < static_cast<int>(state.context.size()); ++row) {
			int target = -1;
			long long best = -1, bestID = 0;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i];
				if (u.body.health <= 0 || u.body.spawnAt > time || u.body.row != row) continue;
				const auto score = DawnLotusRules::ThreatScore(static_cast<long long>(u.body.health),
					u.body.x+u.body.blastAnchorOffset,state.rightEdge);
				const long long id = u.id > 0 ? u.id : 0x100000000LL+static_cast<long long>(i);
				if (target < 0 || score > best || (score == best && id < bestID)) {
					target = static_cast<int>(i); best = score; bestID = id;
				}
			}
			if (target < 0) continue;
			used = true;
			const float center = units[target].body.x + units[target].body.blastAnchorOffset;
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > time || u.row != row) continue;
				const bool primary = static_cast<int>(i) == target;
				if (!primary && std::abs(u.x+u.blastAnchorOffset-center) > strike.radius) continue;
				const float damage = ApplyDiscreteHit(units[i],primary ? strike.damage : strike.splashDamage,false);
				features[6] += u.purchaseCost * damage / std::max(1.0f,initialHealth[i]);
			}
		}
		// 无目标时保留充能；释放一次覆盖所有行，而不是让每行各自获得独立冷却。
		if (used) ready[ability] = time + strike.recharge;
	}
}

/** 按真实爆区覆盖已经出生的单位；场外不是灰烬免疫区，尚未出生的队列仍不承伤。 */
bool CounterHits(const ColdStorageStrategy::BlastThreat& blast, const Unit& unit, float time) {
	const auto& body = unit.body;
	return body.health > 0 && body.spawnAt <= time && blast.reach[body.row] >= 0
		&& std::abs(body.x + (blast.usesObjectX ? body.blastAnchorOffset : 0) - blast.x) <= blast.reach[body.row];
}

/** 多张牌共享真实资源、同卡落点共享冷却；先兑现已提交反制，再选择一次可支付的新动作。 */
void AdvanceCounters(const Snapshot& state, float time, std::vector<Plant>& plants, std::vector<Unit>& units,
	const std::vector<float>& initialHealth, std::vector<float>& ready,
	std::vector<PendingCounter>& pending, float& sun, float& ice, Weights& features,
	float holdSeconds, float storedHoldSeconds, std::vector<float>& holdUntil) {
	for (auto it = pending.begin(); it != pending.end();) {
		if (it->plantID > 0) {
			const auto source = std::find_if(plants.begin(),plants.end(),[&](const auto& p) { return p.id == it->plantID; });
			// 咖啡已经放下仍不等于爆炸已脱离宿主；睡眠/唤醒等待阶段被吃掉就不能引爆。
			if (source == plants.end() || source->health <= 0) { it = pending.erase(it); continue; }
			if (time >= it->invulnerableAt) source->edible = false;
		}
		// 假想新种倭瓜在起跳前继续追踪；不能把移动目标仍判在最初的观察落点。
		// 正式已提交反制没有 target 索引，保持其快照落点，避免替玩家撤销或重新瞄准。
		if (it->target >= 0 && !it->targetLocked) {
			const auto& target = units[it->target].body;
			if (target.health > 0) {
				const float velocity = (target.x - it->lastTargetX) / kStep;
				it->blast.x = target.x + (it->blast.usesObjectX ? target.blastAnchorOffset : 0);
				if (time >= it->at - kSquashFlightSeconds) it->blast.x += velocity * kSquashLeadSeconds;
				it->lastTargetX = target.x;
			}
			if (time >= it->at - kSquashFlightSeconds) it->targetLocked = true;
		}
		if (it->at > time) { ++it; continue; }
		for (size_t i = 0; i < units.size(); ++i) if (CounterHits(it->blast, units[i], time)) {
			auto& body = units[i].body;
			const float damage = ApplyDiscreteHit(units[i],it->blast.damage,true);
			features[6] += body.purchaseCost * damage / std::max(1.0f, initialHealth[i]);
		}
		if (it->plantID > 0) {
			const auto source = std::find_if(plants.begin(),plants.end(),[&](const auto& p) { return p.id == it->plantID; });
			for (auto& plant : plants) if (plant.row == source->row && plant.column == source->column) plant.health = 0;
		}
		it = pending.erase(it);
	}
	int selected = -1;
	int selectedTarget = -1;
	float best = 0;
	ColdStorageStrategy::BlastThreat impact;
	for (size_t c = 0; c < state.counters.size(); ++c) {
		const auto& counter = state.counters[c];
		if (counter.blast.committed || time < ready[counter.source]
			|| (counter.sharedSource >= 0 && time < ready[counter.sharedSource])
			|| counter.sunCost > sun || PlayerIceCost(state,time,counter.iceCost) > ice) continue;
		if (counter.plantID > 0 && std::none_of(plants.begin(),plants.end(),[&](const auto& p) {
			return p.id == counter.plantID && p.health > 0;
		})) continue;
		if (counter.cellRow >= 0 && std::any_of(plants.begin(),plants.end(),[&](const auto& p) {
			return p.health > 0 && p.layer == 1 && p.row == counter.cellRow && p.column == counter.cellColumn;
		})) continue;
		auto candidate = counter.blast;
		int candidateTarget = -1;
		if (counter.targeted) {
			float nearest = kSquashTriggerRange;
			int target = -1;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i].body;
				const float distance = std::abs(u.x - candidate.x);
				if (u.health > 0 && u.spawnAt <= time && u.x <= state.rightEdge
					&& candidate.reach[u.row] >= 0 && distance < nearest) { nearest = distance; target = static_cast<int>(i); }
			}
			if (target < 0) continue;
			candidateTarget = target;
			candidate.x = units[target].body.x + units[target].body.blastAnchorOffset;
			for (float& reach : candidate.reach) if (reach >= 0) reach = kSquashImpactRange;
		}
		float loss = 0;
		bool urgent = false;
		for (size_t i = 0; i < units.size(); ++i) if (CounterHits(candidate, units[i], time)) {
			auto projected = units[i];
			// 不连续把三张清场牌浪费在已被另一张锁定的濒死目标上。
			for (const auto& scheduled : pending) if (CounterHits(scheduled.blast, units[i], time))
				ApplyDiscreteHit(projected,scheduled.blast.damage,true);
			if (projected.body.health <= 0) continue;
			const float stake = std::max(kUnpricedCounterStake,units[i].body.purchaseCost > 0 ? units[i].body.purchaseCost : units[i].body.value);
			loss += stake * ApplyDiscreteHit(projected,candidate.damage,true) / std::max(1.0f, initialHealth[i]);
			urgent = urgent || units[i].body.x < state.houseX + 240;
		}
		if (loss < (urgent ? 1 : counter.targeted ? kTargetCounterStake : kAreaCounterStake)) continue;
		// 等待从首次存在值得反制的目标开始；同一张牌所有落点共用一次等待。
		// 已提交的爆炸走上方独立结算；接近房屋的救险也不为了聚团继续等。
		const float patience = counter.stored ? std::max(holdSeconds,storedHoldSeconds) : holdSeconds;
		if (!urgent && patience > 0) {
			if (holdUntil[counter.source] < 0) holdUntil[counter.source] = time+patience;
			if (time < holdUntil[counter.source]) continue;
		}
		const float value = loss / std::max(1.0f, counter.sunCost * 0.02f + PlayerIceCost(state,time,counter.iceCost) * 0.2f);
		if (value > best) { best = value; selected = static_cast<int>(c); impact = candidate; selectedTarget = candidateTarget; }
	}
	if (selected >= 0) {
		const auto& counter = state.counters[selected];
		sun -= counter.sunCost; ice -= PlayerIceCost(state,time,counter.iceCost);
		ready[counter.source] = time + counter.recharge;
		if (counter.sharedSource >= 0) ready[counter.sharedSource] = time + counter.sharedRecharge;
		holdUntil[counter.source] = -1;
		pending.push_back({impact,time + counter.windup,selectedTarget,
			selectedTarget >= 0 ? units[selectedTarget].body.x : 0});
		pending.back().plantID = counter.plantID;
		pending.back().invulnerableAt = time + counter.vulnerableSeconds;
	}
}
}

/** 按眼前威胁选择可能的补阵，建设与灰烬共用实际资源，不替僵尸指定打法。 */
static void AdvanceConstruction(const Snapshot& state, float time, float horizon, const std::vector<Unit>& units,
	std::vector<Plant>& plants, std::vector<float>& ready, std::vector<RowStrike>& strikes,
	std::vector<float>& strikeReady, float& sun, float& ice, ConstructionStats& stats) {
	if (plants.size() >= kConstructionPlantLimit) return;
	std::array<float,6> threat{}, nearest{}, fire{};
	nearest.fill(state.rightEdge+100);
	for (const auto& u : units) if (u.body.health > 0 && u.body.spawnAt <= time && u.body.x <= state.rightEdge) {
		threat[u.body.row] += u.body.health;
		nearest[u.body.row] = std::min(nearest[u.body.row],u.body.x);
	}
	for (const auto& p : plants) if (p.health > 0) fire[p.row] += p.dps;
	int selected = -1;
	float best = 0;
	for (size_t i = 0; i < state.construction.size(); ++i) {
		const auto& card = state.construction[i]; const auto& p = card.plant;
		if (time < ready[card.source] || card.sunCost > sun || PlayerIceCost(state,time,card.iceCost) > ice) continue;
		// 曙光莲正式限制是同时一株；死亡后才可再次建设，不能按卡槽数量复制名额。
		if (card.strike.damage > 0 && std::any_of(strikes.begin(),strikes.end(),[&](const auto& strike) {
			return std::any_of(plants.begin(),plants.end(),[&](const auto& p) { return p.health > 0 && p.id == strike.plantID; });
		})) continue;
		// 当前合法格也可能被上次假想建设占用；南瓜壳与宿主分别占层。
		if (std::any_of(plants.begin(),plants.end(),[&](const auto& existing) {
			return existing.health > 0 && existing.row == p.row && existing.column == p.column && existing.layer == p.layer;
		})) continue;
		if (p.x+kContact >= nearest[p.row]) continue;
		const float remaining = horizon-time;
		float value = p.dps*remaining*(0.2f+threat[p.row]/(1000+threat[p.row]));
		value += p.sunPerSecond*std::max(0.0f,remaining-card.firstSunDelay);
		if (card.strike.damage > 0) {
			float targets = 0;
			for (float hp : threat) targets += std::min(hp,card.strike.damage);
			value += targets*std::max(0.0f,remaining-card.strike.ready)/std::max(1.0f,card.strike.recharge);
		}
		if (p.dps <= 0 && p.sunPerSecond <= 0 && card.strike.damage <= 0)
			value += std::min(p.health,threat[p.row])*(0.25f+fire[p.row]/50)
				/ (1+std::max(0.0f,nearest[p.row]-p.x)/200);
		else value /= 1+0.15f*p.column; // 输出和生产在后方合法位置有更长的存活机会
		value /= std::max(25,card.sunCost)+PlayerIceCost(state,time,card.iceCost);
		if (value > best) { best = value; selected = static_cast<int>(i); }
	}
	if (selected < 0) return;
	const auto& card = state.construction[selected];
	auto plant = card.plant;
	plant.id = -100000-static_cast<int>(plants.size());
	plant.initialHealth = plant.health;
	plant.productionAt = time+card.firstSunDelay;
	plants.push_back(plant);
	ready[card.source] = time+std::max(kConstructionInterval,card.recharge);
	sun -= card.sunCost; ice -= PlayerIceCost(state,time,card.iceCost);
	++stats.planted; stats.sunSpent += card.sunCost; stats.iceSpent += PlayerIceCost(state,time,card.iceCost);
	if (card.strike.damage > 0) {
		auto strike = card.strike; strike.plantID = plant.id;
		strikes.push_back(strike); strikeReady.push_back(time+strike.ready);
	}
}

bool ValidWeights(const Weights& weights) {
	return std::all_of(weights.begin(), weights.end(), [](float w) { return std::isfinite(w) && std::abs(w) <= 500; });
}

/** 当前存活领域附近是否有能向已入场敌人开火的植物，不读取未来玩家操作。 */
static bool AuraHasTarget(const Snapshot& state, float time, const Plant& source,
	const std::vector<Plant>& plants, const std::vector<Unit>& units) {
	for (const auto& p : plants) {
		if (p.health <= 0 || p.dps <= 0 || std::abs(p.row-source.row)>1 || std::abs(p.column-source.column)>1) continue;
		for (const auto& u : units) if (u.body.health > 0 && u.body.spawnAt <= time && u.body.x <= state.rightEdge
			&& std::abs(u.body.row-p.row) <= p.rowRadius
			&& (p.around ? std::abs(u.body.x-p.x) <= p.range : u.body.x >= p.x-30 && u.body.x <= p.x+p.range)) return true;
	}
	return false;
}

/** 按真实卡价、独立冷却与空格周转预测玩家续航；订单付款、运输、到货分别结算。 */
static void AdvancePlayerEconomy(const Snapshot& state, float time, const std::vector<Plant>& plants,
	std::vector<float>& exchangeReady, std::vector<std::pair<std::array<int,2>,float>>& occupied,
	const std::vector<float>& counterReady, const std::vector<float>& constructionReady,
	const std::vector<AttackAura>& auras, const std::vector<Unit>& units,
	float& sun, float& ice, float& pendingIce, float& arrival, ConstructionStats& stats) {
	occupied.erase(std::remove_if(occupied.begin(),occupied.end(),[&](const auto& p) { return p.second <= time; }),occupied.end());
	float neededIce = 0;
	float delivery = 0;
	for (const auto& order : state.shop) delivery = std::max(delivery,order.delivery);
	for (size_t i = 0; i < state.exchanges.size(); ++i) {
		const auto& card = state.exchanges[i];
		if (card.sunGain <= 0 || PlayerIceCost(state,time,card.iceCost) < 0 || card.cells.empty()) continue;
		for (const auto& cell : card.cells) {
			if (std::any_of(plants.begin(),plants.end(),[&](const auto& p) {
				return p.health > 0 && p.layer == 1 && p.row == cell[0] && p.column == cell[1];
			}) || std::any_of(occupied.begin(),occupied.end(),[&](const auto& p) { return p.first == cell; })) continue;
			if (exchangeReady[i] <= time+delivery) neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
			if (exchangeReady[i] > time || ice < PlayerIceCost(state,time,card.iceCost) || sun >= state.playerSunLimit) break;
			const float gain = std::min(static_cast<float>(card.sunGain),std::max(0.0f,state.playerSunLimit-sun));
			sun += gain; ice -= PlayerIceCost(state,time,card.iceCost);
			exchangeReady[i] = time+std::max(kEconomyClearSeconds,card.recharge);
			occupied.push_back({cell,time+kEconomyClearSeconds});
			++stats.exchanges; stats.exchangeSun += gain; stats.exchangeIce += PlayerIceCost(state,time,card.iceCost);
			break;
		}
	}
	for (const auto& card : state.counters) {
		if (card.blast.committed || counterReady[card.source] > time+delivery
			|| (card.sharedSource >= 0 && counterReady[card.sharedSource] > time+delivery)) continue;
		if (card.plantID > 0 && std::none_of(plants.begin(),plants.end(),[&](const auto& p) {
			return p.id == card.plantID && p.health > 0;
		})) continue;
		neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
	}
	for (const auto& card : state.construction) if (constructionReady[card.source] <= time+delivery)
		neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
	// 已部署的付费能力也能形成订冰需求；来源消失或手动模式没有受益目标时不凭空补货。
	for (const auto& aura : auras) if (aura.active+aura.cooldown <= delivery && aura.blockedUntil <= time+delivery) {
		const auto source = std::find_if(plants.begin(),plants.end(),[&](const Plant& p) { return p.id == aura.plantID && p.health > 0; });
		if (source != plants.end() && (aura.automatic || AuraHasTarget(state,time,*source,plants,units)))
			neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,aura.iceCost)));
	}
	for (const auto& p : plants) if (p.health > 0 && p.repairMaximum-p.health >= p.repairAmount
		&& p.repairRemaining <= delivery && p.repairBlockedUntil <= time+delivery)
		neededIce = std::max(neededIce,PlayerIceCost(state,time,p.repairCost));
	if (pendingIce > 0 || ice >= neededIce || ice >= state.playerIceLimit) return;
	const ShopOrder* selected = nullptr;
	for (const auto& order : state.shop) {
		if (order.sunCost <= 0 || order.iceGain <= 0 || order.delivery < 0 || sun < order.sunCost
			|| time+order.delivery >= Horizon(state)) continue;
		if (!selected || order.sunCost/static_cast<float>(order.iceGain) < selected->sunCost/static_cast<float>(selected->iceGain)) selected = &order;
	}
	if (!selected) return;
	sun -= selected->sunCost; pendingIce = static_cast<float>(selected->iceGain); arrival = time+selected->delivery;
	++stats.orders; stats.orderSun += selected->sunCost; stats.orderIce += selected->iceGain;
}

bool StateModel::IsValid() const {
	return std::all_of(coefficients.begin(),coefficients.end(),[](const auto& row) { return ValidWeights(row); });
}

StateFeatures DescribeState(const Snapshot& s, const Weights& baseline) {
	StateFeatures f{};
	// 这些仅是输入单位归一化，不对应强制进攻阈值；系数正负和组合由实战训练选择。
	f[0] = std::log1p(std::max(0,s.budget)) / std::log(101.0f);
	f[1] = baseline[4] / 100;
	for (const auto& p : s.plants) if (p.health > 0) { f[2] += p.dps / 300; f[3] += p.sunPerSecond / 10; }
	std::vector<int> sources;
	for (const auto& c : s.counters) {
		if (c.blast.committed || c.blast.ready > kMaxDelay || c.sunCost > s.playerSun || c.iceCost > s.playerIce) continue;
		if (std::find(sources.begin(),sources.end(),c.source) == sources.end()) sources.push_back(c.source);
	}
	f[4] = static_cast<float>(sources.size()) / 3;
	for (const auto& strike : s.rowStrikes) if (strike.ready <= kMaxDelay) f[4] += 1.0f / 3;
	f[5] = s.noProgressSeconds / 90;
	for (auto& value : f) value = std::clamp(value,0.0f,3.0f);
	return f;
}

Weights ConditionWeights(const Weights& base, const StateFeatures& inputs, const StateModel* model) {
	auto result = base;
	if (model) for (int j = 0; j < FeatureCount; ++j) {
		for (int i = 0; i < StateFeatureCount; ++i) result[j] += inputs[i] * model->coefficients[i][j];
		result[j] = std::clamp(result[j],-500.0f,500.0f);
	}
	return result;
}

Weights AccountForIce(const Weights& conditioned) {
	auto result = conditioned;
	// 同一种货币不能收入按高价、支出按低价；保留可训练的经济重要性和战术收益。
	const float value = std::clamp(std::abs(conditioned[4]),0.01f,500.0f);
	result[0] = std::clamp(conditioned[0] + value,-500.0f,500.0f); // 击杀既有破阵价值，也兑现冰收入
	result[3] = value * std::clamp(conditioned[3],0.0f,1.0f);
	result[4] = value;
	result[5] = -value;
	result[6] = -std::abs(conditioned[6]); // 爆区损失是风险成本，不能成为替代的采购奖励
	return result;
}

bool ProductionCalibration::IsValid() const {
	if (nodes.empty() || nodes.size() > 63) return false;
	for (size_t i = 0; i < nodes.size(); ++i) {
		const auto& n = nodes[i];
		if (!std::isfinite(n.value) || n.value < 0 || n.value > 1 || !std::isfinite(n.threshold)) return false;
		if (n.feature == -1) { if (n.left != -1 || n.right != -1) return false; }
		else if (n.feature < 0 || n.feature >= ProductionFeatureCount || n.left <= static_cast<int>(i)
			|| n.right <= static_cast<int>(i) || n.left >= static_cast<int>(nodes.size()) || n.right >= static_cast<int>(nodes.size())) return false;
	}
	return true;
}

float ProductionCalibration::Predict(const ProductionFeatures& features) const {
	int at = 0;
	for (size_t step = 0; step < nodes.size(); ++step) {
		const auto& node = nodes[at];
		if (node.feature == -1) return node.value;
		at = features[node.feature] <= node.threshold ? node.left : node.right;
	}
	return 1;
}

float ShieldProtectionFraction(const Unit& unit, const Plant& plant) {
	if (unit.shieldHealth <= 0 || plant.dps <= 0) return 1;
	const auto hit = DescribePlantHit(unit,plant);
	return hit.penetrate || hit.bypass ? std::clamp(1-hit.rate/plant.dps,0.0f,1.0f) : 1;
}

bool ShouldRegroup(const Result& result, int budget, int reserve) {
	if (budget >= reserve || result.actions.empty()) return false;
	const auto& plan = result.features;
	const auto& baseline = result.baselineFeatures;
	if (plan[2] > baseline[2]) return false;
	// 只比较买与不买的差异；新策略可计对方资源消耗，但支出偏好、单纯位移都不是回报。
	const float pressure = result.opponentScore > 0 ? result.baselineOpponentAssets-result.opponentAssets : 0;
	const float gain = (plan[0] - baseline[0]) + (plan[1] - baseline[1]) + (plan[4] - baseline[4]) + pressure;
	return gain < std::max(0.0f,plan[5]-baseline[5]) * kRecoveryReturnFraction;
}

bool ShouldConserveCapital(const Result& result, int budget, int reserve) {
	if (result.actions.empty()) return false;
	const auto& plan = result.features;
	const auto& baseline = result.baselineFeatures;
	if (plan[2] > baseline[2]) return false;
	const float spent = std::max(0.0f,plan[5]-baseline[5]);
	const float cash = plan[0]-baseline[0]+plan[4]-baseline[4];
	const float blastLoss = std::max(0.0f,plan[6]-baseline[6]);
	// 制冰和击杀才是可再投资的现金；已有部队收入、残存兵价与学到的偏好不能替新投资回本。
	if (blastLoss > budget*kCapitalBlastFraction && cash < spent) return true;
	if (spent <= budget*kLargeInvestmentFraction && budget-spent >= reserve) return false;
	const float pressure = result.opponentScore > 0 ? result.baselineOpponentAssets-result.opponentAssets : 0;
	return cash+plan[1]-baseline[1]+pressure < spent;
}

/** 推进一次性付费阶段；硬控只暂停预热，已经付款的爆发/恢复仍消耗游戏时间。 */
static std::array<float,2> AdvanceBurst(Unit& unit, bool inRange, float active, float& ice,
	Weights& features, ConstructionStats& stats) {
	auto& b = unit.burst;
	using Stage = PaidBurst::Stage;
	if (b.range <= 0) return {active,active};
	if (unit.body.health <= b.stopHealth) b.stage = Stage::SPENT;
	float wall = kStep, stopped = kStep-active;
	std::array<float,2> seconds{};
	// 至多经过预热、爆发、恢复；失败支付产生正重试间隔，不会在零时间反复扣款。
	while (wall > 0.0001f) {
		if (b.stage == Stage::READY && b.retryRemaining <= 0 && inRange && wall > stopped) {
			b.stage = Stage::WINDUP; b.remaining = b.windup;
		}
		float span = wall;
		if (b.stage == Stage::WINDUP) span = std::min(span,stopped+std::max(0.0f,b.remaining));
		else if (b.stage == Stage::ACTIVE || b.stage == Stage::RECOVERY) span = std::min(span,std::max(0.0f,b.remaining));
		else if (b.stage == Stage::READY && b.retryRemaining > 0) span = std::min(span,b.retryRemaining);
		const float free = std::max(0.0f,span-stopped);
		stopped = std::max(0.0f,stopped-span);
		if (b.stage == Stage::WINDUP) b.remaining -= free;
		else {
			seconds[0] += free*(b.stage == Stage::ACTIVE ? b.moveMultiplier : b.stage == Stage::RECOVERY ? b.recoveryMoveMultiplier : 1);
			if (b.stage != Stage::RECOVERY) seconds[1] += free*(b.stage == Stage::ACTIVE ? b.biteMultiplier : 1);
			if (b.stage == Stage::ACTIVE || b.stage == Stage::RECOVERY) b.remaining -= span;
		}
		b.retryRemaining = std::max(0.0f,b.retryRemaining-span);
		wall -= span;
		if (b.remaining <= 0) {
			if (b.stage == Stage::WINDUP) {
				if (ice >= b.cost) {
					ice -= b.cost; features[5] += b.cost;
					stats.abilityIceSpent += b.cost; ++stats.burstActivations;
					b.stage = Stage::ACTIVE; b.remaining = b.duration;
				} else {
					b.stage = Stage::READY; b.retryRemaining = std::max(kStep,b.retry);
				}
			} else if (b.stage == Stage::ACTIVE) { b.stage = Stage::RECOVERY; b.remaining = b.recovery; }
			else if (b.stage == Stage::RECOVERY) b.stage = Stage::SPENT;
		}
	}
	return seconds;
}

/** 临时领域按来源存活、真实钱包与持续时间结算；手动模式只预测当前确有受益目标的释放。 */
static void AdvanceAttackAuras(const Snapshot& state, float time, std::vector<AttackAura>& auras,
	const std::vector<Plant>& plants, const std::vector<Unit>& units, float& ice,
	std::vector<float>& rates, ConstructionStats& stats) {
	rates.assign(plants.size(),1);
	for (auto& aura : auras) {
		const auto source = std::find_if(plants.begin(),plants.end(),[&](const Plant& p) { return p.id == aura.plantID && p.health > 0; });
		if (source == plants.end()) continue;
		const bool threatened = !aura.automatic && AuraHasTarget(state,time,*source,plants,units);
		if (aura.active <= 0 && aura.cooldown <= 0 && time >= aura.blockedUntil
			&& (aura.automatic || threatened) && ice >= PlayerIceCost(state,time,aura.iceCost)) {
			ice -= PlayerIceCost(state,time,aura.iceCost); stats.iceSpent += PlayerIceCost(state,time,aura.iceCost); ++stats.auraActivations;
			aura.active = aura.duration;
		}
		const float boosted = std::min(kStep,aura.active);
		if (boosted > 0) {
			for (size_t i=0; i<plants.size(); ++i)
				if (std::abs(plants[i].row-source->row)<=1 && std::abs(plants[i].column-source->column)<=1)
					rates[i] += aura.bonus*boosted/kStep;
			aura.active -= boosted;
			if (aura.active <= 0) aura.cooldown = aura.recharge;
		}
		aura.cooldown = std::max(0.0f,aura.cooldown-(kStep-boosted));
	}
}

/** 一类盾只修剩余护甲；硬控暂停计时，满盾/缺钱均消耗本轮机会，不攒免费修复。 */
static void AdvanceArmorRepair(Unit& unit, float active, float& ice, Weights& features, ConstructionStats& stats) {
	auto& r = unit.repair;
	if (active <= 0 || r.interval <= 0 || r.health <= 0 || unit.body.health-r.health <= r.stopBodyHealth) return;
	r.remaining -= active;
	while (r.remaining <= 0) {
		r.remaining += r.interval;
		if (r.health >= r.maximum || ice < r.cost) continue;
		const float restored = std::min(r.amount,r.maximum-r.health);
		ice -= r.cost; features[5] += r.cost;
		r.health += restored; unit.body.health += restored;
		stats.abilityIceSpent += r.cost; stats.armorRepairIce += r.cost; ++stats.armorRepairs;
	}
}

/** 统一计入啃食与砸击的实际削血；回血会撤回已恢复的削血分，不能靠反复刷血赚分。 */
static bool DamagePlant(Plant& plant, float damage, bool crush, Weights& features) {
	if (plant.health <= 0 || plant.immuneRemaining > 0 || damage <= 0) return false;
	if (crush) damage = plant.crushDamage > 0 ? plant.crushDamage : plant.health;
	const float credit = plant.reward*std::min(plant.health,damage)/std::max(1.0f,plant.initialHealth);
	features[1] += credit; plant.damageCredit += credit;
	plant.health = std::max(0.0f,plant.health-damage);
	if (plant.health <= 0) features[0] += plant.reward;
	else if (crush) plant.immuneRemaining = plant.immuneDuration;
	return true;
}

/** 推进已部署/预测新建坚果的独立计时与付费修复，不能复活或在无敌期重复触发承伤。 */
static void AdvancePlantRepairs(const Snapshot& state, float time, std::vector<Plant>& plants, const std::vector<Unit>& units,
	float& ice, Weights& features, ConstructionStats& stats, bool automaticPhase) {
	for (auto& p : plants) if (p.health > 0 && p.repairMaximum > 0) {
		if (automaticPhase) {
			p.immuneRemaining = std::max(0.0f,p.immuneRemaining-kStep);
			p.repairRemaining = std::max(0.0f,p.repairRemaining-kStep);
		}
		if (p.repairAutomatic != automaticPhase) continue;
		const float missing = p.repairMaximum-p.health;
		if (missing <= 0 || p.repairRemaining > 0 || time < p.repairBlockedUntil || ice < PlayerIceCost(state,time,p.repairCost)) continue;
		// 手动修复只在近身威胁下预测，并排在灰烬反制后，不能假设玩家先花掉救命钱。
		const bool threatened = !p.repairAutomatic && std::any_of(units.begin(),units.end(),[&](const Unit& u) {
			return u.body.health > 0 && u.body.spawnAt <= time && u.body.row == p.row && std::abs(u.body.x-p.x) <= 2*kContact;
		});
		if (!p.repairAutomatic && !threatened) continue;
		if (missing < p.repairAmount && (p.repairAutomatic || p.health > p.repairAmount)) continue;
		const float restored = std::min(missing,p.repairAmount);
		p.health += restored; ice -= PlayerIceCost(state,time,p.repairCost); p.repairRemaining = p.repairRecharge;
		const float credit = std::min(p.damageCredit,p.reward*restored/std::max(1.0f,p.initialHealth));
		p.damageCredit -= credit; features[1] -= credit;
		stats.iceSpent += PlayerIceCost(state,time,p.repairCost); stats.plantRepairIce += PlayerIceCost(state,time,p.repairCost); ++stats.plantRepairs;
	}
}

/** 已提交鼓舞按墙钟到期；敲鼓按未硬控且受冰减速的时间推进，前摇不移动或啃食。 */
static void AdvanceDrums(const Snapshot& s, float time, std::vector<Unit>& units,
	ConstructionStats& stats, std::vector<float>& activity) {
	using namespace CrystalDrummerRules;
	activity.assign(units.size(),1);
	for (auto& unit : units) {
		for (auto& layer : unit.inspiration) layer.second -= kStep;
		unit.inspiration.erase(std::remove_if(unit.inspiration.begin(),unit.inspiration.end(),
			[](const auto& layer) { return layer.second <= 0; }),unit.inspiration.end());
	}
	const auto column = [&](const Unit& u) {
		return static_cast<int>(std::floor((u.body.x+u.body.blastAnchorOffset-s.gridLeft)/s.cellWidth));
	};
	for (size_t i=0; i<units.size(); ++i) {
		auto& source = units[i]; auto& drum = source.drum;
		if (!drum.enabled || source.body.health <= drum.stopHealth || source.body.spawnAt > time) continue;
		const float active = std::max(0.0f,kStep-source.body.stopped);
		const float slow = source.body.slow > 0 ? .5f : 1;
		float available = active*slow, walking = 0;
		while (available > 0) {
			const float consumed = std::min(available,std::max(0.0f,drum.remaining));
			if (!drum.winding) walking += consumed;
			available -= consumed; drum.remaining -= consumed;
			if (drum.remaining > 0) break;
			if (!drum.winding) { drum.winding = true; drum.remaining = Windup; continue; }
			drum.winding = false; drum.remaining = BeatInterval-Windup;
			++stats.drumBeats;
			const int sc = column(source);
			if (sc < 0 || sc >= s.columns || source.body.row < 0 || source.body.row >= s.rows) continue;
			for (auto& target : units) {
				if (&target == &source || target.body.health <= 0 || target.body.spawnAt > time) continue;
				const int tc = column(target);
				if (tc < 0 || tc >= s.columns || target.body.row < 0 || target.body.row >= s.rows
					|| std::abs(sc-tc)+std::abs(source.body.row-target.body.row) > Range) continue;
				auto layer = std::find_if(target.inspiration.begin(),target.inspiration.end(),
					[&](const auto& item) { return item.first == source.id; });
				if (layer == target.inspiration.end()) target.inspiration.emplace_back(source.id,Duration);
				else layer->second = Duration;
				++stats.drumRecipients;
			}
		}
		activity[i] = active > 0 ? walking/(active*slow) : 0;
	}
}

/** 已提交裂隙按到场次序消耗界碑碎片；来源是否还活着不影响事务。 */
static void AdvanceRiftArrivals(const Snapshot& s, float time, std::vector<Plant>& plants,
	std::vector<Unit>& units, std::vector<int>& columns, ConstructionStats& stats) {
	for (auto& p : plants) if (p.health > 0 && p.boundaryRecharge > 0 && time >= p.boundaryBlockedUntil
		&& p.boundaryShards < BoundaryFlowerRules::MaxShards) {
		p.boundaryCharge += kStep;
		if (p.boundaryCharge >= p.boundaryRecharge) { ++p.boundaryShards; p.boundaryCharge -= p.boundaryRecharge; }
		if (p.boundaryShards == BoundaryFlowerRules::MaxShards) p.boundaryCharge = 0;
	}
	std::vector<size_t> ready;
	for (size_t i=0; i<units.size(); ++i) if (columns[i] >= 0 && units[i].body.spawnAt <= time) ready.push_back(i);
	std::stable_sort(ready.begin(),ready.end(),[&](size_t a,size_t b) {
		if (units[a].body.spawnAt != units[b].body.spawnAt) return units[a].body.spawnAt < units[b].body.spawnAt;
		return columns[a] < columns[b];
	});
	for (size_t index : ready) {
		const int row = units[index].body.row, column = columns[index];
		Plant* selected = nullptr; int distance = 10000;
		for (auto& p : plants) if (p.health > 0 && p.boundaryShards > 0 && time >= p.boundaryBlockedUntil
			&& std::abs(p.row-row)<=1 && std::abs(p.column-column)<=1) {
			const int d = std::abs(p.row-row)+std::abs(p.column-column);
			if (!selected || d < distance || (d == distance && p.id < selected->id)) { selected = &p; distance = d; }
		}
		if (selected) { --selected->boundaryShards; units[index].body.x = s.rightEdge+40; ++stats.riftRedirects; }
		columns[index] = -1;
	}
}

/** 对齐正式最高层耐久优先/分路约束选格；免费召唤不生成成交资产或死亡返冰。 */
static void CommitForecastRitual(const Snapshot& s, float time, Unit& source, const std::vector<Plant>& plants,
	std::vector<Unit>& units, size_t& nextSlot, std::vector<int>& columns,
	std::vector<float>& initialHealth, std::vector<float>& initialX, ConstructionStats& stats) {
	struct Cell { int row, column; float value; };
	std::vector<Cell> cells;
	for (const auto& cell : s.riftCells) {
		const Plant* top = nullptr;
		for (const auto& p : plants) if (p.health > 0 && p.row == cell[0] && p.column == cell[1]
			&& (!top || p.layer > top->layer)) top = &p;
		cells.push_back({cell[0],cell[1],top ? (top->maximumHealth > 0 ? top->maximumHealth : top->initialHealth)+1000 : 0});
	}
	std::stable_sort(cells.begin(),cells.end(),[](const Cell& a,const Cell& b) {
		if (a.value != b.value) return a.value > b.value;
		if (a.column != b.column) return a.column < b.column;
		return a.row < b.row;
	});
	const int count = s.ritualWhiteout ? AuroraPriestRules::WhiteoutSummons : AuroraPriestRules::Summons;
	std::array<int,6> rowCounts{};
	std::vector<Cell> chosen;
	for (int pass=0; pass<2; ++pass) for (const auto& cell : cells) {
		if (static_cast<int>(chosen.size()) >= count) break;
		if (cell.row < 0 || cell.row >= s.rows || rowCounts[cell.row] >= 2) continue;
		if (pass == 0 && chosen.size() == 1 && cell.row == chosen.front().row) continue;
		if (std::any_of(chosen.begin(),chosen.end(),[&](const Cell& c) { return c.row == cell.row && c.column == cell.column; })) continue;
		chosen.push_back(cell); ++rowCounts[cell.row];
	}
	if (chosen.empty()) return;
	++source.ritual.releases; ++stats.ritualReleases;
	for (size_t i=0; i<chosen.size() && nextSlot<units.size(); ++i) {
		const auto& cell = chosen[i];
		// 活体来源沿用正式 ID；未出生来源使用稳定候选身份，无法预知未来实体分配顺序。
		const size_t kind = (std::abs(source.id)+i+cell.row+cell.column)%s.ritualSummons.size();
		auto child = s.ritualSummons[kind];
		child.id = -1-static_cast<int>(nextSlot);
		child.body.row = cell.row; child.body.x = s.gridLeft+(cell.column+.5f)*s.cellWidth;
		child.body.spawnAt = time+AuroraPriestRules::Unfold;
		child.body.purchaseCost = 0; child.playerRefund = 0;
		units[nextSlot] = std::move(child); columns[nextSlot] = cell.column;
		initialHealth[nextSlot] = units[nextSlot].body.health; initialX[nextSlot] = units[nextSlot].body.x;
		++nextSlot; ++stats.riftSummons;
	}
}

/** 仪器/头部存活才继续计时，场外只准备不施法；鼓舞不加速仪式。 */
static float AdvanceRitual(const Snapshot& s, float time, Unit& unit, float active, const std::vector<Plant>& plants,
	std::vector<Unit>& units, size_t& nextSlot, std::vector<int>& columns,
	std::vector<float>& initialHealth, std::vector<float>& initialX, ConstructionStats& stats) {
	auto& r = unit.ritual;
	if (!r.enabled) return 1;
	if (r.armor <= 0 || unit.body.health-r.armor <= r.stopHealth || r.releases >= AuroraPriestRules::MaxReleases) {
		r.enabled = false; r.winding = false; return 1;
	}
	r.remaining = std::max(0.0f,r.remaining-active*(unit.body.slow > 0 ? .5f : 1));
	const bool wasWinding = r.winding;
	if (r.remaining <= 0) {
		if (r.winding) {
			CommitForecastRitual(s,time,unit,plants,units,nextSlot,columns,initialHealth,initialX,stats);
			r.winding = false; r.remaining = AuroraPriestRules::Cooldown;
		} else if (unit.body.x+unit.body.blastAnchorOffset <= s.gridLeft+s.columns*s.cellWidth) {
			r.winding = true; r.remaining = AuroraPriestRules::Windup;
		}
	}
	return wasWinding || r.winding ? 0 : 1;
}

Weights Evaluate(const Snapshot& s, const std::vector<Action>& plan, ConstructionStats* construction, float counterHoldSeconds, float storedHoldSeconds) {
	Weights f{};
	if (s.precisionTargetID > 0) f[5] = ColdStorageSkillRules::StrikeIceCost;
	auto units = s.current;
	std::vector<std::pair<size_t,int>> committedRifts;
	for (const auto& rift : s.rifts) {
		auto unit = rift.unit; unit.body.spawnAt = rift.remaining;
		unit.body.purchaseCost = 0; unit.playerRefund = 0;
		committedRifts.emplace_back(units.size(),rift.column); units.push_back(std::move(unit));
	}
	for (const auto& a : plan) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) continue;
		auto unit = s.options[a.option].unit;
		unit.body.spawnAt = a.delay;
		units.push_back(unit);
		f[5] += s.options[a.option].cost;
	}
	// 预分配未激活的子单位，后续不能因 vector 扩容使当前战斗引用失效。
	const size_t originalUnits = units.size();
	std::vector<int> thrownChild(originalUnits,-1);
	std::vector<float> throwRemaining(originalUnits,-1);
	if (FullForecast(s)) for (size_t i = 0; i < originalUnits && units.size() < originalUnits+kForecastSummonLimit; ++i) {
		if (units[i].throwHealth <= 0) continue;
		Unit child;
		child.body.row = units[i].body.row;
		child.body.spawnAt = Horizon(s)+1;
		child.body.speed = kForecastImpWalkSpeed;
		thrownChild[i] = static_cast<int>(units.size());
		units.push_back(child); // 免费召唤既不增加付款资产，也不向玩家凭空返冰
	}
	size_t nextRiftSlot = units.size();
	if (std::any_of(units.begin(),units.end(),[](const Unit& u) { return u.ritual.enabled; })) {
		int slots = 0;
		for (const auto& unit : units) if (unit.ritual.enabled)
			slots += std::max(0,AuroraPriestRules::MaxReleases-unit.ritual.releases)
				* (s.ritualWhiteout ? AuroraPriestRules::WhiteoutSummons : AuroraPriestRules::Summons);
		units.resize(units.size()+std::min(kForecastSummonLimit,slots));
		for (size_t i=nextRiftSlot; i<units.size(); ++i) { units[i].body.health = 0; units[i].body.spawnAt = Horizon(s)+1; }
	}
	std::vector<int> riftColumns(units.size(),-1);
	for (const auto& rift : committedRifts) riftColumns[rift.first] = rift.second;
	float enemyIce = std::max(0.0f,s.budget-f[5]), supplyAt = s.supplyRemaining;
	auto auras = s.attackAuras;
	std::vector<float> attackRates;
	auto plants = s.plants;
	auto mowers = s.mowers;
	for (auto& plant : plants) plant.initialHealth = plant.repairMaximum > 0 ? plant.repairMaximum : plant.health;
	auto strikes = s.rowStrikes;
	ConstructionStats constructionStats;
	if (s.precisionTargetID > 0) constructionStats.abilityIceSpent = ColdStorageSkillRules::StrikeIceCost;
	int precisionID = s.precisionTargetID > 0 ? s.precisionTargetID : s.pendingPrecisionID;
	const float precisionAt = s.precisionTargetID > 0 ? ColdStorageSkillRules::StrikeAimDuration : s.pendingPrecisionRemaining;
	for (size_t i=0; i<units.size(); ++i) if (units[i].id <= 0) units[i].id = -1-static_cast<int>(i);
	std::vector<float> drumActivity;
	std::vector<float> constructionReady;
	for (const auto& card : s.construction) {
		if (constructionReady.size() <= static_cast<size_t>(card.source)) constructionReady.resize(card.source+1);
		constructionReady[card.source] = card.ready;
	}
	float constructionAt = 0;
	std::vector<float> initialHealth, initialX, smash(units.size());
	std::vector<int> smashTarget(units.size(),-1);
	for (const auto& u : units) { initialHealth.push_back(u.body.health); initialX.push_back(u.body.x); }
	std::vector<float> counterReady;
	std::vector<float> rowStrikeReady;
	for (const auto& strike : s.rowStrikes) rowStrikeReady.push_back(strike.ready);
	std::vector<PendingCounter> pending;
	for (const auto& counter : s.counters) {
		if (counter.blast.committed) {
			pending.push_back({counter.blast,counter.blast.ready});
			pending.back().plantID = counter.plantID;
			pending.back().invulnerableAt = counter.vulnerableSeconds;
		}
		else {
			if (counterReady.size() <= static_cast<size_t>(counter.source)) counterReady.resize(counter.source + 1);
			counterReady[counter.source] = counter.blast.ready;
		}
		if (counter.sharedSource >= 0) {
			if (counterReady.size() <= static_cast<size_t>(counter.sharedSource)) counterReady.resize(counter.sharedSource+1);
			counterReady[counter.sharedSource] = counter.sharedReady;
		}
	}
	std::vector<bool> refunded(units.size()), breached(units.size());
	std::vector<float> counterHoldUntil(counterReady.size(),-1);
	std::vector<unsigned char> melonHits(units.size());
	float playerSun = static_cast<float>(s.playerSun), playerIce = static_cast<float>(s.playerIce);
	float pendingIce = static_cast<float>(s.incomingIce), arrival = s.incomingIceAt;
	std::vector<float> exchangeReady;
	for (const auto& card : s.exchanges) exchangeReady.push_back(card.ready);
	std::vector<std::pair<std::array<int,2>,float>> exchangeOccupied;
	const auto capPlayerResources = [&] {
		if (FullForecast(s) || s.anticipateEconomy) {
			playerSun = std::min(playerSun,static_cast<float>(s.playerSunLimit));
			playerIce = std::min(playerIce,static_cast<float>(s.playerIceLimit));
		}
	};
	capPlayerResources();
	// 砸击一旦开始，目标被队友先消灭也要走完原有前摇。锁住原格，
	// 避免残留进度永久挡住投掷，或把半次砸击转移给后面另一格植物。
	const auto advanceSmash = [&](size_t i, float active, float speedFactor) {
		smash[i] += active * (speedFactor < 1 ? 0.6f : 1);
		if (smash[i] < units[i].body.smashSeconds) return;
		const int target = smashTarget[i];
		smash[i] = 0; smashTarget[i] = -1;
		const int row = plants[target].row, column = plants[target].column;
		if (FullForecast(s)) {
			// 正式巨人一次砸击结算原格各层；原宿主消失不改变已经锁定的格位。
			for (auto& p : plants) if (p.health > 0 && p.row == row && p.column == column) {
				DamagePlant(p,p.health,true,f);
			}
		} else {
			auto& p = plants[target];
			if (p.health > 0) {
				DamagePlant(p,p.health,true,f);
			}
		}
	};
	bool orderArrived = false;
	for (float t = 0; t < Horizon(s); t += kStep) {
		if (s.cancellation && s.cancellation->load(std::memory_order_relaxed)) throw SearchCancelled{};
		const float previousKills = f[0];
		if (precisionID > 0 && t >= precisionAt) {
			for (auto& p : plants) if (p.id == precisionID && p.health > 0) {
				// 单株生命周期结束，不借用群伤/砸击，不受壳层或无敌状态阻挡。
				f[1] += p.reward*p.health/std::max(1.0f,p.initialHealth);
				f[0] += p.reward; p.health = 0; ++constructionStats.precisionHits;
				break;
			}
			precisionID = 0; // 消失也消费事务，不能命中新种在同格的替代物。
		}
		if (s.supplyInterval > 0) while (t >= supplyAt) { enemyIce += s.supplyIce; supplyAt += s.supplyInterval; }
		if (s.anticipateEconomy && pendingIce > 0 && t >= arrival) {
			playerIce += pendingIce; pendingIce = 0;
		} else if (!s.anticipateEconomy && !orderArrived && t >= s.incomingIceAt) {
			playerIce += s.incomingIce; orderArrived = true;
		}
		for (const auto& p : plants) if (p.health > 0 && t >= p.productionAt) playerSun += p.sunPerSecond * kStep;
		capPlayerResources(); // 满仓后的溢出不是可被攻击消耗的实际资产
		AdvanceRiftArrivals(s,t,plants,units,riftColumns,constructionStats);
		AdvancePlantRepairs(s,t,plants,units,playerIce,f,constructionStats,true);
		AdvanceRowStrikes(s,strikes,t,plants,units,initialHealth,rowStrikeReady,f);
		AdvanceCounters(s,t,plants,units,initialHealth,counterReady,pending,playerSun,playerIce,f,counterHoldSeconds,storedHoldSeconds,counterHoldUntil);
		if (s.anticipateEconomy) AdvancePlayerEconomy(s,t,plants,exchangeReady,exchangeOccupied,counterReady,
			constructionReady,auras,units,playerSun,playerIce,pendingIce,arrival,constructionStats);
		if (!s.construction.empty() && t >= constructionAt) {
			AdvanceConstruction(s,t,Horizon(s),units,plants,constructionReady,
				strikes,rowStrikeReady,playerSun,playerIce,constructionStats);
			constructionAt = t+kConstructionInterval;
		}
		AdvancePlantRepairs(s,t,plants,units,playerIce,f,constructionStats,false);
		if (!auras.empty()) AdvanceAttackAuras(s,t,auras,plants,units,playerIce,attackRates,constructionStats);
		// 每株植物只对当前实际可见前锋开火。邻行没有引火目标时不凭空产生西瓜溅射。
		for (size_t pi=0; pi<plants.size(); ++pi) if (plants[pi].health > 0 && plants[pi].dps > 0) {
			auto& p = plants[pi];
			const float attackRate = auras.empty() ? 1 : attackRates[pi];
			int target = -1;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t || u.x > s.rightEdge
					|| (p.around ? std::abs(u.x - p.x) > p.range : u.x < p.x - 30 || u.x > p.x + p.range)) continue;
				if (u.row != p.row && (p.melon || std::abs(u.row - p.row) > p.rowRadius)) continue;
				if (target < 0 || u.x < units[target].body.x) target = static_cast<int>(i);
			}
			if (target < 0) continue;
			// 只在存在可攻击目标时成长。领域不仅提高本步伤害，也让后续阶段更快到来；
			// 保守忽略未来受惊重置，不能把尚未成功的近身进攻当成已解除这株高成长火力。
			float damageRate = attackRate;
			if (p.growth.perShot > 0) {
				const float damage = p.growth.Advance(kStep*attackRate*p.growthSpeed);
				p.hitDamage = p.growth.Damage();
				p.dps = p.hitDamage/p.growth.Interval()*p.growthSpeed;
				damageRate = damage/std::max(0.001f,p.dps*kStep);
			}
			const auto impact = units[target].body;
			int secondaryCount = 0;
			if (p.melon) {
				// 与正式弹丸一样，先冻结命中集合再伤害；大编队的次要伤害不能无限线性叠加。
				for (size_t i = 0; i < units.size(); ++i) {
					const auto& u = units[i].body;
					melonHits[i] = static_cast<int>(i) != target && u.health > 0 && u.spawnAt <= t
						&& ColdStorageStrategy::MelonSplashContains(impact,u);
					secondaryCount += melonHits[i];
				}
			}
			const float secondaryDps = ColdStorageStrategy::MelonSecondaryDps(p.dps,secondaryCount);
			float fumeEnd = p.x+p.range;
			if (p.fume && !p.around) for (const auto& unit : units) {
				if (unit.body.health > 0 && unit.body.spawnAt <= t && unit.body.row == p.row
					&& unit.body.x >= p.x-30 && unit.shieldHealth > 0 && unit.blocksFumePiercing)
					fumeEnd = std::min(fumeEnd,unit.body.x);
			}
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t) continue;
				const bool splash = p.melon && melonHits[i];
				const bool area = !p.melon && p.multiTarget && std::abs(u.row - p.row) <= p.rowRadius
					&& (p.around ? std::abs(u.x - p.x) <= p.range : u.x >= p.x - 30 && u.x <= p.x + p.range);
				if (static_cast<int>(i) != target && !splash && !area) continue;
				if (p.fume && !p.around && u.x > fumeEnd) continue;
				const auto hit = DescribePlantHit(units[i],p,splash ? secondaryDps/p.dps : 1);
				const bool slowReachesBody = units[i].shieldHealth <= 0 || p.melon || hit.bypass;
				ApplyDamage(units[i],hit.rate*kStep*damageRate,hit.penetrate,hit.bypass,hit.discardOverflow,p.damageOrigin);
				if (u.canBeChilled && u.slowImmunity <= t && p.slowRate > 0 && slowReachesBody)
					u.slow = std::max(u.slow, std::min(p.slowDuration, p.slowRate * p.slowDuration * kStep * 2 * attackRate));
				u.stopped = std::max(u.stopped, std::min(kStep * 0.9f, p.stopDuty * kStep * attackRate));
			}
		}
		AdvanceDrums(s,t,units,constructionStats,drumActivity);
		for (size_t i = 0; i < units.size(); ++i) {
			auto& worker = units[i]; auto& u = worker.body;
			if (u.health <= 0 || u.spawnAt > t) continue;
			const float active = std::max(0.0f, kStep - u.stopped);
			u.stopped = std::max(0.0f, u.stopped - kStep);
			const float ritualActivity = AdvanceRitual(s,t,worker,active,plants,units,nextRiftSlot,riftColumns,initialHealth,initialX,constructionStats);
			const float speedFactor = u.slow > 0 ? u.slowFactor : 1;
			u.slow = std::max(0.0f, u.slow - kStep);
			if (smashTarget[i] >= 0) {
				advanceSmash(i,active,speedFactor);
				continue; // 动作完成后下一步才恢复投掷/移动，与既有正常砸击的步进顺序一致。
			}
			if (i < originalUnits && thrownChild[i] >= 0 && u.health <= worker.throwHealth
				&& u.x > worker.throwAnchorX+ImpThrowRules::MinimumDistance) {
				if (throwRemaining[i] < 0) throwRemaining[i] = worker.throwWindup;
				throwRemaining[i] -= active*(speedFactor < 1 ? .5f : 1);
				if (throwRemaining[i] <= 0) {
					const int childIndex = thrownChild[i];
					auto& child = units[childIndex];
					const float flight = ImpThrowRules::FlightSeconds(u.x-worker.throwAnchorX);
					child.body.x = u.x-ImpThrowRules::ReleaseOffsetX-ImpThrowRules::HorizontalSpeed*flight;
					child.body.health = ImpThrowRules::Health;
					child.body.spawnAt = t+flight+kForecastImpLanding;
					child.body.slow = u.slow;
					initialHealth[childIndex] = ImpThrowRules::Health; initialX[childIndex] = child.body.x;
					thrownChild[i] = -1; // 脱手后不随投手死亡回滚，且每个投手仅能提交一次
				}
				continue; // 前摇不移动或砸击；期间被杀便不会走到提交分支
			}
			if (u.economic && u.health > worker.productionStopHealth) {
				worker.productionRemaining -= active;
				while (worker.productionRemaining <= 0) {
					const int income = static_cast<int>(worker.nextYield);
					enemyIce += income;
					if (t < kHorizon) f[4] += income;
					worker.productionRemaining += IceProduction::Interval;
					worker.nextYield = std::min(IceProduction::MaximumYield, worker.nextYield * IceProduction::YieldGrowth);
				}
			}
			AdvanceArmorRepair(worker,active,enemyIce,f,constructionStats);
			int contact = -1;
			for (size_t j = 0; j < plants.size(); ++j) {
				const auto& p = plants[j];
				if (p.health <= 0 || !p.edible || p.row != u.row || p.x > u.x + 30) continue;
				if (contact < 0 || p.x > plants[contact].x
					|| (p.x == plants[contact].x && p.layer > plants[contact].layer)) contact = static_cast<int>(j);
			}
			// 触发距离按实体原点，与用碰撞中心判断接敌的坐标分开。
			const float triggerX = u.x+u.blastAnchorOffset;
			const bool inRange = worker.burst.range > 0 && std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
				return p.health > 0 && p.edible && p.row == u.row && triggerX >= p.x && triggerX-p.x <= worker.burst.range;
			});
			const auto activity = AdvanceBurst(worker,inRange,active*drumActivity[i]*ritualActivity,enemyIce,f,constructionStats);
			const float ritualMove = !worker.ritual.present ? 1 : (worker.ritual.armor > 0 ? AuroraPriestRules::NormalSpeed : AuroraPriestRules::OverloadSpeed)
				* (u.x+u.blastAnchorOffset > s.gridLeft+s.columns*s.cellWidth ? AuroraPriestRules::OutsideSpeed : 1);
			const float ritualBite = !worker.ritual.present ? 1 : worker.ritual.armor > 0 ? AuroraPriestRules::NormalSpeed
				: AuroraPriestRules::OverloadSpeed*AuroraPriestRules::OverloadBite/AuroraPriestRules::NormalBite;
			const float drumMove = worker.inspiration.empty() ? 1 : worker.drumSpeedAmplifier*(1+CrystalDrummerRules::MoveBonus*worker.inspiration.size());
			const float drumBite = worker.inspiration.empty() ? 1 : worker.drumSpeedAmplifier*(1+CrystalDrummerRules::BiteBonus*worker.inspiration.size());
			if (contact >= 0 && u.x <= plants[contact].x + kContact) {
				auto& p = plants[contact];
				float damage = worker.biteDps * ritualBite * drumBite * activity[1] * (speedFactor < 1 ? 0.5f : 1);
				if (u.smashSeconds > 0) {
					smashTarget[i] = contact;
					advanceSmash(i,active,speedFactor);
					continue;
				}
				if (worker.vehicleCrush && p.crushDamage > 0) {
					if (active > 0 && DamagePlant(p,p.crushDamage,true,f) && p.health > 0) u.x += p.vehicleRetreat;
				} else DamagePlant(p,damage,false,f);
			} else {
				u.x -= u.speed * ritualMove * drumMove * activity[0] * speedFactor;
				// 高速爆发不能跨步穿墙，车辆也不能越过仍存活的抗碾压坚果。
				if (contact >= 0 && (worker.burst.range > 0 || (worker.vehicleCrush && plants[contact].crushDamage > 0))) u.x = std::max(u.x,plants[contact].x+kContact);
			}
		}
		enemyIce += f[0]-previousKills; // 已兑现击杀在下一逻辑步可付技能费，不能提前借用预测收入
		// 两版搜索共享正式清场/胜负语义：先过清洁车，再判断是否真的进屋。
		AdvanceMowers(mowers,units,t,s.rightEdge);
		for (size_t i = 0; i < units.size(); ++i) if (units[i].body.health > 0 && units[i].body.spawnAt <= t
			&& units[i].body.x < s.houseX) {
				// 进屋只触发一次胜利；重复穿过同一已失守防线不能按人数制造额外胜利收益。
				f[2] = 1; units[i].body.health = 0; breached[i] = true;
			}
		for (size_t i = 0; i < units.size(); ++i) if (!refunded[i] && !breached[i] && units[i].body.health <= 0) {
			playerIce += units[i].playerRefund; refunded[i] = true;
		}
		capPlayerResources();
		// 正式胜负已经发生，后续制冰/建造/伤害均不再兑现，不能继续虚构胜利后的收入。
		if (f[2] > 0) break;
	}
	for (size_t i = 0; i < units.size(); ++i) if (units[i].body.health > 0) {
		const auto& u = units[i].body;
		f[3] += u.purchaseCost * std::clamp(u.health / std::max({1.0f, initialHealth[i], units[i].repair.totalMaximum}), 0.0f, 1.0f);
		f[7] += u.purchaseCost * std::clamp((initialX[i] - u.x) / 800, 0.0f, 1.0f);
	}
	// 建设只是现金换成植株资产；不能把玩家花钱补阵本身误当成被消耗。
	constructionStats.opponentAssets = playerIce+playerSun*s.sunIceValue;
	if (s.anticipateEconomy) {
		constructionStats.pendingIce = pendingIce;
		constructionStats.opponentAssets += pendingIce; // 已付款在途货物仍是玩家资产，不能伪造资源损失
	}
	for (const auto& plant : plants) if (plant.health > 0)
		constructionStats.opponentAssets += plant.assetValue*std::clamp(plant.health/std::max(1.0f,plant.initialHealth),0.0f,1.0f);
	if (construction) *construction = constructionStats;
	return f;
}

QueueRevision ReplanCommitted(Snapshot& s, const Weights& baseWeights, std::uint32_t seed) {
	QueueRevision revision;
	if (s.committed.empty() || (s.searchVersion != 1 && s.searchVersion != 2) || !ValidWeights(baseWeights)
		|| (s.stateModel && !s.stateModel->IsValid()) || !std::isfinite(s.opponentWeight)
		|| s.opponentWeight < 0 || s.opponentWeight > 100) return revision;
	// 重排只消费已验证的队列元数据；不能把误标的在场实体当成可移动的后备兵。
	std::vector<bool> marked(s.current.size());
	for (const auto& paid : s.committed) {
		if (paid.unit < 0 || static_cast<size_t>(paid.unit) >= s.current.size() || marked[paid.unit]) return revision;
		if (s.current[paid.unit].id != 0) return revision;
		const auto& body = s.current[paid.unit].body;
		if (body.row < 0 || body.row >= 6 || !paid.legalRows[body.row]
			|| !std::isfinite(body.spawnAt) || body.spawnAt < 0) return revision;
		marked[paid.unit] = true;
	}
	const auto original = s.current;
	const auto baseline = EvaluatePlan(s,baseWeights,{}, {},0);
	const auto conditioned = ConditionWeights(baseWeights,DescribeState(s,baseline.features),s.stateModel);
	const auto weights = s.netEconomy ? AccountForIce(conditioned) : conditioned;
	revision.beforeScore = revision.afterScore = EvaluatePlan(s,weights,{},baseline.features,baseline.opponentAssets).score;
	revision.evaluated = 1;
	auto best = original;
	std::mt19937 random(seed);
	for (int trial = 0; trial < kQueueTrials; ++trial) {
		s.current = trial < 13 ? original : best;
		if (trial < 13) {
			// 同一已购编队既比较原时序改路，也比较提前；集中只是一组候选，不是强制策略。
			const int row = trial % 6;
			for (const auto& paid : s.committed) {
				auto& body = s.current[paid.unit].body;
				if (trial < 12 && paid.legalRows[row]) body.row = row;
				if (trial >= 6) body.spawnAt = 0;
			}
		} else {
			const auto& paid = s.committed[random()%s.committed.size()];
			auto& body = s.current[paid.unit].body;
			const int row = static_cast<int>(random()%6);
			if (paid.legalRows[row]) body.row = row;
			body.spawnAt = original[paid.unit].body.spawnAt * (random()%101)/100.0f;
		}
		const float score = EvaluatePlan(s,weights,{},baseline.features,baseline.opponentAssets).score;
		++revision.evaluated;
		if (score > revision.afterScore + .001f) { best = s.current; revision.afterScore = score; }
	}
	s.current = std::move(best);
	for (const auto& paid : s.committed) {
		const auto& before = original[paid.unit].body;
		const auto& after = s.current[paid.unit].body;
		if (before.row != after.row || before.spawnAt != after.spawnAt) ++revision.changed;
	}
	return revision;
}

/** 在给定技能意图下搜索编队；正式入口另外比较不施法的机会成本。 */
static Result SearchFormation(const Snapshot& s, const Weights& baseWeights, std::uint32_t seed) {
	Result best;
	if ((s.searchVersion != 1 && s.searchVersion != 2) || !ValidWeights(baseWeights) || (s.stateModel && !s.stateModel->IsValid())
		|| !std::isfinite(s.opponentWeight) || s.opponentWeight < 0 || s.opponentWeight > 100) return best;
	best.features = Evaluate(s, {}, &best.construction); Calibrate(s,best);
	const auto inputs = DescribeState(s,best.features);
	const auto conditioned = ConditionWeights(baseWeights,inputs,s.stateModel);
	const auto weights = s.netEconomy ? AccountForIce(conditioned) : conditioned;
	// 不增援也要承受相同的对手模型，不能拿乐观等待基线去比较保守进攻结果。
	best = EvaluatePlan(s,weights,{}, {},0);
	const float baselineOpponentAssets = best.opponentAssets;
	best.baselineOpponentAssets = baselineOpponentAssets; best.opponentScore = 0;
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.baselineFeatures = best.features; best.score = Score(best.features, weights); best.evaluated = 1;
	if (s.options.empty() || s.capacity <= 0 || PurchaseBudget(s) <= 0) return best;
	const auto cheapest = std::min_element(s.options.begin(),s.options.end(),[](const auto& a,const auto& b) { return a.cost < b.cost; });
	if (cheapest->cost > PurchaseBudget(s)) return best;
	std::mt19937 rng(seed); // 局部共同随机数使同一快照/参数可重复，搜索次数不改变正式战斗随机流。
	std::vector<Result> elite{best};
	bool hasChoice = s.allowWait; // 正式构建启用 fast-math，不能用无穷大充当尚无候选的哨兵。
	bool deferredInvestment = false;
	int capitalRejected = 0;
	const auto rejectInvestment = [&](const Result& candidate) {
		if (ShouldRegroup(candidate,s.budget,s.recoveryReserve)) return true;
		if (s.netEconomy && ShouldConserveCapital(candidate,s.budget,s.recoveryReserve)) {
			++capitalRejected; return true;
		}
		return false;
	};
	const int trials = s.searchVersion == 2 ? kPortfolioTrials : kTrials;
	// 在原有预算内最多拿一半做兵种覆盖，余下仍用于组合变异和完整编队探索。
	const auto coverage = SampleTypeCoverage(s,rng,trials/2);
	int largestPlan = 0;
	for (int trial = 1; trial < trials; ++trial) {
		auto plan = elite[rng() % elite.size()].actions;
		// 独立抽完整队伍，允许跨过“单只亏损、协同才盈利”的谷底，不强制任何兵种模板。
		if (s.searchVersion == 2 && trial % 2 == 0) plan = SamplePortfolio(s,rng,trial);
		else if (trial % 3 == 0) {
			plan.clear();
			const int focus = s.options[rng() % s.options.size()].row;
			const int repeated = s.stateModel ? static_cast<int>(rng() % s.options.size()) : -1;
			const bool mixed = !s.stateModel || rng() % 2 == 0;
			const bool concentrate = !s.stateModel || rng() % 2 == 0;
			for (int count = 1 + rng() % (s.stateModel ? kAdaptiveActions : kMaxActions); count > 0; --count) {
				int option = rng() % s.options.size();
				if (s.stateModel) {
					// 同类/混编、集中/分散均给探索机会；类型取自合法选项，不预设战术名单。
					if (!mixed || rng() % 2 == 0) option = repeated;
					const int row = concentrate && rng() % 4 != 0 ? focus : s.options[rng() % s.options.size()].row;
					const auto& original = s.options[option];
					const auto match = std::find_if(s.options.begin(),s.options.end(),[&](const auto& choice) {
						return choice.type == original.type && choice.cost == original.cost && choice.row == row;
					});
					if (match != s.options.end()) option = static_cast<int>(match-s.options.begin());
				} else if (rng() % 4 != 0) {
					for (int attempt = 0; attempt < 12 && s.options[option].row != focus; ++attempt) option = rng() % s.options.size();
				}
				plan.push_back({option, static_cast<float>(rng() % (s.stateModel ? 61 : 25)) * 0.5f});
			}
		}
		const int mutations = 1 + rng() % 3;
		for (int n = 0; n < mutations; ++n) {
			const int mutation = rng() % 5;
			if (plan.empty() || mutation == 0) plan.push_back({static_cast<int>(rng() % s.options.size()), static_cast<float>(rng() % (s.stateModel ? 61 : 25)) * 0.5f});
			else {
				const size_t at = rng() % plan.size();
				if (mutation == 1) plan.erase(plan.begin() + at);
				else if (mutation == 2) plan[at].option = rng() % s.options.size();
				else if (mutation == 3) plan[at].delay += static_cast<int>(rng() % 13) - 6;
				else plan.push_back({plan[at].option, plan[at].delay + 2});
			}
		}
		if (trial <= static_cast<int>(coverage.size()))
			plan = IntroduceOption(s,best.actions,coverage[trial-1],rng);
		// 给有钱的空场提供一个必定可行的起点；其余候选仍自由比较兵种、路线和队形。
		if (!s.allowWait && trial == 1 && coverage.empty()) plan = {{static_cast<int>(cheapest - s.options.begin()),0}};
		Repair(s, plan);
		largestPlan = std::max(largestPlan,static_cast<int>(plan.size()));
		if (!s.allowWait && !plan.empty()) plan.front().delay = 0;
		auto candidate = EvaluatePlan(s, weights, std::move(plan), best.baselineFeatures, baselineOpponentAssets);
		// 在候选比较中排除亏损增援，不能选完后才丢弃第一名而漏掉其余可行方案。
		if (rejectInvestment(candidate)) {
			deferredInvestment = true;
			continue;
		}
		if ((s.allowWait || !candidate.actions.empty()) && (!hasChoice || candidate.score > best.score + 0.001f)) {
			best = candidate; hasChoice = true;
		}
		elite.push_back(std::move(candidate));
		std::stable_sort(elite.begin(), elite.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
		if (elite.size() > 8) elite.resize(8);
	}
	int investmentEvaluated = 0;
	const float investmentBaseScore = best.score;
	// 大兵池的一次兵种抽样不等于已充分试过经营：错误落点被淘汰后，末尾整体改路无法再救回该兵种。
	// 固定原进攻优案作保底，额外比较“同行跟进”和“只给已有部队增援”；不改收益、不固定买几只。
	if (!coverage.empty()) {
		const auto parent = best.actions;
		for (int phase=0; phase<3 && investmentEvaluated<kInvestmentTrials; ++phase)
			for (int separate=0; separate<2 && investmentEvaluated<kInvestmentTrials; ++separate) {
				if (separate && parent.empty()) continue;
				for (int option=0; option<static_cast<int>(s.options.size()) && investmentEvaluated<kInvestmentTrials; ++option) {
					const auto& choice = s.options[option];
					if (!choice.unit.body.economic || choice.cost <= 0 || choice.cost > PurchaseBudget(s)) continue;
					auto plan = IntroduceOption(s,separate ? std::vector<Action>{} : parent,option,rng);
					plan.back().delay = DelayLimit(s)*phase/2;
					Repair(s,plan);
					if (!s.allowWait && !plan.empty()) plan.front().delay = 0;
					largestPlan = std::max(largestPlan,static_cast<int>(plan.size()));
					auto candidate = EvaluatePlan(s,weights,std::move(plan),best.baselineFeatures,baselineOpponentAssets);
					++investmentEvaluated;
					if (rejectInvestment(candidate)) { deferredInvestment = true; continue; }
					if (candidate.score > best.score+.001f) best = std::move(candidate);
				}
			}
	}
	// 支援能力单独出场可能无收益；显式覆盖边际增援和护卫+工人的可行组合，评分仍完全相同。
	int supportEvaluated = 0;
	const auto supportParent = best.actions;
	std::vector<int> supports;
	for (int i=0; i<static_cast<int>(s.options.size()); ++i)
		if (s.options[i].unit.repair.interval > 0 || s.options[i].unit.drum.enabled) supports.push_back(i);
	std::shuffle(supports.begin(),supports.end(),rng);
	for (int phase=0; phase<3 && supportEvaluated<kSupportTrials; ++phase) for (int option : supports) {
		if (supportEvaluated >= kSupportTrials) break;
		if (s.options[option].cost > PurchaseBudget(s)) continue;
		auto plan = IntroduceOption(s,phase == 0 ? supportParent : std::vector<Action>{},option,rng);
		plan.back().delay = 0;
		if (phase > 0) {
			const auto worker = std::find_if(s.options.begin(),s.options.end(),[&](const Option& o) {
				return o.row == s.options[option].row && o.unit.body.economic
					&& o.cost+s.options[option].cost <= PurchaseBudget(s);
			});
			if (worker == s.options.end() || ActionLimit(s) < 2) continue;
			plan.push_back({static_cast<int>(worker-s.options.begin()),phase*3.0f});
		}
		Repair(s,plan);
		auto candidate = EvaluatePlan(s,weights,std::move(plan),best.baselineFeatures,baselineOpponentAssets);
		++supportEvaluated;
		if (rejectInvestment(candidate)) { deferredInvestment = true; continue; }
		if (candidate.score > best.score+.001f) best = std::move(candidate);
	}
	const float investmentBestScore = best.score;
	// 固定自由搜索选出的兵种、预算和时序，完整比较各合法行。已有部队仍留在原行参与推演，
	// 因此可以发现继续支援巨人的收益，也能因灰烬、溅射或减速而保留分路方案。
	const auto original = best.actions;
	const float baseScore = best.score;
	std::array<float, 6> rowScores{};
	int tested = 0, rejected = 0, chosenRow = -1, evaluated = trials+investmentEvaluated+supportEvaluated;
	if (!original.empty()) for (int row = 0; row < static_cast<int>(s.context.size()); ++row) {
		auto plan = original;
		if (!Concentrate(s, plan, row)) continue;
		auto candidate = EvaluatePlan(s, weights, std::move(plan), best.baselineFeatures, baselineOpponentAssets);
		++evaluated; tested |= 1 << row; rowScores[row] = candidate.score;
		if (rejectInvestment(candidate)) { rejected |= 1 << row; continue; }
		if (candidate.score > best.score + 0.001f) { best = std::move(candidate); chosenRow = row; }
	}
	best.formationBaseScore = baseScore; best.formationScores = rowScores;
	best.formationTested = tested; best.formationRejected = rejected; best.formationChosenRow = chosenRow;
	best.supportEvaluated = supportEvaluated;
	best.investmentEvaluated = investmentEvaluated;
	best.investmentBaseScore = investmentBaseScore; best.investmentBestScore = investmentBestScore;
	best.evaluated = evaluated;
	best.capitalRejected = capitalRejected;
	best.largestPlan = largestPlan;
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.regrouping = best.actions.empty() && deferredInvestment;
	// 小队没有预测到增量击杀、削血或生产时，才升级搜索范围与预测时域。
	// 两次评估不能混加不同时间窗的分数：升级后整案及等待基线一起重算。
	if (s.searchVersion == 1 && s.allowWait && cheapest->cost > 0
		&& std::min(s.capacity,PurchaseBudget(s)/cheapest->cost) > ActionLimit(s)
		&& best.features[2] == 0 && best.features[0] <= best.baselineFeatures[0]
		&& best.features[1] <= best.baselineFeatures[1] && best.features[4] <= best.baselineFeatures[4]) {
		auto expanded = s;
		expanded.searchVersion = 2;
		auto result = SearchFormation(expanded,baseWeights,seed);
		result.expandedForecast = true;
		result.evaluated += best.evaluated;
		result.capitalRejected += best.capitalRejected;
		result.investmentEvaluated += best.investmentEvaluated;
		result.supportEvaluated += best.supportEvaluated;
		result.largestPlan = std::max(result.largestPlan,best.largestPlan);
		return result;
	}
	return best;
}
/** 技能与不施法优案同分制比较；先有限选靶，再只为胜出目标重搜一次可支付编队。 */
Result Search(const Snapshot& input, const Weights& weights, std::uint32_t seed) {
	auto best = SearchFormation(input,weights,seed);
	if (!input.precisionReady || input.pendingPrecisionID > 0
		|| input.budget < ColdStorageSkillRules::StrikeIceCost || !ValidWeights(weights)) return best;
	auto state = input;
	if (best.expandedForecast) state.searchVersion = 2;
	const auto baseline = EvaluatePlan(state,best.effectiveWeights,{}, {},0);
	const auto incumbent = best;
	// 这里只压缩选靶预算，不直接给技能收益。火力、主动能力与前锋阻挡仍须通过完整推演兑现。
	std::vector<std::pair<float,int>> targets;
	for (const auto& p : state.plants) if (p.id > 0 && p.health > 0) {
		float priority = p.dps*Horizon(state)+p.assetValue;
		for (const auto& strike : state.rowStrikes) if (strike.plantID == p.id) priority += strike.damage*state.rows;
		for (const auto& aura : state.attackAuras) if (aura.plantID == p.id)
			for (const auto& ally : state.plants) if (ally.health > 0 && std::abs(ally.row-p.row)<=1 && std::abs(ally.column-p.column)<=1)
				priority += ally.dps*Horizon(state)*aura.bonus;
		for (const auto& counter : state.counters) if (counter.plantID == p.id) priority += counter.blast.damage;
		for (const auto& unit : state.current) if (unit.body.health > 0 && unit.body.row == p.row && unit.body.x > p.x)
			priority += std::min(p.health,unit.body.health)/(1+std::abs(unit.body.x-p.x)/state.cellWidth);
		targets.emplace_back(priority,p.id);
	}
	std::stable_sort(targets.begin(),targets.end(),[](const auto& a,const auto& b) { return a.first > b.first; });
	if (targets.size() > kPrecisionTargets) targets.resize(kPrecisionTargets);
	int evaluated = 0;
	const auto consider = [&](Result candidate) {
		++evaluated;
		// 即使旧参数没有成本惩罚，也不能免费使用新技能；正式净冰模型已经计费，不重复惩罚。
		if (!state.netEconomy) candidate.score -= std::max(0.0f,1+incumbent.effectiveWeights[5])*ColdStorageSkillRules::StrikeIceCost;
		auto without = state; without.precisionTargetID = 0;
		const auto sameArmy = EvaluatePlan(without,incumbent.effectiveWeights,candidate.actions,baseline.features,baseline.opponentAssets);
		// 旧权重可能高度奖励“消灭一株”，新技能另核对真实边际回报，不能用击杀/削血双计分抵掉 60 冰。
		const auto target = std::find_if(state.plants.begin(),state.plants.end(),[&](const Plant& p) { return p.id == candidate.precisionTargetID; });
		const float targetReward = target == state.plants.end() ? 0 : target->reward;
		const bool tacticalGain = candidate.features[2] > sameArmy.features[2]
			|| candidate.features[4] > sameArmy.features[4]+.001f || candidate.features[3] > sameArmy.features[3]+.001f
			|| candidate.features[0] > sameArmy.features[0]+targetReward+.001f
			|| candidate.features[1] > sameArmy.features[1]+targetReward+.001f;
		if (!tacticalGain) return; // 不能只靠单株自身返冰/未来阳光估值，为没有破阵或护卫作用的打击背书。
		const float capitalGain = candidate.features[0]-sameArmy.features[0]
			+ candidate.features[4]-sameArmy.features[4] + candidate.features[3]-sameArmy.features[3]
			+ sameArmy.opponentAssets-candidate.opponentAssets;
		if (candidate.features[2] <= sameArmy.features[2] && capitalGain < ColdStorageSkillRules::StrikeIceCost) return;
		if (candidate.score <= sameArmy.score+.001f || candidate.score <= best.score+.001f) return;
		if (ShouldRegroup(candidate,state.budget,state.recoveryReserve)
			|| (state.netEconomy && ShouldConserveCapital(candidate,state.budget,state.recoveryReserve))) return;
		best = std::move(candidate);
	};
	for (const auto& target : targets) {
		state.precisionTargetID = target.second;
		for (int mode=0; mode<2; ++mode) {
			auto plan = mode == 0 ? incumbent.actions : std::vector<Action>{};
			Repair(state,plan);
			consider(EvaluatePlan(state,incumbent.effectiveWeights,std::move(plan),baseline.features,baseline.opponentAssets));
		}
	}
	if (best.precisionTargetID > 0) {
		state.precisionTargetID = best.precisionTargetID;
		const auto joint = SearchFormation(state,weights,seed);
		evaluated += joint.evaluated;
		consider(EvaluatePlan(state,incumbent.effectiveWeights,joint.actions,baseline.features,baseline.opponentAssets));
	}
	best.precisionEvaluated = evaluated;
	best.evaluated = incumbent.evaluated+evaluated;
	best.effectiveWeights = incumbent.effectiveWeights; best.stateInputs = incumbent.stateInputs;
	best.expandedForecast = incumbent.expandedForecast;
	best.precisionGain = best.precisionTargetID > 0 ? best.score-incumbent.score : 0;
	return best;
}

}
