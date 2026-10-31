#include "Board.h"
#include "../Workshop/Workshop.h"
#include "../Zombie/Zombie.h"
#include "../Bullet/Bullet.h"
#include "../AudioSystem.h"
#include "DeltaTime.h"
#include "ResourceKeys.h"
#include "GameApp.h"
#include <limits>

namespace {
constexpr float kFlightSeconds = 1.2f; // 沿用原版瓜弹的飞行时间，游戏秒
constexpr float kArcHeight = 210; // 与原生西瓜投手相同的抛物线拱高，世界像素
constexpr float kAimHeight = .35f; // 瞄准目标碰撞箱上部，与原生瓜弹一致
constexpr float kLaunchLeft = -45, kLaunchHeight = -55; // 相对本行第一格中心的发射口偏移，世界像素
constexpr float kLaunchVolume = .22f; // 三连射单发音量，避免多个炮台叠音过响
}

Zombie* Board::FindWorkshopCannonTarget(int row) {
    Zombie* target = nullptr;
    float nearest = std::numeric_limits<float>::max();
    const float launchX = GetCellCenterPosition(row, 0).x + kLaunchLeft;
    mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
        if (!zombie || !zombie->IsActive() || zombie->IsDying() || !zombie->HasHead()
            || zombie->IsMindControlled() || !zombie->CanBeTargetedByProjectile(false)) return;
        const auto* collider = zombie->GetColliderComponent();
        if (!collider) return;
        const auto bounds = collider->GetBoundingBox();
        const float center = bounds.x + bounds.w * .5f;
        if (bounds.x + bounds.w < launchX || center > SCENE_WIDTH || center >= nearest) return;
        target = zombie; nearest = center;
    });
    return target;
}

bool Board::FireWorkshopCannon(int index) {
    if (!mWorkshop || index < 0 || index >= Workshop::CellCount
        || mWorkshop->Cells()[index].type != Workshop::Machine::Cannon) return false;
    return FindWorkshopCannonTarget(mWorkshop->ActiveRoute(index)) && mWorkshop->BeginCannonVolley(index, true);
}

/** 工坊只提供发射事务，索敌、原版弹丸、溅射、减速和在途存档继续由 Board 战斗体系负责。 */
void Board::UpdateWorkshopCannons() {
    if (!mWorkshop || mWorkshop->State() != Workshop::Phase::Assault || DeltaTime::GetDeltaTime() <= 0) return;
    for (int i = 0; i < Workshop::CellCount; ++i) {
        const auto& c = mWorkshop->Cells()[i];
        if (c.type != Workshop::Machine::Cannon) continue;
        if (c.volleyRemaining == 0 && FindWorkshopCannonTarget(mWorkshop->ActiveRoute(i))) mWorkshop->BeginCannonVolley(i);
        while (mWorkshop->CannonShotReady(i)) {
            const int row = c.volleyRoute;
            const auto launch = GetCellCenterPosition(row, 0) + Vector(kLaunchLeft, kLaunchHeight);
            Vector landing(SCENE_WIDTH + 20.0f, GetRowCenterYAtX(row, SCENE_WIDTH) - 20);
            if (const auto* target = FindWorkshopCannonTarget(row)) {
                const auto bounds = target->GetColliderComponent()->GetBoundingBox();
                landing.x = target->GetTargetLeadX(kFlightSeconds);
                landing.y = bounds.y + bounds.h * kAimHeight;
            }
            const bool ice = c.volleyAmmo == Workshop::Ammo::IceMelon;
            auto* bullet = CreatePlantBullet(ice ? BulletType::BULLET_WINTERMELON : BulletType::BULLET_MELON,
                row, launch, ice ? PlantType::PLANT_WINTERMELON : PlantType::PLANT_MELONPULT);
            if (!bullet) break;
            bullet->ConfigureLobbedMotion(landing, kFlightSeconds, kArcHeight);
            AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, kLaunchVolume);
            if (ice) AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES, kLaunchVolume);
            mWorkshop->CommitCannonShot(i);
            RecordWorkshopPlantShot(ice ? PlantType::PLANT_WINTERMELON : PlantType::PLANT_MELONPULT);
        }
    }
}
