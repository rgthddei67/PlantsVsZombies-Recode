#include "ColdStorageStrategy.h"
#include <algorithm>
#include <cmath>

namespace ColdStorageStrategy {
namespace {
	constexpr float kRaidHorizon = 60.0f; // 一批进攻的收益预测时域，游戏秒
	constexpr float kContactDistance = 55.0f; // 进入啃食或砸击的距离近似，像素
	constexpr float kBiteDps = 50.0f; // 普通前排单位持续啃食的等效伤害/秒
	constexpr float kSplashCrowding = 0.18f; // 每多一个同路单位，增加的群伤暴露比例
	constexpr float kBombExpectedDamage = 900.0f; // 可用爆炸牌的期望折损；不把持牌视作确定释放
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
				const float impact = target.x + target.boundsOffset + target.boundsWidth * 0.5f;
				auto hit = [&](size_t i) {
					const auto& u = units[i];
					return static_cast<int>(i) != primary && u.health > 0 && time > u.spawnAt && std::abs(row - u.row) <= 1
						&& u.x + u.boundsOffset <= impact + kSplashHalfWidth
						&& u.x + u.boundsOffset + u.boundsWidth >= impact - kSplashHalfWidth;
				};
				int count = 0;
				for (size_t i = 0; i < units.size(); ++i) if (hit(i)) ++count;
				const float secondary = field.melonDps[row] * std::min(1.0f / 3.0f, kSplashDamageBudget / std::max(1, count));
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

RaidResult ForecastRaid(const Raid& raid)
{
	RaidResult result;
	const int count = std::max(1, raid.count);
	const float initialHealth = raid.health * count + raid.guardHealth;
	float health = initialHealth, time = 0.0f, x = raid.spawnX;
	float fire = std::max(0.0f, raid.directDps);
	bool blastApplied = false;
	const float speed = std::max(1.0f, raid.speed * raid.slowFactor);
	for (const auto& target : raid.targets) {
		const float travel = std::max(0.0f, x - target.x - kContactDistance) / speed;
		const float survivors = std::clamp(health / std::max(1.0f, raid.health), 1.0f, static_cast<float>(count));
		const float breaking = raid.smashSeconds > 0.0f
			? raid.smashSeconds / std::max(0.5f, raid.slowFactor)
			: target.health / (kBiteDps * std::min(3.0f, survivors));
		const float interval = travel + breaking;
		if (time + interval > kRaidHorizon) break;
		// 前排吸收直伤，紧邻队员承受部分溅射；不会把整团血量视为无损共享护甲。
		health -= (fire + raid.splashDps * (survivors - 1.0f) * kSplashCrowding) * interval;
		if (!blastApplied && raid.responseWindow <= time + interval) {
			health -= kBombExpectedDamage * std::min(3, count);
			blastApplied = true;
		}
		if (health <= 0.0f) break;
		time += interval;
		x = target.x + kContactDistance;
		result.income += target.reward;
		++result.cellsBroken;
		// 真正清掉火力格才降低后续承伤，体现连续破阵的正反馈。
		fire = std::max(0.0f, fire - target.attackDps);
	}
	result.remainingHealth = std::max(0.0f, health);
	const float losses = std::clamp((initialHealth - health) / std::max(1.0f, raid.health), 0.0f, static_cast<float>(count));
	result.net = result.income - raid.cost * count - losses * raid.cost * 0.75f * kRefundWeight;
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
