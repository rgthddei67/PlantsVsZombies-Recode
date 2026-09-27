#include "Board.h"
#include "Game/Plant/Plant.h"
#include <algorithm>

bool Board::TrySpendPlantAbilityResource(int ice, int sun)
{
	if (mBoardState != BoardState::GAME || mTrophySpawned || ice < 0 || sun < 0) return false;
	if (IsColdStorage()) {
		if (mColdStorage.playerIce < ice) return false;
		mColdStorage.playerIce -= ice;
	} else {
		if (mSun < sun) return false;
		SubSun(sun);
	}
	return true;
}

bool Board::TrySpendZombieAbilityIce(int ice)
{
	if (mBoardState != BoardState::GAME || mTrophySpawned || ice < 0) return false;
	if (!IsColdStorage()) return true;
	if (mColdStorage.enemyIce < ice) return false;
	mColdStorage.enemyIce -= ice;
	mColdStorage.spent += ice;
	mColdStorage.incomeWindow.push_back({mColdStorage.elapsed, 0, ice});
	return true;
}

float Board::GetAreaPlantAttackSpeedBonus(const Plant* target) const
{
	if (!target || target->mIsPreview || !target->IsActive()) return 0.0f;
	float bonus = 0.0f;
	// 领域跟随当前格位，按来源相加；查询九格不保留指针，不需要另存目标 buff。
	for (int row = std::max(0, target->mRow - 1); row <= std::min(mRows - 1, target->mRow + 1); ++row)
		for (int col = std::max(0, target->mColumn - 1); col <= std::min(mColumns - 1, target->mColumn + 1); ++col)
			if (const auto* source = GetNormalPlantAt(row, col)) bonus += source->GetAreaAttackSpeedBonus();
	return bonus;
}
