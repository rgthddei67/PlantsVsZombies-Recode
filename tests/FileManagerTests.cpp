#include "FileManager.h"
#include "Utf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
    /** 使用独立文件写入验证正式 SDL 读取路径；比较完整字节，避免 NUL 截断被掩盖。 */
    void CheckRead(const std::filesystem::path& path, const std::string& expected) {
        {
            std::ofstream file(path, std::ios::binary);
            file.write(expected.data(), static_cast<std::streamsize>(expected.size()));
            if (!file) throw std::runtime_error("fixture write failed");
        }
        const auto actual = FileManager::LoadFileAsString(Utf8::ToString(path.u8string()));
        if (actual != expected) throw std::runtime_error("file contents changed");
        // Windows 上立即删除还可检查读取句柄是否已释放。
        if (!std::filesystem::remove(path)) throw std::runtime_error("fixture removal failed");
    }

    /** 覆盖空文件、小字符串、堆缓冲区、UTF-8 路径和读取失败，不访问玩家存档。 */
    void TestFileReads(const std::filesystem::path& root) {
        CheckRead(root / "empty.txt", "");
        CheckRead(root / "small.txt", "small text\r\n");
        CheckRead(root / "embedded-nul.bin", std::string("a\0b\0", 4));
        CheckRead(root / std::filesystem::path(u8"中文🌻.txt"), "中文🌻\r\n");

        std::string large(256 * 1024, '\0');
        for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>(i % 128);
        CheckRead(root / "large.bin", large);

        const auto missing = Utf8::ToString((root / "missing.txt").u8string());
        if (!FileManager::LoadFileAsString(missing).empty())
            throw std::runtime_error("missing file must return empty content");
    }
}

/** 在唯一临时目录执行文件读取回归，只清理本次创建的文件。 */
int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("pvz-file-read-" + std::to_string(suffix));
    if (!std::filesystem::create_directory(root)) return 1;
    int result = 0;
    try {
        TestFileReads(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(root);
    return result;
}
