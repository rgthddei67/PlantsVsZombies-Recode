#include "Utf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace {
    int failures = 0;

    /** 将编码/文件系统回归报告为测试失败，即使 Release 禁用了 assert 也执行检查。 */
    void Expect(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    /** 验证 UTF-8 字节、路径、JSON 和实际文件读写，不访问玩家存档。 */
    void TestUtf8RoundTrip(const std::filesystem::path& root) {
        const std::string bytes = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x8C\xBB";
        Expect(Utf8::ToString(u8"中文🌻") == bytes, "char8_t conversion changed UTF-8 bytes");
        Expect(std::string("中文🌻") == bytes, "narrow literals are not UTF-8");
        Expect(Utf8::ToString({}).empty(), "empty view conversion failed");
        Expect(Utf8::ToPath({}).empty(), "empty path conversion failed");
        Expect(Utf8::ToString(std::u8string_view(u8"a\0b", 3)) == std::string("a\0b", 3),
            "embedded NUL was truncated");

        const auto filename = std::filesystem::path(u8"中文🌻.json");
        Expect(Utf8::ToString(filename.u8string()) == bytes + ".json", "path encoding changed");
        Expect(Utf8::ToPath(Utf8::ToString(filename.u8string())) == filename,
            "UTF-8/native path round trip failed");
        const auto path = root / filename;
        const nlohmann::json record{{"path", Utf8::ToString(filename.u8string())}};
        {
            std::ofstream file(path, std::ios::binary);
            file << record.dump();
            Expect(file.good(), "Unicode filename write failed");
        }
        std::ifstream file(path, std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(file), {}};
        const auto loaded = nlohmann::json::parse(content, nullptr, false);
        Expect(loaded.is_object() && loaded == record, "UTF-8 JSON file round trip failed");
    }
}

/** 在唯一临时目录执行编码回归，并仅清理本次创建的目录。 */
int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("pvz-utf8-" + std::to_string(suffix));
    if (!std::filesystem::create_directory(root)) return 1;
    try {
        TestUtf8RoundTrip(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ++failures;
    }
    std::filesystem::remove_all(root);
    return failures == 0 ? 0 : 1;
}
