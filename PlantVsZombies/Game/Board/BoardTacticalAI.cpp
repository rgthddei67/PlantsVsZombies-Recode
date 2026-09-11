#include "Game/Board/Board.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/Card.h"
#include "Game/CardSlotManager.h"
#include "Game/Plant/CobCannon.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/HealerZombie.h"
#include "Game/Zombie/InsulatorZombie.h"
#include "Game/Zombie/Zombie.h"
#include "GameApp.h"
#include "Profiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
	constexpr int kPlantTargetMonteCarloRolloutCount = 48; // 蹦极与精英小丑长时域选点的每候选未来样本数
	constexpr int kTreatmentMonteCarloRolloutCount = 40; // 急救员短时域选疗的每候选未来样本数
	constexpr int kMonteCarloHealerDecisionSpacingSteps = 3; // 两次急救员推演至少间隔的固定逻辑步数
	constexpr int kMonteCarloBungeeDecisionSpacingSteps = 3; // 两次蹦极选点推演至少间隔的固定逻辑步数
	constexpr float kMonteCarloHorizonSeconds = 16.0f;    // 植物防线短视推演时域，单位：游戏秒
	constexpr float kMonteCarloBacklineMultiplier = 1.2f; // 当前后半场植物的战略价值倍率
	constexpr float kMonteCarloSunProducerFutureValue = 300.0f; // 当前产能植物的未来经济价值，单位：阳光分
	constexpr float kTreatmentMonteCarloHorizonSeconds = 7.0f; // 急救员从当前选择推演到下一次最早决策的窗口，单位：游戏秒
	constexpr float kTreatmentTerminalPressurePerHealth = 0.08f; // 时域末端每点僵尸生命折算的基础进攻压力
}

/** 选择当前冻结防线中最值得冰封处决的植物，并在启用时复用蒙特卡洛长时域评估。 */
Plant* Board::SelectIceStatueExecutionTarget(
	int sourceZombieID, float strikeInterval, int strikeDamage,
	MonteCarloTargetStats* stats)
{
	if (!SupportsWinterTemperature()) return nullptr;
	if (stats) *stats = {};
	const auto& gameData = GameDataManager::GetInstance();
	const int backlineColumnCount = (mColumns + 1) / 2;
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	std::vector<int> eligiblePlantIDs;
	std::vector<int> requiredStrikeCounts;
	Plant* best = nullptr;
	float bestValue = -1.0f;
	for (const int plantID : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || plant->IsPreview()
			|| plant->IsSquished() || plant->IsBungeeTargeted()
			|| plant->IsIceSealed() || plant->mPlantHealth <= 0
			|| !IsPlantFootprintFrozen(
				plant->mPlantType, plant->mRow, plant->mColumn)) {
			continue;
		}
		const Cell* cell = mCells[plant->mRow][plant->mColumn];
		if (!cell || (cell->GetNormalPlantID() != plantID
			&& cell->GetPumpkinPlantID() != plantID)) {
			continue;
		}
		const PlantSimulationProfile& profile =
			gameData.GetPlantSimulationProfile(plant->mPlantType);
		if (!profile.persistent || profile.supportOnly) continue;
		eligiblePlantIDs.push_back(plantID);
		requiredStrikeCounts.push_back(
			plant->GetIceExecutionRequiredStrikeCount());
		float value = static_cast<float>(
			gameData.GetPlantSunCost(plant->mPlantType));
		if (profile.sunPerSecond > 0.0f) {
			value += kMonteCarloSunProducerFutureValue;
		}
		if (plant->mColumn < backlineColumnCount) {
			value *= kMonteCarloBacklineMultiplier;
		}
		// ID 已升序；相同价值不替换，稳定保留较小 ID。
		if (!best || value > bestValue) {
			best = plant;
			bestValue = value;
		}
	}
	if (GameAPP::GetInstance().mEnableMonteCarloAI
		&& !eligiblePlantIDs.empty()) {
		int selectedPlantID = NULL_PLANT_ID;
		if (PickMonteCarloPlantRemovalTarget(
			eligiblePlantIDs, sourceZombieID, selectedPlantID, stats,
			strikeInterval, strikeDamage, &requiredStrikeCounts)) {
			if (Plant* selected = mEntityRegistry.GetPlant(selectedPlantID)) {
				return selected;
			}
		}
	}
	return best;
}

/**
 * 从当前棋盘采集紧凑数值快照：实体提供真实生命/速度，卡槽提供未来种植候选，
 * 再把纯计算交给共享推演器。这里是唯一接触 GameObject 的边界。
 */
bool Board::BuildMonteCarloCombatSnapshot(
	PlantDefenseMonteCarlo::Snapshot& snapshot, bool mindControlledFaction,
	bool includeNightRoofChargeDetails)
{
	using namespace PlantDefenseMonteCarlo;
	if (mRows <= 0 || mColumns <= 0 || mColumns * mRows > 64) return false;
	snapshot = Snapshot{};
	snapshot.rows = mRows;
	snapshot.columns = mColumns;
	snapshot.sceneWidth = static_cast<float>(SCENE_WIDTH);
	snapshot.initialSun = static_cast<float>(std::max(0, mSun));
	snapshot.cells.reserve(static_cast<std::size_t>(mRows * mColumns));
	for (int row = 0; row < mRows; ++row) {
		for (int column = 0; column < mColumns; ++column) {
			const Cell* cell = GetCell(row, column);
			const Vector center = GetCellCenterPosition(row, column);
			snapshot.cells.push_back({
				row, column, center.x, center.y, cell && !cell->IsEmpty()
			});
		}
	}

	const auto& gameData = GameDataManager::GetInstance();
	const int backlineColumnCount = (mColumns + 1) / 2;
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	snapshot.plants.reserve(plantIDs.size());
	snapshot.supports.reserve(std::min<std::size_t>(
		plantIDs.size(), static_cast<std::size_t>(mRows * mColumns)));
	for (const int plantID : plantIDs) {
		const Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || plant->IsSquished()
			|| plant->mRow < 0 || plant->mRow >= mRows
			|| plant->mColumn < 0 || plant->mColumn >= mColumns) {
			continue;
		}
		const PlantSimulationProfile& profile =
			gameData.GetPlantSimulationProfile(plant->mPlantType);
		const bool sleeping = plant->GetSleepState();
		float strategicValue = static_cast<float>(
			gameData.GetPlantSunCost(plant->mPlantType));
		if (profile.sunPerSecond > 0.0f) {
			strategicValue += kMonteCarloSunProducerFutureValue;
		}
		if (plant->mColumn < backlineColumnCount) {
			strategicValue *= kMonteCarloBacklineMultiplier;
		}

		const ColliderComponent* collider = plant->GetColliderComponent();
		const SDL_FRect bounds = collider
			? collider->GetBoundingBox()
			: SDL_FRect{
				plant->GetPosition().x - CELL_COLLIDER_SIZE_X * 0.5f,
				plant->GetPosition().y - CELL_COLLIDER_SIZE_Y * 0.5f,
				CELL_COLLIDER_SIZE_X, CELL_COLLIDER_SIZE_Y
			};
		const Cell* cell = GetCell(plant->mRow, plant->mColumn);
		const bool isUnder = cell && cell->GetUnderPlantID() == plantID;
		const bool isNormal = cell && cell->GetNormalPlantID() == plantID;
		const bool isPumpkin = cell && cell->GetPumpkinPlantID() == plantID;
		const bool isOverlay = cell && cell->GetOverlayPlantID() == plantID;
		const bool executionLayer = isNormal || isPumpkin || isOverlay;
		if (profile.supportOnly) {
			// 普通花盆/睡莲只保留第二层阻挡所需数据，不占 128 株详细画像容量。
			snapshot.supports.push_back({
				plant->mPlantID,
				plant->mRow,
				plant->mColumn,
				plant->GetPosition().x,
				static_cast<float>(std::max(0, plant->mPlantHealth)),
				static_cast<float>(std::max(1, plant->mPlantMaxHealth)),
				strategicValue,
				{ bounds.x, bounds.y, bounds.w, bounds.h },
				true
			});
			continue;
		}
		bool protectedFromNightRoofCharge = false;
		if (includeNightRoofChargeDetails) {
			protectedFromNightRoofCharge =
				GetNightRoofChargeSupportProtector(plant) != nullptr;
			if (!protectedFromNightRoofCharge) {
				for (const int providerID : plantIDs) {
					const Plant* provider = mEntityRegistry.GetPlant(providerID);
					if (provider && provider->CanGroundNightRoofChargeFor(plant)) {
						protectedFromNightRoofCharge = true;
						break;
					}
				}
			}
		}
		snapshot.plants.push_back({
			plant->mPlantID,
			plant->mRow,
			plant->mColumn,
			plant->GetPosition().x,
			static_cast<float>(std::max(0, plant->mPlantHealth)),
			static_cast<float>(std::max(1, plant->mPlantMaxHealth)),
			strategicValue,
			sleeping ? 0.0f : profile.attackDps,
			profile.attackRowRadius,
			sleeping ? 0.0f : profile.sunPerSecond,
			0.0f,
			{ bounds.x, bounds.y, bounds.w, bounds.h },
			plant->mPlantType == PlantType::PLANT_PUMPKINSHELL,
			executionLayer ? plant->mRow * mColumns + plant->mColumn : -1,
			isNormal || isPumpkin,
			executionLayer,
			GetNightRoofHijackerSupportProtector(plant) != nullptr,
			isPumpkin ? 2 : (isNormal ? 1 : (isUnder ? 0 : -1)),
			plant->CanBeEaten() || isUnder,
			plant->GetShutdownTimeRemaining(),
			protectedFromNightRoofCharge,
			sleeping ? 0.0f : profile.slowApplicationsPerSecond,
			profile.slowDuration,
			sleeping ? 0.0f : profile.frozenApplicationsPerSecond,
			profile.frozenDuration,
			sleeping ? 0.0f : profile.butterApplicationsPerSecond,
			profile.butterDuration,
			sleeping ? 0.0f : profile.paralysisApplicationsPerSecond,
			profile.paralysisDuration
		});
		PlantDefenseMonteCarlo::PlantSnapshot& plantSnapshot =
			snapshot.plants.back();
		plantSnapshot.y = plant->GetPosition().y;
		plantSnapshot.abilityCooldownRemaining = sleeping
			? 0.0f : plant->GetSimulationAbilityCooldownRemaining();
		plantSnapshot.magneticPulseCooldown = sleeping
			? 0.0f : profile.magneticPulseCooldown;
		plantSnapshot.magneticPulseRadius = profile.magneticPulseRadius;
		plantSnapshot.magneticPulseParalysisDuration =
			profile.magneticPulseParalysisDuration;
		plantSnapshot.magneticSearchRowRadius = profile.magneticSearchRowRadius;
		plantSnapshot.magneticSearchRadius =
			profile.magneticSearchRadiusInCells * CELL_COLLIDER_SIZE_X;
		plantSnapshot.magneticEatingSearchRadius =
			profile.magneticEatingSearchRadiusInCells * CELL_COLLIDER_SIZE_X;
		plantSnapshot.magneticRowDistancePenalty = CELL_COLLIDER_SIZE_X;
		plantSnapshot.cobBlastCooldown = sleeping
			? 0.0f : profile.cobBlastCooldown;
		plantSnapshot.cobBlastDamage = sleeping
			? 0.0f : profile.cobBlastDamage;
		plantSnapshot.cobBlastRadius = profile.cobBlastRadius;
		plantSnapshot.cobBlastRowRadius = profile.cobBlastRowRadius;
		if (!sleeping) {
			if (const auto* cannon = dynamic_cast<const CobCannon*>(plant)) {
				const float pendingDelay = cannon->GetPendingSimulationBlastDelay();
				if (pendingDelay >= 0.0f) {
					const Vector target = cannon->GetPendingTarget();
					snapshot.pendingCobBlasts.push_back({
						plant->mPlantID, cannon->GetPendingTargetRow(),
						target.x, target.y, pendingDelay,
						profile.cobBlastDamage, profile.cobBlastRadius,
						profile.cobBlastRowRadius
					});
				}
			}
		}
	}

	// 已离膛玉米棒是独立提交效果；来源植物随后被移除也不能回滚这次固定落点。
	const PlantSimulationProfile& cobProfile =
		gameData.GetPlantSimulationProfile(PlantType::PLANT_COBCANNON);
	std::vector<int> bulletIDs = mEntityRegistry.GetAllBulletIDs();
	std::sort(bulletIDs.begin(), bulletIDs.end());
	for (const int bulletID : bulletIDs) {
		const Bullet* bullet = mEntityRegistry.GetBullet(bulletID);
		if (!bullet || !bullet->IsActive() || !bullet->IsCobCannonMotion()) continue;
		const Vector target = bullet->GetCobTarget();
		snapshot.pendingCobBlasts.push_back({
			-1, bullet->GetCobTargetRow(), target.x, target.y,
			std::max(0.0f, bullet->GetCobDuration() - bullet->GetCobElapsed()),
			static_cast<float>(std::max(0, bullet->GetBulletDamage())),
			cobProfile.cobBlastRadius,
			cobProfile.cobBlastRowRadius
		});
	}

	std::vector<int> zombieIDs = mEntityRegistry.GetAllZombieIDs();
	std::sort(zombieIDs.begin(), zombieIDs.end());
	snapshot.zombies.reserve(zombieIDs.size());
	for (const int zombieID : zombieIDs) {
		const Zombie* zombie = mEntityRegistry.GetZombie(zombieID);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| !zombie->HasHead()
			|| (zombie->IsMindControlled() != mindControlledFaction
				&& !(includeNightRoofChargeDetails
					&& zombie->IsNightRoofChargeGuideType()))
			|| zombie->mRow < 0 || zombie->mRow >= mRows) {
			continue;
		}
		const bool simulatedCombatant =
			zombie->IsMindControlled() == mindControlledFaction;
		const auto* insulator = dynamic_cast<const InsulatorZombie*>(zombie);
		const bool canProtectNightRoofCharge = includeNightRoofChargeDetails
			&& insulator && !insulator->IsWet()
			&& zombie->mHelmHealth > 0
			&& zombie->CanBeAffectedByGroundHazards();
		auto getSimulatedImmunityRemaining = [zombie](ZombieControlEffect effect) {
			const float timed = zombie->GetControlImmunityTimeRemaining(effect);
			// 永久免疫没有有限计时；用最大 float 让纯数值时域自然保持门禁。
			return timed > 0.0f ? timed
				: (zombie->IsControlImmune(effect)
					? std::numeric_limits<float>::max() : 0.0f);
		};
		float centerX = zombie->GetPosition().x;
		float centerY = zombie->GetPosition().y;
		SDL_FRect zombieBounds{
			centerX - CELL_COLLIDER_SIZE_X * 0.5f,
			centerY - CELL_COLLIDER_SIZE_Y * 0.5f,
			CELL_COLLIDER_SIZE_X,
			CELL_COLLIDER_SIZE_Y
		};
		if (const ColliderComponent* collider = zombie->GetColliderComponent()) {
			zombieBounds = collider->GetBoundingBox();
			centerX = zombieBounds.x + zombieBounds.w * 0.5f;
			centerY = zombieBounds.y + zombieBounds.h * 0.5f;
		}
		snapshot.zombies.push_back({
			zombie->mZombieID,
			zombie->IsEating() ? zombie->GetEatingPlantID() : -1,
			zombie->mRow,
			centerX,
			centerY,
			zombie->GetUncontrolledHorizontalMoveSpeed(),
			static_cast<float>(std::max(0, zombie->mBodyHealth)),
			static_cast<float>(std::max(0, zombie->mBodyMaxHealth)),
			static_cast<float>(std::max(0, zombie->mHelmHealth)),
			static_cast<float>(std::max(0, zombie->mHelmMaxHealth)),
			static_cast<float>(std::max(0, zombie->mShieldHealth)),
			static_cast<float>(std::max(0, zombie->mShieldMaxHealth)),
			static_cast<float>(std::max(0, zombie->mAttackDamage)),
			zombie->IsEating(),
			zombie->GetCooldownTimer(),
			zombie->GetFrozenTimer(),
			zombie->GetButterTimer(),
			zombie->GetParalysisTimeRemaining(),
			getSimulatedImmunityRemaining(ZombieControlEffect::SLOW),
			getSimulatedImmunityRemaining(ZombieControlEffect::FROZEN),
			getSimulatedImmunityRemaining(ZombieControlEffect::BUTTER),
			getSimulatedImmunityRemaining(ZombieControlEffect::PARALYSIS),
			zombie->CanBeChilled(),
			zombie->CanBeFrozen(),
			zombie->CanBeButtered(),
			zombie->CanBeParalyzed(),
			zombie->CanBeAffectedByGroundHazards(),
			canProtectNightRoofCharge,
			includeNightRoofChargeDetails
				&& IsNightRoofChargeProtectionSuppressed(zombie),
			canProtectNightRoofCharge
				? 1.5f * CELL_COLLIDER_SIZE_X : 0.0f,
			zombie->IsMindControlled(),
			simulatedCombatant,
			false
		});
		PlantDefenseMonteCarlo::ZombieSnapshot& zombieSnapshot =
			snapshot.zombies.back();
		zombieSnapshot.bounds = {
			zombieBounds.x, zombieBounds.y, zombieBounds.w, zombieBounds.h
		};
		zombieSnapshot.magneticItemAvailable =
			zombie->CanBeTargetedByMagnetShroom();
		const MagneticSimulationLayer magneticLayer =
			zombie->GetMagneticSimulationLayer();
		zombieSnapshot.magneticRemovesHelm =
			magneticLayer == MagneticSimulationLayer::HELM;
		zombieSnapshot.magneticRemovesShield =
			magneticLayer == MagneticSimulationLayer::SHIELD;
	}

	if (mCardSlotManager) {
		const auto& cards = mCardSlotManager->GetCards();
		snapshot.cards.reserve(cards.size());
		for (Card* card : cards) {
			if (!card) continue;
			const PlantType type = card->GetPlantType();
			const PlantSimulationProfile& profile =
				gameData.GetPlantSimulationProfile(type);
			if (!profile.persistent || !profile.futurePlantable
				|| profile.supportOnly) continue;
			const bool dormant = profile.daytimeDormant
				&& !GameAPP::GetInstance().GetBackgroundIsNight(mBackGround);

			std::uint64_t legalCellMask = 0;
			for (int row = 0; row < mRows; ++row) {
				for (int column = 0; column < mColumns; ++column) {
					const int cellIndex = row * mColumns + column;
					if (CanPlantAt(type, row, column)) {
						legalCellMask |= (1ULL << cellIndex);
					}
				}
			}
			if (legalCellMask == 0) continue;

			const int cost = card->GetSunCost();
			float strategicValue = static_cast<float>(cost);
			if (!dormant && profile.sunPerSecond > 0.0f) {
				strategicValue += kMonteCarloSunProducerFutureValue;
			}
			snapshot.cards.push_back({
				static_cast<int>(type),
				cost,
				card->GetCooldownTimer(),
				card->GetCooldownTime(),
				static_cast<float>(profile.baseHealth),
				strategicValue,
				dormant ? 0.0f : profile.attackDps,
				profile.attackRowRadius,
				dormant ? 0.0f : profile.sunPerSecond,
				profile.firstSunDelay,
				legalCellMask,
				type == PlantType::PLANT_PUMPKINSHELL,
				type == PlantType::PLANT_PUMPKINSHELL ? 2
					: (IsUnderPlantLayerType(type) ? 0 : 1),
				dormant ? 0.0f : profile.slowApplicationsPerSecond,
				profile.slowDuration,
				dormant ? 0.0f : profile.frozenApplicationsPerSecond,
				profile.frozenDuration,
				dormant ? 0.0f : profile.butterApplicationsPerSecond,
				profile.butterDuration,
				dormant ? 0.0f : profile.paralysisApplicationsPerSecond,
				profile.paralysisDuration
			});
			PlantDefenseMonteCarlo::CardSnapshot& cardSnapshot =
				snapshot.cards.back();
			cardSnapshot.magneticPulseCooldown = dormant
				? 0.0f : profile.magneticPulseCooldown;
			cardSnapshot.magneticPulseRadius = profile.magneticPulseRadius;
			cardSnapshot.magneticPulseParalysisDuration =
				profile.magneticPulseParalysisDuration;
			cardSnapshot.magneticSearchRowRadius = profile.magneticSearchRowRadius;
			cardSnapshot.magneticSearchRadius =
				profile.magneticSearchRadiusInCells * CELL_COLLIDER_SIZE_X;
			cardSnapshot.magneticEatingSearchRadius =
				profile.magneticEatingSearchRadiusInCells * CELL_COLLIDER_SIZE_X;
			cardSnapshot.magneticRowDistancePenalty = CELL_COLLIDER_SIZE_X;
			cardSnapshot.cobBlastCooldown = dormant
				? 0.0f : profile.cobBlastCooldown;
			cardSnapshot.cobBlastDamage = dormant
				? 0.0f : profile.cobBlastDamage;
			cardSnapshot.cobBlastRadius = profile.cobBlastRadius;
			cardSnapshot.cobBlastRowRadius = profile.cobBlastRowRadius;
		}
	}
	return true;
}


bool Board::PickMonteCarloPlantBlastTarget(
	int minRow, int maxRow, int damage, float radius, int sourceZombieID,
	int& targetRow, Vector& targetPosition, MonteCarloTargetStats* stats,
	const std::vector<int>* removalPlantIDs, int* selectedRemovalPlantID,
	float removalStrikeInterval, int removalStrikeDamage,
	const std::vector<int>* removalStrikeCounts,
	int maxCandidateRolloutEvaluations)
{
	PROFILE_SCOPE("MC.PlantTarget.Total");
	using namespace PlantDefenseMonteCarlo;
	const bool removalMode = removalPlantIDs != nullptr;
	if (removalMode ? removalPlantIDs->empty()
		: (damage <= 0 || radius <= 0.0f)) {
		return false;
	}
	minRow = std::clamp(minRow, 0, std::max(0, mRows - 1));
	maxRow = std::clamp(maxRow, minRow, std::max(0, mRows - 1));

	Snapshot snapshot;
	bool snapshotBuilt = false;
	{
		PROFILE_SCOPE("MC.PlantTarget.Snapshot");
		snapshotBuilt = BuildMonteCarloCombatSnapshot(snapshot, false);
	}
	if (!snapshotBuilt) return false;
	std::vector<std::pair<int, int>> candidateCells;
	const std::unordered_set<int> eligibleRemovalIDs = removalMode
		? std::unordered_set<int>(removalPlantIDs->begin(), removalPlantIDs->end())
		: std::unordered_set<int>();
	std::unordered_map<int, int> strikeCountByPlantID;
	if (removalMode && removalStrikeCounts
		&& removalStrikeCounts->size() == removalPlantIDs->size()) {
		for (std::size_t i = 0; i < removalPlantIDs->size(); ++i) {
			strikeCountByPlantID.emplace(
				(*removalPlantIDs)[i], (*removalStrikeCounts)[i]);
		}
	}
	for (const PlantSnapshot& plant : snapshot.plants) {
		if (removalMode && eligibleRemovalIDs.find(plant.id) != eligibleRemovalIDs.end()) {
			const Vector center = GetCellCenterPosition(plant.row, plant.column);
			snapshot.candidates.push_back({
				plant.row, plant.column, center.x, center.y, plant.id
			});
			Candidate& candidate = snapshot.candidates.back();
			const auto strikeCount = strikeCountByPlantID.find(plant.id);
			if (strikeCount != strikeCountByPlantID.end()) {
				candidate.targetStrikeInterval = removalStrikeInterval;
				candidate.targetStrikeDamage = static_cast<float>(removalStrikeDamage);
				candidate.targetStrikeCount = std::max(0, strikeCount->second);
			}
		}
		else if (!removalMode && plant.row >= minRow && plant.row <= maxRow) {
			candidateCells.emplace_back(plant.row, plant.column);
		}
	}
	for (const SupportSnapshot& support : snapshot.supports) {
		if (removalMode
			&& eligibleRemovalIDs.find(support.id) != eligibleRemovalIDs.end()) {
			const Vector center = GetCellCenterPosition(support.row, support.column);
			snapshot.candidates.push_back({
				support.row, support.column, center.x, center.y, support.id
			});
		}
		else if (!removalMode
			&& support.row >= minRow && support.row <= maxRow) {
			candidateCells.emplace_back(support.row, support.column);
		}
	}
	if (!removalMode) {
		std::sort(candidateCells.begin(), candidateCells.end());
		candidateCells.erase(
			std::unique(candidateCells.begin(), candidateCells.end()),
			candidateCells.end());
		for (const auto& cell : candidateCells) {
			const Vector center = GetCellCenterPosition(cell.first, cell.second);
			snapshot.candidates.push_back({
				cell.first, cell.second, center.x, center.y
			});
			// 冻结与正式爆炸相同的遮挡资格，仍由通用推演器归并南瓜保护。
			if (IsMineBackground()) {
				auto& blocked = snapshot.candidates.back().blockedPlantIds;
				for (const auto& plant : snapshot.plants)
					if (MineBlocksSegment(center, GetCellCenterPosition(plant.row,plant.column))) blocked.push_back(plant.id);
				for (const auto& support : snapshot.supports)
					if (MineBlocksSegment(center, GetCellCenterPosition(support.row,support.column))) blocked.push_back(support.id);
			}
		}
	}
	if (snapshot.candidates.empty()) return false;

	int rolloutCount = kPlantTargetMonteCarloRolloutCount;
	if (maxCandidateRolloutEvaluations > 0) {
		// 密集防线按总候选评估量封顶；候选少时仍保留完整样本数。
		rolloutCount = std::clamp(
			maxCandidateRolloutEvaluations
				/ static_cast<int>(snapshot.candidates.size()),
			1, kPlantTargetMonteCarloRolloutCount);
	}
	Config config;
	ConfigureMonteCarloPlantImpactConfig(config,
		rolloutCount, kMonteCarloHorizonSeconds,
		damage, radius);

	std::uint32_t seed = 2166136261u;
	auto mixSeed = [&seed](std::uint32_t value) {
		seed ^= value;
		seed *= 16777619u;
	};
	mixSeed(static_cast<std::uint32_t>(mBoardFrame));
	mixSeed(static_cast<std::uint32_t>(mCurrentWave));
	mixSeed(static_cast<std::uint32_t>(sourceZombieID));
	Result result;
	{
		PROFILE_SCOPE("MC.PlantTarget.Rollouts");
		result = ChooseTarget(snapshot, config, seed);
	}
	if (stats) {
		stats->rolloutCount = result.rolloutCount;
		stats->candidateCount =
			static_cast<int>(snapshot.candidates.size());
		stats->sampledZombieCount = result.sampledZombieCount;
		stats->sampledPlantCount = result.sampledPlantCount;
		stats->supportPlantCount = result.supportPlantCount;
		stats->cardCount = result.cardCount;
		stats->bestScore = result.score;
		stats->coordinationLoss = result.coordinationLoss;
	}
	if (result.candidateIndex < 0
		|| result.candidateIndex >= static_cast<int>(snapshot.candidates.size())) {
		return false;
	}
	const Candidate& chosen = snapshot.candidates[result.candidateIndex];
	targetRow = chosen.row;
	targetPosition = Vector(chosen.x, chosen.y);
	if (selectedRemovalPlantID) {
		*selectedRemovalPlantID = chosen.targetPlantId;
	}
	return true;
}

bool Board::PickMonteCarloPlantRemovalTarget(
	const std::vector<int>& eligiblePlantIDs, int sourceZombieID,
	int& targetPlantID, MonteCarloTargetStats* stats,
	float strikeInterval, int strikeDamage,
	const std::vector<int>* strikeCounts,
	int maxCandidateRolloutEvaluations)
{
	int targetRow = -1;
	Vector targetPosition;
	targetPlantID = NULL_PLANT_ID;
	return PickMonteCarloPlantBlastTarget(
		0, std::max(0, mRows - 1), 0, 0.0f, sourceZombieID,
		targetRow, targetPosition, stats, &eligiblePlantIDs, &targetPlantID,
		strikeInterval, strikeDamage, strikeCounts,
		maxCandidateRolloutEvaluations);
}

bool Board::TryClaimMonteCarloHealerDecisionSlot()
{
	if (mMonteCarloHealerDecisionCooldownSteps > 0) return false;
	mMonteCarloHealerDecisionCooldownSteps =
		kMonteCarloHealerDecisionSpacingSteps;
	return true;
}

bool Board::TryClaimMonteCarloBungeeDecisionSlot()
{
	if (mMonteCarloBungeeDecisionCooldownSteps > 0) return false;
	mMonteCarloBungeeDecisionCooldownSteps =
		kMonteCarloBungeeDecisionSpacingSteps;
	return true;
}

bool Board::PickMonteCarloZombieTreatment(
	const MonteCarloTreatmentRequest& request,
	MonteCarloTreatmentDecision& decision, MonteCarloTargetStats* stats)
{
	PROFILE_SCOPE("MC.Healer.Total");
	using namespace PlantDefenseMonteCarlo;
	Zombie* source = mEntityRegistry.GetZombie(request.sourceZombieID);
	if (!source || source->IsMindControlled() || !source->IsActive()
		|| source->IsDying() || !source->HasHead()
		|| request.castSeconds <= 0.0f
		|| request.areaRadius <= 0.0f || request.focusedRadius <= 0.0f
		|| request.areaHealAmount <= 0.0f || request.focusedHealAmount <= 0.0f) {
		return false;
	}

	Snapshot snapshot;
	bool snapshotBuilt = false;
	{
		PROFILE_SCOPE("MC.Healer.Snapshot");
		snapshotBuilt = BuildMonteCarloCombatSnapshot(snapshot, false);
	}
	if (!snapshotBuilt) return false;
	Config treatmentCombatConfig;
	ConfigureMonteCarloCombatConfig(treatmentCombatConfig,
		kTreatmentMonteCarloRolloutCount,
		kTreatmentMonteCarloHorizonSeconds);
	std::unordered_set<int> areaTargetIDs(
		request.areaTargetIDs.begin(), request.areaTargetIDs.end());
	std::unordered_set<int> focusedTargetIDs(
		request.focusedTargetIDs.begin(), request.focusedTargetIDs.end());
	std::unordered_set<int> forcedZombieIDs{ request.sourceZombieID };
	const int lockedHijackerID = GetNightRoofHijackerID();
	if (lockedHijackerID != NULL_ZOMBIE_ID) forcedZombieIDs.insert(lockedHijackerID);

	std::vector<PendingTreatment> pendingTreatments;
	for (const int zombieID : mEntityRegistry.GetAllZombieIDs()) {
		auto* healer = dynamic_cast<HealerZombie*>(mEntityRegistry.GetZombie(zombieID));
		if (!healer || healer->mZombieID == request.sourceZombieID
			|| !healer->IsActive() || healer->IsDying()
			|| healer->IsMindControlled() != source->IsMindControlled()) {
			continue;
		}
		const HealerZombie::TreatmentState state = healer->GetTreatmentState();
		if (state != HealerZombie::TreatmentState::AREA
			&& state != HealerZombie::TreatmentState::FOCUSED) {
			continue;
		}
		forcedZombieIDs.insert(healer->mZombieID);
		if (state == HealerZombie::TreatmentState::FOCUSED) {
			forcedZombieIDs.insert(healer->GetFocusedTargetID());
		}
		pendingTreatments.push_back({
			state == HealerZombie::TreatmentState::AREA
				? TreatmentAction::AREA : TreatmentAction::FOCUSED,
			healer->mZombieID,
			healer->GetFocusedTargetID(),
			healer->GetCastRemaining(),
			state == HealerZombie::TreatmentState::AREA
				? request.areaRadius : request.focusedRadius,
			state == HealerZombie::TreatmentState::AREA
				? request.areaHealAmount : request.focusedHealAmount
		});
	}

	struct RankedZombie {
		ZombieSnapshot snapshot;
		float priority = 0.0f;
		bool forced = false;
	};
	std::vector<RankedZombie> ranked;
	ranked.reserve(snapshot.zombies.size());
	for (const ZombieSnapshot& zombie : snapshot.zombies) {
		const float health = zombie.bodyHealth
			+ zombie.helmHealth + zombie.shieldHealth;
		const float distanceFactor = 1.0f
			+ 400.0f / std::max(100.0f, zombie.x);
		float priority = std::max(1.0f, health)
			* std::max(1.0f, zombie.attackDamage)
			* std::max(1.0f, zombie.moveSpeed) * distanceFactor;
		if (zombie.id == request.sourceZombieID) {
			priority = std::numeric_limits<float>::max();
		}
		else if (zombie.id == lockedHijackerID) {
			priority = std::numeric_limits<float>::max() * 0.5f;
		}
		else if (focusedTargetIDs.find(zombie.id) != focusedTargetIDs.end()) {
			priority *= 2.0f;
		}
		else if (areaTargetIDs.find(zombie.id) != areaTargetIDs.end()) {
			priority *= 1.5f;
		}
		ranked.push_back({
			zombie, priority,
			forcedZombieIDs.find(zombie.id) != forcedZombieIDs.end()
		});
	}
	std::sort(ranked.begin(), ranked.end(),
		[](const RankedZombie& lhs, const RankedZombie& rhs) {
			if (lhs.forced != rhs.forced) return lhs.forced;
			if (lhs.priority != rhs.priority) return lhs.priority > rhs.priority;
			return lhs.snapshot.id < rhs.snapshot.id;
		});
	if (ranked.size() > static_cast<std::size_t>(
		treatmentCombatConfig.maxZombiesPerRollout)) {
		ranked.resize(treatmentCombatConfig.maxZombiesPerRollout);
	}
	snapshot.zombies.clear();
	std::unordered_set<int> sampledZombieIDs;
	for (const RankedZombie& zombie : ranked) {
		snapshot.zombies.push_back(zombie.snapshot);
		sampledZombieIDs.insert(zombie.snapshot.id);
	}
	if (sampledZombieIDs.find(request.sourceZombieID) == sampledZombieIDs.end()) {
		return false;
	}

	float areaOverflowPressure = 0.0f;
	for (const int zombieID : request.areaTargetIDs) {
		if (sampledZombieIDs.find(zombieID) != sampledZombieIDs.end()) continue;
		const Zombie* zombie = mEntityRegistry.GetZombie(zombieID);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()) continue;
		auto repairPotential = [&request](int current, int maximum) {
			if (current <= 0 || maximum <= 0 || current >= maximum) return 0.0f;
			return std::min(request.areaHealAmount,
				static_cast<float>(maximum - current));
		};
		const float restored = repairPotential(
			zombie->mBodyHealth, zombie->mBodyMaxHealth)
			+ repairPotential(zombie->mHelmHealth, zombie->mHelmMaxHealth)
			+ repairPotential(zombie->mShieldHealth, zombie->mShieldMaxHealth);
		const float attackFactor = 0.5f
			+ static_cast<float>(std::max(0, zombie->mAttackDamage)) / 50.0f;
		const float progressFactor = 1.0f + std::max(
			0.0f, 900.0f - zombie->GetPosition().x) / 900.0f;
		areaOverflowPressure += restored * attackFactor * progressFactor
			* kTreatmentTerminalPressurePerHealth;
	}

	std::vector<TreatmentCandidate> candidates;
	if (!request.areaTargetIDs.empty()) {
		candidates.push_back({
			TreatmentAction::AREA, NULL_ZOMBIE_ID, 0.0f, areaOverflowPressure
		});
	}
	for (const int targetID : request.focusedTargetIDs) {
		if (sampledZombieIDs.find(targetID) == sampledZombieIDs.end()) continue;
		candidates.push_back({ TreatmentAction::FOCUSED, targetID, 0.0f, 0.0f });
	}
	if (candidates.empty()) return false;
	if (request.allowWait && request.waitSeconds > 0.0f) {
		const std::size_t immediateCount = candidates.size();
		candidates.reserve(immediateCount * 2);
		for (std::size_t i = 0; i < immediateCount; ++i) {
			TreatmentCandidate delayed = candidates[i];
			delayed.delaySeconds = request.waitSeconds;
			candidates.push_back(delayed);
		}
	}

	TreatmentConfig config;
	config.combat = treatmentCombatConfig;
	config.sourceZombieId = request.sourceZombieID;
	config.castSeconds = request.castSeconds;
	config.areaRadius = request.areaRadius;
	config.focusedRadius = request.focusedRadius;
	config.areaHealAmount = request.areaHealAmount;
	config.focusedHealAmount = request.focusedHealAmount;
	config.terminalZombiePressurePerHealth = kTreatmentTerminalPressurePerHealth;
	config.hijackerZombieId = lockedHijackerID;
	config.survivalMode = mIsSurvival;
	PopulateNightRoofHijackerTreatmentForecast(
		lockedHijackerID, config.hijackerExecutionSeconds,
		config.survivalExecutionLineCap);

	std::uint32_t seed = 2166136261u;
	auto mixSeed = [&seed](std::uint32_t value) {
		seed ^= value;
		seed *= 16777619u;
	};
	mixSeed(static_cast<std::uint32_t>(mBoardFrame));
	mixSeed(static_cast<std::uint32_t>(mCurrentWave));
	mixSeed(static_cast<std::uint32_t>(request.sourceZombieID));
	TreatmentResult result;
	{
		PROFILE_SCOPE("MC.Healer.Rollouts");
		result = ChooseTreatment(
			snapshot, candidates, pendingTreatments, config, seed);
	}
	if (stats) {
		stats->rolloutCount = result.rolloutCount;
		stats->candidateCount = static_cast<int>(candidates.size());
		stats->sampledZombieCount = result.sampledZombieCount;
		stats->sampledPlantCount = result.sampledPlantCount;
		stats->supportPlantCount = result.supportPlantCount;
		stats->cardCount = result.cardCount;
		stats->bestScore = result.score;
		stats->coordinationLoss = 0.0f;
	}
	if (result.candidateIndex < 0
		|| result.candidateIndex >= static_cast<int>(candidates.size())) {
		return false;
	}
	const TreatmentCandidate& chosen = candidates[result.candidateIndex];
	if (chosen.delaySeconds > 0.0f) {
		decision.action = MonteCarloTreatmentAction::WAIT;
		decision.targetZombieID = NULL_ZOMBIE_ID;
	}
	else if (chosen.action == TreatmentAction::AREA) {
		decision.action = MonteCarloTreatmentAction::AREA;
		decision.targetZombieID = NULL_ZOMBIE_ID;
	}
	else {
		decision.action = MonteCarloTreatmentAction::FOCUSED;
		decision.targetZombieID = chosen.targetZombieId;
	}
	return true;
}
