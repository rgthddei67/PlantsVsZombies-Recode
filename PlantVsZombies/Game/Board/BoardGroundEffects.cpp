#include "Board.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
#include <algorithm>
#include <array>
#include <cmath>

bool Board::IsCellWithinConnectedRange(int sr, int sc, int row, int col, int range) const
{
	const auto valid = [this](int r, int c) {
		return r >= 0 && r < mRows && c >= 0 && c < mColumns
			&& (!IsMineBackground() || (!mMineGrid.IsRock(r,c) && mMineGrid.connected[r*mColumns+c]));
	};
	if (!valid(sr, sc) || !valid(row, col) || range < 0) return false;
	if (std::abs(row-sr) + std::abs(col-sc) > range) return false;
	if (!IsMineBackground()) return true;
	// 地面效果可向四邻格传播，不使用只允许朝房屋下降的敌人行进图。
	std::array<int, MineGrid::Count> distance;
	distance.fill(-1);
	std::array<int, MineGrid::Count> queue{};
	int head = 0, tail = 0;
	queue[tail++] = sr*mColumns+sc;
	distance[queue[0]] = 0;
	while (head < tail) {
		const int cell = queue[head++], r = cell/mColumns, c = cell%mColumns;
		if (r == row && c == col) return true;
		if (distance[cell] >= range) continue;
		for (const auto delta : {std::array<int,2>{0,-1},{-1,0},{1,0},{0,1}}) {
			const int nr=r+delta[0], nc=c+delta[1];
			if (!valid(nr,nc) || distance[nr*mColumns+nc] >= 0) continue;
			const int next=nr*mColumns+nc;
			distance[next]=distance[cell]+1;
			queue[tail++]=next;
		}
	}
	return false;
}

float Board::GetGroundSlowFactor(const Zombie& zombie) const
{
	if (zombie.IsMindControlled() || zombie.IsDying() || !zombie.IsActive()
		|| !zombie.CanBeAffectedByGroundHazards() || zombie.IsControlImmune(ZombieControlEffect::SLOW)) return 1.0f;
	const float left = GetCellCenterPosition(zombie.mRow,0).x - CELL_COLLIDER_SIZE_X*0.5f;
	const int col = static_cast<int>(std::floor((zombie.GetPosition().x-left)/CELL_COLLIDER_SIZE_X));
	if (zombie.mRow < 0 || zombie.mRow >= mRows || col < 0 || col >= mColumns) return 1.0f;
	float factor=1.0f;
	for (const int id : mEntityRegistry.GetAllPlantIDs()) {
		const Plant* plant=mEntityRegistry.GetPlant(id);
		if (plant) factor=std::min(factor,plant->GetGroundSlowFactorAtCell(zombie.mRow,col));
	}
	return factor;
}
