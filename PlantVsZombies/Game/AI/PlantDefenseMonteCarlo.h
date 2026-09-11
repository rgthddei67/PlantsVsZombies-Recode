#pragma once

#include <cstdint>
#include <vector>

namespace PlantDefenseMonteCarlo {

struct Bounds {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

struct PlantSnapshot {
	int id = 0;
	int row = 0;
	int column = 0;
	float x = 0.0f;
	float health = 0.0f;
	float maxHealth = 1.0f;
	float strategicValue = 0.0f;
	float attackDps = 0.0f;
	int attackRowRadius = 0;
	float sunPerSecond = 0.0f;
	float productionDelay = 0.0f;
	Bounds bounds;
	bool pumpkinShell = false;
	int hijackerExecutionGroup = -1;
	bool countsForHijackerExecution = false;
	bool diesWithHijackerExecutionGroup = false;
	bool protectedFromHijackerExecution = false;
	int eatingLayerPriority = 1; // 正式战斗层级：under=0、normal=1、pumpkin=2；负数不参与啃食
	bool canBeEaten = true;
	float shutdownRemaining = 0.0f;
	bool protectedFromNightRoofCharge = false;
	float slowApplicationsPerSecond = 0.0f;
	float slowDuration = 0.0f;
	float frozenApplicationsPerSecond = 0.0f;
	float frozenDuration = 0.0f;
	float butterApplicationsPerSecond = 0.0f;
	float butterDuration = 0.0f;
	float paralysisApplicationsPerSecond = 0.0f;
	float paralysisDuration = 0.0f;
	float y = 0.0f;
	float abilityCooldownRemaining = 0.0f;
	float magneticPulseCooldown = 0.0f;
	float magneticPulseRadius = 0.0f;
	float magneticPulseParalysisDuration = 0.0f;
	int magneticSearchRowRadius = 0;
	float magneticSearchRadius = 0.0f;
	float magneticEatingSearchRadius = 0.0f;
	float magneticRowDistancePenalty = 0.0f;
	float cobBlastCooldown = 0.0f;
	float cobBlastDamage = 0.0f;
	float cobBlastRadius = 0.0f;
	int cobBlastRowRadius = 0;
};

/**
 * @brief 每格至多一株的普通承载植物摘要；不占详细植物数组容量。
 */
struct SupportSnapshot {
	int id = -1;
	int row = 0;
	int column = 0;
	float x = 0.0f;
	float health = 0.0f;
	float maxHealth = 1.0f;
	float strategicValue = 0.0f;
	Bounds bounds;
	bool canBeEaten = true;
};

struct ZombieSnapshot {
	int id = 0;
	int eatingPlantId = -1;
	int row = 0;
	float x = 0.0f;
	float y = 0.0f;
	float moveSpeed = 0.0f;
	float bodyHealth = 0.0f;
	float bodyMaxHealth = 0.0f;
	float helmHealth = 0.0f;
	float helmMaxHealth = 0.0f;
	float shieldHealth = 0.0f;
	float shieldMaxHealth = 0.0f;
	float attackDamage = 0.0f;
	bool isEating = false;
	float slowRemaining = 0.0f;
	float frozenRemaining = 0.0f;
	float butterRemaining = 0.0f;
	float paralysisRemaining = 0.0f;
	float slowImmunityRemaining = 0.0f;
	float frozenImmunityRemaining = 0.0f;
	float butterImmunityRemaining = 0.0f;
	float paralysisImmunityRemaining = 0.0f;
	bool canBeChilled = true;
	bool canBeFrozen = true;
	bool canBeButtered = true;
	bool canBeParalyzed = true;
	bool canBeAffectedByNightRoofCharge = true;
	bool canProtectFromNightRoofCharge = false;
	bool nightRoofProtectionSuppressed = false;
	float nightRoofProtectionRadius = 0.0f;
	bool mindControlled = false;
	bool simulatedCombatant = true;
	bool forcedForDecision = false;
	Bounds bounds;
	bool magneticItemAvailable = false;
	bool magneticRemovesHelm = false;
	bool magneticRemovesShield = false;
};

struct CardSnapshot {
	int typeKey = 0;
	int cost = 0;
	float cooldownRemaining = 0.0f;
	float cooldownTime = 0.0f;
	float maxHealth = 300.0f;
	float strategicValue = 0.0f;
	float attackDps = 0.0f;
	int attackRowRadius = 0;
	float sunPerSecond = 0.0f;
	float firstSunDelay = 0.0f;
	std::uint64_t legalCellMask = 0;
	bool pumpkinShell = false;
	int eatingLayerPriority = 1;
	float slowApplicationsPerSecond = 0.0f;
	float slowDuration = 0.0f;
	float frozenApplicationsPerSecond = 0.0f;
	float frozenDuration = 0.0f;
	float butterApplicationsPerSecond = 0.0f;
	float butterDuration = 0.0f;
	float paralysisApplicationsPerSecond = 0.0f;
	float paralysisDuration = 0.0f;
	float magneticPulseCooldown = 0.0f;
	float magneticPulseRadius = 0.0f;
	float magneticPulseParalysisDuration = 0.0f;
	int magneticSearchRowRadius = 0;
	float magneticSearchRadius = 0.0f;
	float magneticEatingSearchRadius = 0.0f;
	float magneticRowDistancePenalty = 0.0f;
	float cobBlastCooldown = 0.0f;
	float cobBlastDamage = 0.0f;
	float cobBlastRadius = 0.0f;
	int cobBlastRowRadius = 0;
};

struct CellSnapshot {
	int row = 0;
	int column = 0;
	float x = 0.0f;
	float y = 0.0f;
	bool occupied = false;
};

struct Candidate {
	int row = 0;
	int column = 0;
	float x = 0.0f;
	float y = 0.0f;
	int targetPlantId = -1; // >=0 时候选动作只移除这株植物；-1 保持圆形范围伤害
	float targetStrikeInterval = 0.0f; // 大于 0 时改为立即停机、随后按间隔锤击
	float targetStrikeDamage = 0.0f; // 每次延迟锤击伤害
	int targetStrikeCount = 0; // 最后一击无论剩余生命都提交处决
	std::vector<int> blockedPlantIds{}; // Board 冻结的爆点视线遮挡；普通地形为空
};

struct PendingCobBlast {
	int sourcePlantId = -1; // 未离膛时绑定来源；-1 表示已离膛且不可回滚
	int targetRow = 0;
	float x = 0.0f;
	float y = 0.0f;
	float resolveSeconds = 0.0f;
	float damage = 0.0f;
	float radius = 0.0f;
	int rowRadius = 0;
};

struct Snapshot {
	int rows = 0;
	int columns = 0;
	float sceneWidth = 0.0f;
	float initialSun = 0.0f;
	std::vector<PlantSnapshot> plants;
	std::vector<SupportSnapshot> supports;
	std::vector<ZombieSnapshot> zombies;
	std::vector<CardSnapshot> cards;
	std::vector<CellSnapshot> cells;
	std::vector<Candidate> candidates;
	std::vector<PendingCobBlast> pendingCobBlasts;
};

struct Config {
	int rolloutCount = 32;                  // 每个候选使用的短视未来样本数
	int maxZombiesPerRollout = 16;          // 单次样本最多推进的当前敌方僵尸数
	float horizonSeconds = 16.0f;           // 单次样本向前推演的游戏秒
	float stepSeconds = 0.25f;              // 固定数值步长，越小越精细但开销越高
	float impactDamage = 50.0f;             // 候选动作在 t=0 对植物造成的伤害
	float impactRadius = 100.0f;             // 候选动作的圆形爆区半径，单位 px
	int pumpkinProtectionCellRadius = 1;       // 南瓜头保护的逻辑格半径；1 表示自身九宫格
	float pumpkinImpactDamageMultiplier = 5.0f; // 南瓜头拦截候选范围伤害时的输入倍率
	float biteInterval = 1.0f;               // 简化啃咬的等效间隔，单位游戏秒
	float contactDistance = 55.0f;           // 僵尸进入啃咬态的水平距离，单位 px
	float plantDecisionInterval = 2.0f;      // 玩家在样本中尝试种下一株植物的间隔秒数
	float houseX = 110.0f;                   // 僵尸越过此坐标后的防线失守判定
	float breachPenalty = 1500.0f;           // 每只越线僵尸从玩家效用中扣除的分数
	float terminalBlockedSecondUtility = 12.0f; // 终局每秒剩余破墙时间折算的玩家防守效用
	float terminalBlockedSecondsCap = 90.0f; // 单个阻挡植物计入终局分的最大剩余破墙秒数
};

struct Result {
	int candidateIndex = -1;
	float score = 0.0f;
	int rolloutCount = 0;
	int sampledZombieCount = 0;
	int sampledPlantCount = 0;
	int supportPlantCount = 0;
	int cardCount = 0;
	float coordinationLoss = 0.0f;
};

enum class TreatmentAction {
	AREA,
	FOCUSED,
};

struct TreatmentCandidate {
	TreatmentAction action = TreatmentAction::AREA;
	int targetZombieId = -1;
	float delaySeconds = 0.0f;
	float overflowPressure = 0.0f;
};

struct PendingTreatment {
	TreatmentAction action = TreatmentAction::AREA;
	int sourceZombieId = -1;
	int targetZombieId = -1;
	float resolveSeconds = 0.0f;
	float radius = 0.0f;
	float healAmount = 0.0f;
};

struct TreatmentConfig {
	Config combat;
	int sourceZombieId = -1;
	float castSeconds = 1.0f;
	float areaRadius = 140.0f;
	float focusedRadius = 280.0f;
	float areaHealAmount = 100.0f;
	float focusedHealAmount = 400.0f;
	float terminalZombiePressurePerHealth = 0.08f;
	int hijackerZombieId = -1;
	float hijackerExecutionSeconds = -1.0f;
	bool survivalMode = false;
	float survivalExecutionLineCap = 1200.0f;
};

struct TreatmentResult {
	int candidateIndex = -1;
	float score = 0.0f;
	int rolloutCount = 0;
	int sampledZombieCount = 0;
	int sampledPlantCount = 0;
	int supportPlantCount = 0;
	int cardCount = 0;
};

struct PendingControlEvent {
	int sourcePlantId = -1;
	float resolveSeconds = 0.0f;
	float damage = 0.0f;
	float slowDuration = 0.0f;
	float frozenDurationMin = 0.0f;
	float frozenDurationMax = 0.0f;
};

struct NightRoofChargeCandidate {
	int row = 0;
	int guideZombieId = -1;
	float resolveSeconds = 4.0f;
	float plantShutdownSeconds = 8.0f;
	float wetPlantShutdownSeconds = 20.0f;
	int wetSlopeColumnCount = 5;
	float zombieDamage = 200.0f;
	float wetZombieDamage = 600.0f;
	float paralysisSeconds = 1.5f;
	float wetParalysisSeconds = 5.5f;
	float wetSlopeEndX = 0.0f;
	bool wetRow = false;
	bool guided = false;
};

struct NightRoofChargeConfig {
	Config combat;
	std::vector<PendingControlEvent> pendingControlEvents;
	int hijackerZombieId = -1;
	float hijackerExecutionSeconds = -1.0f;
	bool survivalMode = false;
	float survivalExecutionLineCap = 1200.0f;
	float guideImmunitySeconds = 30.0f;
	float guideImmunityRadius = 130.0f;
};

struct NightRoofChargeResult {
	int candidateIndex = -1;
	float score = 0.0f;
	int rolloutCount = 0;
	int sampledZombieCount = 0;
	int sampledPlantCount = 0;
	int supportPlantCount = 0;
	int cardCount = 0;
};

/**
 * @brief 用当前实体和卡槽的轻量快照比较候选攻击，返回令玩家未来效用损失最大的落点。
 *
 * 每个候选与无攻击基线使用相同 rollout seed；函数只使用局部随机数，不推进游戏全局 RNG。
 */
Result ChooseTarget(const Snapshot& snapshot, const Config& config, std::uint32_t seed);

/**
 * @brief 比较立即或延迟开始的群疗/单疗动作，返回令玩家未来效用损失最大的候选。
 *
 * 延迟候选仍在同一组 rollout seed 上比较；函数只推进纯数值副本，不消费游戏 RNG。
 */
TreatmentResult ChooseTreatment(const Snapshot& snapshot,
	const std::vector<TreatmentCandidate>& candidates,
	const std::vector<PendingTreatment>& pendingTreatments,
	const TreatmentConfig& config, std::uint32_t seed);

/**
 * @brief 统一比较普通行放电与接地引导路线；植物停机、僵尸友伤和控制状态均延迟到正式放电边沿。
 */
NightRoofChargeResult ChooseNightRoofChargeRoute(const Snapshot& snapshot,
	const std::vector<NightRoofChargeCandidate>& candidates,
	const NightRoofChargeConfig& config, std::uint32_t seed);

} // namespace PlantDefenseMonteCarlo
