#include "Workshop.h"
#include <algorithm>

namespace Workshop {

std::vector<int> World::CargoCleanupCells(int index) const {
    if (!Valid(index) || mCells[index].type != Machine::Feeder || ActiveFilter(index) < 0) return {};
    std::vector<int> result{index};
    std::array<bool, CellCount> visited{};
    visited[index] = true;
    // 只逆向追踪物流出口，包含停机和分流器暂未选中的支路；绝不穿过仓库/加工机。
    for (size_t cursor = 0; cursor < result.size(); ++cursor) {
        for (int direction = 0; direction < 4; ++direction) {
            const int source = Neighbor(result[cursor], direction);
            if (source < 0 || visited[source]) continue;
            const auto& c = mCells[source];
            if (c.type != Machine::Belt && c.type != Machine::Splitter) continue;
            if (Neighbor(source, c.direction) != result[cursor]
                && (c.type != Machine::Splitter || Neighbor(source, (c.direction + 1) % 4) != result[cursor])) continue;
            visited[source] = true; result.push_back(source);
        }
    }
    return result;
}

std::array<int, ItemCount> World::ObsoleteCargo(int index) const {
    std::array<int, ItemCount> items{};
    for (int cell : CargoCleanupCells(index)) for (int a = 0; a < ItemCount; ++a)
        if (a != ActiveFilter(index)) items[a] += mCells[cell].Inventory(static_cast<Ammo>(a));
    return items;
}

bool World::ClearObsoleteCargo(int index) {
    if (mPhase == Phase::Dead) return false;
    bool changed = false;
    for (int cell : CargoCleanupCells(index)) for (int a = 0; a < ItemCount; ++a) {
        if (a == ActiveFilter(index)) continue;
        auto& stock = mCells[cell].Inventory(static_cast<Ammo>(a));
        changed |= stock > 0; stock = 0;
    }
    return changed;
}

float World::SolarProduction() const {
    std::array<int, MaxUpgradeLevel> counts{};
    for (int i = 0; i < CellCount; ++i)
        if (mCells[i].type == Machine::Solar && Enabled(i)) ++counts[mCells[i].level - 1];
    float rate = 0;
    int remaining = FullSolarCount;
    for (int level = MaxUpgradeLevel - 1; level >= 0; --level) {
        const int full = std::min(remaining, counts[level]); remaining -= full;
        rate += SolarRates[level] * (full + (counts[level] - full) * ExtraSolarRatio);
    }
    return rate;
}

}
