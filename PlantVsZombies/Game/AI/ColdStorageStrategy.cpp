#include "ColdStorageStrategy.h"
#include <algorithm>
#include <cmath>

namespace ColdStorageStrategy {
namespace {
	constexpr float kBlastHorizon = 30.0f; // 梯队可能进入同一爆区的预测窗口，游戏秒
	constexpr float kBlastStep = 0.5f; // 爆区轨迹采样间隔，游戏秒
	constexpr float kBreachHealth = 2000.0f; // 厚障碍需要等待破障单位先接触，生命值
	constexpr float kRaidHorizon = 60.0f; // 一批进攻的收益预测时域，游戏秒
	constexpr float kContactDistance = 55.0f; // 进入啃食或砸击的距离近似，像素
	constexpr float kBiteDps = 50.0f; // 普通前排单位持续啃食的等效伤害/秒
	constexpr float kSplashCrowding = 0.18f; // 每多一个同路单位，增加的群伤暴露比例
	constexpr float kRefundWeight = 0.5f; // 给玩家返冰对进攻净收益的额外折损权重
	constexpr float kGrowthFraction = 0.25f; // 玩家未来火力相对当前增加到此比例时考虑抢攻
	constexpr float kSiegeDpsPerRow = 55.0f; // 平均行火力达到此值时采用成型防线攻坚预算
	constexpr float kSiegeHealthPerRow = 6500.0f; // 平均行耐久达到此值时采用攻坚预算
	constexpr float kSplashHorizon = 20.0f; // 邻路引火的短时评估窗，游戏秒
	constexpr float kSplashStep = 0.5f; // 引火推演步长，游戏秒
	constexpr float kSplashHalfWidth = 30.0f; // 正式西瓜命中窗口半宽，像素，另与单位碰撞箱相交
	constexpr float kSplashDamageBudget = 7.0f; // 次要目标总伤害最多为直击七倍，与正式西瓜结算一致
	constexpr float kProgressWeight = 0.5f; // 推进损失相对单位价值的权重，避免只统计扣血而忽略群体减速
}

bool MelonSplashContains(const SplashUnit& primary, const SplashUnit& secondary) {
	const float impact = primary.x + primary.boundsOffset + primary.boundsWidth * 0.5f;
	return std::abs(primary.row-secondary.row) <= 1
		&& secondary.x+secondary.boundsOffset <= impact+kSplashHalfWidth
		&& secondary.x+secondary.boundsOffset+secondary.boundsWidth >= impact-kSplashHalfWidth;
}

float MelonSecondaryDps(float directDps, int secondaryCount) {
	return directDps * std::min(1.0f/3.0f,kSplashDamageBudget/std::max(1,secondaryCount));
}

float ForecastSplashExternality(const SplashField& field, const std::vector<SplashUnit>& current,
	const std::vector<SplashUnit>& additions)
{
	if (current.empty() || additions.empty()
		|| std::none_of(field.melonDps.begin(), field.melonDps.end(), [](float dps) { return dps > 0; })) return 0;
	// 两个分支共享同一快照和确定性步长；只评价原队伍，候选自身收益由出兵评分负责。
	auto retainedValue = [&](bool adding) {
		auto units = current;
		if (adding) units.insert(units.end(), additions.begin(), additions.end());
		std::vector<float> damage(units.size()), chilling(units.size()), chillDuration(units.size());
		std::vector<float> futureSlow(units.size()), futureSlowDuty(units.size());
		for (float time = kSplashStep; time <= kSplashHorizon; time += kSplashStep) {
			std::array<int, 6> targets;
			targets.fill(-1);
			std::fill(damage.begin(), damage.end(), 0.0f);
			std::fill(chilling.begin(), chilling.end(), 0.0f);
			std::fill(chillDuration.begin(), chillDuration.end(), 0.0f);
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i];
				if (u.health <= 0 || time <= u.spawnAt) continue;
				const float age = time - u.spawnAt;
				if (age > u.stopped + u.eating) {
					const float dt = std::min(kSplashStep, age - u.stopped - u.eating);
					const float duty = std::max(std::clamp(u.slow / kSplashStep, 0.0f, 1.0f),
						std::clamp(futureSlow[i] / kSplashStep, 0.0f, 1.0f) * futureSlowDuty[i]);
					const float slowFactor = 1.0f - (1.0f - u.slowFactor) * duty;
					u.x -= std::min(std::max(0.0f, u.x - u.stopX), u.speed * dt * slowFactor);
				}
				u.slow = std::max(0.0f, u.slow - kSplashStep);
				futureSlow[i] = std::max(0.0f, futureSlow[i] - kSplashStep);
				const float center = u.x + u.boundsOffset + u.boundsWidth * 0.5f;
				if (center > field.targetRight || u.x + u.boundsOffset + u.boundsWidth < field.plantX[u.row]) continue;
				const int old = targets[u.row];
				if (old < 0 || center < units[old].x + units[old].boundsOffset + units[old].boundsWidth * 0.5f)
					targets[u.row] = static_cast<int>(i);
			}
			for (int row = 0; row < 6; ++row) {
				const int primary = targets[row];
				if (primary < 0) continue;
				damage[primary] += field.directDps[row] * kSplashStep;
				chilling[primary] = std::max(chilling[primary], field.directSlowDuty[row]);
				chillDuration[primary] = std::max(chillDuration[primary], field.slowDuration[row]);
				if (field.melonDps[row] <= 0) continue;
				const auto& target = units[primary];
				auto hit = [&](size_t i) {
					const auto& u = units[i];
					return static_cast<int>(i) != primary && u.health > 0 && time > u.spawnAt && MelonSplashContains(target,u);
				};
				int count = 0;
				for (size_t i = 0; i < units.size(); ++i) if (hit(i)) ++count;
				const float secondary = MelonSecondaryDps(field.melonDps[row],count);
				for (size_t i = 0; i < units.size(); ++i) if (hit(i)) {
					damage[i] += secondary * kSplashStep;
					chilling[i] = std::max(chilling[i], field.melonSlowDuty[row]);
					chillDuration[i] = std::max(chillDuration[i], field.slowDuration[row]);
				}
			}
			// 同步结算避免单位枚举顺序改变这一小步的溅射集合；不改正式弹丸或减速规则。
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i];
				u.health = std::max(0.0f, u.health - damage[i]);
				if (chilling[i] > 0 && u.canBeChilled && time - u.spawnAt >= u.slowImmunity) {
					if (futureSlow[i] <= 0) futureSlowDuty[i] = 0;
					futureSlow[i] = std::max(futureSlow[i], chillDuration[i]);
					futureSlowDuty[i] = std::max(futureSlowDuty[i], std::clamp(chilling[i], 0.0f, 1.0f));
				}
			}
		}
		float result = 0;
		for (size_t i = 0; i < current.size(); ++i) {
			const auto& before = current[i];
			const float fraction = units[i].health / std::max(1.0f, before.health);
			const float progress = std::clamp((before.x - units[i].x) / std::max(1.0f, before.speed * kSplashHorizon), 0.0f, 1.0f);
			result += before.value * fraction * (1.0f + kProgressWeight * progress);
		}
		return result;
	};
	return retainedValue(false) - retainedValue(true);
}

BlastRisk ForecastBlastRisk(const std::vector<SplashUnit>& units, size_t firstAdded,
	const std::vector<BlastThreat>& threats, const std::array<float, 6>& slowDuty, float rightEdge)
{
	BlastRisk result;
	if (firstAdded >= units.size() || threats.empty()) return result;
	// 先缓存轨迹，再扫描爆点；比较的是同一未来时刻的队伍，不把分路误认为分散。
	constexpr int samples = static_cast<int>(kBlastHorizon / kBlastStep) + 1;
	std::vector<std::array<float, samples>> positions(units.size());
	for (size_t i = 0; i < units.size(); ++i) {
		const auto& u = units[i];
		for (int step = 0; step < samples; ++step) {
			const float age = std::max(0.0f, step * kBlastStep - u.spawnAt);
			const float active = std::max(0.0f, age - u.stopped - u.eating);
			const float slowed = std::clamp(u.slow - u.stopped - u.eating, 0.0f, active);
			const float currentDistance = u.speed * (active - slowed * (1.0f - u.slowFactor));
			const float outside = std::max(0.0f, u.x - rightEdge);
			const float duty = u.canBeChilled && age >= u.slowImmunity ? std::clamp(slowDuty[u.row], 0.0f, 1.0f) : 0;
			const float futureDistance = std::min(outside, u.speed * active)
				+ std::max(0.0f, u.speed * active - outside) * (1.0f - (1.0f - u.slowFactor) * duty);
			positions[i][step] = u.x - std::min(std::max(0.0f, u.x - u.stopX), std::min(currentDistance, futureDistance));
		}
	}
	for (const auto& threat : threats) {
		if (threat.ready > kBlastHorizon) continue;
		const int first = std::clamp(static_cast<int>(std::ceil(threat.ready / kBlastStep)), 0, samples - 1);
		const int last = threat.committed ? first : samples - 1;
		for (int step = first; step <= last; ++step) {
			float loss = 0, added = 0;
			auto hit = [&](size_t i) {
				const auto& u = units[i];
				return u.health > 0 && step * kBlastStep >= u.spawnAt && threat.reach[u.row] >= 0
					&& std::abs(positions[i][step] + (threat.usesObjectX ? u.blastAnchorOffset : 0.0f) - threat.x) <= threat.reach[u.row];
			};
			for (size_t i = 0; i < units.size(); ++i) if (hit(i)) {
				const float value = units[i].purchaseCost * std::min(1.0f, threat.damage / units[i].health);
				loss += value;
				if (i >= firstAdded) added += value;
			}
			if (added <= 0 || loss <= result.loss) continue;
			result.loss = loss; result.addedLoss = added; result.time = step * kBlastStep;
			result.damageByUnit.assign(units.size(), 0);
			for (size_t i = 0; i < units.size(); ++i) if (hit(i)) result.damageByUnit[i] = threat.damage;
		}
	}
	return result;
}

RaidResult ForecastRaid(const Raid& raid)
{
	RaidResult result;
	auto members = raid.members;
	if (members.empty()) return result;
	float guards = raid.guardHealth, time = 0.0f, x = raid.spawnX, losses = 0;
	float fire = std::max(0.0f, raid.directDps);
	bool blastApplied = false;
	for (const auto& target : raid.targets) {
		int alive = 0;
		float speed = 0, smash = guards > 0 ? raid.guardSmashSeconds : 0;
		for (const auto& m : members) if (m.health > 0) {
			++alive;
			speed = std::max(speed, m.speed);
			if (m.smashSeconds > 0) smash = smash > 0 ? std::min(smash, m.smashSeconds) : m.smashSeconds;
		}
		if (alive == 0) break;
		// 厚障碍由砸击单位先接触，快速单位随后利用缺口；纯橄榄仍需支付啃食时间。
		if (smash > 0 && target.health >= kBreachHealth)
		{
			if (guards > 0 && raid.guardSmashSeconds > 0) speed = std::min(speed, raid.guardSpeed);
			for (const auto& m : members) if (m.health > 0 && m.smashSeconds > 0) speed = std::min(speed, m.speed);
		}
		const float travel = std::max(0.0f, x - target.x - kContactDistance) / std::max(1.0f, speed * raid.slowFactor);
		const float breaking = smash > 0 ? smash / std::max(0.5f, raid.slowFactor)
			: target.health / (kBiteDps * std::min(3, alive));
		const float interval = travel + breaking;
		if (time + interval > kRaidHorizon) break;
		float direct = fire * interval;
		const float guardDamage = std::min(guards, direct);
		guards -= guardDamage; direct -= guardDamage;
		for (auto& m : members) if (m.health > 0) {
			const float hit = std::min(m.health, direct);
			m.health -= hit; direct -= hit;
		}
		for (size_t i = 0; i < members.size(); ++i) {
			auto& m = members[i];
			m.health = std::max(0.0f, m.health - raid.splashDps * kSplashCrowding * interval);
			if (!blastApplied && raid.blastTime <= time + interval && i < raid.blastDamage.size())
				m.health = std::max(0.0f, m.health - raid.blastDamage[i]);
		}
		if (!blastApplied && raid.blastTime <= time + interval) {
			guards = std::max(0.0f, guards - raid.guardBlastDamage);
			blastApplied = true;
		}
		if (std::none_of(members.begin(), members.end(), [](const auto& m) { return m.health > 0; })) break;
		// 破障者在接触前被炸死时，后面的快兵不能继承一次并未发生的砸击。
		if (smash > 0 && target.health >= kBreachHealth && !(guards > 0 && raid.guardSmashSeconds > 0)
			&& std::none_of(members.begin(), members.end(), [](const auto& m) { return m.health > 0 && m.smashSeconds > 0; })) break;
		time += interval; x = target.x + kContactDistance;
		result.income += target.reward; ++result.cellsBroken;
		fire = std::max(0.0f, fire - target.attackDps);
	}
	result.remainingHealth = guards;
	for (size_t i = 0; i < members.size(); ++i) {
		const auto& m = members[i];
		const float initial = raid.members[i].health;
		losses += (1.0f - m.health / std::max(1.0f, initial)) * m.cost;
		result.net -= m.cost;
		result.remainingHealth += m.health;
	}
	result.net += result.income - losses * 0.75f * kRefundWeight;
	return result;
}

Policy Choose(const Situation& s)
{
	Policy policy;
	const bool growingFast = s.playerGrowthDps >= std::max(20.0f, s.defenseDps * kGrowthFraction);
	const bool productionWins = s.productionIncome >= 60.0f && s.investmentNet > 8.0f;
	// 进攻能连续破格且收益优于投资，或玩家会迅速补齐防线时，库存优先服务眼前窗口。
	if (s.raidCellsBroken >= 2 && (s.raidNet > std::max(8.0f, s.investmentNet)
		|| (growingFast && !productionWins))) {
		policy.name = "short_game";
		policy.spendingHorizon = 45.0f;
		policy.racePlayer = true;
	}
	else if (s.investmentNet > 8.0f && (!growingFast || productionWins)) {
		policy.name = "long_game";
		policy.spendingHorizon = 150.0f;
		policy.buildEconomy = true;
	}
	else if (s.defenseDps >= s.rows * kSiegeDpsPerRow || s.defenseHealth >= s.rows * kSiegeHealthPerRow) {
		policy.name = "siege";
		policy.spendingHorizon = 75.0f;
		policy.siege = true;
	}
	else if (growingFast) {
		policy.name = "short_game";
		policy.spendingHorizon = 60.0f;
		policy.racePlayer = true;
	}
	return policy;
}
}
