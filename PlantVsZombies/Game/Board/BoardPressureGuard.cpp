#include "Board.h"
#include "Game/Zombie/PressureGuardZombie.h"
#include <cmath>

int Board::SharePressureGuardDamage(Zombie& target, int damage)
{
    if (damage <= 0 || !target.IsActive() || target.IsDying()) return damage;
    const auto eligible = [&](Zombie* zombie) -> PressureGuardZombie* {
        auto* guard = dynamic_cast<PressureGuardZombie*>(zombie);
        if (!guard || !guard->CanProtect() || guard->IsMindControlled() != target.IsMindControlled()
            || std::abs(guard->mRow-target.mRow)>1
            || std::abs(guard->GetPosition().x-target.GetPosition().x)>CELL_COLLIDER_SIZE_X*PressureGuardRules::RadiusCells)
            return nullptr;
        return guard;
    };
    auto* guard = eligible(mEntityRegistry.GetZombie(target.mPressureProtectorID));
    if (!guard) {
        // 使用护卫专用索引，并且包含受击者自身：最前方护卫直接承受自己的伤害，不转给后排。
        mEntityRegistry.ForEachPressureGuard([&](Zombie* zombie) {
                auto* candidate=eligible(zombie);
                if (!candidate) return;
                const float x=candidate->GetPosition().x, best=guard ? guard->GetPosition().x : 0;
                if (!guard || (target.IsMindControlled() ? x>best : x<best)
                    || (x==best && candidate->mZombieID<guard->mZombieID)) guard=candidate;
        });
        target.mPressureProtectorID=guard ? guard->mZombieID : -1;
    }
    if (!guard || guard==&target) return damage;
    // 同类仍持盾时由选中的一面盾完整承接；群攻不能同时磨损范围内的多面承压盾。
    if (auto* peer=dynamic_cast<PressureGuardZombie*>(&target); peer && peer->HasPressureShield()) {
        guard->TakeTransferredDamage(damage);
        return 0;
    }
    // 累计五分之一单位的小数余量，连续五次1点受伤仍准确分担4点，不凭空制造伤害。
    const int64_t numerator=static_cast<int64_t>(damage)*4+target.mPressureShareRemainder;
    const int transferred=static_cast<int>(numerator/5);
    target.mPressureShareRemainder=static_cast<int>(numerator%5);
    guard->TakeTransferredDamage(transferred);
    return damage-transferred;
}
