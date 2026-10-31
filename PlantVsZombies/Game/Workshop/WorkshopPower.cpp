#include "Workshop.h"
#include <algorithm>

namespace Workshop {
namespace {
constexpr std::array<float, 3> kPowerLevels{1, 1.2f, 1.4f}; // 升级功率倍率，增长低于产速，奖励升级效率
}

float RatedPower(Machine type, int level) {
    float base = 0;
    switch (type) {
    case Machine::Grower: case Machine::Recycler: base = 2; break;
    case Machine::SporeGrower: base = 3; break;
    case Machine::Cooler: base = 4; break;
    case Machine::MelonGrower: base = 6; break;
    default: break;
    }
    return base * kPowerLevels.at(level - 1);
}

int UnlockedGridLevel(int completedWaves) {
    int level = 1;
    while (level < static_cast<int>(GridUnlockWaves.size()) && completedWaves >= GridUnlockWaves[level]) ++level;
    return level;
}

bool World::UpgradeGrid() {
    if (mPhase == Phase::Dead || mGridLevel >= UnlockedGridLevel(mCompletedWaves) || !SpendEnergy(static_cast<float>(GridCosts[mGridLevel]))) return false;
    ++mGridLevel; return true;
}

bool World::SetMachineRunning(int index, bool running) {
    if (!Valid(index) || mPhase == Phase::Dead || RatedPower(mCells[index].type, mCells[index].level) == 0) return false;
    mCells[index].manual = running; return true;
}

/** 与生产事务使用相同的开工条件；需求在每个模拟子步开始时同时采样，避免格位先后改变分电。 */
bool World::CanProcess(int index) const {
    const auto& c = mCells[index];
    if (!Enabled(index)) return false;
    switch (c.type) {
    case Machine::Grower: case Machine::SporeGrower: case Machine::MelonGrower: {
        const float cost = c.type == Machine::Grower ? GrowEnergy : c.type == Machine::SporeGrower ? SporeEnergy : MelonEnergy;
        return c.TotalStock() < Capacity(c.type, c.level) && mEnergy >= cost;
    }
    case Machine::Cooler: {
        const int recipe = c.processing ? c.recipe : ActivePlan(index);
        return c.Inventory(recipe ? Ammo::Melon : Ammo::Pea) > 0
            && c.Inventory(recipe ? Ammo::IceMelon : Ammo::Ice) < 5 && mEnergy >= (recipe ? IceMelonEnergy : CoolingEnergy);
    }
    case Machine::Recycler: return c.Inventory(Ammo::Scrap) > 0 && mEnergy < EnergyLimit;
    default: return false;
    }
}

float World::MachinePower(int index) const {
    if (!Valid(index) || !Enabled(index)) return 0;
    return RatedPower(mCells[index].type, mCells[index].level) * (CanProcess(index) ? 1 : StandbyPowerRatio);
}

PowerInfo World::Power() const {
    PowerInfo result{GridCapacities[mGridLevel - 1]};
    for (int i = 0; i < CellCount; ++i) result.demand += MachinePower(i);
    result.speed = mLegacyAssault || result.demand <= result.capacity ? 1 : result.capacity / result.demand;
    return result;
}

PowerInfo World::PowerAfterBuild(Machine type) const {
    auto result = Power();
    // 新增设备以额定满负荷预估，避免缺料时看似不占电、开始加工后突然降速。
    result.demand += RatedPower(type);
    result.speed = mLegacyAssault || result.demand <= result.capacity ? 1 : result.capacity / result.demand;
    return result;
}

int World::StageSpawnEnd(int stage) const {
    const int total = 5 + std::min(100, mCompletedWaves * 2);
    return (total * (std::clamp(stage, 0, 2) + 1) + 2) / 3;
}

int World::AssaultStage() const {
    return mSpawned < StageSpawnEnd(0) ? 0 : mSpawned < StageSpawnEnd(1) ? 1 : 2;
}

int World::FocusRoute(int stage) const {
    if (mLegacyAssault || mCompletedWaves < 2) return -1;
    // 每波和每段换主攻路，预告与出生共用公式；不消费随机数，存档/重试不会改预告。
    return (mCompletedWaves * 2 + std::clamp(stage, 0, 2) * 2) % Rows;
}
}
