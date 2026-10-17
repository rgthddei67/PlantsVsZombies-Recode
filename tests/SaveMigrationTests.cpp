#include "SaveMigration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int gFailureCount = 0;

	void Expect(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++gFailureCount;
		}
	}

	void WriteFile(const std::filesystem::path& path, const std::string& content) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file << content;
	}

	std::string ReadFile(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}

	void TestMovesFilesAndRemovesEmptySource(const std::filesystem::path& root) {
		const auto source = root / "move" / "saves";
		const auto destination = root / "move" / "central";
		WriteFile(source / "PlayerInfo.json", "player");
		WriteFile(source / "level1001_data.json", "level");

		const auto result = SaveMigration::MigrateDirectory(source, destination);
		Expect(result.migratedFileCount == 2, "两个旧存档均应迁移");
		Expect(result.errorCount == 0, "正常迁移不应报错");
		Expect(result.sourceDirectoryRemoved, "迁移完成后应删除空的旧 saves 目录");
		Expect(ReadFile(destination / "PlayerInfo.json") == "player", "玩家存档内容必须保持不变");
		Expect(ReadFile(destination / "level1001_data.json") == "level", "关卡存档内容必须保持不变");
	}

	void TestPreservesConflictingFiles(const std::filesystem::path& root) {
		const auto source = root / "conflict" / "saves";
		const auto destination = root / "conflict" / "central";
		WriteFile(source / "PlayerInfo.json", "legacy");
		WriteFile(destination / "PlayerInfo.json", "central");

		const auto result = SaveMigration::MigrateDirectory(source, destination);
		Expect(result.conflictFileCount == 1, "不同内容的同名存档应报告冲突");
		Expect(result.errorCount == 0, "保留冲突档是正常结果，不应额外报告迁移错误");
		Expect(ReadFile(source / "PlayerInfo.json") == "legacy", "冲突的旧存档必须保留");
		Expect(ReadFile(destination / "PlayerInfo.json") == "central", "中央存档不得被覆盖");
	}

	void TestRemovesMatchingDuplicate(const std::filesystem::path& root) {
		const auto source = root / "duplicate" / "saves";
		const auto destination = root / "duplicate" / "central";
		WriteFile(source / "PlayerInfo.json", "same");
		WriteFile(destination / "PlayerInfo.json", "same");

		const auto result = SaveMigration::MigrateDirectory(source, destination);
		Expect(result.duplicateFileCount == 1, "相同内容的同名存档应识别为重复档");
		Expect(result.sourceDirectoryRemoved, "清理重复档后应删除空旧目录");
		Expect(ReadFile(destination / "PlayerInfo.json") == "same", "重复档清理不得改写目标内容");
	}

	void TestKeepsSourceWhenDestinationCannotBeCreated(const std::filesystem::path& root) {
		const auto source = root / "blocked" / "saves";
		const auto destination = root / "blocked" / "central";
		WriteFile(source / "PlayerInfo.json", "player");
		WriteFile(destination, "not a directory");

		const auto result = SaveMigration::MigrateDirectory(source, destination);
		Expect(result.errorCount > 0, "目标目录不可创建时应报告错误");
		Expect(ReadFile(source / "PlayerInfo.json") == "player", "迁移失败时源存档必须保留");
	}
    /** 失败存档必须原样保留，包括非 JSON 字节；同名不同内容绝不覆盖历史备份。 */
    void TestRejectedBackup(const std::filesystem::path& root) {
        const auto source = root / "rejected-test" / std::filesystem::path(u8"关卡.json");
        const std::string damaged("{broken\0json", 12);
        WriteFile(source, damaged);
        std::filesystem::path first, repeated, second;
        Expect(SaveMigration::PreserveRejectedFile(source, first), "拒绝档应能备份");
        Expect(ReadFile(first) == damaged && ReadFile(source) == damaged, "备份应保留原始字节和源文件");
        Expect(SaveMigration::PreserveRejectedFile(source, repeated) && first == repeated,
            "重复失败不得无限复制同一个坏档");
        WriteFile(source, "changed");
        Expect(SaveMigration::PreserveRejectedFile(source, second) && second != first,
            "不同坏档应另编号");
        Expect(ReadFile(first) == damaged && ReadFile(second) == "changed", "历史备份不得覆盖");
        const auto blocked = root / "blocked-backup" / "level.json";
        WriteFile(blocked, "keep");
        WriteFile(blocked.parent_path() / "rejected", "not a directory");
        Expect(!SaveMigration::PreserveRejectedFile(blocked, second) && second.empty(),
            "目标不可写时必须返回失败");
        Expect(ReadFile(blocked) == "keep", "备份失败不得删除或修改源文件");
        Expect(!SaveMigration::PreserveRejectedFile(root / "missing.json", second),
            "源文件缺失不得报告已备份");
    }

}

int main() {
	const auto unique = std::to_string(
		std::chrono::high_resolution_clock::now().time_since_epoch().count());
	const auto root = std::filesystem::temp_directory_path() /
		("pvz-save-migration-tests-" + unique);

    TestRejectedBackup(root);
	TestMovesFilesAndRemovesEmptySource(root);
	TestPreservesConflictingFiles(root);
	TestRemovesMatchingDuplicate(root);
	TestKeepsSourceWhenDestinationCannotBeCreated(root);

	std::error_code cleanupError;
	std::filesystem::remove_all(root, cleanupError);
	if (cleanupError) {
		std::cerr << "FAILED: 无法清理测试目录: " << cleanupError.message() << '\n';
		++gFailureCount;
	}

	if (gFailureCount == 0) {
		std::cout << "SaveMigrationTests passed\n";
	}
	return gFailureCount == 0 ? 0 : 1;
}
