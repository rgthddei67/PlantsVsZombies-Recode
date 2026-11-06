#pragma once
#include "WorkshopRoster.h"
#include <algorithm>
#include <vector>

namespace Workshop {
inline constexpr int PlantGuideBase = 100; // 图解页的植物编号起点；仅用于 UI，不写入经营存档
inline constexpr int PowerGuide = 40, DispatchGuide = 41, FocusGuide = 42; // 电网、调度、主攻预告图解页
inline constexpr int CommissionGuide = 43; // 戴夫委托与随机蓝图奖励图解页

/** 玩家级提示记录使用稳定资源名，不依赖目录排序或机器/植物枚举值。 */
inline std::string GuideKey(int topic) {
    if (topic == 0) return "production";
    if (topic == PowerGuide) return "power";
    if (topic == DispatchGuide) return "dispatch";
    if (topic == FocusGuide) return "focus";
    if (topic == CommissionGuide) return "commissions";
    if (topic > 0 && topic < static_cast<int>(Machine::Count)) return std::string("machine:") + Describe(static_cast<Machine>(topic)).key;
    if (topic >= PlantGuideBase && topic < PlantGuideBase + static_cast<int>(Plants.size())) return std::string("plant:") + Plants[topic - PlantGuideBase].key;
    return {};
}

/** 提示记录是可选偏好；忽略损坏条目，并排序去重，不能因此拒绝玩家存档。 */
inline std::vector<std::string> ReadTutorialHistory(const nlohmann::json& document) {
    std::vector<std::string> result;
    const auto it = document.find("workshopTutorialsSeen");
    if (it == document.end() || !it->is_array()) return result;
    for (const auto& entry : *it) {
        if (!entry.is_string()) continue;
        const auto key = entry.get<std::string>();
        bool known = key == "practice:intro";
        for (int topic : {0, PowerGuide, DispatchGuide, FocusGuide, CommissionGuide}) known |= key == GuideKey(topic);
        for (int topic = 1; topic < static_cast<int>(Machine::Count); ++topic) known |= key == GuideKey(topic);
        for (int topic = PlantGuideBase; topic < PlantGuideBase + static_cast<int>(Plants.size()); ++topic) known |= key == GuideKey(topic);
        if (known) result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

inline int GuideUnlockWave(int topic) {
    if (topic == 0) return 0;
    if (topic == PowerGuide) return 0;
    if (topic == CommissionGuide) return 1;
    if (topic == DispatchGuide) return std::max(Describe(Machine::StockSensor).unlockWave, Describe(Machine::Not).unlockWave);
    if (topic == FocusGuide) return Describe(Machine::EnemySensor).unlockWave;
    if (topic > 0 && topic < static_cast<int>(Machine::Count)) return Describe(static_cast<Machine>(topic)).unlockWave;
    if (topic >= PlantGuideBase && topic < PlantGuideBase + static_cast<int>(Plants.size())) return Plants[topic - PlantGuideBase].unlockWave;
    return -1;
}

/** 按实际解锁表生成图解目录或新解锁批次；同波内容合并，未开放内容不进入目录。 */
inline std::vector<int> GuideTopics(int completedWaves, int afterWave = -1) {
    std::vector<int> topics;
    auto add = [&](int topic) {
        const int wave = GuideUnlockWave(topic);
        if (wave > afterWave && wave <= completedWaves) topics.push_back(topic);
    };
    add(0);
    add(PowerGuide); add(DispatchGuide); add(FocusGuide); add(CommissionGuide);
    for (int i = 1; i < static_cast<int>(Machine::Count); ++i) add(i);
    for (int i = 0; i < static_cast<int>(Plants.size()); ++i) add(PlantGuideBase + i);
    std::stable_sort(topics.begin(), topics.end(), [](int a, int b) { return GuideUnlockWave(a) < GuideUnlockWave(b); });
    return topics;
}

/** 蓝图图解只在当前基地实际获得后进入教程书，波次条件本身不授予奖励植物。 */
inline std::vector<int> AvailableGuideTopics(const World& world, int afterWave = -1) {
    auto topics = GuideTopics(world.CompletedWaves(), afterWave);
    std::erase_if(topics, [&world](int id) {
        if (id == static_cast<int>(Machine::PressureGrower)) return !world.Unlocked(Machine::PressureGrower);
        return id >= PlantGuideBase && !PlantUnlocked(world, Plants[id - PlantGuideBase]);
    });
    return topics;
}

/** 旧档缺少阅读进度时从当前波次开始提醒，避免一次补弹所有历史教程。
 * 此字段只控制提示；损坏时回到当前进度，不影响基地恢复和真实解锁。
 */
inline int ReadGuideSeenWave(const nlohmann::json& document, int completedWaves) {
    const auto it = document.find("guideSeenWave");
    if (it == document.end() || !it->is_number_integer()) return completedWaves;
    if (it->is_number_unsigned()) return static_cast<int>(std::min<std::uint64_t>(it->get<std::uint64_t>(), completedWaves));
    const auto wave = it->get<std::int64_t>();
    return wave < 0 ? completedWaves : static_cast<int>(std::min<std::int64_t>(wave, completedWaves));
}
}
