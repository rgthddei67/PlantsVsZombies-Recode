#include "Board.h"

#include "../../GameApp.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../DamageSource.h"
#include "../Plant/DawnLotus.h"
#include "../Plant/Plant.h"
#include "../PlantDamageOrigin.h"
#include "../Zombie/Zombie.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace {
constexpr float kAuroraRiftUnfoldSeconds = 0.8f; // 裂隙从提交到正式出生的展开游戏秒
constexpr float kTemporalAnchorSeconds = 6.0f; // 时间锚独立持续游戏秒
constexpr float kTemporalMarkPulseSeconds = 0.82f; // 时间锚在目标身上续显的游戏秒间隔
constexpr int kAuroraRiftCount = 3; // 常态单次裂隙数量
constexpr int kWhiteoutAuroraRiftCount = 4; // 白毛风提交时的裂隙数量
constexpr int kTemporalTargetLimit = 12; // 单个时间锚最多记录的僵尸数
constexpr float kDawnNavigationSeconds = 8.0f; // 强风模块全场导航持续游戏秒
constexpr int kDawnDamage = 1400; // 每次释放对每行最高威胁目标的普通数值伤害
constexpr int kDawnSplashDamage = 300; // 每次释放对主目标附近其他敌人的普通溅射伤害
constexpr float kDawnSplashRadiusCells = 1.5f; // 同行溅射半径，按当前棋盘格宽换算
constexpr std::array<ZombieType, 5> kAuroraSummonTypes{
	ZombieType::ZOMBIE_BUCKET,
	ZombieType::ZOMBIE_DOOR,
	ZombieType::ZOMBIE_LADDER,
	ZombieType::ZOMBIE_POGO,
	ZombieType::ZOMBIE_FOOTBALL,
};

/** 复合编队和首领不进入单体稳定 ID 时间恢复。 */
bool IsTemporalAnchorTargetType(ZombieType type)
{
	return type != ZombieType::ZOMBIE_BOBSLED_TEAM
		&& type != ZombieType::ZOMBIE_ROOF_MARSHAL
		&& type != ZombieType::ZOMBIE_BOSS;
}

/** 越靠近房屋且总生命越高，时间锚和曙光莲越优先处理。 */
long long ThreatScore(const Zombie* zombie)
{
	if (!zombie) return std::numeric_limits<long long>::min();
	const long long health = static_cast<long long>(zombie->mBodyHealth)
		+ zombie->mHelmHealth + zombie->mShieldHealth;
	const long long proximity = static_cast<long long>(
		std::max(0.0f, static_cast<float>(SCENE_WIDTH) - zombie->GetPosition().x));
	return health * 16LL + proximity;
}
}

bool Board::CommitAuroraPriestRitual(int ownerZombieID, int sourceRow, bool whiteout)
{
	if (mTrophySpawned) return false;
	for (TemporalAnchor& anchor : mTemporalAnchors) {
		for (TemporalTargetSnapshot& target : anchor.targets) {
			if (target.zombieID == ownerZombieID) target.specialActionSubmitted = true;
		}
	}
	const int requested = whiteout ? kWhiteoutAuroraRiftCount : kAuroraRiftCount;
	struct Candidate { int row; int column; int plantValue; };
	std::vector<Candidate> candidates;
	for (int row = 0; row < mRows; ++row) {
		for (int column = 4; column <= std::min(6, mColumns - 1); ++column) {
			if (HasSnowHoleAt(row, column) || !CanPlantOnMineCell(row, column)) continue;
			int value = 0;
			if (const Plant* plant = GetTopPlantAt(row, column)) {
				value = plant->mPlantMaxHealth + 1000;
			}
			candidates.push_back({ row, column, value });
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a,
		const Candidate& b) {
		if (a.plantValue != b.plantValue) return a.plantValue > b.plantValue;
		if (a.column != b.column) return a.column < b.column;
		return a.row < b.row;
	});

	std::array<int, 8> rowCounts{};
	std::vector<Candidate> chosen;
	for (const Candidate& candidate : candidates) {
		if (static_cast<int>(chosen.size()) >= requested) break;
		if (rowCounts[candidate.row] >= 2) continue;
		if (chosen.size() == 1 && candidate.row == chosen.front().row) continue;
		chosen.push_back(candidate);
		++rowCounts[candidate.row];
	}
	for (const Candidate& candidate : candidates) {
		if (static_cast<int>(chosen.size()) >= requested) break;
		if (rowCounts[candidate.row] >= 2) continue;
		if (std::any_of(chosen.begin(), chosen.end(), [&](const Candidate& value) {
			return value.row == candidate.row && value.column == candidate.column;
		})) continue;
		chosen.push_back(candidate);
		++rowCounts[candidate.row];
	}

	for (std::size_t index = 0; index < chosen.size(); ++index) {
		const Candidate& target = chosen[index];
		PendingAuroraRift rift;
		rift.type = kAuroraSummonTypes[(ownerZombieID + static_cast<int>(index)
			+ target.row + target.column) % kAuroraSummonTypes.size()];
		rift.row = target.row;
		rift.column = target.column;
		rift.spawnWave = mCurrentWave;
		rift.timer = kAuroraRiftUnfoldSeconds;
		rift.transactionID = mNextDiscontinuousTransactionID++;
		rift.ownerZombieID = ownerZombieID;
		mPendingAuroraRifts.push_back(rift);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("AuroraRiftUnfold",
				GetCellCenterPosition(target.row, target.column));
		}
	}
	return !chosen.empty();
}

void Board::CommitPolarClockAnchor(int ownerZombieID, int sourceRow)
{
	if (mTrophySpawned) return;
	for (TemporalAnchor& existing : mTemporalAnchors) {
		for (TemporalTargetSnapshot& target : existing.targets) {
			if (target.zombieID == ownerZombieID) target.specialActionSubmitted = true;
		}
	}
	for (const TemporalAnchor& anchor : mTemporalAnchors) {
		if (anchor.ownerZombieID == ownerZombieID) return;
	}
	std::unordered_set<int> alreadyAnchored;
	for (const TemporalAnchor& anchor : mTemporalAnchors) {
		for (const TemporalTargetSnapshot& target : anchor.targets) {
			alreadyAnchored.insert(target.zombieID);
		}
	}
	std::vector<Zombie*> candidates;
	for (int zombieID : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mEntityRegistry.GetZombie(zombieID);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| zombie->IsMindControlled() || !zombie->HasHead()
			|| std::abs(zombie->mRow - sourceRow) > 1
			|| alreadyAnchored.find(zombieID) != alreadyAnchored.end()
			|| !IsTemporalAnchorTargetType(zombie->mZombieType)) continue;
		candidates.push_back(zombie);
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const Zombie* a,
		const Zombie* b) {
		const long long scoreA = ThreatScore(a);
		const long long scoreB = ThreatScore(b);
		return scoreA != scoreB ? scoreA > scoreB : a->mZombieID < b->mZombieID;
	});
	if (candidates.empty()) return;

	TemporalAnchor anchor;
	anchor.ownerZombieID = ownerZombieID;
	anchor.timer = kTemporalAnchorSeconds;
	for (Zombie* zombie : candidates) {
		if (static_cast<int>(anchor.targets.size()) >= kTemporalTargetLimit) break;
		TemporalTargetSnapshot target;
		target.zombieID = zombie->mZombieID;
		target.type = zombie->mZombieType;
		target.row = zombie->mRow;
		target.x = zombie->GetPosition().x;
		if (IsMineBackground()) {
			// 连续换行的实际位置与已承诺节点必须一起回溯，不能只恢复行桶。
			target.mineRowOffset = zombie->GetPosition().y - GetZombieSpawnY(target.row, target.x);
			target.mineTargetCell = zombie->mMineTargetCell;
			target.mineTargetReturning = zombie->mMineTargetReturning;
		}
		target.bodyHealth = zombie->mBodyHealth;
		target.helmType = zombie->mHelmType;
		target.helmHealth = zombie->mHelmHealth;
		target.shieldType = zombie->mShieldType;
		target.shieldHealth = zombie->mShieldHealth;
		target.slowTimer = zombie->GetCooldownTimer();
		target.frozenTimer = zombie->GetFrozenTimer();
		target.butterTimer = zombie->GetButterTimer();
		target.paralysisTimer = zombie->GetParalysisTimeRemaining();
		target.hasHead = zombie->HasHead();
		target.hasArm = zombie->HasArm();
		target.specialActionSubmitted = zombie->HasCommittedIrreversibleSpecialAction()
			|| zombie->mZombieID == ownerZombieID;
		// 来源钟匠在 Board 提交时仍处于 WINDUP；显式不记录它可防止自我刷新。
		if (zombie->mZombieID != ownerZombieID) {
			ZombieTemporalAbilityState abilityState;
			target.abilityStateValid = zombie->CaptureTemporalAbilityState(abilityState);
			if (target.abilityStateValid) {
				target.abilityPhase = abilityState.phase;
				target.abilityRemaining = std::max(0.0f, abilityState.remaining);
				target.abilityReleaseCount = abilityState.releaseCount;
			}
		}
		anchor.targets.push_back(target);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("PolarClockMark", zombie->GetVisualPosition());
		}
	}
	mTemporalAnchors.push_back(std::move(anchor));
}

bool Board::TryRejectDiscontinuousZombieEntry(int row, int column)
{
	Plant* selected = nullptr;
	int selectedDistance = std::numeric_limits<int>::max();
	for (int plantID : mEntityRegistry.GetAllPlantIDs()) {
		Plant* plant = mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->CoversBoundaryEntryCell(row, column)) continue;
		const int distance = std::abs(plant->mRow - row)
			+ std::abs(plant->mColumn - column);
		if (!selected || distance < selectedDistance
			|| (distance == selectedDistance && plantID < selected->mPlantID)) {
			selected = plant;
			selectedDistance = distance;
		}
	}
	return selected && selected->TryConsumeBoundaryShard();
}

void Board::MarkTemporalTargetIrreversible(int zombieID)
{
	for (TemporalAnchor& anchor : mTemporalAnchors) {
		for (TemporalTargetSnapshot& target : anchor.targets) {
			if (target.zombieID == zombieID) target.irreversible = true;
		}
	}
}

void Board::MarkTemporalTargetEquipmentExtracted(int zombieID, bool shieldLayer)
{
	for (TemporalAnchor& anchor : mTemporalAnchors) {
		for (TemporalTargetSnapshot& target : anchor.targets) {
			if (target.zombieID != zombieID) continue;
			if (shieldLayer) target.restoreShield = false;
			else target.restoreHelm = false;
		}
	}
}

void Board::UpdatePolarFinaleRituals(float deltaTime)
{
	if (deltaTime <= 0.0f) return;
	mDawnNavigationTimer = std::max(0.0f, mDawnNavigationTimer - deltaTime);
	if (mTrophySpawned) {
		mPendingAuroraRifts.clear();
		mTemporalAnchors.clear();
		return;
	}

	std::stable_sort(mPendingAuroraRifts.begin(), mPendingAuroraRifts.end(),
		[](const PendingAuroraRift& a, const PendingAuroraRift& b) {
			if (a.timer != b.timer) return a.timer < b.timer;
			if (a.column != b.column) return a.column < b.column;
			return a.transactionID < b.transactionID;
		});
	for (auto it = mPendingAuroraRifts.begin(); it != mPendingAuroraRifts.end();) {
		it->timer = std::max(0.0f, it->timer - deltaTime);
		if (it->timer > 0.0f) { ++it; continue; }
		const bool rejected = TryRejectDiscontinuousZombieEntry(it->row, it->column);
		const float spawnX = rejected ? static_cast<float>(SCENE_WIDTH) + 40.0f
			: GetCellCenterPosition(it->row, it->column).x;
		if (Zombie* zombie = CreateZombie(it->type, it->row, spawnX)) {
			zombie->mSpawnWave = it->spawnWave;
		}
		if (g_particleSystem) {
			g_particleSystem->EmitEffect(rejected ? "BoundaryEntryRedirect"
				: "AuroraRiftArrival", Vector(spawnX,
				GetRowCenterYAtX(it->row, spawnX)));
		}
		it = mPendingAuroraRifts.erase(it);
	}

	for (auto it = mTemporalAnchors.begin(); it != mTemporalAnchors.end();) {
		it->visualPulseTimer -= deltaTime;
		if (it->visualPulseTimer <= 0.0f && g_particleSystem) {
			// 锚持续六秒，周期性局部齿轮让玩家始终看得出哪些目标仍会被回溯。
			for (const TemporalTargetSnapshot& target : it->targets) {
				Zombie* marked = mEntityRegistry.GetZombie(target.zombieID);
				const Vector position = marked && marked->IsActive()
					? marked->GetVisualPosition()
					: Vector(target.x, GetRowCenterYAtX(target.row, target.x));
				g_particleSystem->EmitEffect("PolarClockMark", position);
			}
			it->visualPulseTimer = kTemporalMarkPulseSeconds;
		}
		it->timer = std::max(0.0f, it->timer - deltaTime);
		if (it->timer > 0.0f) { ++it; continue; }
		for (const TemporalTargetSnapshot& target : it->targets) {
			// 旧档中已经提交的复合编队快照也必须保持无效，避免恢复出孤立队员。
			if (target.irreversible || !IsTemporalAnchorTargetType(target.type)) continue;
			Zombie* zombie = mEntityRegistry.GetZombie(target.zombieID);
			const bool survivor = zombie && zombie->IsActive() && !zombie->IsDying()
				&& !zombie->IsMindControlled();
			const int column = std::clamp(static_cast<int>((target.x
				- CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X), 0, mColumns - 1);
			const bool positionRejected = TryRejectDiscontinuousZombieEntry(
				target.row, column);
			if (!survivor) {
				const float spawnX = positionRejected
					? static_cast<float>(SCENE_WIDTH) + 40.0f : target.x;
				zombie = zombie && zombie->IsActive() && zombie->IsDying()
					? ReplaceDyingZombieWithID(zombie, target.type, target.row,
						spawnX, target.zombieID)
					: CreateZombieWithID(target.type, target.row, spawnX, target.zombieID);
				if (!zombie) continue;
			}
			const HelmType helmType = target.restoreHelm ? target.helmType
				: (survivor ? zombie->mHelmType : HelmType::HELMTYPE_NONE);
			const int helmHealth = target.restoreHelm ? target.helmHealth
				: (survivor ? zombie->mHelmHealth : 0);
			const ShieldType shieldType = target.restoreShield ? target.shieldType
				: (survivor ? zombie->mShieldType : ShieldType::SHIELDTYPE_NONE);
			const int shieldHealth = target.restoreShield ? target.shieldHealth
				: (survivor ? zombie->mShieldHealth : 0);
			zombie->RestoreTemporalCoreState(target.row, target.x,
				target.bodyHealth, helmType, helmHealth, shieldType, shieldHealth, target.hasHead,
				target.hasArm, target.slowTimer, target.frozenTimer,
				target.butterTimer, target.paralysisTimer, !positionRejected);
			if (IsMineBackground() && !positionRejected) {
				zombie->SetPosition(Vector(target.x,
					GetZombieSpawnY(target.row, target.x) + target.mineRowOffset));
				zombie->mMineTargetCell = target.mineTargetCell;
				zombie->mMineTargetReturning = target.mineTargetReturning;
			}
			if (target.abilityStateValid) {
				// 只回放目标自己的阶段；锚内已提交的裂隙和时间锚仍留在 Board。
				zombie->RestoreTemporalAbilityState({
					target.abilityPhase, target.abilityRemaining, target.abilityReleaseCount });
			}
			else if (target.zombieID != it->ownerZombieID) {
				// v10 旧档非来源目标继续使用只保留 submitted 的兼容语义。
				zombie->RestoreCommittedIrreversibleSpecialAction(
					target.specialActionSubmitted);
			}
			if (!survivor) zombie->OnTemporalRecreated();
			// 来源钟匠不记录也不恢复局部技能状态，避免六秒结算刷新自己的循环冷却。
			// 局部阶段可以覆盖轨道，故啮食表现必须在全部阶段恢复之后最终对齐。
			zombie->ReconcileTemporalEatingPresentation();
			if (g_particleSystem) {
				g_particleSystem->EmitEffect("PolarClockRewind", zombie->GetVisualPosition());
			}
		}
		it = mTemporalAnchors.erase(it);
	}
}

bool Board::ActivateDawnLotus(int sourcePlantID, int dangerMask)
{
	Plant* source = mEntityRegistry.GetPlant(sourcePlantID);
	if (!source || !source->IsActive() || source->IsShutdown()) return false;
	// 非极夜不读取遗留仪表；基础打击始终提交，红色位只决定附加模块。
	if (!SupportsPolarNightEnvironment()) dangerMask = 0;
	for (int row = 0; row < mRows; ++row) {
		Zombie* target = nullptr;
		mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* candidate) {
			if (!candidate || candidate->IsMindControlled() || candidate->IsDying()) return;
			if (!target || ThreatScore(candidate) > ThreatScore(target)
				|| (ThreatScore(candidate) == ThreatScore(target)
					&& candidate->mZombieID < target->mZombieID)) target = candidate;
		});
		if (!target) continue;
		// 命中前固定中心与目标 ID；主目标只吃主伤害，溅射不会连锁或跨行。
		const int targetID = target->mZombieID;
		const float targetX = target->GetPosition().x;
		const float splashRadius = CELL_COLLIDER_SIZE_X * kDawnSplashRadiusCells;
		const auto origin = PlantDamageOrigin::FromPlant(source->mPlantType);
		if (g_particleSystem) {
			// 死亡或破甲前取当前受击区域中心，离体短爆发不依赖随后变化的动画轨。
			Vector strikeCenter = target->GetVisualPosition();
			if (const ColliderComponent* collider = target->GetColliderComponent()) {
				const SDL_FRect bounds = collider->GetBoundingBox();
				strikeCenter = Vector(bounds.x + bounds.w * 0.5f,
					bounds.y + bounds.h * 0.5f);
			}
			g_particleSystem->EmitEffect("DawnLotusStrike", strikeCenter);
		}
		target->TakeDamage(kDawnDamage, DamageSource::PLANT, false, false, false, origin);
		mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* candidate) {
			if (!candidate || candidate->mZombieID == targetID
				|| candidate->IsMindControlled() || candidate->IsDying()
				|| std::abs(candidate->GetPosition().x - targetX) > splashRadius) return;
			candidate->TakeDamage(kDawnSplashDamage, DamageSource::PLANT,
				false, false, false, origin);
		});
	}
	if ((dangerMask & 2) != 0) {
		for (int row = 0; row < mRows; ++row) SealSnowHole(row);
		for (int zombieID : mEntityRegistry.GetAllZombieIDs()) {
			Zombie* zombie = mEntityRegistry.GetZombie(zombieID);
			if (zombie && zombie->IsActive() && !zombie->IsMindControlled()) {
				zombie->ForceSurfaceFromGroundHazard();
			}
		}
	}
	if ((dangerMask & 4) != 0) {
		mDawnNavigationTimer = std::max(mDawnNavigationTimer, kDawnNavigationSeconds);
	}
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("DawnLotusRelease", source->GetVisualPosition());
	}
	return true;
}

bool Board::ActivateDawnLotusAt(int row, int column)
{
	auto* lotus = dynamic_cast<DawnLotus*>(GetNormalPlantAt(row, column));
	return lotus && lotus->TryActivate();
}
