#pragma once

#include "ColdStorageBoundedTrials.h"
#include <nlohmann/json_fwd.hpp>

/** 有界试探历史的严格存档适配；不猜测缺失付款波次或战果。 */
namespace ColdStorageTrials {
/** 仅供关卡schema v30→v31显式迁移：为每条旧记录保留唯一已知latestPaidWave。
 * 验证旧字段和真实总账后才替换value；坏历史或提前带入paidWaves时原值完全不变。
 * 旧规则只在全体部队/返回事务结束后换latest，未知更早波次已闭合；累计收支不重算。
 * 完整验证后，将旧记录中实际已回本的未决位置false；其余账本和禁用位保留。
 * 外部边界与Decode相同，不提供猜测性默认值；失败调用方应关闭试探而非清空重开。
 */
bool MigrateLegacyV30(nlohmann::json& value,int maxPaidWave,float elapsed,
    int totalSpent,int totalProduction);

/** 完整保存禁用位与原始累计记录；不丢弃失败或未决假设。 */
nlohmann::json Encode(const State& state);

/** 检查全部字段、记录和正式总账后事务式恢复。
 * 缺少新格式字段或任一记录非法时，保留目标原记录并永久禁用本State。
 * 已禁用状态只能幂等读取相同的禁用存档，不能靠另一次加载重新获得试探权限。
 * maxPaidWave、elapsed、totalSpent、totalProduction必须来自已恢复的真实Board账本。
 */
bool Decode(const nlohmann::json& value,State& state,int maxPaidWave,float elapsed,
    int totalSpent,int totalProduction);
}
