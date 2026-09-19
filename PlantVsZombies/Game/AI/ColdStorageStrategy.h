#pragma once

#include <vector>
#include <array>

/** 冷藏站战略层只消费 Board 提供的可见数值，不读取实体、不改变资源或正式随机序列。 */
namespace ColdStorageStrategy {
/** 编队推演的单位投影；位置为碰撞参考 X，命中原点使用相对偏移，队列以出生时间进入。 */
struct SplashUnit {
	int row = 0;
	float x = 0, speed = 0, health = 0, value = 0;
	float purchaseCost = 0; // 爆区内的兵力投资价值，冰块；与包含产能的经营价值分开
	float smashSeconds = 0; // 破障前锋的砸击周期，零表示普通啃食单位
	float blastAnchorOffset = 0; // 碰撞箱参考 X 到正式爆炸使用的对象 X 的偏移，像素
	float spawnAt = 0, stopped = 0, eating = 0, slow = 0, slowFactor = 0.3f;
	float slowImmunity = 0, stopX = 0, boundsOffset = -25, boundsWidth = 50;
	bool canBeChilled = true;
	bool economic = false;
};

struct SplashField {
	std::array<float, 6> directDps{}, melonDps{}, melonSlowDuty{}, directSlowDuty{}, slowDuration{}, plantX{};
	float targetRight = 1100;
};

/** 比较增援前后已有队伍的承伤和推进价值；允许负值表示增援替队友减轻火力。 */
float ForecastSplashExternality(const SplashField& field, const std::vector<SplashUnit>& current,
	const std::vector<SplashUnit>& additions);

/** Board 提供当前合法爆点及可用时间；负半径代表不覆盖该行。 */
struct BlastThreat {
	float x = 0, ready = 0, damage = 0;
	std::array<float, 6> reach{};
	bool committed = false;
	bool usesObjectX = true;
};

struct BlastRisk {
	float loss = 0, addedLoss = 0, time = 0;
	std::vector<float> damageByUnit;
};

/** 在有限时域内找出会覆盖新增队员的最昂贵爆区，逐只计损；已有队员也计入同一爆区。 */
BlastRisk ForecastBlastRisk(const std::vector<SplashUnit>& units, size_t firstAdded,
	const std::vector<BlastThreat>& threats, const std::array<float, 6>& slowDuty, float rightEdge);

struct Target {
	float x = 0.0f;
	float health = 0.0f;
	float attackDps = 0.0f;
	float reward = 0.0f;
};

struct Raid {
	struct Member { float health, speed, cost, smashSeconds; };
	std::vector<Member> members; // 实际拟购编队，顺序也是前后梯队顺序
	std::vector<Target> targets; // 已按前线到后排排列，同格普通层/保护层合并
	float spawnX = 0.0f;
	float guardHealth = 0.0f;
	float guardSmashSeconds = 0.0f, guardSpeed = 0.0f;
	float directDps = 0.0f;
	float splashDps = 0.0f;
	float slowFactor = 1.0f;
	float blastTime = 60.0f;
	float guardBlastDamage = 0.0f;
	std::vector<float> blastDamage; // 对应 members，各自承受的爆炸伤害；不能当作共享血池
};

struct RaidResult {
	float income = 0.0f;
	float net = 0.0f; // 击杀返冰减购兵支出，并折损预计送给玩家的返冰
	float remainingHealth = 0.0f;
	int cellsBroken = 0;
};

/** 对同一路的整批进攻估算前进、承伤、破格和击杀收益；不提前发放预测收入。 */
RaidResult ForecastRaid(const Raid& raid);

struct Situation {
	float defenseDps = 0.0f;
	float defenseHealth = 0.0f;
	float playerGrowthDps = 0.0f; // 当前卡槽/资源约束下未来一分钟可增加的火力
	float productionIncome = 0.0f; // 已有工人未来一分钟预计收入
	float investmentNet = 0.0f;
	float raidNet = 0.0f;
	int raidCellsBroken = 0;
	int rows = 5;
};

struct Policy {
	const char* name = "balanced";
	float spendingHorizon = 120.0f; // 将当前库存分摊到未来多久，游戏秒，不是关卡结束时间
	bool racePlayer = false;
	bool buildEconomy = false;
	bool siege = false;
};

/** 比较玩家补阵、己方生产和当前破阵收益，选择抢攻、经营或集中攻坚。 */
Policy Choose(const Situation& situation);
}
