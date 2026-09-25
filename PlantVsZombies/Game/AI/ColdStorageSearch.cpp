#include "ColdStorageSearch.h"
#include "Game/Board/IceProduction.h"
#include "Game/Plant/DawnLotusRules.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace ColdStorageSearch {
namespace {
constexpr float kHorizon = 60; // 推演覆盖的游戏秒，实际对局评测负责检验更长期收益
constexpr float kStep = 0.5f; // 仅候选预测的积分步长；真实比赛仍使用正式固定步
constexpr int kTrials = 96; // 自由搜索的评估数，之后最多补六次同编队逐行比较
constexpr int kMaxActions = 8; // 一次搜索最多承诺的新增单位数，下次观察后可以继续部署
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

/** 修复变异后的越界和超预算动作，稳定排序保留同一时刻的提交次序。 */
void Repair(const Snapshot& s, std::vector<Action>& actions) {
	int spent = 0;
	actions.erase(std::remove_if(actions.begin(), actions.end(), [&](Action& a) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) return true;
		const int cost = s.options[a.option].cost;
		if (cost <= 0 || spent + cost > s.budget) return true;
		spent += cost;
		a.delay = std::clamp(a.delay, 0.0f, s.stateModel ? kAdaptiveDelay : kMaxDelay);
		return false;
	}), actions.end());
	const int limit = std::max(0, std::min(s.stateModel ? kAdaptiveActions : kMaxActions, s.capacity));
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
	auto worker = [&](const Unit& unit) {
		++count; f[3] += unit.body.health / IceProduction::WorkerHealth;
		const auto& context = s.context[unit.body.row];
		f[6] += context[5]; f[7] += context[4]; f[8] += context[3];
		for (const auto& guard : s.current)
			if (!guard.body.economic && guard.body.health > 0 && guard.body.row == unit.body.row && guard.body.x < unit.body.x)
				f[4] += guard.body.health / 3000.0f;
	};
	for (const auto& unit : s.current) if (unit.body.economic && unit.body.health > 0) { ++f[1]; worker(unit); }
	for (const auto& action : plan) {
		const auto& option = s.options[action.option];
		if (option.unit.body.economic) {
			++f[2]; worker(option.unit);
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
Result EvaluatePlan(const Snapshot& s, const Weights& weights, std::vector<Action> plan, const Weights& baseline) {
	Result candidate;
	candidate.actions = std::move(plan);
	candidate.features = Evaluate(s, candidate.actions, &candidate.construction);
	Calibrate(s, candidate);
	candidate.baselineFeatures = baseline;
	candidate.score = Score(candidate.features, weights);
	for (const auto& action : candidate.actions) {
		const auto& option = s.options[action.option];
		auto context = s.context[option.row];
		context[0] = 1;
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
				const float damage = std::min(u.health,primary ? strike.damage : strike.splashDamage);
				features[6] += u.purchaseCost * damage / std::max(1.0f,initialHealth[i]);
				u.health -= damage;
			}
		}
		// 无目标时保留充能；释放一次覆盖所有行，而不是让每行各自获得独立冷却。
		if (used) ready[ability] = time + strike.recharge;
	}
}

/** 在当前推演位置判断覆盖，不沿用战斗开始时的静态轨迹。 */
bool CounterHits(const ColdStorageStrategy::BlastThreat& blast, const Unit& unit, float time, float rightEdge) {
	const auto& body = unit.body;
	return body.health > 0 && body.spawnAt <= time && body.x <= rightEdge && blast.reach[body.row] >= 0
		&& std::abs(body.x + (blast.usesObjectX ? body.blastAnchorOffset : 0) - blast.x) <= blast.reach[body.row];
}

/** 多张牌共享真实资源、同卡落点共享冷却；先兑现已提交反制，再选择一次可支付的新动作。 */
void AdvanceCounters(const Snapshot& state, float time, const std::vector<Plant>& plants, std::vector<Unit>& units,
	const std::vector<float>& initialHealth, std::vector<float>& ready,
	std::vector<PendingCounter>& pending, float& sun, float& ice, Weights& features) {
	for (auto it = pending.begin(); it != pending.end();) {
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
		for (size_t i = 0; i < units.size(); ++i) if (CounterHits(it->blast, units[i], time, state.rightEdge)) {
			auto& body = units[i].body;
			const float damage = std::min(body.health, it->blast.damage);
			features[6] += body.purchaseCost * damage / std::max(1.0f, initialHealth[i]);
			body.health -= damage;
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
			|| counter.sunCost > sun || counter.iceCost > ice) continue;
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
		for (size_t i = 0; i < units.size(); ++i) if (CounterHits(candidate, units[i], time, state.rightEdge)) {
			float health = units[i].body.health;
			// 不连续把三张清场牌浪费在已被另一张锁定的濒死目标上。
			for (const auto& scheduled : pending) if (CounterHits(scheduled.blast, units[i], time, state.rightEdge))
				health -= scheduled.blast.damage;
			if (health <= 0) continue;
			loss += units[i].body.purchaseCost * std::min(health,candidate.damage) / std::max(1.0f, initialHealth[i]);
			urgent = urgent || units[i].body.x < state.houseX + 240;
		}
		if (loss < (urgent ? 1 : counter.targeted ? kTargetCounterStake : kAreaCounterStake)) continue;
		const float value = loss / std::max(1.0f, counter.sunCost * 0.02f + counter.iceCost * 0.2f);
		if (value > best) { best = value; selected = static_cast<int>(c); impact = candidate; selectedTarget = candidateTarget; }
	}
	if (selected >= 0) {
		const auto& counter = state.counters[selected];
		sun -= counter.sunCost; ice -= counter.iceCost;
		ready[counter.source] = time + counter.recharge;
		pending.push_back({impact,time + counter.windup,selectedTarget,
			selectedTarget >= 0 ? units[selectedTarget].body.x : 0});
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
		if (time < ready[card.source] || card.sunCost > sun || card.iceCost > ice) continue;
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
		value /= std::max(25,card.sunCost)+card.iceCost;
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
	sun -= card.sunCost; ice -= card.iceCost;
	++stats.planted; stats.sunSpent += card.sunCost; stats.iceSpent += card.iceCost;
	if (card.strike.damage > 0) {
		auto strike = card.strike; strike.plantID = plant.id;
		strikes.push_back(strike); strikeReady.push_back(time+strike.ready);
	}
}

bool ValidWeights(const Weights& weights) {
	return std::all_of(weights.begin(), weights.end(), [](float w) { return std::isfinite(w) && std::abs(w) <= 500; });
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

bool ShouldRegroup(const Result& result, int budget, int reserve) {
	if (budget >= reserve || result.actions.empty()) return false;
	const auto& plan = result.features;
	const auto& baseline = result.baselineFeatures;
	if (plan[2] > baseline[2]) return false;
	// 只比较买与不买的差异；支出偏好、单纯走过的距离和已存在工人的收入都不是回报。
	const float gain = (plan[0] - baseline[0]) + (plan[1] - baseline[1]) + (plan[4] - baseline[4]);
	return gain < plan[5] * kRecoveryReturnFraction;
}

Weights Evaluate(const Snapshot& s, const std::vector<Action>& plan, ConstructionStats* construction) {
	Weights f{};
	auto units = s.current;
	for (const auto& a : plan) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) continue;
		auto unit = s.options[a.option].unit;
		unit.body.spawnAt = a.delay;
		units.push_back(unit);
		f[5] += s.options[a.option].cost;
	}
	auto plants = s.plants;
	for (auto& plant : plants) plant.initialHealth = plant.health;
	auto strikes = s.rowStrikes;
	ConstructionStats constructionStats;
	std::vector<float> constructionReady;
	for (const auto& card : s.construction) {
		if (constructionReady.size() <= static_cast<size_t>(card.source)) constructionReady.resize(card.source+1);
		constructionReady[card.source] = card.ready;
	}
	float constructionAt = 0;
	std::vector<float> initialHealth, initialX, smash(units.size());
	for (const auto& u : units) { initialHealth.push_back(u.body.health); initialX.push_back(u.body.x); }
	std::vector<float> counterReady;
	std::vector<float> rowStrikeReady;
	for (const auto& strike : s.rowStrikes) rowStrikeReady.push_back(strike.ready);
	std::vector<PendingCounter> pending;
	for (const auto& counter : s.counters) {
		if (counter.blast.committed) pending.push_back({counter.blast,counter.blast.ready});
		else {
			if (counterReady.size() <= static_cast<size_t>(counter.source)) counterReady.resize(counter.source + 1);
			counterReady[counter.source] = counter.blast.ready;
		}
	}
	std::vector<bool> refunded(units.size()), breached(units.size());
	std::vector<unsigned char> melonHits(units.size());
	float playerSun = static_cast<float>(s.playerSun), playerIce = static_cast<float>(s.playerIce);
	bool orderArrived = false;
	for (float t = 0; t < (s.stateModel ? kAdaptiveHorizon : kHorizon); t += kStep) {
		if (!orderArrived && t >= s.incomingIceAt) {
			playerIce += s.incomingIce; orderArrived = true;
		}
		for (const auto& p : plants) if (p.health > 0 && t >= p.productionAt) playerSun += p.sunPerSecond * kStep;
		AdvanceRowStrikes(s,strikes,t,plants,units,initialHealth,rowStrikeReady,f);
		AdvanceCounters(s,t,plants,units,initialHealth,counterReady,pending,playerSun,playerIce,f);
		if (!s.construction.empty() && t >= constructionAt) {
			AdvanceConstruction(s,t,s.stateModel ? kAdaptiveHorizon : kHorizon,units,plants,constructionReady,
				strikes,rowStrikeReady,playerSun,playerIce,constructionStats);
			constructionAt = t+kConstructionInterval;
		}
		// 每株植物只对当前实际可见前锋开火。邻行没有引火目标时不凭空产生西瓜溅射。
		for (const auto& p : plants) if (p.health > 0 && p.dps > 0) {
			int target = -1;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t || u.x > s.rightEdge
					|| (p.around ? std::abs(u.x - p.x) > p.range : u.x < p.x - 30 || u.x > p.x + p.range)) continue;
				if (u.row != p.row && (p.melon || std::abs(u.row - p.row) > p.rowRadius)) continue;
				if (target < 0 || u.x < units[target].body.x) target = static_cast<int>(i);
			}
			if (target < 0) continue;
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
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t) continue;
				const bool splash = p.melon && melonHits[i];
				const bool area = !p.melon && p.multiTarget && std::abs(u.row - p.row) <= p.rowRadius
					&& (p.around ? std::abs(u.x - p.x) <= p.range : u.x >= p.x - 30 && u.x <= p.x + p.range);
				if (static_cast<int>(i) != target && !splash && !area) continue;
				u.health -= (splash ? secondaryDps : p.dps) * kStep;
				if (u.canBeChilled && u.slowImmunity <= t && p.slowRate > 0)
					u.slow = std::max(u.slow, std::min(p.slowDuration, p.slowRate * p.slowDuration * kStep * 2));
				u.stopped = std::max(u.stopped, std::min(kStep * 0.9f, p.stopDuty * kStep));
			}
		}
		for (size_t i = 0; i < units.size(); ++i) {
			auto& worker = units[i]; auto& u = worker.body;
			if (u.health <= 0 || u.spawnAt > t) continue;
			const float active = std::max(0.0f, kStep - u.stopped);
			u.stopped = std::max(0.0f, u.stopped - kStep);
			const float speedFactor = u.slow > 0 ? u.slowFactor : 1;
			u.slow = std::max(0.0f, u.slow - kStep);
			if (u.economic && u.health > worker.productionStopHealth) {
				worker.productionRemaining -= active;
				while (worker.productionRemaining <= 0) {
					if (t < kHorizon) f[4] += static_cast<int>(worker.nextYield);
					worker.productionRemaining += IceProduction::Interval;
					worker.nextYield = std::min(IceProduction::MaximumYield, worker.nextYield * IceProduction::YieldGrowth);
				}
			}
			int contact = -1;
			for (size_t j = 0; j < plants.size(); ++j) {
				const auto& p = plants[j];
				if (p.health <= 0 || !p.edible || p.row != u.row || p.x > u.x + 30) continue;
				if (contact < 0 || p.x > plants[contact].x
					|| (p.x == plants[contact].x && p.layer > plants[contact].layer)) contact = static_cast<int>(j);
			}
			if (contact >= 0 && u.x <= plants[contact].x + kContact) {
				auto& p = plants[contact];
				float damage = worker.biteDps * active * (speedFactor < 1 ? 0.5f : 1);
				if (u.smashSeconds > 0) {
					smash[i] += active * (speedFactor < 1 ? 0.6f : 1);
					damage = smash[i] >= u.smashSeconds ? p.health : 0;
					if (damage > 0) smash[i] = 0;
				}
				f[1] += p.reward * std::min(p.health, damage) / std::max(1.0f, p.initialHealth);
				p.health -= damage;
				if (p.health <= 0) f[0] += p.reward;
			} else {
				u.x -= u.speed * active * speedFactor;
				if (u.x < s.houseX) { f[2] += 1; u.health = 0; breached[i] = true; }
			}
		}
		for (size_t i = 0; i < units.size(); ++i) if (!refunded[i] && !breached[i] && units[i].body.health <= 0) {
			playerIce += units[i].playerRefund; refunded[i] = true;
		}
	}
	for (size_t i = 0; i < units.size(); ++i) if (units[i].body.health > 0) {
		const auto& u = units[i].body;
		f[3] += u.purchaseCost * std::clamp(u.health / std::max(1.0f, initialHealth[i]), 0.0f, 1.0f);
		f[7] += u.purchaseCost * std::clamp((initialX[i] - u.x) / 800, 0.0f, 1.0f);
	}
	if (construction) *construction = constructionStats;
	return f;
}

Result Search(const Snapshot& s, const Weights& baseWeights, std::uint32_t seed) {
	Result best;
	if (!ValidWeights(baseWeights) || (s.stateModel && !s.stateModel->IsValid())) return best;
	best.features = Evaluate(s, {}, &best.construction); Calibrate(s,best);
	const auto inputs = DescribeState(s,best.features);
	const auto conditioned = ConditionWeights(baseWeights,inputs,s.stateModel);
	const auto weights = s.netEconomy ? AccountForIce(conditioned) : conditioned;
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.baselineFeatures = best.features; best.score = Score(best.features, weights); best.evaluated = 1;
	if (s.options.empty() || s.capacity <= 0 || s.budget <= 0) return best;
	const auto cheapest = std::min_element(s.options.begin(),s.options.end(),[](const auto& a,const auto& b) { return a.cost < b.cost; });
	if (cheapest->cost > s.budget) return best;
	std::mt19937 rng(seed); // 局部共同随机数使同一快照/参数可重复，搜索次数不改变正式战斗随机流。
	std::vector<Result> elite{best};
	bool hasChoice = s.allowWait; // 正式构建启用 fast-math，不能用无穷大充当尚无候选的哨兵。
	bool deferredInvestment = false;
	for (int trial = 1; trial < kTrials; ++trial) {
		auto plan = elite[rng() % elite.size()].actions;
		// 独立抽完整队伍，允许跨过“单只亏损、协同才盈利”的谷底，不强制任何兵种模板。
		if (trial % 3 == 0) {
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
		// 给有钱的空场提供一个必定可行的起点；其余候选仍自由比较兵种、路线和队形。
		if (!s.allowWait && trial == 1) plan = {{static_cast<int>(cheapest - s.options.begin()),0}};
		Repair(s, plan);
		if (!s.allowWait && !plan.empty()) plan.front().delay = 0;
		auto candidate = EvaluatePlan(s, weights, std::move(plan), best.baselineFeatures);
		// 在候选比较中排除亏损增援，不能选完后才丢弃第一名而漏掉其余可行方案。
		if (ShouldRegroup(candidate, s.budget, s.recoveryReserve)) {
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
	// 固定自由搜索选出的兵种、预算和时序，完整比较各合法行。已有部队仍留在原行参与推演，
	// 因此可以发现继续支援巨人的收益，也能因灰烬、溅射或减速而保留分路方案。
	const auto original = best.actions;
	const float baseScore = best.score;
	std::array<float, 6> rowScores{};
	int tested = 0, rejected = 0, chosenRow = -1, evaluated = kTrials;
	if (!original.empty()) for (int row = 0; row < static_cast<int>(s.context.size()); ++row) {
		auto plan = original;
		if (!Concentrate(s, plan, row)) continue;
		auto candidate = EvaluatePlan(s, weights, std::move(plan), best.baselineFeatures);
		++evaluated; tested |= 1 << row; rowScores[row] = candidate.score;
		if (ShouldRegroup(candidate, s.budget, s.recoveryReserve)) { rejected |= 1 << row; continue; }
		if (candidate.score > best.score + 0.001f) { best = std::move(candidate); chosenRow = row; }
	}
	best.formationBaseScore = baseScore; best.formationScores = rowScores;
	best.formationTested = tested; best.formationRejected = rejected; best.formationChosenRow = chosenRow;
	best.evaluated = evaluated;
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.regrouping = best.actions.empty() && deferredInvestment;
	return best;
}
}
