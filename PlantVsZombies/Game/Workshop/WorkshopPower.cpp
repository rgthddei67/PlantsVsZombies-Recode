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
    case Machine::PressureGrower: base = 2 * PressureGrowerPower; break;
    case Machine::SporeGrower: base = 3; break;
    case Machine::Cooler: case Machine::RepairAssembler: base = 4; break;
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
    if (!Valid(index) || mPhase == Phase::Dead || (RatedPower(mCells[index].type, mCells[index].level) == 0 && mCells[index].type != Machine::PowerBox)) return false;
    mCells[index].manual = running; return true;
}

/** 与生产事务使用相同的开工条件；需求在每个模拟子步开始时同时采样，避免格位先后改变分电。 */
bool World::CanProcess(int index) const {
    const auto& c = mCells[index];
    if (!Enabled(index)) return false;
    switch (c.type) {
    case Machine::Grower: case Machine::PressureGrower: case Machine::SporeGrower: case Machine::MelonGrower: {
        const float cost = c.type == Machine::SporeGrower ? SporeEnergy : c.type == Machine::MelonGrower ? MelonEnergy : GrowEnergy;
        return c.TotalStock() < Capacity(c.type, c.level) && mEnergy >= cost;
    }
    case Machine::Cooler: {
        const int recipe = c.processing ? c.recipe : ActivePlan(index);
        return c.Inventory(recipe ? Ammo::Melon : Ammo::Pea) > 0
            && c.Inventory(recipe ? Ammo::IceMelon : Ammo::Ice) < 5 && mEnergy >= (recipe ? IceMelonEnergy : CoolingEnergy);
    }
    case Machine::RepairAssembler: return c.Inventory(Ammo::Scrap) > 0 && c.Inventory(Ammo::RepairKit) < 5 && mEnergy >= RepairKitEnergy;
    case Machine::Recycler: return c.Inventory(Ammo::Scrap) > 0 && mEnergy < EnergyLimit;
    default: return false;
    }
}

bool HasSignalInput(Machine type, int port) {
    if (port == 1) return HasSelectionInput(type) || type == Machine::And || type == Machine::Or || type == Machine::Latch || type == Machine::Counter;
    return port == 0 && type != Machine::Empty && RatedPower(type) == 0 && type != Machine::Switch && type != Machine::EnemySensor;
}

bool World::CanConnectPower(int source, int target) const {
    if (!Valid(target) || mPhase == Phase::Dead) return false;
    const auto type = mCells[target].type;
    if (type != Machine::PowerBox && RatedPower(type) == 0) return false;
    if (source == -1) return true;
    if (type == Machine::PowerBox) return source == MainPowerSource;
    return Valid(source) && mCells[source].type == Machine::PowerBox;
}

bool World::ConnectPower(int source, int target) {
    if (!CanConnectPower(source, target)) return false;
    mCells[target].powerSource = source; return true;
}

float World::MachinePower(int index) const {
    if (!Valid(index) || !Enabled(index) || RatedPower(mCells[index].type) == 0) return 0;
    const int box = mCells[index].powerSource;
    if (!Valid(box) || mCells[box].type != Machine::PowerBox || mCells[box].powerSource != MainPowerSource || !Enabled(box)) return 0;
    return RatedPower(mCells[index].type, mCells[index].level) * (CanProcess(index) ? 1 : StandbyPowerRatio);
}

/** 每拍先汇总真实负载，再按优先层级分配；同级同比分配，未使用电力立即供下一层。 */
PowerNetwork World::Distribution() const {
    PowerNetwork result;
    result.total.capacity = GridCapacities[mGridLevel - 1];
    for (int i = 0; i < CellCount; ++i) if (mCells[i].type == Machine::PowerBox) {
        auto& b = result.branches[i];
        b.connected = mCells[i].powerSource == MainPowerSource;
        b.enabled = b.connected && Enabled(i);
        b.priority = ActivePlan(i) == 1;
        b.speed = b.enabled ? 1 : 0;
    }
    for (int i = 0; i < CellCount; ++i) if (RatedPower(mCells[i].type) > 0) {
        const int box = mCells[i].powerSource;
        if (box >= 0) result.branches[box].demand += MachinePower(i);
    }
    float remaining = result.total.capacity, weighted = 0;
    for (bool priority : {true, false}) {
        float demand = 0;
        for (const auto& b : result.branches) if (b.priority == priority) demand += b.demand;
        const float supply = std::min(remaining, demand);
        remaining -= supply; result.total.demand += demand;
        const float ratio = demand > 0 ? supply / demand : 1;
        for (auto& b : result.branches) if (b.priority == priority && b.enabled) {
            b.allocated = b.demand * ratio;
            b.speed = ratio * ratio;
            weighted += b.demand * b.speed;
        }
    }
    result.total.speed = result.total.demand > 0 ? weighted / result.total.demand : 1;
    return result;
}

float World::MachineSpeed(int index) const {
    if (!Valid(index) || !Enabled(index) || mCells[index].powerSource < 0) return 0;
    return Distribution().branches[mCells[index].powerSource].speed;
}

PowerInfo World::Power() const { return Distribution().total; }

PowerInfo World::PowerAfterBuild(Machine type) const {
    auto result = Power();
    result.demand += RatedPower(type);
    const float ratio = result.demand > result.capacity ? result.capacity / result.demand : 1;
    result.speed = ratio * ratio; return result;
}

bool World::ItemUnlocked(int item) const {
    constexpr std::array<int, ItemCount> waves{0, 3, 4, 5, 8, 10, 1}; // 各物资的已守住波次门槛
    return item < 0 || (item < ItemCount && mCompletedWaves >= waves[item]);
}

bool World::CanRepair(int row, float sunCost) const {
    return mPhase == Phase::Assault && row >= 0 && row < Rows && sunCost >= 0
        && mEnergy >= sunCost && mLanes[row].Inventory(Ammo::RepairKit) > 0;
}

bool World::SpendRepair(int row, float sunCost) {
    if (!CanRepair(row, sunCost)) return false;
    mEnergy -= sunCost; --mLanes[row].Inventory(Ammo::RepairKit); return true;
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
