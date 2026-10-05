#pragma once
#include <vector>

namespace ColdStorageSearch {
struct Snapshot;
struct Action;

/** 旧指挥官经营/进攻经验生成的完整购物车起点。
 * 只使用当前合法选项、现金及名额，不读实体、不付款、不修改评分；自由搜索可替换全部成员。
 */
std::vector<std::vector<Action>> BuildExperiencedFormations(const Snapshot& state, int actionLimit, int budget);
}
