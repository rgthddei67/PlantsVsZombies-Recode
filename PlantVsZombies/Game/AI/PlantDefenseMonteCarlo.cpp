#include "PlantDefenseMonteCarlo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

namespace PlantDefenseMonteCarlo {
namespace {
	constexpr int kMaxSimulationPlants = 128; // 当前植物与短视未来新种植物的固定数组容量
	constexpr int kMaxSimulationSupports = 64; // 普通花盆/睡莲按每格一株保存，不占详细植物容量
	constexpr int kMaxSnapshotZombies = 64;  // 候选抽样前接受的当前敌方僵尸快照上限
	constexpr int kMaxSimulationZombies = 16; // 单个 rollout 的硬上限，Config 只能在此范围内下调
	constexpr int kMaxSimulationCards = 16;  // 当前卡槽的固定数组容量
	constexpr float kMinimumHealth = 0.001f; // 浮点生命判活阈值
	constexpr float kScoreTieEpsilon = 0.001f; // 候选平均损失分并列容差
	constexpr float kSunUtilityMultiplier = 1.0f; // 未消费阳光折算为玩家效用的倍率
	constexpr float kProducerFutureValueSeconds = 24.0f; // 新产能植物的额外未来价值折算窗口
	constexpr float kMoveSpeedJitter = 0.1f; // rollout 对当前移速施加的正负随机比例
	constexpr float kPlantChoiceJitter = 0.2f; // 玩家选牌/选格启发式的正负随机比例

	struct SimPlant {
		int id = -1;
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
		int eatingLayerPriority = 1;
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

	struct SimSupport {
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

	struct SimZombie {
		int id = -1;
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
		float biteCharge = 0.0f;
		bool breached = false;
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
		Bounds bounds;
		bool magneticItemAvailable = false;
		bool magneticRemovesHelm = false;
		bool magneticRemovesShield = false;
	};

	struct SimCard {
		const CardSnapshot* profile = nullptr;
		float cooldownRemaining = 0.0f;
	};

	struct SimulationState {
		std::array<SimPlant, kMaxSimulationPlants> plants;
		std::array<SimSupport, kMaxSimulationSupports> supports;
		std::array<SimZombie, kMaxSimulationZombies> zombies;
		std::array<SimCard, kMaxSimulationCards> cards;
		int plantCount = 0;
		int supportCount = 0;
		int zombieCount = 0;
		int cardCount = 0;
		float sun = 0.0f;
		float breachLoss = 0.0f;
		float directPlayerUtilityAdjustment = 0.0f;
		std::uint64_t reservedCells = 0;
		float sceneWidth = 0.0f;
		int magneticItemCount = 0;
	};

	struct ScenarioUtility {
		float total = 0.0f;
		float coordination = 0.0f;
	};

	float Random01(std::minstd_rand& random)
	{
		return static_cast<float>(random())
			/ static_cast<float>(std::minstd_rand::max());
	}

	float RandomSigned(std::minstd_rand& random)
	{
		return Random01(random) * 2.0f - 1.0f;
	}

	bool CircleOverlapsBounds(
		const Candidate& center, float radius, const Bounds& bounds)
	{
		const float nearestX = std::clamp(
			center.x, bounds.x, bounds.x + bounds.width);
		const float nearestY = std::clamp(
			center.y, bounds.y, bounds.y + bounds.height);
		const float dx = center.x - nearestX;
		const float dy = center.y - nearestY;
		return dx * dx + dy * dy <= radius * radius;
	}

	bool IsAlive(const SimPlant& plant)
	{
		return plant.health > kMinimumHealth;
	}

	bool IsAlive(const SimSupport& support)
	{
		return support.health > kMinimumHealth;
	}

	bool IsAlive(const SimZombie& zombie)
	{
		return zombie.bodyHealth > kMinimumHealth && !zombie.breached;
	}

	void ApplyZombieDamage(SimZombie& zombie, float damage)
	{
		float remaining = std::max(0.0f, damage);
		const float shieldDamage = std::min(zombie.shieldHealth, remaining);
		zombie.shieldHealth -= shieldDamage;
		remaining -= shieldDamage;
		const float helmDamage = std::min(zombie.helmHealth, remaining);
		zombie.helmHealth -= helmDamage;
		remaining -= helmDamage;
		zombie.bodyHealth = std::max(0.0f, zombie.bodyHealth - remaining);
	}

	float ZombieThreat(const ZombieSnapshot& zombie)
	{
		if (!zombie.simulatedCombatant) return 0.0f;
		const float totalHealth = zombie.bodyHealth
			+ zombie.helmHealth + zombie.shieldHealth;
		const float distanceFactor = 1.0f
			+ 400.0f / std::max(100.0f, zombie.x);
		return std::max(1.0f, totalHealth)
			* std::max(1.0f, zombie.attackDamage)
			* std::max(1.0f, zombie.moveSpeed) * distanceFactor;
	}

	// 用带轻微扰动的威胁排序截取当前僵尸，并构建不持有 GameObject 的固定容量状态。
	void BuildInitialState(const Snapshot& snapshot, const Config& config,
		std::uint32_t seed, SimulationState& state)
	{
		std::minstd_rand random(seed);
		state.sun = std::max(0.0f, snapshot.initialSun);
		state.sceneWidth = std::max(0.0f, snapshot.sceneWidth);

		state.plantCount = std::min(
			static_cast<int>(snapshot.plants.size()), kMaxSimulationPlants);
		for (int i = 0; i < state.plantCount; ++i) {
			const PlantSnapshot& source = snapshot.plants[i];
			state.plants[i] = {
				source.id,
				source.row,
				source.column,
				source.x,
				std::max(0.0f, source.health),
				std::max(1.0f, source.maxHealth),
				std::max(0.0f, source.strategicValue),
				std::max(0.0f, source.attackDps),
				std::max(0, source.attackRowRadius),
				std::max(0.0f, source.sunPerSecond),
				std::max(0.0f, source.productionDelay),
				source.bounds,
				source.pumpkinShell,
				source.hijackerExecutionGroup,
				source.countsForHijackerExecution,
				source.diesWithHijackerExecutionGroup,
				source.protectedFromHijackerExecution,
				source.eatingLayerPriority,
				source.canBeEaten,
				std::max(0.0f, source.shutdownRemaining),
				source.protectedFromNightRoofCharge,
				std::max(0.0f, source.slowApplicationsPerSecond),
				std::max(0.0f, source.slowDuration),
				std::max(0.0f, source.frozenApplicationsPerSecond),
				std::max(0.0f, source.frozenDuration),
				std::max(0.0f, source.butterApplicationsPerSecond),
				std::max(0.0f, source.butterDuration),
				std::max(0.0f, source.paralysisApplicationsPerSecond),
				std::max(0.0f, source.paralysisDuration)
			};
			SimPlant& target = state.plants[i];
			target.y = source.y;
			target.abilityCooldownRemaining = std::max(
				0.0f, source.abilityCooldownRemaining);
			target.magneticPulseCooldown = std::max(
				0.0f, source.magneticPulseCooldown);
			target.magneticPulseRadius = std::max(
				0.0f, source.magneticPulseRadius);
			target.magneticPulseParalysisDuration = std::max(
				0.0f, source.magneticPulseParalysisDuration);
			target.magneticSearchRowRadius = std::max(
				0, source.magneticSearchRowRadius);
			target.magneticSearchRadius = std::max(
				0.0f, source.magneticSearchRadius);
			target.magneticEatingSearchRadius = std::max(
				0.0f, source.magneticEatingSearchRadius);
			target.magneticRowDistancePenalty = std::max(
				0.0f, source.magneticRowDistancePenalty);
			target.cobBlastCooldown = std::max(
				0.0f, source.cobBlastCooldown);
			target.cobBlastDamage = std::max(
				0.0f, source.cobBlastDamage);
			target.cobBlastRadius = std::max(
				0.0f, source.cobBlastRadius);
			target.cobBlastRowRadius = std::max(
				0, source.cobBlastRowRadius);
		}

		state.supportCount = std::min(
			static_cast<int>(snapshot.supports.size()), kMaxSimulationSupports);
		for (int i = 0; i < state.supportCount; ++i) {
			const SupportSnapshot& source = snapshot.supports[i];
			state.supports[i] = {
				source.id,
				source.row,
				source.column,
				source.x,
				std::max(0.0f, source.health),
				std::max(1.0f, source.maxHealth),
				std::max(0.0f, source.strategicValue),
				source.bounds,
				source.canBeEaten
			};
		}

		state.cardCount = std::min(
			static_cast<int>(snapshot.cards.size()), kMaxSimulationCards);
		for (int i = 0; i < state.cardCount; ++i) {
			state.cards[i].profile = &snapshot.cards[i];
			state.cards[i].cooldownRemaining =
				std::max(0.0f, snapshot.cards[i].cooldownRemaining);
		}

		struct RankedZombie {
			int index = 0;
			float priority = 0.0f;
			bool forced = false;
		};
		std::array<RankedZombie, kMaxSnapshotZombies> ranked{};
		const int snapshotZombieCount = std::min(
			static_cast<int>(snapshot.zombies.size()), kMaxSnapshotZombies);
		for (int i = 0; i < snapshotZombieCount; ++i) {
			ranked[i] = {
				i,
				ZombieThreat(snapshot.zombies[i])
					* (0.75f + Random01(random) * 0.5f),
				snapshot.zombies[i].forcedForDecision
			};
		}
		std::sort(ranked.begin(), ranked.begin() + snapshotZombieCount,
			[](const RankedZombie& lhs, const RankedZombie& rhs) {
				if (lhs.forced != rhs.forced) return lhs.forced;
				return lhs.priority > rhs.priority;
			});

		state.zombieCount = std::min({
			snapshotZombieCount,
			std::max(0, config.maxZombiesPerRollout),
			kMaxSimulationZombies
		});
		const float biteInterval = std::max(0.1f, config.biteInterval);
		for (int i = 0; i < state.zombieCount; ++i) {
			const ZombieSnapshot& source =
				snapshot.zombies[ranked[i].index];
			const float speedJitter =
				1.0f + RandomSigned(random) * kMoveSpeedJitter;
			state.zombies[i] = {
				source.id,
				source.eatingPlantId,
				source.row,
				source.x,
				source.y,
				std::max(0.0f, source.moveSpeed * speedJitter),
				std::max(0.0f, source.bodyHealth),
				std::max(0.0f, source.bodyMaxHealth),
				std::max(0.0f, source.helmHealth),
				std::max(0.0f, source.helmMaxHealth),
				std::max(0.0f, source.shieldHealth),
				std::max(0.0f, source.shieldMaxHealth),
				std::max(0.0f, source.attackDamage),
				source.isEating ? biteInterval : Random01(random) * biteInterval,
				false,
				std::max(0.0f, source.slowRemaining),
				std::max(0.0f, source.frozenRemaining),
				std::max(0.0f, source.butterRemaining),
				std::max(0.0f, source.paralysisRemaining),
				std::max(0.0f, source.slowImmunityRemaining),
				std::max(0.0f, source.frozenImmunityRemaining),
				std::max(0.0f, source.butterImmunityRemaining),
				std::max(0.0f, source.paralysisImmunityRemaining),
				source.canBeChilled,
				source.canBeFrozen,
				source.canBeButtered,
				source.canBeParalyzed,
				source.canBeAffectedByNightRoofCharge,
				source.canProtectFromNightRoofCharge,
				source.nightRoofProtectionSuppressed,
				std::max(0.0f, source.nightRoofProtectionRadius),
				source.mindControlled,
				source.simulatedCombatant
			};
			SimZombie& target = state.zombies[i];
			target.bounds = source.bounds;
			target.magneticItemAvailable = source.magneticItemAvailable;
			target.magneticRemovesHelm = source.magneticRemovesHelm;
			target.magneticRemovesShield = source.magneticRemovesShield;
			if (target.magneticItemAvailable && target.simulatedCombatant
				&& !target.mindControlled) {
				++state.magneticItemCount;
			}
		}
	}

	// 为命中层稳定选择九宫格内最近南瓜；自身是南瓜时直接由自身承伤。
	int FindPumpkinProtector(const SimulationState& state, int row, int column,
		int selfPlantIndex, const Config& config)
	{
		if (selfPlantIndex >= 0 && selfPlantIndex < state.plantCount
			&& state.plants[selfPlantIndex].pumpkinShell) {
			return selfPlantIndex;
		}

		int pumpkinIndex = -1;
		int bestDistanceSquared = std::numeric_limits<int>::max();
		for (int candidateIndex = 0;
			candidateIndex < state.plantCount; ++candidateIndex) {
			const SimPlant& candidatePlant = state.plants[candidateIndex];
			if (!IsAlive(candidatePlant) || !candidatePlant.pumpkinShell) continue;
			const int rowDelta = candidatePlant.row - row;
			const int columnDelta = candidatePlant.column - column;
			if (std::abs(rowDelta) > config.pumpkinProtectionCellRadius
				|| std::abs(columnDelta) > config.pumpkinProtectionCellRadius) {
				continue;
			}

			const int distanceSquared = rowDelta * rowDelta
				+ columnDelta * columnDelta;
			const SimPlant* best = pumpkinIndex >= 0
				? &state.plants[pumpkinIndex] : nullptr;
			const bool stableTieBreak = best
				&& distanceSquared == bestDistanceSquared
				&& (candidatePlant.row < best->row
					|| (candidatePlant.row == best->row
						&& (candidatePlant.column < best->column
							|| (candidatePlant.column == best->column
								&& candidatePlant.id < best->id))));
			if (!best || distanceSquared < bestDistanceSquared || stableTieBreak) {
				pumpkinIndex = candidateIndex;
				bestDistanceSquared = distanceSquared;
			}
		}
		return pumpkinIndex;
	}

	// 与正式范围伤害一致：详细层和压缩支撑层都先解析保护南瓜，外壳按 ID 归并承伤。
	void ApplyCandidateImpact(
		SimulationState& state, const Candidate& candidate, const Config& config)
	{
		if (candidate.targetPlantId >= 0) {
			for (int i = 0; i < state.plantCount; ++i) {
				SimPlant& plant = state.plants[i];
				if (IsAlive(plant) && plant.id == candidate.targetPlantId) {
					if (candidate.targetStrikeCount > 0
						&& candidate.targetStrikeInterval > 0.0f) {
						// 冰封在选择瞬间就停止目标工作；生命与阻挡保留到后续锤击提交。
						plant.shutdownRemaining = std::numeric_limits<float>::max();
					}
					else {
						plant.health = 0.0f;
					}
					return;
				}
			}
			for (int i = 0; i < state.supportCount; ++i) {
				SimSupport& support = state.supports[i];
				if (IsAlive(support) && support.id == candidate.targetPlantId) {
					support.health = 0.0f;
					return;
				}
			}
			return;
		}

		std::array<bool, kMaxSimulationPlants> normalHits{};
		std::array<bool, kMaxSimulationPlants> pumpkinHits{};
		std::array<bool, kMaxSimulationSupports> supportHits{};
		for (int i = 0; i < state.plantCount; ++i) {
			const SimPlant& plant = state.plants[i];
			if (!IsAlive(plant)
				|| std::find(candidate.blockedPlantIds.begin(), candidate.blockedPlantIds.end(), plant.id) != candidate.blockedPlantIds.end()
				|| !CircleOverlapsBounds(candidate, config.impactRadius, plant.bounds)) {
				continue;
			}

			const int pumpkinIndex = FindPumpkinProtector(
				state, plant.row, plant.column, i, config);
			if (pumpkinIndex >= 0) pumpkinHits[pumpkinIndex] = true;
			else normalHits[i] = true;
		}
		for (int i = 0; i < state.supportCount; ++i) {
			const SimSupport& support = state.supports[i];
			if (!IsAlive(support)
				|| std::find(candidate.blockedPlantIds.begin(), candidate.blockedPlantIds.end(), support.id) != candidate.blockedPlantIds.end()
				|| !CircleOverlapsBounds(candidate, config.impactRadius, support.bounds)) {
				continue;
			}
			const int pumpkinIndex = FindPumpkinProtector(
				state, support.row, support.column, -1, config);
			if (pumpkinIndex >= 0) pumpkinHits[pumpkinIndex] = true;
			else supportHits[i] = true;
		}

		for (int i = 0; i < state.plantCount; ++i) {
			SimPlant& plant = state.plants[i];
			const float damage = pumpkinHits[i]
				? config.impactDamage * config.pumpkinImpactDamageMultiplier
				: (normalHits[i] ? config.impactDamage : 0.0f);
			if (damage > 0.0f) {
				plant.health = std::max(0.0f, plant.health - damage);
			}
		}
		for (int i = 0; i < state.supportCount; ++i) {
			if (supportHits[i]) {
				SimSupport& support = state.supports[i];
				support.health = std::max(
					0.0f, support.health - config.impactDamage);
			}
		}
	}

	void UpdatePlantProduction(SimulationState& state, float deltaTime)
	{
		for (int i = 0; i < state.plantCount; ++i) {
			SimPlant& plant = state.plants[i];
			if (!IsAlive(plant) || plant.shutdownRemaining > 0.0f
				|| plant.sunPerSecond <= 0.0f) continue;
			if (plant.productionDelay > 0.0f) {
				plant.productionDelay =
					std::max(0.0f, plant.productionDelay - deltaTime);
				continue;
			}
			state.sun += plant.sunPerSecond * deltaTime;
		}
	}

	void UpdatePlantShutdowns(SimulationState& state, float deltaTime)
	{
		for (int i = 0; i < state.plantCount; ++i) {
			state.plants[i].shutdownRemaining = std::max(
				0.0f, state.plants[i].shutdownRemaining - deltaTime);
		}
	}

	void UpdateCardCooldowns(SimulationState& state, float deltaTime)
	{
		for (int i = 0; i < state.cardCount; ++i) {
			state.cards[i].cooldownRemaining = std::max(
				0.0f, state.cards[i].cooldownRemaining - deltaTime);
		}
	}

	float RowThreat(const SimulationState& state, int row)
	{
		float threat = 0.0f;
		for (int i = 0; i < state.zombieCount; ++i) {
			const SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie) || !zombie.simulatedCombatant
				|| zombie.row != row) continue;
			const float health = zombie.bodyHealth
				+ zombie.helmHealth + zombie.shieldHealth;
			threat += std::max(1.0f, health)
				* (1.0f + std::max(0.0f, 900.0f - zombie.x) / 900.0f);
		}
		return threat;
	}

	// 模拟高操作玩家从实际卡槽中选取可负担且已冷却的高收益牌，并启发式放到合法格。
	void TryPlantFromCards(const Snapshot& snapshot, SimulationState& state,
		float remainingTime, std::minstd_rand& random)
	{
		int bestCard = -1;
		float bestCardScore = std::numeric_limits<float>::lowest();
		for (int i = 0; i < state.cardCount; ++i) {
			const SimCard& card = state.cards[i];
			if (!card.profile || card.cooldownRemaining > 0.0f
				|| static_cast<float>(card.profile->cost) > state.sun) {
				continue;
			}
			const std::uint64_t available =
				card.profile->legalCellMask & ~state.reservedCells;
			if (available == 0) continue;

			const float futureIncome = card.profile->sunPerSecond
				* std::max(0.0f, remainingTime - card.profile->firstSunDelay);
			const float costDivisor =
				static_cast<float>(std::max(25, card.profile->cost));
			const float baseScore =
				(card.profile->strategicValue + futureIncome) / costDivisor;
			const float score = baseScore
				* (1.0f + RandomSigned(random) * kPlantChoiceJitter);
			if (score > bestCardScore) {
				bestCardScore = score;
				bestCard = i;
			}
		}
		if (bestCard < 0 || state.plantCount >= kMaxSimulationPlants) return;

		const CardSnapshot& card = *state.cards[bestCard].profile;
		const std::uint64_t available = card.legalCellMask & ~state.reservedCells;
		int bestCell = -1;
		float bestCellScore = std::numeric_limits<float>::lowest();
		for (std::size_t cellIndex = 0;
			cellIndex < snapshot.cells.size() && cellIndex < 64; ++cellIndex) {
			if ((available & (1ULL << cellIndex)) == 0) continue;
			const CellSnapshot& cell = snapshot.cells[cellIndex];
			const float threat = RowThreat(state, cell.row);
			float placementBias = threat * 0.002f;
			if (card.sunPerSecond > 0.0f) {
				placementBias += static_cast<float>(
					std::max(0, snapshot.columns - cell.column)) * 3.0f;
			}
			else if (card.attackDps <= 0.0f) {
				placementBias += static_cast<float>(cell.column) * 2.5f;
			}
			else {
				placementBias += static_cast<float>(cell.column) * 1.25f;
			}
			placementBias *=
				1.0f + RandomSigned(random) * kPlantChoiceJitter;
			if (placementBias > bestCellScore) {
				bestCellScore = placementBias;
				bestCell = static_cast<int>(cellIndex);
			}
		}
		if (bestCell < 0) return;

		const CellSnapshot& cell = snapshot.cells[bestCell];
		SimPlant& plant = state.plants[state.plantCount++];
		plant.id = -1;
		plant.row = cell.row;
		plant.column = cell.column;
		plant.x = cell.x;
		plant.health = std::max(1.0f, card.maxHealth);
		plant.maxHealth = plant.health;
		plant.strategicValue = std::max(
			0.0f, card.strategicValue
				+ card.sunPerSecond * kProducerFutureValueSeconds);
		plant.attackDps = std::max(0.0f, card.attackDps);
		plant.attackRowRadius = std::max(0, card.attackRowRadius);
		plant.sunPerSecond = std::max(0.0f, card.sunPerSecond);
		plant.productionDelay = std::max(0.0f, card.firstSunDelay);
		plant.bounds = {
			cell.x - 40.0f, cell.y - 50.0f, 80.0f, 100.0f
		};
		plant.pumpkinShell = card.pumpkinShell;
		// rollout 内新种层缺少正式 Cell 层级关系，不参与劫持者处决组近似，避免误杀承载层。
		plant.hijackerExecutionGroup = -1;
		plant.countsForHijackerExecution = false;
		plant.diesWithHijackerExecutionGroup = false;
		plant.protectedFromHijackerExecution = false;
		plant.eatingLayerPriority = card.eatingLayerPriority;
		plant.canBeEaten = true;
		plant.shutdownRemaining = 0.0f;
		plant.protectedFromNightRoofCharge = false;
		plant.slowApplicationsPerSecond = card.slowApplicationsPerSecond;
		plant.slowDuration = card.slowDuration;
		plant.frozenApplicationsPerSecond = card.frozenApplicationsPerSecond;
		plant.frozenDuration = card.frozenDuration;
		plant.butterApplicationsPerSecond = card.butterApplicationsPerSecond;
		plant.butterDuration = card.butterDuration;
		plant.paralysisApplicationsPerSecond = card.paralysisApplicationsPerSecond;
		plant.paralysisDuration = card.paralysisDuration;
		plant.y = cell.y;
		plant.abilityCooldownRemaining = 0.0f;
		plant.magneticPulseCooldown = card.magneticPulseCooldown;
		plant.magneticPulseRadius = card.magneticPulseRadius;
		plant.magneticPulseParalysisDuration =
			card.magneticPulseParalysisDuration;
		plant.magneticSearchRowRadius = card.magneticSearchRowRadius;
		plant.magneticSearchRadius = card.magneticSearchRadius;
		plant.magneticEatingSearchRadius = card.magneticEatingSearchRadius;
		plant.magneticRowDistancePenalty = card.magneticRowDistancePenalty;
		plant.cobBlastCooldown = card.cobBlastCooldown;
		plant.cobBlastDamage = card.cobBlastDamage;
		plant.cobBlastRadius = card.cobBlastRadius;
		plant.cobBlastRowRadius = card.cobBlastRowRadius;
		// 初始合法性由正式 CanPlantAt 快照决定；这里只阻止同一 rollout 再占用新种格。
		state.reservedCells |= (1ULL << bestCell);
		state.sun -= static_cast<float>(card.cost);
		state.cards[bestCard].cooldownRemaining =
			std::max(0.0f, card.cooldownTime);
	}

	void ApplyPlantControl(
		const SimPlant& plant, SimZombie& zombie, float deltaTime,
		std::minstd_rand& random)
	{
		auto triggered = [&](float applicationsPerSecond) {
			if (applicationsPerSecond <= 0.0f) return false;
			const float probability = 1.0f
				- std::exp(-applicationsPerSecond * deltaTime);
			return Random01(random) < probability;
		};
		if (zombie.canBeChilled && zombie.slowImmunityRemaining <= 0.0f
			&& triggered(plant.slowApplicationsPerSecond)) {
			zombie.slowRemaining = std::max(
				zombie.slowRemaining, plant.slowDuration);
		}
		if (zombie.canBeChilled && zombie.canBeFrozen
			&& zombie.frozenImmunityRemaining <= 0.0f
			&& triggered(plant.frozenApplicationsPerSecond)) {
			zombie.frozenRemaining = std::max(
				zombie.frozenRemaining, plant.frozenDuration);
		}
		if (zombie.canBeButtered && zombie.butterImmunityRemaining <= 0.0f
			&& triggered(plant.butterApplicationsPerSecond)) {
			zombie.butterRemaining = std::max(
				zombie.butterRemaining, plant.butterDuration);
		}
		if (zombie.canBeParalyzed && zombie.paralysisImmunityRemaining <= 0.0f
			&& triggered(plant.paralysisApplicationsPerSecond)) {
			zombie.paralysisRemaining = std::max(
				zombie.paralysisRemaining, plant.paralysisDuration);
		}
	}

	// 将配置画像中的等效 DPS 与控制频率分派给每个覆盖行最靠近房屋的存活僵尸。
	void UpdatePlantAttacks(SimulationState& state, float deltaTime, int rows,
		std::minstd_rand& random)
	{
		for (int plantIndex = 0; plantIndex < state.plantCount; ++plantIndex) {
			const SimPlant& plant = state.plants[plantIndex];
			if (!IsAlive(plant) || plant.shutdownRemaining > 0.0f
				|| (plant.attackDps <= 0.0f
					&& plant.slowApplicationsPerSecond <= 0.0f
					&& plant.frozenApplicationsPerSecond <= 0.0f
					&& plant.butterApplicationsPerSecond <= 0.0f
					&& plant.paralysisApplicationsPerSecond <= 0.0f)) continue;
			const int minRow = std::max(0, plant.row - plant.attackRowRadius);
			const int maxRow = std::min(rows - 1, plant.row + plant.attackRowRadius);
			for (int row = minRow; row <= maxRow; ++row) {
				int targetIndex = -1;
				float closestX = std::numeric_limits<float>::max();
				for (int zombieIndex = 0;
					zombieIndex < state.zombieCount; ++zombieIndex) {
					const SimZombie& zombie = state.zombies[zombieIndex];
					if (!IsAlive(zombie) || !zombie.simulatedCombatant || zombie.row != row
						|| zombie.x + 10.0f < plant.x || zombie.x >= closestX) {
						continue;
					}
					closestX = zombie.x;
					targetIndex = zombieIndex;
				}
				if (targetIndex >= 0) {
					ApplyZombieDamage(
						state.zombies[targetIndex], plant.attackDps * deltaTime);
					if (IsAlive(state.zombies[targetIndex])) {
						ApplyPlantControl(plant, state.zombies[targetIndex],
							deltaTime, random);
					}
				}
			}
		}
	}

	/**
	 * 玉米炮采用高操作玩家默认策略：枚举活僵尸中心，先最大化命中数，再最大化有效伤害。
	 */
	void UpdateCobBlasts(SimulationState& state, float deltaTime)
	{
		for (int plantIndex = 0; plantIndex < state.plantCount; ++plantIndex) {
			SimPlant& plant = state.plants[plantIndex];
			if (!IsAlive(plant) || plant.shutdownRemaining > 0.0f
				|| plant.cobBlastCooldown <= 0.0f
				|| plant.cobBlastDamage <= 0.0f
				|| plant.cobBlastRadius <= 0.0f) {
				continue;
			}
			plant.abilityCooldownRemaining = std::max(
				0.0f, plant.abilityCooldownRemaining - deltaTime);
			if (plant.abilityCooldownRemaining > 0.0f) continue;

			int bestCenterIndex = -1;
			int bestHitCount = 0;
			float bestEffectiveDamage = -1.0f;
			for (int centerIndex = 0;
				centerIndex < state.zombieCount; ++centerIndex) {
				const SimZombie& center = state.zombies[centerIndex];
				if (!IsAlive(center) || !center.simulatedCombatant
					|| center.mindControlled) continue;
				int hitCount = 0;
				float effectiveDamage = 0.0f;
				for (int targetIndex = 0;
					targetIndex < state.zombieCount; ++targetIndex) {
					const SimZombie& target = state.zombies[targetIndex];
					if (!IsAlive(target) || !target.simulatedCombatant
						|| target.mindControlled
						|| std::abs(target.row - center.row)
							> plant.cobBlastRowRadius) continue;
					const Bounds currentBounds{
						target.x - target.bounds.width * 0.5f,
						target.y - target.bounds.height * 0.5f,
						target.bounds.width,
						target.bounds.height
					};
					const Candidate blastCenter{
						center.row, 0, center.x, center.y, -1
					};
					if (!CircleOverlapsBounds(
						blastCenter, plant.cobBlastRadius, currentBounds)) continue;
					++hitCount;
					effectiveDamage += std::min(plant.cobBlastDamage,
						target.bodyHealth + target.helmHealth + target.shieldHealth);
				}
				const bool stableTie = bestCenterIndex >= 0
					&& hitCount == bestHitCount
					&& std::abs(effectiveDamage - bestEffectiveDamage)
						<= kScoreTieEpsilon
					&& center.id < state.zombies[bestCenterIndex].id;
				if (hitCount > bestHitCount
					|| (hitCount == bestHitCount
						&& effectiveDamage > bestEffectiveDamage + kScoreTieEpsilon)
					|| stableTie) {
					bestCenterIndex = centerIndex;
					bestHitCount = hitCount;
					bestEffectiveDamage = effectiveDamage;
				}
			}
			if (bestCenterIndex < 0 || bestHitCount <= 0) continue;

			const SimZombie& center = state.zombies[bestCenterIndex];
			const Candidate blastCenter{ center.row, 0, center.x, center.y, -1 };
			for (int targetIndex = 0;
				targetIndex < state.zombieCount; ++targetIndex) {
				SimZombie& target = state.zombies[targetIndex];
				if (!IsAlive(target) || !target.simulatedCombatant
					|| target.mindControlled
					|| std::abs(target.row - center.row)
						> plant.cobBlastRowRadius) continue;
				const Bounds currentBounds{
					target.x - target.bounds.width * 0.5f,
					target.y - target.bounds.height * 0.5f,
					target.bounds.width,
					target.bounds.height
				};
				if (CircleOverlapsBounds(
					blastCenter, plant.cobBlastRadius, currentBounds)) {
					ApplyZombieDamage(target, plant.cobBlastDamage);
				}
			}
			plant.abilityCooldownRemaining = plant.cobBlastCooldown;
		}
	}

	/** 已离膛玉米棒保持玩家锁定落点；它不再依赖来源植物是否仍存活。 */
	void ApplyPendingCobBlast(
		SimulationState& state, const PendingCobBlast& blast)
	{
		if (blast.sourcePlantId >= 0) {
			bool sourceCanCommit = false;
			for (int plantIndex = 0; plantIndex < state.plantCount; ++plantIndex) {
				const SimPlant& source = state.plants[plantIndex];
				if (source.id != blast.sourcePlantId) continue;
				sourceCanCommit = IsAlive(source) && source.shutdownRemaining <= 0.0f;
				break;
			}
			if (!sourceCanCommit) return;
		}
		const Candidate center{ blast.targetRow, 0, blast.x, blast.y, -1 };
		for (int zombieIndex = 0;
			zombieIndex < state.zombieCount; ++zombieIndex) {
			SimZombie& target = state.zombies[zombieIndex];
			if (!IsAlive(target) || !target.simulatedCombatant
				|| target.mindControlled
				|| std::abs(target.row - blast.targetRow) > blast.rowRadius) continue;
			const Bounds currentBounds{
				target.x - target.bounds.width * 0.5f,
				target.y - target.bounds.height * 0.5f,
				target.bounds.width,
				target.bounds.height
			};
			if (CircleOverlapsBounds(center, blast.radius, currentBounds)) {
				ApplyZombieDamage(target, blast.damage);
			}
		}
	}

	void ResolvePendingCobBlasts(const Snapshot& snapshot,
		SimulationState& state, float nextElapsed,
		std::vector<unsigned char>& resolved)
	{
		for (std::size_t i = 0; i < snapshot.pendingCobBlasts.size(); ++i) {
			if (resolved[i]
				|| snapshot.pendingCobBlasts[i].resolveSeconds > nextElapsed) continue;
			ApplyPendingCobBlast(state, snapshot.pendingCobBlasts[i]);
			resolved[i] = 1;
		}
	}

	/**
	 * 只在存在实时磁性目标时推进一次条件脉冲；装备资格与对应护甲层在副本中原子消费。
	 */
	void UpdateMagneticPulses(SimulationState& state, float deltaTime)
	{
		for (int plantIndex = 0; plantIndex < state.plantCount; ++plantIndex) {
			SimPlant& plant = state.plants[plantIndex];
			if (!IsAlive(plant) || plant.shutdownRemaining > 0.0f
				|| plant.magneticPulseCooldown <= 0.0f
				|| plant.magneticPulseRadius <= 0.0f
				|| plant.magneticPulseParalysisDuration <= 0.0f
				|| plant.magneticSearchRadius <= 0.0f) {
				continue;
			}
			plant.abilityCooldownRemaining = std::max(
				0.0f, plant.abilityCooldownRemaining - deltaTime);
			if (plant.abilityCooldownRemaining > 0.0f) continue;
			if (state.magneticItemCount <= 0) continue;

			int targetIndex = -1;
			float bestScore = std::numeric_limits<float>::max();
			for (int zombieIndex = 0;
				zombieIndex < state.zombieCount; ++zombieIndex) {
				const SimZombie& zombie = state.zombies[zombieIndex];
				if (!IsAlive(zombie) || !zombie.simulatedCombatant
					|| zombie.mindControlled || !zombie.magneticItemAvailable
					|| std::abs(zombie.row - plant.row)
						> plant.magneticSearchRowRadius) {
					continue;
				}
				const float radius = zombie.eatingPlantId >= 0
					? plant.magneticEatingSearchRadius
					: plant.magneticSearchRadius;
				const Bounds currentBounds{
					zombie.x - zombie.bounds.width * 0.5f,
					zombie.y - zombie.bounds.height * 0.5f,
					zombie.bounds.width,
					zombie.bounds.height
				};
				if (state.sceneWidth > 0.0f
					&& currentBounds.x > state.sceneWidth) continue;
				const Candidate center{ plant.row, plant.column, plant.x, plant.y, -1 };
				if (!CircleOverlapsBounds(center, radius, currentBounds)) continue;
				const float dx = zombie.x - plant.x;
				const float dy = zombie.y - plant.y;
				const float score = std::sqrt(dx * dx + dy * dy)
					+ static_cast<float>(std::abs(zombie.row - plant.row))
						* plant.magneticRowDistancePenalty;
				if (score < bestScore
					|| (std::abs(score - bestScore) <= kScoreTieEpsilon
						&& targetIndex >= 0
						&& zombie.id < state.zombies[targetIndex].id)) {
					bestScore = score;
					targetIndex = zombieIndex;
				}
			}
			if (targetIndex < 0) continue;

			SimZombie& source = state.zombies[targetIndex];
			source.magneticItemAvailable = false;
			state.magneticItemCount = std::max(0, state.magneticItemCount - 1);
			if (source.magneticRemovesHelm) source.helmHealth = 0.0f;
			else if (source.magneticRemovesShield) source.shieldHealth = 0.0f;
			plant.abilityCooldownRemaining = plant.magneticPulseCooldown;

			for (int zombieIndex = 0;
				zombieIndex < state.zombieCount; ++zombieIndex) {
				SimZombie& target = state.zombies[zombieIndex];
				if (!IsAlive(target) || !target.simulatedCombatant
					|| target.mindControlled || !target.canBeParalyzed
					|| target.paralysisImmunityRemaining > 0.0f) continue;
				const Bounds currentBounds{
					target.x - target.bounds.width * 0.5f,
					target.y - target.bounds.height * 0.5f,
					target.bounds.width,
					target.bounds.height
				};
				const Candidate pulseCenter{
					source.row, 0, source.x, source.y, -1
				};
				if (!CircleOverlapsBounds(
					pulseCenter, plant.magneticPulseRadius, currentBounds)) continue;
				target.paralysisRemaining = std::max(target.paralysisRemaining,
					plant.magneticPulseParalysisDuration);
			}
		}
	}

	struct BlockerRef {
		int index = -1;
		bool support = false;

		bool IsValid() const { return index >= 0; }
	};

	int BlockerID(const SimulationState& state, const BlockerRef& blocker)
	{
		return blocker.support
			? state.supports[blocker.index].id
			: state.plants[blocker.index].id;
	}

	float BlockerX(const SimulationState& state, const BlockerRef& blocker)
	{
		return blocker.support
			? state.supports[blocker.index].x
			: state.plants[blocker.index].x;
	}

	float& BlockerHealth(SimulationState& state, const BlockerRef& blocker)
	{
		return blocker.support
			? state.supports[blocker.index].health
			: state.plants[blocker.index].health;
	}

	// 保留正式已锁定目标；重新索敌时严格按 pumpkin > normal > under 解析同格层级。
	BlockerRef FindFrontBlocker(
		const SimulationState& state, const SimZombie& zombie)
	{
		if (zombie.eatingPlantId >= 0) {
			for (int i = 0; i < state.plantCount; ++i) {
				const SimPlant& plant = state.plants[i];
				if (IsAlive(plant) && plant.id == zombie.eatingPlantId) {
					return { i, false };
				}
			}
			for (int i = 0; i < state.supportCount; ++i) {
				const SimSupport& support = state.supports[i];
				if (IsAlive(support) && support.id == zombie.eatingPlantId) {
					return { i, true };
				}
			}
		}

		BlockerRef target;
		float frontX = std::numeric_limits<float>::lowest();
		int layerPriority = std::numeric_limits<int>::lowest();
		auto consider = [&](float x, int priority, int index, bool support) {
			if (x > zombie.x + 10.0f || x < frontX) return;
			const bool samePosition = std::abs(x - frontX) <= kMinimumHealth;
			if (samePosition && target.IsValid() && priority <= layerPriority) return;
			frontX = x;
			layerPriority = priority;
			target = { index, support };
		};
		for (int i = 0; i < state.plantCount; ++i) {
			const SimPlant& plant = state.plants[i];
			if (!IsAlive(plant) || !plant.canBeEaten
				|| plant.eatingLayerPriority < 0 || plant.row != zombie.row) continue;
			consider(plant.x, plant.eatingLayerPriority, i, false);
		}
		for (int i = 0; i < state.supportCount; ++i) {
			const SimSupport& support = state.supports[i];
			if (!IsAlive(support) || !support.canBeEaten
				|| support.row != zombie.row) continue;
			consider(support.x, 0, i, true);
		}
		return target;
	}

	bool IsCastingTreatment(const SimZombie& zombie, float elapsed,
		const TreatmentCandidate* candidate,
		const std::vector<PendingTreatment>* pendingTreatments,
		const TreatmentConfig* treatmentConfig)
	{
		if (candidate && treatmentConfig
			&& zombie.id == treatmentConfig->sourceZombieId
			&& elapsed + kMinimumHealth >= candidate->delaySeconds
			&& elapsed < candidate->delaySeconds + treatmentConfig->castSeconds) {
			return true;
		}
		if (!pendingTreatments) return false;
		for (const PendingTreatment& pending : *pendingTreatments) {
			if (zombie.id == pending.sourceZombieId
				&& elapsed < pending.resolveSeconds) {
				return true;
			}
		}
		return false;
	}

	// 以统一速度、接触距离和等效啃咬间隔推进僵尸；治疗施法者在前摇内停止移动和啃食。
	void UpdateZombies(SimulationState& state, const Config& config, float deltaTime,
		float elapsed = 0.0f, const TreatmentCandidate* candidate = nullptr,
		const std::vector<PendingTreatment>* pendingTreatments = nullptr,
		const TreatmentConfig* treatmentConfig = nullptr)
	{
		const float biteInterval = std::max(0.1f, config.biteInterval);
		for (int i = 0; i < state.zombieCount; ++i) {
			SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie) || !zombie.simulatedCombatant) continue;
			zombie.slowRemaining = std::max(0.0f, zombie.slowRemaining - deltaTime);
			zombie.frozenRemaining = std::max(0.0f, zombie.frozenRemaining - deltaTime);
			zombie.butterRemaining = std::max(0.0f, zombie.butterRemaining - deltaTime);
			zombie.paralysisRemaining = std::max(
				0.0f, zombie.paralysisRemaining - deltaTime);
			zombie.slowImmunityRemaining = std::max(
				0.0f, zombie.slowImmunityRemaining - deltaTime);
			zombie.frozenImmunityRemaining = std::max(
				0.0f, zombie.frozenImmunityRemaining - deltaTime);
			zombie.butterImmunityRemaining = std::max(
				0.0f, zombie.butterImmunityRemaining - deltaTime);
			zombie.paralysisImmunityRemaining = std::max(
				0.0f, zombie.paralysisImmunityRemaining - deltaTime);
			if (IsCastingTreatment(zombie, elapsed, candidate,
				pendingTreatments, treatmentConfig)) {
				continue;
			}
			if (zombie.frozenRemaining > 0.0f || zombie.butterRemaining > 0.0f
				|| zombie.paralysisRemaining > 0.0f) continue;
			const float effectiveMoveSpeed = zombie.moveSpeed
				* (zombie.slowRemaining > 0.0f ? 0.5f : 1.0f);

			const BlockerRef blocker = FindFrontBlocker(state, zombie);
			if (!blocker.IsValid()) {
				zombie.x -= effectiveMoveSpeed * deltaTime;
			}
			else {
				float& blockerHealth = BlockerHealth(state, blocker);
				const float distance = zombie.x - BlockerX(state, blocker);
				const bool lockedEatingTarget =
					zombie.eatingPlantId >= 0
					&& zombie.eatingPlantId == BlockerID(state, blocker);
				if (!lockedEatingTarget && distance > config.contactDistance) {
					zombie.x = std::max(
						BlockerX(state, blocker) + config.contactDistance,
						zombie.x - effectiveMoveSpeed * deltaTime);
				}
				else {
					zombie.biteCharge += deltaTime;
					while (zombie.biteCharge >= biteInterval
						&& blockerHealth > kMinimumHealth) {
						blockerHealth = std::max(
							0.0f, blockerHealth - zombie.attackDamage);
						zombie.biteCharge -= biteInterval;
					}
				}
			}

			if (zombie.x <= config.houseX) {
				zombie.breached = true;
				state.breachLoss += config.breachPenalty;
			}
		}
	}

	// 在时域末端估算正在啃食的僵尸还要被挡多久，使视野外的破墙协同仍能进入评分。
	float TerminalBlockerUtility(
		const SimulationState& state, const Config& config)
	{
		std::array<float, kMaxSimulationPlants> biteDps{};
		std::array<float, kMaxSimulationSupports> supportBiteDps{};
		const float biteInterval = std::max(0.1f, config.biteInterval);
		for (int zombieIndex = 0;
			zombieIndex < state.zombieCount; ++zombieIndex) {
			const SimZombie& zombie = state.zombies[zombieIndex];
			if (!IsAlive(zombie) || !zombie.simulatedCombatant
				|| zombie.attackDamage <= 0.0f) continue;
			const BlockerRef blocker = FindFrontBlocker(state, zombie);
			if (!blocker.IsValid()) continue;
			const float distance = zombie.x - BlockerX(state, blocker);
			const bool lockedEatingTarget =
				zombie.eatingPlantId >= 0
				&& zombie.eatingPlantId == BlockerID(state, blocker);
			if (!lockedEatingTarget
				&& (distance > config.contactDistance + 1.0f
					|| distance < -10.0f)) {
				continue;
			}
			if (blocker.support) {
				supportBiteDps[blocker.index] += zombie.attackDamage / biteInterval;
			}
			else {
				biteDps[blocker.index] += zombie.attackDamage / biteInterval;
			}
		}

		float utility = 0.0f;
		for (int plantIndex = 0;
			plantIndex < state.plantCount; ++plantIndex) {
			const SimPlant& plant = state.plants[plantIndex];
			if (!IsAlive(plant) || biteDps[plantIndex] <= 0.0f) continue;
			const float remainingBlockedSeconds = std::min(
				std::max(0.0f, config.terminalBlockedSecondsCap),
				plant.health / biteDps[plantIndex]);
			utility += remainingBlockedSeconds
				* std::max(0.0f, config.terminalBlockedSecondUtility);
		}
		for (int supportIndex = 0;
			supportIndex < state.supportCount; ++supportIndex) {
			const SimSupport& support = state.supports[supportIndex];
			if (!IsAlive(support) || supportBiteDps[supportIndex] <= 0.0f) continue;
			const float remainingBlockedSeconds = std::min(
				std::max(0.0f, config.terminalBlockedSecondsCap),
				support.health / supportBiteDps[supportIndex]);
			utility += remainingBlockedSeconds
				* std::max(0.0f, config.terminalBlockedSecondUtility);
		}
		return utility;
	}

	// 把剩余阳光、植物战略价值和越线惩罚归一成候选之间可比较的玩家效用。
	float PlayerUtility(const SimulationState& state)
	{
		float utility = state.sun * kSunUtilityMultiplier - state.breachLoss
			+ state.directPlayerUtilityAdjustment;
		for (int i = 0; i < state.plantCount; ++i) {
			const SimPlant& plant = state.plants[i];
			if (!IsAlive(plant)) continue;
			const float healthFraction = std::clamp(
				plant.health / std::max(1.0f, plant.maxHealth), 0.0f, 1.0f);
			utility += plant.strategicValue * healthFraction;
		}
		for (int i = 0; i < state.supportCount; ++i) {
			const SimSupport& support = state.supports[i];
			if (!IsAlive(support)) continue;
			const float healthFraction = std::clamp(
				support.health / std::max(1.0f, support.maxHealth), 0.0f, 1.0f);
			utility += support.strategicValue * healthFraction;
		}
		return utility;
	}

	SimZombie* FindZombie(SimulationState& state, int zombieId)
	{
		for (int i = 0; i < state.zombieCount; ++i) {
			if (state.zombies[i].id == zombieId) return &state.zombies[i];
		}
		return nullptr;
	}

	float RepairPool(float& current, float maximum, float amount)
	{
		if (current <= kMinimumHealth || maximum <= kMinimumHealth
			|| current >= maximum || amount <= 0.0f) {
			return 0.0f;
		}
		const float before = current;
		current = std::min(maximum, current + amount);
		return current - before;
	}

	float ApplyTreatmentToZombie(SimZombie& zombie, float amount)
	{
		if (!IsAlive(zombie)) return 0.0f;
		return RepairPool(zombie.bodyHealth, zombie.bodyMaxHealth, amount)
			+ RepairPool(zombie.helmHealth, zombie.helmMaxHealth, amount)
			+ RepairPool(zombie.shieldHealth, zombie.shieldMaxHealth, amount);
	}

	bool IsWithinTreatmentRadius(
		const SimZombie& source, const SimZombie& target, float radius)
	{
		const float dx = target.x - source.x;
		const float dy = target.y - source.y;
		return dx * dx + dy * dy <= radius * radius;
	}

	float ApplyTreatmentEvent(SimulationState& state, TreatmentAction action,
		int sourceZombieId, int targetZombieId, float radius, float amount,
		float overflowPressure)
	{
		SimZombie* source = FindZombie(state, sourceZombieId);
		if (!source || !IsAlive(*source)) return 0.0f;

		float restored = 0.0f;
		if (action == TreatmentAction::AREA) {
			for (int i = 0; i < state.zombieCount; ++i) {
				SimZombie& target = state.zombies[i];
				if (IsAlive(target)
					&& IsWithinTreatmentRadius(*source, target, radius)) {
					restored += ApplyTreatmentToZombie(target, amount);
				}
			}
			// 未详细推进的群疗对象只贡献有界终局压力，不改变实体数组与碰撞结果。
			state.directPlayerUtilityAdjustment -= std::max(0.0f, overflowPressure);
		}
		else {
			SimZombie* target = FindZombie(state, targetZombieId);
			if (target && target->id != source->id && IsAlive(*target)
				&& IsWithinTreatmentRadius(*source, *target, radius)) {
				restored = ApplyTreatmentToZombie(*target, amount);
			}
		}
		return restored;
	}

	float TerminalZombiePressure(
		const SimulationState& state, const TreatmentConfig& config)
	{
		float pressure = 0.0f;
		for (int i = 0; i < state.zombieCount; ++i) {
			const SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie)) continue;
			const float health = zombie.bodyHealth
				+ zombie.helmHealth + zombie.shieldHealth;
			const float attackFactor = 0.5f
				+ std::max(0.0f, zombie.attackDamage) / 50.0f;
			const float progressFactor = 1.0f
				+ std::max(0.0f, 900.0f - zombie.x) / 900.0f;
			pressure += health * attackFactor * progressFactor
				* std::max(0.0f, config.terminalZombiePressurePerHealth);
		}
		return pressure;
	}

	// 按正式生命口径与格子层组在数值副本中提交一次已锁定劫持者处决。
	void ApplyHijackerExecution(SimulationState& state, int hijackerZombieId,
		bool survivalMode, float survivalExecutionLineCap)
	{
		SimZombie* hijacker = FindZombie(state, hijackerZombieId);
		if (!hijacker || !IsAlive(*hijacker)) return;
		float executionLine = hijacker->bodyHealth
			+ hijacker->helmHealth + hijacker->shieldHealth;
		if (survivalMode) {
			executionLine = std::min(executionLine,
				std::max(0.0f, survivalExecutionLineCap));
		}
		if (executionLine <= kMinimumHealth) return;

		std::array<float, 64> groupHealth{};
		std::array<bool, 64> groupProtected{};
		for (int i = 0; i < state.plantCount; ++i) {
			const SimPlant& plant = state.plants[i];
			const int group = plant.hijackerExecutionGroup;
			if (!IsAlive(plant) || group < 0 || group >= 64) continue;
			if (plant.countsForHijackerExecution) {
				groupHealth[group] += plant.health;
			}
			if (plant.protectedFromHijackerExecution) {
				groupProtected[group] = true;
			}
		}
		std::array<bool, 64> killedGroups{};
		for (int group = 0; group < 64; ++group) {
			killedGroups[group] = !groupProtected[group]
				&& groupHealth[group] > kMinimumHealth
				&& groupHealth[group] <= executionLine;
		}
		for (int i = 0; i < state.plantCount; ++i) {
			SimPlant& plant = state.plants[i];
			const int group = plant.hijackerExecutionGroup;
			if (plant.diesWithHijackerExecutionGroup
				&& group >= 0 && group < 64 && killedGroups[group]) {
				plant.health = 0.0f;
			}
		}
		for (int i = 0; i < state.zombieCount; ++i) {
			SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie) || zombie.id == hijacker->id) continue;
			const float health = zombie.bodyHealth
				+ zombie.helmHealth + zombie.shieldHealth;
			if (health > kMinimumHealth && health <= executionLine) {
				zombie.bodyHealth = 0.0f;
			}
		}
		hijacker->bodyHealth = 0.0f;
	}

	void ApplyHijackerExecution(
		SimulationState& state, const TreatmentConfig& config)
	{
		ApplyHijackerExecution(state, config.hijackerZombieId,
			config.survivalMode, config.survivalExecutionLineCap);
	}

	// 治疗者选择以玩家效用损失为目标，因此终局僵尸压力从玩家效用中扣除。
	float TreatmentPlayerUtility(
		const SimulationState& state, const Config& combat,
		const TreatmentConfig& treatment)
	{
		return PlayerUtility(state) + TerminalBlockerUtility(state, combat)
			- TerminalZombiePressure(state, treatment);
	}

	// 从同一 rollout 的初始状态副本推进治疗场景，避免每个候选重复构造共同随机样本。
	ScenarioUtility RunTreatmentScenario(const Snapshot& snapshot,
		const SimulationState& initialState,
		const TreatmentConfig& treatmentConfig,
		const std::vector<PendingTreatment>& pendingTreatments,
		std::vector<unsigned char>& pendingResolved,
		const TreatmentCandidate* candidate, std::uint32_t seed)
	{
		const Config& config = treatmentConfig.combat;
		SimulationState state = initialState;

		std::fill(pendingResolved.begin(), pendingResolved.end(), 0);
		std::vector<unsigned char> cobResolved(
			snapshot.pendingCobBlasts.size(), 0);
		bool candidateResolved = candidate == nullptr;
		bool hijackerResolved = treatmentConfig.hijackerExecutionSeconds < 0.0f;
		std::minstd_rand random(seed ^ 0x9E3779B9u);
		const float deltaTime = std::max(0.1f, config.stepSeconds);
		const int stepCount = std::max(
			1, static_cast<int>(std::ceil(config.horizonSeconds / deltaTime)));
		float nextPlantDecision = Random01(random)
			* std::max(deltaTime, config.plantDecisionInterval);

		for (int step = 0; step < stepCount; ++step) {
			const float elapsed = static_cast<float>(step) * deltaTime;
			const float nextElapsed = std::min(
				config.horizonSeconds, elapsed + deltaTime);
			const float remaining = std::max(
				0.0f, config.horizonSeconds - elapsed);
			ResolvePendingCobBlasts(snapshot, state, nextElapsed, cobResolved);
			UpdatePlantShutdowns(state, deltaTime);
			UpdatePlantProduction(state, deltaTime);
			UpdateCardCooldowns(state, deltaTime);
			if (elapsed >= nextPlantDecision) {
				TryPlantFromCards(snapshot, state, remaining, random);
				nextPlantDecision += std::max(
					deltaTime, config.plantDecisionInterval);
			}
			UpdateMagneticPulses(state, deltaTime);
			UpdateCobBlasts(state, deltaTime);
			UpdatePlantAttacks(state, deltaTime, snapshot.rows, random);
			UpdateZombies(state, config, deltaTime, elapsed, candidate,
				&pendingTreatments, &treatmentConfig);

			for (std::size_t i = 0; i < pendingTreatments.size(); ++i) {
				const PendingTreatment& pending = pendingTreatments[i];
				if (pendingResolved[i] || pending.resolveSeconds > nextElapsed) continue;
				ApplyTreatmentEvent(state, pending.action, pending.sourceZombieId,
					pending.targetZombieId, pending.radius, pending.healAmount, 0.0f);
				pendingResolved[i] = true;
			}
			if (!candidateResolved && candidate
				&& candidate->delaySeconds + treatmentConfig.castSeconds <= nextElapsed) {
				const bool area = candidate->action == TreatmentAction::AREA;
				ApplyTreatmentEvent(state, candidate->action,
					treatmentConfig.sourceZombieId, candidate->targetZombieId,
					area ? treatmentConfig.areaRadius : treatmentConfig.focusedRadius,
					area ? treatmentConfig.areaHealAmount
						: treatmentConfig.focusedHealAmount,
					candidate->overflowPressure);
				candidateResolved = true;
			}
			if (!hijackerResolved
				&& treatmentConfig.hijackerExecutionSeconds <= nextElapsed) {
				ApplyHijackerExecution(state, treatmentConfig);
				hijackerResolved = true;
			}
		}

		float total = TreatmentPlayerUtility(state, config, treatmentConfig);
		if (!hijackerResolved
			&& FindZombie(state, treatmentConfig.hijackerZombieId)) {
			SimulationState executed = state;
			ApplyHijackerExecution(executed, treatmentConfig);
			const float executionSwing = TreatmentPlayerUtility(
				executed, config, treatmentConfig) - total;
			const float secondsBeyondHorizon = std::max(0.0f,
				treatmentConfig.hijackerExecutionSeconds - config.horizonSeconds);
			const float discount = 1.0f / (1.0f + secondsBeyondHorizon / 7.0f);
			total += executionSwing * discount;
		}
		return { total, TerminalBlockerUtility(state, config) };
	}

	// 从同一 rollout 的初始状态副本推进攻击场景；candidate 为空时即为无攻击基线。
	ScenarioUtility RunScenario(const Snapshot& snapshot,
		const SimulationState& initialState, const Config& config,
		const Candidate* candidate, std::uint32_t seed)
	{
		SimulationState state = initialState;
		if (candidate) ApplyCandidateImpact(state, *candidate, config);
		int candidateStrikesApplied = 0;
		std::vector<unsigned char> cobResolved(
			snapshot.pendingCobBlasts.size(), 0);

		std::minstd_rand random(seed ^ 0x9E3779B9u);
		const float deltaTime = std::max(0.1f, config.stepSeconds);
		const int stepCount = std::max(
			1, static_cast<int>(std::ceil(config.horizonSeconds / deltaTime)));
		float nextPlantDecision = Random01(random)
			* std::max(deltaTime, config.plantDecisionInterval);

		for (int step = 0; step < stepCount; ++step) {
			const float elapsed = static_cast<float>(step) * deltaTime;
			const float nextElapsed = std::min(
				config.horizonSeconds, elapsed + deltaTime);
			const float remaining = std::max(
				0.0f, config.horizonSeconds - elapsed);
			ResolvePendingCobBlasts(snapshot, state, nextElapsed, cobResolved);
			while (candidate && candidate->targetStrikeCount > 0
				&& candidate->targetStrikeInterval > 0.0f
				&& candidateStrikesApplied < candidate->targetStrikeCount
				&& static_cast<float>(candidateStrikesApplied + 1)
					* candidate->targetStrikeInterval <= nextElapsed) {
				++candidateStrikesApplied;
				for (int plantIndex = 0;
					plantIndex < state.plantCount; ++plantIndex) {
					SimPlant& target = state.plants[plantIndex];
					if (!IsAlive(target)
						|| target.id != candidate->targetPlantId) continue;
					target.health = std::max(0.0f,
						target.health - candidate->targetStrikeDamage);
					if (candidateStrikesApplied >= candidate->targetStrikeCount) {
						target.health = 0.0f;
					}
					break;
				}
			}
			UpdatePlantShutdowns(state, deltaTime);
			UpdatePlantProduction(state, deltaTime);
			UpdateCardCooldowns(state, deltaTime);
			if (elapsed >= nextPlantDecision) {
				TryPlantFromCards(snapshot, state, remaining, random);
				nextPlantDecision += std::max(
					deltaTime, config.plantDecisionInterval);
			}
			UpdateMagneticPulses(state, deltaTime);
			UpdateCobBlasts(state, deltaTime);
			UpdatePlantAttacks(state, deltaTime, snapshot.rows, random);
			UpdateZombies(state, config, deltaTime);
		}
		const float coordination = TerminalBlockerUtility(state, config);
		return {
			PlayerUtility(state) + coordination,
			coordination
		};
	}

	void ApplyPendingControlEvent(SimulationState& state,
		const PendingControlEvent& event, std::minstd_rand& random)
	{
		for (int i = 0; i < state.plantCount; ++i) {
			if (state.plants[i].id == event.sourcePlantId) {
				state.plants[i].health = 0.0f;
				break;
			}
		}
		for (int i = 0; i < state.zombieCount; ++i) {
			SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie) || !zombie.canBeChilled) continue;
			if (zombie.slowImmunityRemaining <= 0.0f) {
				zombie.slowRemaining = std::max(
					zombie.slowRemaining, std::max(0.0f, event.slowDuration));
			}
			// 寒冰菇沿用正式 StartFrozen 门禁：阶段免冻者只吃减速尾巴，连伤害也豁免。
			if (!zombie.canBeFrozen) continue;
			ApplyZombieDamage(zombie, event.damage);
			if (!IsAlive(zombie) || zombie.frozenImmunityRemaining > 0.0f) continue;
			const float durationRange = std::max(
				0.0f, event.frozenDurationMax - event.frozenDurationMin);
			zombie.frozenRemaining = std::max(zombie.frozenRemaining,
				std::max(0.0f, event.frozenDurationMin)
					+ Random01(random) * durationRange);
		}
	}

	void ApplyNightRoofChargeEvent(SimulationState& state,
		const NightRoofChargeCandidate& candidate,
		const NightRoofChargeConfig& config)
	{
		for (int i = 0; i < state.plantCount; ++i) {
			SimPlant& plant = state.plants[i];
			if (!IsAlive(plant) || plant.row != candidate.row
				|| plant.protectedFromNightRoofCharge) continue;
			const bool wetSlope = candidate.wetRow && plant.column >= 0
				&& plant.column < candidate.wetSlopeColumnCount;
			plant.shutdownRemaining = std::max(plant.shutdownRemaining,
				wetSlope ? candidate.wetPlantShutdownSeconds
					: candidate.plantShutdownSeconds);
		}

		if (candidate.guided) {
			SimZombie* guide = FindZombie(state, candidate.guideZombieId);
			if (!guide || !IsAlive(*guide) || guide->helmHealth <= kMinimumHealth) return;
			const float immunity = std::max(0.0f, config.guideImmunitySeconds);
			const float radius = std::max(0.0f, config.guideImmunityRadius);
			const float radiusSquared = radius * radius;
			for (int i = 0; i < state.zombieCount; ++i) {
				SimZombie& target = state.zombies[i];
				if (!IsAlive(target) || target.mindControlled != guide->mindControlled) continue;
				const float dx = target.x - guide->x;
				const float dy = target.y - guide->y;
				if (dx * dx + dy * dy > radiusSquared) continue;
				target.slowRemaining = 0.0f;
				target.frozenRemaining = 0.0f;
				target.butterRemaining = 0.0f;
				target.slowImmunityRemaining = std::max(
					target.slowImmunityRemaining, immunity);
				target.frozenImmunityRemaining = std::max(
					target.frozenImmunityRemaining, immunity);
				target.butterImmunityRemaining = std::max(
					target.butterImmunityRemaining, immunity);
			}
			return;
		}

		for (int i = 0; i < state.zombieCount; ++i) {
			SimZombie& zombie = state.zombies[i];
			if (!IsAlive(zombie) || zombie.row != candidate.row
				|| !zombie.canBeAffectedByNightRoofCharge) continue;
			const bool wetSlope = candidate.wetRow
				&& zombie.x <= candidate.wetSlopeEndX;
			const float damage = wetSlope
				? candidate.wetZombieDamage : candidate.zombieDamage;
			const float paralysis = wetSlope
				? candidate.wetParalysisSeconds : candidate.paralysisSeconds;

			SimZombie* protector = nullptr;
			float nearestDistance = std::numeric_limits<float>::max();
			for (int providerIndex = 0;
				providerIndex < state.zombieCount; ++providerIndex) {
				SimZombie& provider = state.zombies[providerIndex];
				if (!IsAlive(provider) || provider.row != zombie.row
					|| provider.helmHealth <= kMinimumHealth
					|| !provider.canProtectFromNightRoofCharge
					|| provider.nightRoofProtectionSuppressed) continue;
				const float distance = std::abs(provider.x - zombie.x);
				if (distance <= provider.nightRoofProtectionRadius
					&& distance < nearestDistance) {
					nearestDistance = distance;
					protector = &provider;
				}
			}
			if (protector) {
				protector->helmHealth = std::max(
					0.0f, protector->helmHealth - std::max(0.0f, damage));
				continue;
			}
			ApplyZombieDamage(zombie, damage);
			if (IsAlive(zombie) && zombie.canBeParalyzed
				&& zombie.paralysisImmunityRemaining <= 0.0f) {
				zombie.paralysisRemaining = std::max(
					zombie.paralysisRemaining, std::max(0.0f, paralysis));
			}
		}
	}

	ScenarioUtility RunNightRoofChargeScenario(const Snapshot& snapshot,
		const SimulationState& initialState,
		const NightRoofChargeConfig& routeConfig,
		const NightRoofChargeCandidate* candidate, std::uint32_t seed)
	{
		const Config& config = routeConfig.combat;
		SimulationState state = initialState;
		std::vector<unsigned char> controlResolved(
			routeConfig.pendingControlEvents.size(), 0);
		std::vector<unsigned char> cobResolved(
			snapshot.pendingCobBlasts.size(), 0);
		bool candidateResolved = candidate == nullptr;
		bool hijackerResolved = routeConfig.hijackerExecutionSeconds < 0.0f;
		std::minstd_rand random(seed ^ 0xC2B2AE35u);
		const float deltaTime = std::max(0.1f, config.stepSeconds);
		const int stepCount = std::max(
			1, static_cast<int>(std::ceil(config.horizonSeconds / deltaTime)));
		float nextPlantDecision = Random01(random)
			* std::max(deltaTime, config.plantDecisionInterval);

		for (int step = 0; step < stepCount; ++step) {
			const float elapsed = static_cast<float>(step) * deltaTime;
			const float nextElapsed = std::min(
				config.horizonSeconds, elapsed + deltaTime);
			const float remaining = std::max(
				0.0f, config.horizonSeconds - elapsed);
			ResolvePendingCobBlasts(snapshot, state, nextElapsed, cobResolved);
			UpdatePlantShutdowns(state, deltaTime);
			UpdatePlantProduction(state, deltaTime);
			UpdateCardCooldowns(state, deltaTime);
			if (elapsed >= nextPlantDecision) {
				TryPlantFromCards(snapshot, state, remaining, random);
				nextPlantDecision += std::max(
					deltaTime, config.plantDecisionInterval);
			}
			for (std::size_t i = 0;
				i < routeConfig.pendingControlEvents.size(); ++i) {
				if (controlResolved[i]
					|| routeConfig.pendingControlEvents[i].resolveSeconds > nextElapsed) {
					continue;
				}
				ApplyPendingControlEvent(
					state, routeConfig.pendingControlEvents[i], random);
				controlResolved[i] = 1;
			}
			if (!hijackerResolved
				&& routeConfig.hijackerExecutionSeconds <= nextElapsed) {
				ApplyHijackerExecution(state, routeConfig.hijackerZombieId,
					routeConfig.survivalMode,
					routeConfig.survivalExecutionLineCap);
				hijackerResolved = true;
			}
			if (!candidateResolved && candidate
				&& candidate->resolveSeconds <= nextElapsed) {
				ApplyNightRoofChargeEvent(state, *candidate, routeConfig);
				candidateResolved = true;
			}
			UpdateMagneticPulses(state, deltaTime);
			UpdateCobBlasts(state, deltaTime);
			UpdatePlantAttacks(state, deltaTime, snapshot.rows, random);
			UpdateZombies(state, config, deltaTime);
		}

		const float coordination = TerminalBlockerUtility(state, config);
		return { PlayerUtility(state) + coordination, coordination };
	}
}

Result ChooseTarget(const Snapshot& snapshot, const Config& config, std::uint32_t seed)
{
	Result result;
	result.rolloutCount = std::max(1, config.rolloutCount);
	result.sampledZombieCount = std::min({
		static_cast<int>(snapshot.zombies.size()),
		std::max(0, config.maxZombiesPerRollout),
		kMaxSimulationZombies
	});
	result.sampledPlantCount = std::min(
		static_cast<int>(snapshot.plants.size()), kMaxSimulationPlants);
	result.supportPlantCount = std::min(
		static_cast<int>(snapshot.supports.size()), kMaxSimulationSupports);
	result.cardCount = std::min(
		static_cast<int>(snapshot.cards.size()), kMaxSimulationCards);
	if (snapshot.candidates.empty()
		|| (snapshot.plants.empty() && snapshot.supports.empty())
		|| snapshot.rows <= 0 || snapshot.columns <= 0) {
		return result;
	}

	std::vector<float> totalLosses(snapshot.candidates.size(), 0.0f);
	std::vector<float> totalCoordinationLosses(snapshot.candidates.size(), 0.0f);
	// 每个共同随机样本只排序、截取并扰动一次初始状态，再复制给基线和全部候选。
	for (int rollout = 0; rollout < result.rolloutCount; ++rollout) {
		const std::uint32_t rolloutSeed =
			seed + static_cast<std::uint32_t>(rollout) * 0x85EBCA6Bu;
		SimulationState initialState;
		BuildInitialState(snapshot, config, rolloutSeed, initialState);
		const ScenarioUtility baselineUtility = RunScenario(
			snapshot, initialState, config, nullptr, rolloutSeed);
		for (std::size_t candidateIndex = 0;
			candidateIndex < snapshot.candidates.size(); ++candidateIndex) {
			const ScenarioUtility attackedUtility = RunScenario(
				snapshot, initialState, config,
				&snapshot.candidates[candidateIndex], rolloutSeed);
			totalLosses[candidateIndex] +=
				baselineUtility.total - attackedUtility.total;
			totalCoordinationLosses[candidateIndex] +=
				baselineUtility.coordination - attackedUtility.coordination;
		}
	}

	float bestScore = std::numeric_limits<float>::lowest();
	std::mt19937 tieRandom(seed ^ 0x9E3779B9u);
	int tiedBestCount = 0;
	for (std::size_t candidateIndex = 0;
		candidateIndex < snapshot.candidates.size(); ++candidateIndex) {
		const float averageLoss =
			totalLosses[candidateIndex] / static_cast<float>(result.rolloutCount);
		const float averageCoordinationLoss =
			totalCoordinationLosses[candidateIndex]
			/ static_cast<float>(result.rolloutCount);
		if (averageLoss > bestScore + kScoreTieEpsilon) {
			bestScore = averageLoss;
			tiedBestCount = 1;
			result.candidateIndex = static_cast<int>(candidateIndex);
			result.score = averageLoss;
			result.coordinationLoss = averageCoordinationLoss;
		}
		else if (std::abs(averageLoss - bestScore) <= kScoreTieEpsilon) {
			++tiedBestCount;
			std::uniform_int_distribution<int> chooseTie(1, tiedBestCount);
			if (chooseTie(tieRandom) == 1) {
				result.candidateIndex = static_cast<int>(candidateIndex);
				result.score = averageLoss;
				result.coordinationLoss = averageCoordinationLoss;
			}
		}
	}
	return result;
}

TreatmentResult ChooseTreatment(const Snapshot& snapshot,
	const std::vector<TreatmentCandidate>& candidates,
	const std::vector<PendingTreatment>& pendingTreatments,
	const TreatmentConfig& config, std::uint32_t seed)
{
	TreatmentResult result;
	result.rolloutCount = std::max(1, config.combat.rolloutCount);
	result.sampledZombieCount = std::min({
		static_cast<int>(snapshot.zombies.size()),
		std::max(0, config.combat.maxZombiesPerRollout),
		kMaxSimulationZombies
	});
	result.sampledPlantCount = std::min(
		static_cast<int>(snapshot.plants.size()), kMaxSimulationPlants);
	result.supportPlantCount = std::min(
		static_cast<int>(snapshot.supports.size()), kMaxSimulationSupports);
	result.cardCount = std::min(
		static_cast<int>(snapshot.cards.size()), kMaxSimulationCards);
	if (candidates.empty() || snapshot.zombies.empty()
		|| snapshot.rows <= 0 || snapshot.columns <= 0
		|| config.sourceZombieId < 0) {
		return result;
	}

	std::vector<float> totalLosses(candidates.size(), 0.0f);
	std::vector<unsigned char> pendingResolved(pendingTreatments.size(), 0);
	// 每个共同随机样本只构造一次初始状态；所有治疗候选共享它的截取与随机扰动。
	for (int rollout = 0; rollout < result.rolloutCount; ++rollout) {
		const std::uint32_t rolloutSeed =
			seed + static_cast<std::uint32_t>(rollout) * 0x85EBCA6Bu;
		SimulationState initialState;
		BuildInitialState(snapshot, config.combat, rolloutSeed, initialState);
		const ScenarioUtility baselineUtility = RunTreatmentScenario(
			snapshot, initialState, config, pendingTreatments,
			pendingResolved, nullptr, rolloutSeed);
		for (std::size_t candidateIndex = 0;
			candidateIndex < candidates.size(); ++candidateIndex) {
			const ScenarioUtility treatedUtility = RunTreatmentScenario(
				snapshot, initialState, config, pendingTreatments, pendingResolved,
				&candidates[candidateIndex], rolloutSeed);
			totalLosses[candidateIndex] +=
				baselineUtility.total - treatedUtility.total;
		}
	}

	float bestScore = std::numeric_limits<float>::lowest();
	std::mt19937 tieRandom(seed ^ 0x7F4A7C15u);
	int tiedBestCount = 0;
	for (std::size_t candidateIndex = 0;
		candidateIndex < candidates.size(); ++candidateIndex) {
		const float averageLoss = totalLosses[candidateIndex]
			/ static_cast<float>(result.rolloutCount);
		if (averageLoss > bestScore + kScoreTieEpsilon) {
			bestScore = averageLoss;
			tiedBestCount = 1;
			result.candidateIndex = static_cast<int>(candidateIndex);
			result.score = averageLoss;
		}
		else if (std::abs(averageLoss - bestScore) <= kScoreTieEpsilon) {
			const bool candidateWaits = candidates[candidateIndex].delaySeconds > 0.0f;
			const bool selectedWaits = result.candidateIndex >= 0
				&& candidates[static_cast<std::size_t>(
					result.candidateIndex)].delaySeconds > 0.0f;
			// 同收益时立即动作严格优先，等待必须靠真实未来收益胜出而不能靠并列随机。
			if (candidateWaits != selectedWaits) {
				if (!candidateWaits) {
					result.candidateIndex = static_cast<int>(candidateIndex);
					result.score = averageLoss;
					tiedBestCount = 1;
				}
				continue;
			}
			++tiedBestCount;
			std::uniform_int_distribution<int> chooseTie(1, tiedBestCount);
			if (chooseTie(tieRandom) == 1) {
				result.candidateIndex = static_cast<int>(candidateIndex);
				result.score = averageLoss;
			}
		}
	}
	return result;
}

NightRoofChargeResult ChooseNightRoofChargeRoute(const Snapshot& snapshot,
	const std::vector<NightRoofChargeCandidate>& candidates,
	const NightRoofChargeConfig& config, std::uint32_t seed)
{
	NightRoofChargeResult result;
	result.rolloutCount = std::max(1, config.combat.rolloutCount);
	result.sampledZombieCount = std::min({
		static_cast<int>(snapshot.zombies.size()),
		std::max(0, config.combat.maxZombiesPerRollout),
		kMaxSimulationZombies
	});
	result.sampledPlantCount = std::min(
		static_cast<int>(snapshot.plants.size()), kMaxSimulationPlants);
	result.supportPlantCount = std::min(
		static_cast<int>(snapshot.supports.size()), kMaxSimulationSupports);
	result.cardCount = std::min(
		static_cast<int>(snapshot.cards.size()), kMaxSimulationCards);
	if (candidates.empty() || snapshot.rows <= 0 || snapshot.columns <= 0) {
		return result;
	}

	std::vector<float> totalLosses(candidates.size(), 0.0f);
	for (int rollout = 0; rollout < result.rolloutCount; ++rollout) {
		const std::uint32_t rolloutSeed =
			seed + static_cast<std::uint32_t>(rollout) * 0x85EBCA6Bu;
		SimulationState initialState;
		BuildInitialState(snapshot, config.combat, rolloutSeed, initialState);
		const ScenarioUtility baseline = RunNightRoofChargeScenario(
			snapshot, initialState, config, nullptr, rolloutSeed);
		for (std::size_t candidateIndex = 0;
			candidateIndex < candidates.size(); ++candidateIndex) {
			const ScenarioUtility routed = RunNightRoofChargeScenario(
				snapshot, initialState, config,
				&candidates[candidateIndex], rolloutSeed);
			totalLosses[candidateIndex] += baseline.total - routed.total;
		}
	}

	float bestScore = std::numeric_limits<float>::lowest();
	std::mt19937 tieRandom(seed ^ 0x27D4EB2Fu);
	int tiedBestCount = 0;
	for (std::size_t candidateIndex = 0;
		candidateIndex < candidates.size(); ++candidateIndex) {
		const float averageLoss = totalLosses[candidateIndex]
			/ static_cast<float>(result.rolloutCount);
		if (averageLoss > bestScore + kScoreTieEpsilon) {
			bestScore = averageLoss;
			tiedBestCount = 1;
			result.candidateIndex = static_cast<int>(candidateIndex);
			result.score = averageLoss;
		}
		else if (std::abs(averageLoss - bestScore) <= kScoreTieEpsilon) {
			++tiedBestCount;
			std::uniform_int_distribution<int> chooseTie(1, tiedBestCount);
			if (chooseTie(tieRandom) == 1) {
				result.candidateIndex = static_cast<int>(candidateIndex);
				result.score = averageLoss;
			}
		}
	}
	return result;
}

} // namespace PlantDefenseMonteCarlo
