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
