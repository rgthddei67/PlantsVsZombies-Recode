#include "FileManager.h"
#include "Utf8.h"
#include <SDL2/SDL.h>
#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {
	/** 将 FileManager 约定的 UTF-8 路径转换为本机 filesystem 路径，兼容非 ASCII 用户名。 */
	std::filesystem::path Utf8Path(const std::string& path) {
		return Utf8::ToPath(path);
	}
#if defined(__ANDROID__) || defined(__linux__)
	/** 为经典资源的大小写不敏感引用生成查询键，实际打开仍使用 manifest 的原始名称。 */
	std::string AssetPathKey(std::string path) {
		std::replace(path.begin(), path.end(), '\\', '/');
		while (path.rfind("./", 0) == 0) path.erase(0, 2);
		std::transform(path.begin(), path.end(), path.begin(),
			[](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); });
		return path;
	}

	/** 只索引大小写敏感平台的资源清单；直接读 SDL，避免 OpenRead 首次初始化时递归。 */
	const std::unordered_map<std::string, std::string>& AssetPaths() {
		static const auto paths = [] {
			std::unordered_map<std::string, std::string> result;
			SDL_RWops* rw = SDL_RWFromFile("resources/manifest.txt", "rb");
			if (!rw) return result;
			size_t size = 0;
			void* bytes = SDL_LoadFile_RW(rw, &size, 1);
			if (!bytes) return result;
			std::istringstream stream(std::string(static_cast<const char*>(bytes), size));
			SDL_free(bytes);
			std::string path;
			while (std::getline(stream, path)) {
				if (!path.empty() && path.back() == '\r') path.pop_back();
				while (path.rfind("./", 0) == 0) path.erase(0, 2);
				if (path.rfind("resources/", 0) != 0) continue;
				auto [it, inserted] = result.emplace(AssetPathKey(path), path);
				// 有歧义时只允许原名精确打开，不任意选择其中一个资源。
				if (!inserted && it->second != path) it->second.clear();
			}
			return result;
		}();
		return paths;
	}
#endif
}

SDL_RWops* FileManager::OpenRead(const std::string& path) {
#if defined(__ANDROID__) || defined(__linux__)
	// APK 和 Linux 统一使用正斜杠相对名称；保留绝对路径语义。
	std::string normalized = path;
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	while (normalized.rfind("./", 0) == 0) normalized.erase(0, 2);
	if (SDL_RWops* rw = SDL_RWFromFile(normalized.c_str(), "rb")) return rw;
	// 原版 reanim 的 IMAGE_REANIM_* 常由大写键拼出路径，APK / Linux 路径则区分大小写。
	// 仅对清单内资源补充解析；绝对存档路径和不存在的资源仍正常报告失败。
	if (AssetPathKey(normalized).rfind("resources/", 0) == 0) {
		const auto& paths = AssetPaths();
		const auto found = paths.find(AssetPathKey(normalized));
		if (found != paths.end() && !found->second.empty() && found->second != normalized)
			return SDL_RWFromFile(found->second.c_str(), "rb");
	}
	return nullptr;
#else
	return SDL_RWFromFile(path.c_str(), "rb");
#endif
}

bool FileManager::FileExists(const std::string& path) {
	std::ifstream file(Utf8Path(path));
	return file.good();
}

std::string FileManager::LoadFileAsString(const std::string& path) {
	// 复用二进制读取（同样走 SDL_RWops）；空文件与打开失败都返回 ""，与原 ifstream 行为一致。
	std::vector<char> bytes = LoadFileAsBinary(path);
	return std::string(bytes.begin(), bytes.end());
}

std::vector<char> FileManager::LoadFileAsBinary(const std::string& path) {
	// SDL_RWFromFile：相对路径在 Android 自动读 APK assets / 桌面读 CWD；绝对路径走真实文件系统。
	SDL_RWops* rw = OpenRead(path);
	if (!rw) {
		// LogError("Failed to open file: " + path);
		return {};
	}

	Sint64 size = SDL_RWsize(rw);
	if (size < 0) {
		LogError("Failed to get file size: " + path);
		SDL_RWclose(rw);
		return {};
	}

	std::vector<char> buffer(static_cast<size_t>(size));
	Sint64 readTotal = 0;
	if (size > 0) {
		readTotal = SDL_RWread(rw, buffer.data(), 1, static_cast<size_t>(size));
	}
	SDL_RWclose(rw);

	if (readTotal != size) {
		LogError("Failed to read file: " + path);
		return {};
	}
	return buffer;
}

bool FileManager::SaveFile(const std::string& path, const std::string& content) {
	std::ofstream file(Utf8Path(path));
	if (!file.is_open()) {
		LogError("Failed to create file: " + path);
		return false;
	}

	file << content;
	return !file.fail();
}

bool FileManager::SaveBinaryFile(const std::string& path, const void* data, size_t size) {
	std::ofstream file(Utf8Path(path), std::ios::binary);
	if (!file.is_open()) {
		LogError("Failed to create binary file: " + path);
		return false;
	}

	file.write(static_cast<const char*>(data), size);
	return !file.fail();
}

bool FileManager::AppendToFile(const std::string& path, const std::string& content) {
	std::ofstream file(Utf8Path(path), std::ios::app);
	if (!file.is_open()) {
		LogError("Failed to open file for appending: " + path);
		return false;
	}

	file << content;
	return !file.fail();
}

pugi::xml_document FileManager::LoadXMLFile(const std::string& path) {
	pugi::xml_document doc;
	LoadXMLFile(path, doc);
	return doc;
}

bool FileManager::LoadXMLFile(const std::string& path, pugi::xml_document& doc) {
	std::string content = LoadFileAsString(path);
	if (content.empty()) {
		return false;
	}

	pugi::xml_parse_result result = doc.load_string(content.c_str());
	if (!result) {
		LogError("Failed to parse XML file: " + path + ", error: " + result.description());
		return false;
	}

	return true;
}

bool FileManager::SaveXMLFile(const std::string& path, const pugi::xml_document& doc) {
	std::stringstream ss;
	doc.save(ss);
	return SaveFile(path, ss.str());
}

bool FileManager::SaveXMLFile(const std::string& path, const pugi::xml_node& node) {
	std::stringstream ss;
	node.print(ss);
	return SaveFile(path, ss.str());
}

nlohmann::json FileManager::LoadJsonFile(const std::string& path) {
	nlohmann::json json;
	LoadJsonFile(path, json);
	return json;
}

bool FileManager::LoadJsonFile(const std::string& path, nlohmann::json& json) {
	std::string content = LoadFileAsString(path);
	if (content.empty()) {
		return false;
	}

	try {
		json = nlohmann::json::parse(content);
		return true;
	}
	catch (const nlohmann::json::parse_error& e) {
		LogError("Failed to parse JSON file: " + path + ", error: " + e.what());
		return false;
	}
}

bool FileManager::SaveJsonFile(const std::string& path, const nlohmann::json& json) {
	try {
		std::string content = json.dump(4); // 使用4空格缩进
		return SaveFile(path, content);
	}
	catch (const nlohmann::json::exception& e) {
		LogError("Failed to serialize JSON for file: " + path + ", error: " + e.what());
		return false;
	}
}

size_t FileManager::GetFileSize(const std::string& path) {
	std::ifstream file(Utf8Path(path), std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		return 0;
	}
	return file.tellg();
}

bool FileManager::CreateDirectory(const std::string& path) {
	try {
		if (std::filesystem::create_directories(Utf8Path(path))) {
			return true;
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		LogError("Failed to create directory: " + path + ", error: " + e.what());
	}
	return false;
}

bool FileManager::IsDirectory(const std::string& path) {
	try {
		return std::filesystem::is_directory(Utf8Path(path));
	}
	catch (const std::filesystem::filesystem_error&) {
		return false;
	}
}

std::vector<std::string> FileManager::GetFilesInDirectory(const std::string& directory,
	const std::string& extension) {
	std::vector<std::string> files;

	try {
		for (const auto& entry : std::filesystem::directory_iterator(Utf8Path(directory))) {
			if (entry.is_regular_file()) {
				std::string filename = Utf8::ToString(entry.path().u8string());

				// 如果指定了扩展名，则只添加匹配扩展名的文件
				if (extension.empty() || GetFileExtension(filename) == extension) {
					files.push_back(filename);
				}
			}
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		LogError("Failed to list files in directory: " + directory + ", error: " + e.what());
	}

	return files;
}

std::vector<std::string> FileManager::ListResourceFiles(const std::string& directory,
	const std::string& extension) {
	// 读构建期烘焙的清单（经 LoadFileAsString = SDL_RWops，APK 可读）；
	// 资源加载在启动期单线程，故首次解析后用函数局部 static 缓存整份清单
	//（C++11 线程安全静态初始化）。每个进程读一次，永远与本次构建产物一致。
	static const std::vector<std::string> manifest = []() {
		std::vector<std::string> lines;
		std::string content = LoadFileAsString("./resources/manifest.txt");
		std::stringstream ss(content);
		std::string line;
		while (std::getline(ss, line)) {
			// 去掉行尾的 \r（CRLF）与首尾空白
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
				line.back() == ' ' || line.back() == '\t')) {
				line.pop_back();
			}
			if (!line.empty()) {
				lines.push_back(line);
			}
		}
		return lines;
	}();

	// 归一查询目录：去尾部斜杠，使 "./a/b/" 与 "./a/b" 等价。
	std::string dir = directory;
	while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
		dir.pop_back();
	}

	// 小写化目标扩展名，便于大小写不敏感比较（空 = 不过滤）。
	std::string extLower = extension;
	std::transform(extLower.begin(), extLower.end(), extLower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	std::vector<std::string> files;

	if (!manifest.empty()) {
		for (const std::string& entry : manifest) {
			// 非递归：父目录字符串须与 dir 精确相等（匹配 directory_iterator 语义）。
			if (GetDirectory(entry) != dir) {
				continue;
			}
			if (!extLower.empty()) {
				std::string fileExt = GetFileExtension(entry);
				std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (fileExt != extLower) {
					continue;
				}
			}
			// 行内容即与 directory_iterator 逐字一致的全路径（如 ./resources/.../foo.png）。
			files.push_back(entry);
		}
		return files;
	}

	// 清单缺失：桌面回退 std::filesystem（保证桌面永不退化）；Android 必须有清单。
#if defined(__ANDROID__)
	LogError("资源清单缺失且 Android 无文件系统回退: ./resources/manifest.txt");
	return files;
#else
	return GetFilesInDirectory(directory, extension);
#endif
}

bool FileManager::DeleteFile(const std::string& path) {
	try {
		return std::filesystem::remove(Utf8Path(path));
	}
	catch (const std::filesystem::filesystem_error& e) {
		LogError("Failed to delete file: " + path + ", error: " + e.what());
		return false;
	}
}

void FileManager::LogError(const std::string& message) {
	LOG_ERROR("FileManager") << message;
}
