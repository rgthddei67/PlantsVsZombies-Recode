#include "Board.h"

/** 覆盖资格归品种，Board只负责稳定选择；不会逐目标重复扣甲或递归分担。 */
bool Board::TryInterceptPlantLobbedProjectile(int row, float x, int damage)
{
    Zombie* protector = nullptr;
    for (int candidateRow = 0; candidateRow < mRows; ++candidateRow) {
        mEntityRegistry.ForEachZombieInRow(candidateRow, [&](Zombie* zombie) {
            if (zombie->CanInterceptLobbedFor(row, x)
                && (!protector || zombie->mZombieID < protector->mZombieID)) protector = zombie;
        });
    }
    return protector && protector->InterceptLobbedProjectile(damage);
}
