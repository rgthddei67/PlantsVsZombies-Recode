#include "Board.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/FogFerryRules.h"
#include "ParticleSystem/ParticleSystem.h"
#include <cmath>

int Board::GetFogFerryTargetColumn(const Zombie& source) const
{
    if (source.mRow < 0 || source.mRow >= mRows || mColumns <= FogFerryRules::ClearLeftColumn) return -1;
    const int column = static_cast<int>(std::floor((source.GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
    return FogFerryRules::TargetColumn(column, GetEffectiveFogLeftColumn(), mColumns);
}

std::vector<int> Board::GetFogFerryPassengers(const Zombie& source) const
{
    std::vector<int> result;
    const int target = GetFogFerryTargetColumn(source);
    if (target < 0 || !source.IsActive() || source.IsDying() || source.IsMindControlled()) return result;
    const int sourceColumn = static_cast<int>(std::floor((source.GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
    const float targetX = FogFerryRules::LandingX(target, CELL_INITALIZE_POS_X, CELL_COLLIDER_SIZE_X);
    for (const int id : mEntityRegistry.GetAllZombieIDs()) {
        const auto* z = mEntityRegistry.GetZombie(id);
        if (!z || z == &source || !z->IsActive() || z->IsDying()
            || z->mRow != source.mRow || z->IsMindControlled() != source.IsMindControlled()
            || z->mZombieType == ZombieType::ZOMBIE_FOG_FERRY || z->IsFlying()
            || !z->CanBeAffectedByGroundHazards() || !z->CanBeTargetedByProjectile(false)
            || z->IsTangleKelpTarget()) continue;
        const int column = static_cast<int>(std::floor((z->GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
        if (std::abs(column - sourceColumn) <= 1 && z->GetPosition().x > targetX + 5.0f) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

int Board::CommitFogFerry(const Zombie& source)
{
    const auto ids = GetFogFerryPassengers(source);
    if (ids.empty()) return 0;
    const int target = GetFogFerryTargetColumn(source);
    const float targetX = FogFerryRules::LandingX(target, CELL_INITALIZE_POS_X, CELL_COLLIDER_SIZE_X);
    int moved = 0;
    for (const int id : ids) {
        auto* z = mEntityRegistry.GetZombie(id);
        if (!z || !z->IsActive() || z->IsDying()) continue;
        // 使用同一落点附近的小间距，不按原队列长度把末尾乘客留在原地。
        const float x = targetX + static_cast<float>(moved % 5);
        const bool showEffect = moved < FogFerryRules::VisualPassengerLimit;
        if (showEffect && g_particleSystem) {
            g_particleSystem->EmitEffect(z->mZombieType == ZombieType::ZOMBIE_ZAMBONI
                || z->mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI
                || z->mZombieType == ZombieType::ZOMBIE_CATAPULT
                || z->mZombieType == ZombieType::ZOMBIE_ELITE_CATAPULT
                ? "FogFerryDepartWide" : "FogFerryDepart", z->GetPosition() + Vector(0, -40));
        }
        z->TeleportWithinRow(x);
        if (showEffect && g_particleSystem)
            g_particleSystem->EmitEffect("FogFerryArrive", z->GetPosition() + Vector(0, -40));
        ++moved;
    }
    return moved;
}
