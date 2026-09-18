#include "Board.h"
#include "Game/AI/MineWaveFormation.h"
#include "Game/Plant/GameDataManager.h"
#include "GameRandom.h"
#include <algorithm>

int Board::GetMineForecastMainEntranceMask() const
{
	if (mMineGrid.layoutRevision == 0) return 0;
	std::array<int,5> points{};
	int total = 0, largest = 0, mask = 0;
	for (const auto& entry : mMineWavePlan) {
		const int cost = GameDataManager::GetInstance().GetZombieWeight(entry.first);
		points[entry.second] += cost; total += cost;
		largest = std::max(largest,points[entry.second]);
	}
	if (total <= 0 || largest*5 < total*2) return 0;
	for (int row = 0; row < 5; ++row) if (points[row] == largest) mask |= 1 << row;
	return mask;
}

void Board::ArrangeMineWave()
{
	if (mMineGrid.layoutRevision == 0 || mMineWavePlan.size() < 3) return;
	// 初次教学保留原有一组示范，其余波次在相同类型/数量上改变配合与方向。
	if ((mLevel == 74 || mLevel == 75 || mLevel == 79) && mMinePlannedWave == 3) return;
	if (mLevel == 77 && mMinePlannedWave == 5) return;
	std::vector<MineWaveFormation::Unit> units;
	for (const auto& entry : mMineWavePlan) {
		int mask = 0;
		for (int row = 0; row < mRows; ++row)
			if (IsNaturalWaveSpawnRowCompatible(entry.first,row)) mask |= 1 << row;
		units.push_back({static_cast<int>(entry.first),entry.second,mask,
			GameDataManager::GetInstance().GetZombieMineFormationRole(entry.first)});
	}
	std::array<int,5> order{0,1,2,3,4};
	for (int i = 4; i > 0; --i) std::swap(order[i],order[GameRandom::Range(0,i)]);
	MineWaveFormation::Arrange(units,order,GameRandom::Range(0,1) != 0);
	for (size_t i = 0; i < units.size(); ++i)
		mMineWavePlan[i] = {static_cast<ZombieType>(units[i].type),units[i].row};
}
