#include "FileManager.h"
#include "Utf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

    /** 写入必须完整替换旧内容，失败必须保留旧文件且清理临时文件。 */
    void TestAtomicWrites(const std::filesystem::path& root) {
        const auto path = root / std::filesystem::path(u8"存档🌻.json");
        const auto name = Utf8::ToString(path.u8string());
        const std::string original = "old save\r\n";
        if (!FileManager::SaveFile(name, original)) throw std::runtime_error("initial save failed");
        const std::string binary("new\n\0save", 9);
        if (!FileManager::SaveBinaryFile(name, binary.data(), binary.size())
            || FileManager::LoadFileAsString(name) != binary)
            throw std::runtime_error("replacement changed bytes or retained old tail");
        const std::string large(256 * 1024, 'x');
        if (!FileManager::SaveFile(name, large) || FileManager::LoadFileAsString(name) != large)
            throw std::runtime_error("large replacement was incomplete");
        if (!FileManager::SaveFile(name, "") || std::filesystem::file_size(path) != 0)
            throw std::runtime_error("empty replacement failed");
        const nlohmann::json expected{{"name", "中文🌻"}, {"value", 42}};
        if (!FileManager::SaveJsonFile(name, expected) || FileManager::LoadJsonFile(name) != expected)
            throw std::runtime_error("JSON replacement failed");

        if (!FileManager::SaveFile(name, original)) throw std::runtime_error("restore fixture failed");
        if (FileManager::SaveBinaryFile(name, nullptr, 1)
            || FileManager::LoadFileAsString(name) != original)
            throw std::runtime_error("invalid write damaged original");
#ifdef _WIN32
        // 允许普通读写但不允许替换：旧的直接截断写会成功，原子提交应失败并保留原档。
        const HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("lock fixture failed");
        const bool saved = FileManager::SaveFile(name, "must not replace old save");
        CloseHandle(lock);
        if (saved || FileManager::LoadFileAsString(name) != original)
            throw std::runtime_error("failed replacement damaged original or reported success");
#endif
        const auto directory = root / "directory";
        std::filesystem::create_directory(directory);
        if (FileManager::SaveFile(Utf8::ToString(directory.u8string()), "invalid")
            || FileManager::SaveFile(Utf8::ToString((root / "missing" / "save.json").u8string()), "invalid"))
            throw std::runtime_error("invalid destination reported success");
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (Utf8::ToString(entry.path().filename().u8string()).find(".tmp-") != std::string::npos)
                throw std::runtime_error("temporary file leaked after save");
        }
    }
}

/** 在唯一临时目录执行文件读写回归，只清理本次创建的文件。 */
int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("pvz-file-read-" + std::to_string(suffix));
    if (!std::filesystem::create_directory(root)) return 1;
    int result = 0;
    try {
        TestFileReads(root);
        TestAtomicWrites(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::filesystem::remove_all(root);
    return result;
}
