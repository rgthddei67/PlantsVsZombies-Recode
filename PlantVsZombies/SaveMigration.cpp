#include "SaveMigration.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {
	/** 比较两个普通文件的长度和逐字节内容，供迁移后校验及重复档识别。 */
	bool FilesMatch(const std::filesystem::path& left, const std::filesystem::path& right) {
		std::error_code leftError;
		std::error_code rightError;
		const auto leftSize = std::filesystem::file_size(left, leftError);
		const auto rightSize = std::filesystem::file_size(right, rightError);
		if (leftError || rightError || leftSize != rightSize) {
			return false;
		}

		std::ifstream leftFile(left, std::ios::binary);
		std::ifstream rightFile(right, std::ios::binary);
		if (!leftFile.is_open() || !rightFile.is_open()) {
			return false;
		}

		std::array<char, 64 * 1024> leftBuffer;
		std::array<char, 64 * 1024> rightBuffer;
		std::uintmax_t remaining = leftSize;
		while (remaining > 0) {
			const auto chunkSize = static_cast<std::streamsize>(
				std::min<std::uintmax_t>(remaining, leftBuffer.size()));
			leftFile.read(leftBuffer.data(), chunkSize);
			rightFile.read(rightBuffer.data(), chunkSize);
			if (leftFile.gcount() != chunkSize || rightFile.gcount() != chunkSize ||
				std::memcmp(leftBuffer.data(), rightBuffer.data(),
					static_cast<std::size_t>(chunkSize)) != 0) {
				return false;
			}
			remaining -= static_cast<std::uintmax_t>(chunkSize);
		}
		return true;
	}

	/** 将路径绝对化并做词法归一，避免源目录与目标目录实为同一路径时误迁移。 */
	std::filesystem::path NormalizeForComparison(const std::filesystem::path& path) {
		std::error_code error;
		auto absolute = std::filesystem::absolute(path, error);
		return (error ? path : absolute).lexically_normal();
	}
}

SaveMigration::Result SaveMigration::MigrateDirectory(
	const std::filesystem::path& source, const std::filesystem::path& destination)
{
	Result result;
	if (NormalizeForComparison(source) == NormalizeForComparison(destination)) {
		return result;
	}

	std::error_code error;
	const bool sourceExists = std::filesystem::exists(source, error);
	if (error) {
		++result.errorCount;
		return result;
	}
	if (!sourceExists) {
		return result;
	}
	if (!std::filesystem::is_directory(source, error) || error) {
		++result.errorCount;
		return result;
	}

	std::filesystem::create_directories(destination, error);
	if (error || !std::filesystem::is_directory(destination, error) || error) {
		++result.errorCount;
		return result;
	}

	std::filesystem::directory_iterator iterator(source, error);
	const std::filesystem::directory_iterator end;
	while (!error && iterator != end) {
		const auto entry = *iterator;
		std::error_code entryError;
		const bool isRegularFile = entry.is_regular_file(entryError);
		const bool isSymlink = entry.is_symlink(entryError);
		if (entryError) {
			++result.errorCount;
		}
		else if (isRegularFile && !isSymlink) {
			const auto sourceFile = entry.path();
			const auto destinationFile = destination / sourceFile.filename();
			const bool destinationExists = std::filesystem::exists(destinationFile, entryError);
			if (entryError) {
				++result.errorCount;
			}
			else if (destinationExists) {
				if (std::filesystem::is_regular_file(destinationFile, entryError) &&
					!entryError && FilesMatch(sourceFile, destinationFile)) {
					std::filesystem::remove(sourceFile, entryError);
					if (entryError) {
						++result.errorCount;
					}
					else {
						++result.duplicateFileCount;
					}
				}
				else if (entryError) {
					++result.errorCount;
				}
				else {
					// 新目录优先，冲突的旧档留在原处供人工恢复，绝不静默覆盖。
					++result.conflictFileCount;
				}
			}
			else {
				const bool copied = std::filesystem::copy_file(sourceFile, destinationFile,
					std::filesystem::copy_options::none, entryError);
				if (entryError || !copied) {
					++result.errorCount;
				}
				else if (!FilesMatch(sourceFile, destinationFile)) {
					++result.errorCount;
					// 只有本次确实创建了目标文件才清理，避免并发迁移时误删另一进程的结果。
					std::error_code cleanupError;
					std::filesystem::remove(destinationFile, cleanupError);
				}
				else {
					++result.migratedFileCount;
					std::filesystem::remove(sourceFile, entryError);
					if (entryError) {
						++result.errorCount;
					}
				}
			}
		}

		iterator.increment(error);
	}
	if (error) {
		++result.errorCount;
	}

	// 仅删除已经为空的旧目录；其中若有子目录、冲突档或未知条目都会原样保留。
	error.clear();
	const bool sourceIsEmpty = std::filesystem::is_empty(source, error);
	if (error) {
		++result.errorCount;
	}
	else if (sourceIsEmpty) {
		result.sourceDirectoryRemoved = std::filesystem::remove(source, error);
		if (error) {
			++result.errorCount;
			result.sourceDirectoryRemoved = false;
		}
	}

	return result;
}


bool SaveMigration::PreserveRejectedFile(const std::filesystem::path& source,
    std::filesystem::path& backup)
{
    backup.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) return false;
    const auto directory = source.parent_path() / "rejected";
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    // 不覆盖历史诊断；同一坏档反复进入时复用已经验证的备份。
    for (unsigned index = 0; index < 10000; ++index) {
        auto name = source.filename();
        if (index != 0) name += "." + std::to_string(index);
        const auto candidate = directory / name;
        const bool exists = std::filesystem::exists(candidate, error);
        if (error) return false;
        if (exists) {
            if (!FilesMatch(source, candidate)) continue;
        } else {
            if (!std::filesystem::copy_file(source, candidate,
                std::filesystem::copy_options::none, error) || error) return false;
            if (!FilesMatch(source, candidate)) return false;
        }
        backup = candidate;
        return true;
    }
    return false;
}
