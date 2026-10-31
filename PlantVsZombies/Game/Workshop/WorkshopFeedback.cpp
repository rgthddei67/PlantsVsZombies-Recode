#include "Workshop.h"
#include "WorkshopRoster.h"
#include <cmath>
#include <format>
#include <functional>

namespace Workshop {
const char* SignalName(bool signal) { return signal ? "开（高）" : "关（低）"; }
std::string PortLabel(const Cell& cell, int port) {
    if (port == 1) {
        if (HasSelectionInput(cell.type)) return "B 方案";
        if (cell.type == Machine::Counter || cell.type == Machine::Latch) return "B 清零";
        return "B 条件";
    }
    if (cell.type == Machine::StockSensor) return "A 看库存";
    if (cell.type == Machine::EnemySensor || cell.type == Machine::Switch) return "A 未使用";
    if (cell.type == Machine::Counter) return cell.counterMode == CounterMode::Signal ? "A 信号" : "A 计量";
    if (cell.type == Machine::Latch) return "A 开启";
    if (cell.type == Machine::Cannon) return "A 开火";
    if (IsLogic(cell.type) && cell.type != Machine::Timer) return "A 条件";
    return "A 启停";
}

std::vector<std::string> World::Explain(int index) const {
    if (!Valid(index) || mCells[index].type == Machine::Empty) return {};
    const auto& c = mCells[index];
    std::vector<std::string> result;
    result.push_back(mPhase == Phase::Planning ? "准备期全厂停机；线上的开/关保留上次结果，开波后重新计算。"
        : mPhase == Phase::Dead ? "基地失守，所有经营计时已停止。" : "高信号=开，低信号=关；表示条件成立与否，不表示电力大小。");
    // 有限展开，环路保留为有名元件，不把反馈电路递归展开到无限长。
    std::function<std::string(int, int)> rule = [&](int at, int depth) -> std::string {
        if (!Valid(at)) return "未连接";
        const auto& node = mCells[at];
        if (depth == 0) return std::format("{}({},{})", Describe(node.type).name, at / MaxColumns + 1, at % MaxColumns + 1);
        if (node.type == Machine::EnemySensor) return node.setting == 1 ? std::format("第{}路有僵尸", node.route + 1) : std::format("第{}路敌数≥{}", node.route + 1, node.setting);
        if (node.type == Machine::StockSensor) return std::format("{}{}库存≥{}", node.inputs[0] < 0 ? std::format("第{}路", node.route + 1) : "指定机器的", AmmoName(node.filter), node.setting);
        if (node.type == Machine::Not) {
            if (Valid(node.inputs[0]) && mCells[node.inputs[0]].type == Machine::EnemySensor) {
                const auto& enemy = mCells[node.inputs[0]];
                return enemy.setting == 1 ? std::format("第{}路没有僵尸", enemy.route + 1) : std::format("第{}路敌数<{}", enemy.route + 1, enemy.setting);
            }
            return "条件不成立（" + rule(node.inputs[0], depth - 1) + "）";
        }
        if (node.type == Machine::And || node.type == Machine::Or) return "（" + rule(node.inputs[0], depth - 1)
            + (node.type == Machine::And ? " 且 " : " 或 ") + rule(node.inputs[1], depth - 1) + "）";
        return Describe(node.type).name;
    };
    for (int port = 0; port < 2; ++port) {
        const int source = c.inputs[port];
        if (port == 1 && !HasSelectionInput(c.type) && c.type != Machine::And && c.type != Machine::Or && c.type != Machine::Latch && c.type != Machine::Counter) continue;
        if (source < 0) {
            result.push_back(PortLabel(c, port) + "：未接线。" + (port == 1 && HasSelectionInput(c.type) ? "使用手动选择的方案。"
                : port == 0 && !IsLogic(c.type) ? "默认允许运行。" : ""));
        } else {
            const bool measuring = port == 0 && (c.type == Machine::StockSensor || (c.type == Machine::Counter && c.counterMode != CounterMode::Signal));
            result.push_back(PortLabel(c, port) + " ← " + rule(source, 3) + (measuring ? "（读取数量）" : std::string("；当前信号：") + SignalName(mCells[source].signal)));
        }
    }
    if (c.type == Machine::EnemySensor) result.push_back(std::format("监视第{}路：当前{}只，门槛{}只。达到门槛才开；不能直接比较两路谁更多。", c.route + 1, mEnemyCounts[c.route], c.setting));
    if (c.type == Machine::StockSensor) {
        const auto& lane = mLanes[c.route];
        const int count = c.inputs[0] >= 0 ? (c.filter < 0 ? mCells[c.inputs[0]].TotalStock() : mCells[c.inputs[0]].Inventory(static_cast<Ammo>(c.filter)))
            : c.filter < 0 ? lane.TotalStock() : c.filter < AmmoCount ? lane.Inventory(static_cast<Ammo>(c.filter)) : 0;
        result.push_back(std::format("检测{}库存：当前{}，门槛{}；达到门槛输出开（高）。", AmmoName(c.filter), count, c.setting));
        if (c.inputs[0] < 0 && c.filter >= 0 && c.filter < AmmoCount && c.setting > lane.Quota(static_cast<Ammo>(c.filter)))
            result.push_back("提醒：门槛高于这个弹种的配额，正常补货无法补到门槛；检查弹仓配额。");
    }
    if (IsLogic(c.type)) result.push_back(std::string("当前输出：") + SignalName(c.signal) + "；每拍传播一级，开波后可能需数拍才稳定。");
    if (HasSelectionInput(c.type)) {
        result.push_back(std::format("B 关（低）→方案1；B 开（高）→方案2。当前方案{}。", ActivePlan(index) + 1));
        if (c.type == Machine::Feeder) result.push_back(std::format("当前送第{}路的{}弹药；不会因另一方案缺弹而自行切换。", ActiveRoute(index) + 1, AmmoName(ActiveFilter(index))));
    }
    if (RatedPower(c.type, c.level) > 0) result.push_back(std::format("加工电源{}，本机需求{:.1f}电力；手动开关与 A口同时允许才运行。", c.manual ? "开" : "关", MachinePower(index)));
    if (const auto warning = MaterialWarning(index); !warning.empty()) result.push_back(warning);
    if (c.type == Machine::Feeder) {
        const auto& lane = mLanes[ActiveRoute(index)];
        if (lane.TotalStock() >= MagazineCapacity) result.push_back("目标路线的总弹仓已满，等待植物消耗后再送。");
        else for (int a = 0; a < AmmoCount; ++a) {
            if ((ActiveFilter(index) >= 0 && ActiveFilter(index) != a) || (ActiveFilter(index) < 0 && c.Inventory(static_cast<Ammo>(a)) == 0)) continue;
            const auto item = static_cast<Ammo>(a);
            if (lane.Inventory(item) >= lane.Quota(item)) result.push_back(std::format("第{}路{}配额已满：{}/{}。总仓剩余空间预留给其他弹种，可点弹仓调整配额。", ActiveRoute(index) + 1, AmmoName(a), lane.Inventory(item), lane.Quota(item)));
        }
    }
    result.push_back("状态：" + Status(index));
    if (HasDirectionalOutput(c.type)) result.push_back(InspectOutput(index).message);
    return result;
}

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
