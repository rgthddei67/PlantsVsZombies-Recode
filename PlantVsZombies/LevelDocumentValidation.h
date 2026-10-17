#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace LevelDocumentValidation {
/**
 * schema 升级后、任何 Board/实体写入前校验关卡根字段和通用实体外壳。
 * 只验证类型与必填项，不重抽计划、不改写数值/未知字段；错误返回 JSON 路径。
 * 各品种 extraData 的内部语义与资源创建异常仍由各自加载器处理，不承诺全场景回滚。
 */
bool Validate(const nlohmann::json& document, std::string& error);
}
