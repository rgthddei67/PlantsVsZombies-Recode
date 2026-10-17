#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string>

/** 在真实 GOM/绘制循环中检查阶段屏障和排序隔离；仅由显式 AutoTest 命令调用。 */
bool RunFrameworkProbe(const nlohmann::json& command, std::string& error);
