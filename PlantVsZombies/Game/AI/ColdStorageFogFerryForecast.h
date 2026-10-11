#pragma once
#include <array>
#include <vector>

namespace ColdStorageSearch {
struct Snapshot;
struct Unit;
struct ConstructionStats;
/** 推进所有存活运输者；只搬运已出生的合法乘客，不创建单位、不扣冰。
 * illumination 为当前存活光源的逐格照明，fogLeft 为天气雾边界。
 * 本步停步/被运输标记由调用方消费，已脱手弹道不受影响。
 */
void AdvanceFogFerries(const Snapshot& state, float time, float step, int fogLeft,
    const std::array<float,54>& illumination, std::vector<Unit>& units, ConstructionStats& stats);
}
