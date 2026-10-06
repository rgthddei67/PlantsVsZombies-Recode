#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Utf8 {
    /** 将 char8_t UTF-8 代码单元复制为 SDL/JSON/日志使用的 char 字节，不经系统代码页转换；保留内嵌 NUL。 */
    inline std::string ToString(std::u8string_view text) {
        if (text.empty()) return {};
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    /** 从 UTF-8 char 字节构造本机路径，替代已弃用的 u8path；Windows 不经过本地代码页。 */
    inline std::filesystem::path ToPath(std::string_view text) {
        if (text.empty()) return {};
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
}
