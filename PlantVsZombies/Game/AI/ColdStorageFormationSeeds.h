#pragma once
#include <vector>

namespace ColdStorageSearch {
struct Snapshot;
struct Action;

/** 旧指挥官经营/进攻经验生成的完整购物车起点。
 * 只使用当前合法选项、现金及名额，不读实体、不付款、不修改评分；自由搜索可替换全部成员。
 * 抗灰轻装对照不设炮数门槛；就绪/装填/在途炮击、补阵和后续资金仍由同一完整评价裁决。
 * 压力护卫按保护能力独立选角，基础与攻坚混编只使用当前卡池，不因固定配方免除收益审核。
 * 投篮掩护比较多路同步/错峰完整队伍，避免单路亏损使整套协同永远没有比较机会。
 */
std::vector<std::vector<Action>> BuildExperiencedFormations(const Snapshot& state, int actionLimit, int budget);
}
