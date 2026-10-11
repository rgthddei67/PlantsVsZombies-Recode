#pragma once
#include <nlohmann/json.hpp>

/** 炮阵陪练只消费已通过实际绘制可见性校验的目标投影，不把诊断全量实体表当作视野。 */
namespace CobSparringTargets {
/** 丢弃无见证/雾内/死亡目标，只保留可见目标的画面内落点和评分所需公开属性。 */
nlohmann::json Observe(const nlohmann::json& entities, int rows, int viewportWidth);
}
