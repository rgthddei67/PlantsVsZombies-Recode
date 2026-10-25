#include "ThornLily.h"
#include "ThornLilyRules.h"
#include "Game/Board/Board.h"
#include "Game/Zombie/Zombie.h"
#include <cmath>

void ThornLily::SetupPlant()
{
    LilyPad::SetupPlant();
    mPlantHealth = mPlantMaxHealth = ThornLilyRules::Health;
}

/** 就绪时按行快照目标；近战走植物直伤，不经过重潜的直射弹减伤。 */
void ThornLily::PlantUpdate()
{
    LilyPad::PlantUpdate();
    mAttackRemaining = std::max(0.0f, mAttackRemaining
        - DeltaTime::GetDeltaTime() * GetAttackSpeedMultiplier());
    if (mAttackRemaining > 0 || !mBoard) return;
    std::vector<int> targets;
    for (int row = mRow - ThornLilyRules::RowRadius; row <= mRow + ThornLilyRules::RowRadius; ++row) {
        mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* z) {
            if (!z || !z->IsActive() || z->IsDying() || z->IsMindControlled()
                || !z->CanBeAffectedByGroundHazards()
                || std::abs(z->GetPosition().x - GetPosition().x) > ThornLilyRules::HalfWidthCells * CELL_COLLIDER_SIZE_X
                || mBoard->MineBlocksSegment(GetPosition(), z->GetPosition())) return;
            targets.push_back(z->mZombieID);
        });
    }
    if (targets.empty()) return;
    mAttackRemaining = ThornLilyRules::Interval;
    ++mStrikes;
    for (int id : targets) {
        Zombie* z = mBoard->mEntityRegistry.GetZombie(id);
        if (!z || !z->IsActive() || z->IsDying()) continue;
        const Vector impact = z->GetPosition() + Vector(0, 22);
        z->TakeDamage(ThornLilyRules::Damage, DamageSource::PLANT, false, false, false,
            PlantDamageOrigin::FromPlant(mPlantType));
        if (g_particleSystem) g_particleSystem->EmitEffect("ThornLilyStrike", impact);
    }
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FLOOP, .22f);
}

/** 攻击底座显示自身血量；置于上层数字下方，避免叠层读数覆盖。 */
void ThornLily::Draw(Graphics* g) { Plant::Draw(g); }

void ThornLily::SaveExtraData(nlohmann::json& j) const
{
    LilyPad::SaveExtraData(j);
    j["thornRemaining"] = mAttackRemaining;
    j["thornStrikes"] = mStrikes;
}

/** 恢复未完成装填，不补发根刺、音效或粒子。 */
void ThornLily::LoadExtraData(const nlohmann::json& j)
{
    LilyPad::LoadExtraData(j);
    const float remaining = j.value("thornRemaining", 0.0f);
    mAttackRemaining = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, ThornLilyRules::Interval) : 0;
    mStrikes = std::max(0, j.value("thornStrikes", 0));
}
