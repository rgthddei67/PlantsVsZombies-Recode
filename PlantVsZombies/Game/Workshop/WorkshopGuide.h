#pragma once
#include "WorkshopRoster.h"
#include <algorithm>
#include <vector>

namespace Workshop {
inline constexpr int PlantGuideBase = 100; // 图解页的植物编号起点；仅用于 UI，不写入经营存档
inline constexpr int PowerGuide = 40, DispatchGuide = 41, FocusGuide = 42; // 电网、调度、主攻预告图解页

inline int GuideUnlockWave(int topic) {
    if (topic == 0) return 0;
    if (topic == PowerGuide) return 0;
    if (topic == DispatchGuide || topic == FocusGuide) return 2;
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
    add(PowerGuide); add(DispatchGuide); add(FocusGuide);
    for (int i = 1; i < static_cast<int>(Machine::Count); ++i) add(i);
    for (int i = 0; i < static_cast<int>(Plants.size()); ++i) add(PlantGuideBase + i);
    std::stable_sort(topics.begin(), topics.end(), [](int a, int b) { return GuideUnlockWave(a) < GuideUnlockWave(b); });
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
