#include "Workshop.h"
#include "WorkshopRoster.h"
#include <cmath>

namespace Workshop {
float World::PlantCooldown(const std::string& key) const {
    const auto found = mPlantCooldowns.find(key);
    return mPhase == Phase::Planning || found == mPlantCooldowns.end() ? 0 : found->second;
}
void World::StartPlantCooldown(const std::string& key, float seconds) {
    if (mPhase != Phase::Assault || !std::isfinite(seconds) || seconds <= 0 || seconds > 600
        || !FindPlant(std::string_view(key))) return;
    mPlantCooldowns[key] = seconds;
}

/** 只检查接到本格的真实库存；混料缓存不删除，冷凝机可保留下一配方原料。 */
std::string World::MaterialWarning(int i) const {
    if (!Valid(i)) return {};
    const auto& c = mCells[i];
    if (c.type != Machine::Cooler && c.type != Machine::Feeder && c.type != Machine::Store
        && c.type != Machine::Cannon && c.type != Machine::Recycler) return {};
    const int expected = c.type == Machine::Cooler ? static_cast<int>(ActivePlan(i) ? Ammo::Melon : Ammo::Pea)
        : c.type == Machine::Recycler ? static_cast<int>(Ammo::Scrap) : ActiveFilter(i);
    auto name = [](int item) -> std::string { return item == 0 ? "普通豌豆" : item == 1 ? "寒冰豌豆" : AmmoName(item); };
    // 已缓存的成品不是来料；冷凝时只对比两种原料。B 接管后的备料属于正常调度。
    if (expected >= 0 && c.inputs[1] < 0 && c.Inventory(static_cast<Ammo>(expected)) == 0) {
        for (int a = 0; a < ItemCount; ++a) {
            if (a == expected || c.Inventory(static_cast<Ammo>(a)) == 0) continue;
            if (c.type == Machine::Cooler && a != 0 && a != static_cast<int>(Ammo::Melon)) continue;
            if (c.type == Machine::Cannon) continue;
            return "库存待切换：现有" + name(a) + "，当前方案使用" + name(expected);
        }
    }
    for (int direction = 0; direction < 4; ++direction) {
        const int source = Neighbor(i, direction);
        if (source < 0) continue;
        const auto& upstream = mCells[source];
        if (!HasDirectionalOutput(upstream.type)) continue;
        for (int port = 0; port < (upstream.type == Machine::Splitter ? 2 : 1); ++port) {
            if (Neighbor(source, (upstream.direction + port) % 4) != i) continue;
            if (upstream.type == Machine::Splitter && upstream.strictSplit && port != ActivePlan(source)) continue;
            for (int a = 0; a < ItemCount; ++a) {
                const auto item = static_cast<Ammo>(a);
                if (upstream.Inventory(item) == 0) continue;
                if (upstream.type == Machine::Cooler && item != Ammo::Ice && item != Ammo::IceMelon) continue;
                if (upstream.type == Machine::Splitter && !upstream.strictSplit && upstream.filter >= 0
                    && (upstream.filter == a) != (port == 1)) continue;
                if (!Accepts(c, item)) return "来料不匹配：上游送来" + name(a) + "，本机接收" + (expected >= 0 ? name(expected) : "瓜弹");
                if (c.type == Machine::Cooler && expected != a && c.inputs[1] < 0)
                    return "配方不匹配：送来" + name(a) + "，当前加工" + name(expected) + "；可点击方案切换";
            }
        }
    }
    return {};
}
}
