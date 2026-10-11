#pragma once
#include <vector>

namespace ColdStorageSearch {
struct Snapshot;
struct Action;

/** 为已合法、可支付的完整编队生成至多四个事件后同步/分段出生提案。
 * 只改原成员的延迟，保持类型、路线、次序和费用；设备仍在零秒生效。
 * maxDelay 为当前搜索阶段允许的游戏秒数。超出窗口的事件不假装提前落地。
 * 没有待结算技能/炮弹时，也比较活炮当前装填窗口；其他就绪炮仍照常反制。
 * 时间只是候选起点，不提交技能、不预设诱炮成功，调用方仍须完整修复及评分。
 */
std::vector<std::vector<Action>> BuildAssaultSchedules(const Snapshot& state,
    const std::vector<Action>& formation, float maxDelay);
}
