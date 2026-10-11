#include "CobSparringTargets.h"
#include <cmath>

nlohmann::json CobSparringTargets::Observe(const nlohmann::json& entities, int rows, int viewportWidth) {
    using Json = nlohmann::json;
    Json visible = Json::array();
    for (const auto& entity : entities) {
        const auto aim = entity.find("publicCobAim");
        if (aim == entity.end() || !aim->is_object() || entity.value("fogObscured", true)
            || entity.value("isDying", false) || entity.value("bodyHealth", 0) <= 0
            || !entity.value("cobBlastTargetable", false)) continue;
        if (!aim->contains("x") || !aim->at("x").is_number_integer()
            || !aim->contains("row") || !aim->at("row").is_number_integer()) continue;
        const int x = aim->at("x"), row = aim->at("row");
        if (x < 0 || x >= viewportWidth || row < 0 || row >= rows) continue;
        // 不复制原始屏外坐标、预测或隐藏扩展字段，所有覆盖判断也使用同一公共落点。
        visible.push_back({{"id", entity.value("id", -1)}, {"type", entity.value("type", std::string{})},
            {"row", row}, {"xInt", x}, {"bodyHealth", entity.at("bodyHealth")},
            {"hasHead", entity.value("hasHead", true)}, {"fogObscured", false},
            {"cobBlastTargetable", true}});
    }
    return visible;
}
