#include "Board.h"
#include "Game/Zombie/ExcavatorZombie.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/GameDataManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {
	constexpr float kEconomyValueSeconds = 120; // 产光植物的未来经济价值折算游戏秒数
}

Zombie* Board::GetMineWallOwner(int cell) const
{
	if (cell < 0 || cell >= MineGrid::Count) return nullptr;
	auto* owner = dynamic_cast<ExcavatorZombie*>(mEntityRegistry.GetZombie(mMineWallOwners[cell]));
	return owner && owner->IsActive() && !owner->IsDying() && owner->HasHead()
		&& !owner->IsMindControlled() && owner->GetWall() == cell ? owner : nullptr;
}

/** 在节点采集实际防线/在场兵力；确定性矿道推演通过后才预留，不猜测未来波次。 */
bool Board::ReserveMineExcavation(Zombie* zombie, int start, int& wall, int& stand)
{
	if (!zombie || !IsMineBackground()) return false;
	const auto begin = std::chrono::steady_clock::now();
	MineExcavationTactics::Snapshot snapshot;
	snapshot.grid = mMineGrid;
	snapshot.start = start;
	snapshot.workSeconds = ExcavatorZombie::GetWorkSeconds();
	snapshot.approachMultiplier = ExcavatorZombie::GetApproachMultiplier();
	snapshot.fogReduction = GetMineFogReduction()*GetMineFogStrength();
	snapshot.fogRemaining = mMineFogElapsed >= 0 ? kMineFogDuration-mMineFogElapsed : 0;
	snapshot.fogFirstColumn = GetMineFogFirstColumn();
	for (int cell = 0; cell < MineGrid::Count; ++cell)
		snapshot.excluded[cell] = cell == mMineDigCell || GetMineWallOwner(cell) != nullptr;
	const auto& data = GameDataManager::GetInstance();
	auto plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(),plantIDs.end());
	for (int id : plantIDs) {
		const Plant* p = mEntityRegistry.GetPlant(id);
		if (!p || !p->IsActive() || p->IsPreview() || p->IsSquished() || p->mPlantHealth <= 0
			|| !MineGrid::Valid(p->mRow,p->mColumn)) continue;
		const auto& profile = data.GetPlantSimulationProfile(p->mPlantType);
		const auto* cell = GetCell(p->mRow,p->mColumn);
		const int layer = !p->CanBeEaten() ? -1 : (cell->GetPumpkinPlantID() == id ? 2
			: (cell->GetNormalPlantID() == id ? 1 : (cell->GetUnderPlantID() == id ? 0 : -1)));
		const bool dormant = p->GetSleepState();
		MineExcavationTactics::Plant plant;
		plant.cell = MineGrid::Index(p->mRow,p->mColumn); plant.layer = layer;
		plant.health = static_cast<float>(p->mPlantHealth);
		plant.value = static_cast<float>(data.GetPlantSunCost(p->mPlantType)) + profile.sunPerSecond*kEconomyValueSeconds;
		plant.dps = dormant ? 0 : p->GetSimulationAttackDps(profile.attackDps) * p->GetAttackSpeedMultiplier();
		plant.rowRadius = profile.attackRowRadius; plant.range = static_cast<float>(profile.mineAttackRange);
		plant.shape = static_cast<MineExcavationTactics::AttackShape>(profile.mineAttackShape);
		plant.multiTarget = profile.mineMultiTarget;
		plant.slowUptime = std::clamp(profile.slowApplicationsPerSecond*profile.slowDuration
			+ profile.butterApplicationsPerSecond*profile.butterDuration,0.0f,1.0f);
		plant.shutdown = p->GetShutdownTimeRemaining();
		snapshot.plants.push_back(plant);
	}
	auto zombieIDs = mEntityRegistry.GetAllZombieIDs();
	// 超额兵力只取距工兵最近的局部攻势，省略的友军不提供虚构伤害/承伤；防守植物仍全部纳入。
	std::sort(zombieIDs.begin(),zombieIDs.end(),[&](int a, int b) {
		const auto* left = mEntityRegistry.GetZombie(a);
		const auto* right = mEntityRegistry.GetZombie(b);
		const float da = left ? (left->GetPosition()-zombie->GetPosition()).sqrMagnitude() : std::numeric_limits<float>::max();
		const float db = right ? (right->GetPosition()-zombie->GetPosition()).sqrMagnitude() : std::numeric_limits<float>::max();
		return da != db ? da < db : a < b;
	});
	// 工兵永远位于下标0；距离并列使用稳定ID，不依赖容器迭代顺序。
	zombieIDs.erase(std::remove(zombieIDs.begin(),zombieIDs.end(),zombie->mZombieID),zombieIDs.end());
	zombieIDs.insert(zombieIDs.begin(),zombie->mZombieID);
	for (int id : zombieIDs) {
		const Zombie* z = mEntityRegistry.GetZombie(id);
		if (!z || !z->IsActive() || z->IsDying() || !z->HasHead() || z->IsMindControlled()
			|| z->UsesMineExitRoute() || z->IsFlying() || z->mRow < 0 || z->mRow >= mRows) continue;
		MineExcavationTactics::Zombie unit;
		unit.id = id;
		unit.row = (z->GetPosition().y-GetZombieSpawnY(0,z->GetPosition().x))/GetCellHeight();
		unit.column = (z->GetPosition().x-GetCellCenterPosition(0,0).x)/CELL_COLLIDER_SIZE_X;
		unit.target = z->mMineTargetCell;
		unit.health = static_cast<float>(std::max(0,z->mBodyHealth)+std::max(0,z->mHelmHealth)+std::max(0,z->mShieldHealth));
		unit.speed = z->GetMineSimulationMoveSpeed()/CELL_COLLIDER_SIZE_X;
		unit.dps = z->GetMineSimulationAttackDps(); unit.smashSeconds = z->GetMineSimulationSmashSeconds();
		unit.stun = std::max({z->GetFrozenTimer(),z->GetButterTimer(),z->GetParalysisTimeRemaining()});
		unit.slow = z->GetCooldownTimer(); unit.prism = z->GetPrismMarkRemaining();
		snapshot.zombies.push_back(unit);
		if (snapshot.zombies.size() == MineExcavationTactics::MaxZombies) break;
	}
	const auto result = MineExcavationTactics::Choose(snapshot);
	if (auto* excavator = dynamic_cast<ExcavatorZombie*>(zombie)) {
		excavator->mLastDecision = result;
		excavator->mDecisionMicros = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now()-begin).count());
	}
	wall = result.wall; stand = result.stand;
	if (wall < 0) return false;
	mMineWallOwners[wall] = zombie->mZombieID;
	return true;
}
