#pragma once

#include <cstddef>
#include <filesystem>

namespace SaveMigration {
	struct Result {
		std::size_t migratedFileCount = 0;
		std::size_t duplicateFileCount = 0;
		std::size_t conflictFileCount = 0;
		std::size_t errorCount = 0;
		bool sourceDirectoryRemoved = false;
	};

	/** 逐字节备份拒绝加载的原文件到同级 rejected 目录；同内容复用，异内容另编号，失败不动源文件。 */
	bool PreserveRejectedFile(const std::filesystem::path& source, std::filesystem::path& backup);

	/**
	 * \brief 将旧存档目录中的普通文件安全迁移到新的存档目录。
	 *
	 * 迁移采用“复制、校验、再删源文件”的顺序，跨磁盘也能工作。目标已有同名文件时，
	 * 内容相同则清理重复源文件，内容不同则保留两份并报告冲突，绝不覆盖新目录中的存档。
	 */
	Result MigrateDirectory(const std::filesystem::path& source,
		const std::filesystem::path& destination);
}
