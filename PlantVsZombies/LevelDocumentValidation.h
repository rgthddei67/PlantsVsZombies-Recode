#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace LevelDocumentValidation {
/**
 * schema 升级后、任何 Board/实体写入前校验关卡根字段和通用实体外壳。
 * 验证类型、必填项和同类实体 ID 唯一性/编号范围；不改写文档，错误返回 JSON 路径。
 * 各品种 extraData 的内部语义与资源创建异常仍由各自加载器处理，不承诺全场景回滚。
 */
bool Validate(const nlohmann::json& document, std::string& error);
}
