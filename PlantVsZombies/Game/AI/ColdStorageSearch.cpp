#include "ColdStorageSearch.h"
#include "Game/Board/IceProduction.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace ColdStorageSearch {
namespace {
constexpr float kHorizon = 60; // 推演覆盖的游戏秒，实际对局评测负责检验更长期收益
constexpr float kStep = 0.5f; // 仅候选预测的积分步长；真实比赛仍使用正式固定步
constexpr int kTrials = 96; // 每次决策最多评估的自由计划数量，限制游戏内开销
constexpr int kMaxActions = 8; // 一次搜索最多承诺的新增单位数，下次观察后可以继续部署
constexpr float kMaxDelay = 12; // 新队员最迟出生时间，游戏秒
constexpr float kContact = 55; // 接触植物的预测距离，像素
constexpr float kAreaCounterStake = 24; // 玩家倾向对至少此冰价的集中兵力使用大范围清场；危及房屋时不等待
constexpr float kTargetCounterStake = 8; // 窄范围反制允许用于较小目标，冰价
constexpr float kSquashTriggerRange = 125; // 倭瓜候选种植点可触发近邻目标的预测距离，像素
constexpr float kSquashImpactRange = 50; // 倭瓜落点的窄范围碰撞近似，像素

/** 修复变异后的越界和超预算动作，稳定排序保留同一时刻的提交次序。 */
void Repair(const Snapshot& s, std::vector<Action>& actions) {
	int spent = 0;
	actions.erase(std::remove_if(actions.begin(), actions.end(), [&](Action& a) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) return true;
		const int cost = s.options[a.option].cost;
		if (cost <= 0 || spent + cost > s.budget) return true;
		spent += cost;
		a.delay = std::clamp(a.delay, 0.0f, kMaxDelay);
		return false;
	}), actions.end());
	if (actions.size() > static_cast<size_t>(std::max(0, std::min(kMaxActions, s.capacity))))
		actions.resize(std::max(0, std::min(kMaxActions, s.capacity)));
	std::stable_sort(actions.begin(), actions.end(), [](auto a, auto b) { return a.delay < b.delay; });
}
float Score(const Weights& f, const Weights& w) {
	float value = 0;
	for (int i = 0; i < FeatureCount; ++i) value += f[i] * w[i];
	return value;
}

struct PendingCounter { ColdStorageStrategy::BlastThreat blast; float at; };

/** 在当前推演位置判断覆盖，不沿用战斗开始时的静态轨迹。 */
bool CounterHits(const ColdStorageStrategy::BlastThreat& blast, const Unit& unit, float time, float rightEdge) {
	const auto& body = unit.body;
	return body.health > 0 && body.spawnAt <= time && body.x <= rightEdge && blast.reach[body.row] >= 0
		&& std::abs(body.x + (blast.usesObjectX ? body.blastAnchorOffset : 0) - blast.x) <= blast.reach[body.row];
}

/** 多张牌共享真实资源、同卡落点共享冷却；先兑现已提交反制，再选择一次可支付的新动作。 */
void AdvanceCounters(const Snapshot& state, float time, std::vector<Unit>& units,
	const std::vector<float>& initialHealth, std::vector<float>& ready,
	std::vector<PendingCounter>& pending, float& sun, float& ice, Weights& features) {
	for (auto it = pending.begin(); it != pending.end();) {
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
	float best = 0;
	ColdStorageStrategy::BlastThreat impact;
	for (size_t c = 0; c < state.counters.size(); ++c) {
		const auto& counter = state.counters[c];
		if (counter.blast.committed || time < ready[counter.source]
			|| counter.sunCost > sun || counter.iceCost > ice) continue;
		auto candidate = counter.blast;
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
		if (value > best) { best = value; selected = static_cast<int>(c); impact = candidate; }
	}
	if (selected >= 0) {
		const auto& counter = state.counters[selected];
		sun -= counter.sunCost; ice -= counter.iceCost;
		ready[counter.source] = time + counter.recharge;
		pending.push_back({impact,time + counter.windup});
	}
}
}

bool ValidWeights(const Weights& weights) {
	return std::all_of(weights.begin(), weights.end(), [](float w) { return std::isfinite(w) && std::abs(w) <= 500; });
}

Weights Evaluate(const Snapshot& s, const std::vector<Action>& plan) {
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
	std::vector<float> initialHealth, initialX, smash(units.size());
	for (const auto& u : units) { initialHealth.push_back(u.body.health); initialX.push_back(u.body.x); }
	std::vector<float> counterReady;
	std::vector<PendingCounter> pending;
	for (const auto& counter : s.counters) {
		if (counter.blast.committed) pending.push_back({counter.blast,counter.blast.ready});
		else {
			if (counterReady.size() <= static_cast<size_t>(counter.source)) counterReady.resize(counter.source + 1);
			counterReady[counter.source] = counter.blast.ready;
		}
	}
	std::vector<bool> refunded(units.size()), breached(units.size());
	float playerSun = static_cast<float>(s.playerSun), playerIce = static_cast<float>(s.playerIce);
	bool orderArrived = false;
	for (float t = 0; t < kHorizon; t += kStep) {
		if (!orderArrived && t >= s.incomingIceAt) {
			playerIce += s.incomingIce; orderArrived = true;
		}
		for (const auto& p : plants) if (p.health > 0) playerSun += p.sunPerSecond * kStep;
		AdvanceCounters(s,t,units,initialHealth,counterReady,pending,playerSun,playerIce,f);
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
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t) continue;
				const bool splash = p.melon && std::abs(u.row - impact.row) <= p.rowRadius
					&& std::abs(u.x - impact.x) < 60 + u.boundsWidth * 0.5f;
				const bool area = !p.melon && p.multiTarget && std::abs(u.row - p.row) <= p.rowRadius
					&& (p.around ? std::abs(u.x - p.x) <= p.range : u.x >= p.x - 30 && u.x <= p.x + p.range);
				if (static_cast<int>(i) != target && !splash && !area) continue;
				u.health -= p.dps * kStep * (splash && static_cast<int>(i) != target ? 1.0f / 3 : 1);
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
			if (u.economic) {
				worker.productionRemaining -= active;
				while (worker.productionRemaining <= 0) {
					f[4] += static_cast<int>(worker.nextYield);
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
				f[1] += p.reward * std::min(p.health, damage) / std::max(1.0f, s.plants[contact].health);
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
	return f;
}

Result Search(const Snapshot& s, const Weights& weights, std::uint32_t seed) {
	Result best;
	if (!ValidWeights(weights)) return best;
	best.features = Evaluate(s, {}); best.baselineFeatures = best.features; best.score = Score(best.features, weights); best.evaluated = 1;
	if (s.options.empty() || s.capacity <= 0 || s.budget <= 0) return best;
	const auto cheapest = std::min_element(s.options.begin(),s.options.end(),[](const auto& a,const auto& b) { return a.cost < b.cost; });
	if (cheapest->cost > s.budget) return best;
	std::mt19937 rng(seed); // 局部共同随机数使同一快照/参数可重复，搜索次数不改变正式战斗随机流。
	std::vector<Result> elite{best};
	bool hasChoice = s.allowWait; // 正式构建启用 fast-math，不能用无穷大充当尚无候选的哨兵。
	for (int trial = 1; trial < kTrials; ++trial) {
		auto plan = elite[rng() % elite.size()].actions;
		// 独立抽完整队伍，允许跨过“单只亏损、协同才盈利”的谷底，不强制任何兵种模板。
		if (trial % 3 == 0) {
			plan.clear();
			const int focus = s.options[rng() % s.options.size()].row;
			for (int count = 1 + rng() % kMaxActions; count > 0; --count) {
				int option = rng() % s.options.size();
				if (rng() % 4 != 0) {
					for (int attempt = 0; attempt < 12 && s.options[option].row != focus; ++attempt) option = rng() % s.options.size();
				}
				plan.push_back({option, static_cast<float>(rng() % 25) * 0.5f});
			}
		}
		const int mutations = 1 + rng() % 3;
		for (int n = 0; n < mutations; ++n) {
			const int mutation = rng() % 5;
			if (plan.empty() || mutation == 0) plan.push_back({static_cast<int>(rng() % s.options.size()), static_cast<float>(rng() % 25) * 0.5f});
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
		Result candidate; candidate.actions = std::move(plan); candidate.features = Evaluate(s, candidate.actions);
		candidate.baselineFeatures = best.baselineFeatures;
		candidate.score = Score(candidate.features, weights);
		// 特殊能力的模型残差由真实对局学习，不在这里按品种写固定的偏好名单。
		for (const auto& action : candidate.actions) {
			const auto& option = s.options[action.option];
			auto context = s.context[option.row];
			context[0] = 1;
			// 计划中的同行队友也算协作背景，允许搜索发现尚未出生的支援组合。
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
		if ((s.allowWait || !candidate.actions.empty()) && (!hasChoice || candidate.score > best.score + 0.001f)) {
			best = candidate; hasChoice = true;
		}
		elite.push_back(std::move(candidate));
		std::stable_sort(elite.begin(), elite.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
		if (elite.size() > 8) elite.resize(8);
	}
	best.evaluated = kTrials;
	return best;
}
}
