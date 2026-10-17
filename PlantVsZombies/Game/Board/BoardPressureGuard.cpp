#include "Board.h"
#include "Game/Zombie/PressureGuardZombie.h"
#include <cmath>
#include <algorithm>

/** 冻结本击全部邻近护卫，按前后位置分层承伤；代伤只扣承接者自身，禁止循环转交。 */
int Board::SharePressureGuardDamage(Zombie& target, int damage)
{
    if (damage <= 0 || !target.IsActive() || target.IsDying()) return damage;
    std::vector<PressureGuardZombie*> guards;
    mEntityRegistry.ForEachPressureGuard([&](Zombie* zombie) {
        auto* guard = static_cast<PressureGuardZombie*>(zombie);
        if (guard == &target || !guard->CanProtect() || guard->IsMindControlled() != target.IsMindControlled()
            || std::abs(guard->mRow-target.mRow)>1
            || std::abs(guard->GetPosition().x-target.GetPosition().x)>CELL_COLLIDER_SIZE_X*PressureGuardRules::RadiusCells)
            return;
        guards.push_back(guard);
    });
    std::sort(guards.begin(),guards.end(),[&](const auto* a,const auto* b) {
        const float ax=a->GetPosition().x, bx=b->GetPosition().x;
        return ax == bx ? a->mZombieID < b->mZombieID : target.IsMindControlled() ? ax > bx : ax < bx;
    });
    target.mPressureProtectorID=guards.empty() ? -1 : guards.front()->mZombieID;
    for (size_t i=0; i<guards.size() && damage>0; ++i) {
        const int transferred=PressureGuardRules::TransferDamage(damage,static_cast<int>(i),target.mPressureShareRemainder);
        guards[i]->TakeTransferredDamage(transferred);
        // 封顶丢弃的部分不回流，也不重新分配给下一名护卫。
        damage-=transferred;
    }
    return damage;
}
