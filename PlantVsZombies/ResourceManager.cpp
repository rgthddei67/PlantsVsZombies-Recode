#include "ResourceManager.h"
#include "./Game/Plant/GameDataManager.h"
#include "./Renderer/RenderBackend.h"
#include "Logger.h"
#include "FileManager.h"
#include "GameApp.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

ResourceManager* ResourceManager::instance = nullptr;

namespace {
	/** AutoTest 只读保留上传前的真实 alpha；不改变源表面、GPU 纹理或普通游戏分配。 */
	void CaptureAutoTestAlpha(Texture& texture, SDL_Surface* surface) {
		constexpr std::uint64_t kMaximumAlphaPixels = 4 * 1024 * 1024; // 单图诊断遮罩像素上限；超大图不提供证据
		if (!GameAPP::mAutoTestMode || !surface || surface->w <= 0 || surface->h <= 0
			|| surface->format->format != SDL_PIXELFORMAT_ABGR8888) return;
		const auto count = static_cast<std::uint64_t>(surface->w) * surface->h;
		if (count > kMaximumAlphaPixels || SDL_LockSurface(surface) != 0) return;
		texture.autoTestAlpha.resize(static_cast<std::size_t>(count));
		for (int y = 0; y < surface->h; ++y) {
			const auto* row = reinterpret_cast<const Uint32*>(
				static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
			for (int x = 0; x < surface->w; ++x)
				texture.autoTestAlpha[static_cast<std::size_t>(y) * surface->w + x]
					= static_cast<std::uint8_t>((row[x] & surface->format->Amask) >> surface->format->Ashift);
		}
		SDL_UnlockSurface(surface);
	}

	// worker 线程可安全调用：只做 打开→解码→转 ABGR8888，不碰共享状态、不打日志。
	// 失败时 surface==nullptr，error 携带与旧 LoadTexture 逐字一致的日志文案，由主线程统一输出。
	struct DecodedImage {
		SDL_Surface* surface = nullptr;
		std::string error;
	};

	/**
	 * @brief 为原版“JPG 彩色图 + 同名 _.png 灰度遮罩”恢复透明通道。
	 * @details 遮罩不存在是普通 JPG 的合法路径；遮罩存在但损坏或尺寸不符才算加载失败。
	 */
	bool ApplyLegacyJpegAlphaMask(SDL_Surface* color,
		const std::string& filepath, std::string& error)
	{
		const std::size_t dot = filepath.find_last_of('.');
		if (!color || dot == std::string::npos) return true;
		std::string extension = filepath.substr(dot);
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
		if (extension != ".jpg" && extension != ".jpeg") return true;

		const std::string maskPath = filepath.substr(0, dot) + "_.png";
		SDL_RWops* maskRw = FileManager::OpenRead(maskPath);
		if (!maskRw) return true;

		SDL_Surface* loadedMask = IMG_Load_RW(maskRw, 1);
		if (!loadedMask) {
			error = "LoadTexture 无法加载 JPG 透明遮罩: " + maskPath
				+ " - " + IMG_GetError();
			return false;
		}
		SDL_Surface* mask = SDL_ConvertSurfaceFormat(
			loadedMask, SDL_PIXELFORMAT_ABGR8888, 0);
		SDL_FreeSurface(loadedMask);
		if (!mask) {
			error = "LoadTexture 无法转换 JPG 透明遮罩: " + maskPath;
			return false;
		}
		if (mask->w != color->w || mask->h != color->h) {
			error = "LoadTexture JPG 透明遮罩尺寸不匹配: " + filepath
				+ " vs " + maskPath;
			SDL_FreeSurface(mask);
			return false;
		}

		if (SDL_LockSurface(color) != 0) {
			error = "LoadTexture 无法锁定 JPG 表面: " + filepath;
			SDL_FreeSurface(mask);
			return false;
		}
		if (SDL_LockSurface(mask) != 0) {
			error = "LoadTexture 无法锁定 JPG 透明遮罩: " + maskPath;
			SDL_UnlockSurface(color);
			SDL_FreeSurface(mask);
			return false;
		}

		for (int y = 0; y < color->h; ++y) {
			auto* colorRow = reinterpret_cast<Uint32*>(
				static_cast<Uint8*>(color->pixels) + y * color->pitch);
			auto* maskRow = reinterpret_cast<Uint32*>(
				static_cast<Uint8*>(mask->pixels) + y * mask->pitch);
			for (int x = 0; x < color->w; ++x) {
				Uint8 colorR = 0, colorG = 0, colorB = 0, colorA = 0;
				Uint8 maskR = 0, maskG = 0, maskB = 0, maskA = 0;
				SDL_GetRGBA(colorRow[x], color->format,
					&colorR, &colorG, &colorB, &colorA);
				SDL_GetRGBA(maskRow[x], mask->format,
					&maskR, &maskG, &maskB, &maskA);
				const unsigned int luminance = (static_cast<unsigned int>(maskR)
					+ maskG + maskB) / 3U;
				const Uint8 alpha = static_cast<Uint8>(luminance * maskA / 255U);
				colorRow[x] = SDL_MapRGBA(color->format,
					colorR, colorG, colorB, alpha);
			}
		}
		SDL_UnlockSurface(mask);
		SDL_UnlockSurface(color);
		SDL_FreeSurface(mask);
		return true;
	}

	DecodedImage DecodeImageFile(const std::string& filepath) {
		DecodedImage out;
		SDL_RWops* rw = FileManager::OpenRead(filepath);
		if (!rw) {
			out.error = "LoadTexture 无法打开图片: " + filepath;
			return out;
		}
		SDL_Surface* surface = IMG_Load_RW(rw, 1);   // freesrc=1
		if (!surface) {
			out.error = "LoadTexture 无法加载图片: " + filepath + " - " + IMG_GetError();
			return out;
		}
		// 统一转换为 RGBA，解决对齐与源格式不确定问题
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
		SDL_FreeSurface(surface);
		if (!converted) {
			out.error = "LoadTexture 无法转换图片格式: " + filepath;
			return out;
		}
		if (!ApplyLegacyJpegAlphaMask(converted, filepath, out.error)) {
			SDL_FreeSurface(converted);
			return out;
		}
		out.surface = converted;
		return out;
	}
}

ResourceManager& ResourceManager::GetInstance() {
	if (!instance) {
		instance = new ResourceManager();
	}
	return *instance;
}

void ResourceManager::ReleaseInstance() {
	if (instance) {
		instance->UnloadAll();
		delete instance;
		instance = nullptr;
	}
}

bool ResourceManager::Initialize(const std::string& configPath) {
	return configReader.LoadConfig(configPath);
}

const Texture* ResourceManager::LoadTexture(const std::string& filepath, const std::string& key) {
	std::string actualKey = key.empty() ? filepath : key;
	auto it = mTextures.find(actualKey);
	if (it != mTextures.end()) {
		return &it->second;
	}

	DecodedImage decoded = DecodeImageFile(filepath);
	if (!decoded.surface) {
		LOG_ERROR("ResourceManager") << decoded.error;
		return nullptr;
	}
	return UploadDecodedTexture(decoded.surface, actualKey, filepath);
}

const Texture* ResourceManager::LoadTextureFromCandidates(
	const std::vector<std::string>& filepaths, const std::string& key)
{
	if (filepaths.empty()) return nullptr;
	const std::string actualKey = key.empty() ? filepaths.front() : key;
	if (auto it = mTextures.find(actualKey); it != mTextures.end()) {
		return &it->second;
	}

	std::vector<std::string> errors;
	errors.reserve(filepaths.size());
	for (const std::string& filepath : filepaths) {
		DecodedImage decoded = DecodeImageFile(filepath);
		if (decoded.surface) {
			return UploadDecodedTexture(decoded.surface, actualKey, filepath);
		}
		errors.push_back(std::move(decoded.error));
	}

	// 一个逻辑资源允许有多种扩展名；只有全部候选失败才是需要玩家处理的资源错误。
	std::string combined = "LoadTexture 所有候选路径均加载失败: " + actualKey;
	for (const std::string& error : errors) combined += " | " + error;
	LOG_ERROR("ResourceManager") << combined;
	return nullptr;
}

const Texture* ResourceManager::UploadDecodedTexture(SDL_Surface* converted, const std::string& key, const std::string& filepath) {
	Texture tex;
	tex.width = converted->w;
	tex.height = converted->h;
	CaptureAutoTestAlpha(tex, converted);

	if (mTextureBackend) {
		pvz::RenderTexture* uploaded = mTextureBackend->CreateTextureRGBA8(converted->w, converted->h, converted->pixels);
		if (uploaded) {
			tex.renderTexture = uploaded;
		}
		else {
			LOG_ERROR("ResourceManager") << "LoadTexture 当前纹理后端上传失败: " << filepath;
		}
	}
	SDL_FreeSurface(converted);

	mTextures[key] = std::move(tex);
	return &mTextures[key];
}

size_t ResourceManager::ParallelDecodeAndUpload(const std::vector<TextureJob>& jobs) {
	if (jobs.empty()) return 0;

	const size_t n = jobs.size();
	std::vector<DecodedImage> results(n);
	std::vector<uint8_t> done(n, 0);
	std::mutex doneMutex;
	std::condition_variable doneCv;
	std::atomic<size_t> nextJob{ 0 };

	// hardware_concurrency 可能返回 0 且类型无符号，必须先转 int 再减 1，防回绕
	const int hc = static_cast<int>(std::thread::hardware_concurrency());
	const size_t threadCount = static_cast<size_t>(std::clamp(hc - 1, 2, 8));

	auto workerFn = [&]() {
		for (;;) {
			const size_t i = nextJob.fetch_add(1, std::memory_order_relaxed);
			if (i >= n) break;
			DecodedImage decoded = DecodeImageFile(jobs[i].path);
			{
				std::lock_guard<std::mutex> lock(doneMutex);
				results[i] = decoded;
				done[i] = 1;
			}
			doneCv.notify_all();
		}
	};

	std::vector<std::thread> workers;
	const size_t spawn = std::min(threadCount, n);
	workers.reserve(spawn);
	for (size_t t = 0; t < spawn; ++t) {
		workers.emplace_back(workerFn);
	}

	// 主线程严格按原列表顺序消费：key"先到先得"覆盖语义、日志顺序与串行版一致；
	// 等第 i 槽期间 worker 已在解码后续项（流水线重叠）。注意 worker 无背压：若上传慢于解码，
	// 已解码 surface 会在 results 中堆积，峰值受限于本批总解码体积（当前全部图片 ~30MB，无害）。
	size_t successCount = 0;
	for (size_t i = 0; i < n; ++i) {
		DecodedImage decoded;
		{
			std::unique_lock<std::mutex> lock(doneMutex);
			doneCv.wait(lock, [&] { return done[i] != 0; });
			decoded = results[i];
			results[i].surface = nullptr;
		}

		// 与串行 LoadTexture 相同的去重语义：已存在则跳过（仍算成功）
		if (mTextures.contains(jobs[i].key)) {
			if (decoded.surface) SDL_FreeSurface(decoded.surface);
			++successCount;
			continue;
		}

		if (!decoded.surface) {
			LOG_ERROR("ResourceManager") << decoded.error;
			if (!jobs[i].failMsg.empty()) {
				LOG_ERROR("ResourceManager") << jobs[i].failMsg;
			}
			continue;
		}

		UploadDecodedTexture(decoded.surface, jobs[i].key, jobs[i].path);
		++successCount;
	}

	for (auto& w : workers) w.join();
	return successCount;
}

const Texture* ResourceManager::GetTexture(const std::string& key, bool warnOnMiss) const {
	auto it = mTextures.find(key);
	if (it != mTextures.end()) {
		return &it->second;
	}
	// 与 GetSound/GetMusic 一致：miss 默认记 WARN，否则裸键名拼错时会变成看不见的 bug。
	// 但"存在性探测"调用方（warnOnMiss=false）的 miss 是预期路径，由其自身处理，不在此告警。
	if (warnOnMiss) {
		LOG_WARN("ResourceManager") << "纹理未找到: " << key;
	}
	return nullptr;
}

void ResourceManager::UnloadTexture(const std::string& key) {
	auto it = mTextures.find(key);
	if (it != mTextures.end()) {
		if (mTextureBackend && it->second.renderTexture) {
			mTextureBackend->DestroyTexture(it->second.renderTexture);
		}
		mTextures.erase(it);
		LOG_DEBUG("ResourceManager") << "卸载纹理: " << key;
	}
}

bool ResourceManager::HasTexture(const std::string& key) const {
	return mTextures.contains(key);
}

bool ResourceManager::LoadTiledTexture(const TiledImageInfo& info, const std::string& prefix) {
	// 加载原图到表面（用于分割）
	SDL_RWops* rw = FileManager::OpenRead(info.path);
	if (!rw) {
		LOG_ERROR("ResourceManager") << "LoadTiledTexture 无法打开图片: " << info.path;
		return false;
	}
	SDL_Surface* loadedSurface = IMG_Load_RW(rw, 1);   // freesrc=1
	if (!loadedSurface) {
		LOG_ERROR("ResourceManager") << "LoadTiledTexture 无法加载图片: " << info.path << " - " << IMG_GetError();
		return false;
	}

	// 切片前统一转为 ABGR8888，统一处理索引色/灰度/RGB 等源格式，
	// 否则 SDL_CreateRGBSurface 用源 BPP/Rmask 复制时不会带调色板，
	// 导致索引色 PNG 切出来的子图全黑/全透明
	SDL_Surface* originalSurface = SDL_ConvertSurfaceFormat(loadedSurface, SDL_PIXELFORMAT_ABGR8888, 0);
	SDL_FreeSurface(loadedSurface);
	if (!originalSurface) {
		LOG_ERROR("ResourceManager") << "LoadTiledTexture 无法转换源图片格式: " << info.path << " - " << SDL_GetError();
		return false;
	}
	// 关闭 src 的 alpha-blend，blit 走纯拷贝，避免与 dst 全 0 alpha 混合丢失
	SDL_SetSurfaceBlendMode(originalSurface, SDL_BLENDMODE_NONE);

	int imgW = originalSurface->w;
	int imgH = originalSurface->h;
	int tileW = imgW / info.columns;
	int tileH = imgH / info.rows;

	if (imgW % info.columns != 0 || imgH % info.rows != 0) {
		LOG_WARN("ResourceManager") << "图片尺寸 " << imgW << "x" << imgH
			<< " 不能被 " << info.columns << "x" << info.rows
			<< " 整除，可能产生边缘裁剪";
	}

	std::string baseKey = GenerateStandardKey(info.path, prefix);
	bool success = true;

	for (int row = 0; row < info.rows; ++row) {
		for (int col = 0; col < info.columns; ++col) {
			// 直接创建 ABGR8888 子表面（源已统一为该格式）
			SDL_Surface* tileSurface = SDL_CreateRGBSurfaceWithFormat(
				0, tileW, tileH, 32, SDL_PIXELFORMAT_ABGR8888);
			if (!tileSurface) {
				LOG_ERROR("ResourceManager") << "LoadTiledTexture 无法创建子表面: " << SDL_GetError();
				success = false;
				continue;
			}

			SDL_Rect srcRect = { col * tileW, row * tileH, tileW, tileH };
			if (SDL_BlitSurface(originalSurface, &srcRect, tileSurface, nullptr) != 0) {
				LOG_ERROR("ResourceManager") << "LoadTiledTexture Blit失败: " << SDL_GetError();
				SDL_FreeSurface(tileSurface);
				success = false;
				continue;
			}

			Texture tex;
			tex.width = tileW;
			tex.height = tileH;
			CaptureAutoTestAlpha(tex, tileSurface);
			if (mTextureBackend) {
				pvz::RenderTexture* uploaded = mTextureBackend->CreateTextureRGBA8(tileW, tileH, tileSurface->pixels);
				if (uploaded) {
					tex.renderTexture = uploaded;
				}
				else {
					LOG_ERROR("ResourceManager") << "LoadTiledTexture 分割贴图上传失败: " << info.path << " tile " << row << "," << col;
					success = false;
				}
			}
			SDL_FreeSurface(tileSurface);

			int index = row * info.columns + col;
			std::string key = baseKey + "_PART_" + std::to_string(index);
			mTextures[key] = std::move(tex);

		}
	}

	SDL_FreeSurface(originalSurface);
	return success;
}

bool ResourceManager::LoadAllGameImages() {
	bool success = true;
	const auto& infos = GetGameImageInfos();
	std::vector<TextureJob> jobs;
	jobs.reserve(infos.size());
	for (const auto& info : infos) {
		if (info.columns <= 1 && info.rows <= 1) {
			// 普通图片：收集为并行任务（与分割贴图的 key 不相交，先后顺序互不影响）
			jobs.push_back({ info.path, GenerateStandardKey(info.path, "IMAGE_"),
			                 "加载游戏图片失败: " + info.path });
		}
		else {
			// 分割贴图（个位数）：保持串行
			if (!LoadTiledTexture(info, "IMAGE_")) {
				LOG_ERROR("ResourceManager") << "加载分割贴图失败: " << info.path;
				success = false;
			}
		}
	}
	if (ParallelDecodeAndUpload(jobs) != jobs.size()) {
		success = false;
	}
	return success;
}

bool ResourceManager::LoadAllParticleTextures() {
	bool success = true;
	const auto& infos = GetParticleTextureInfos();
	std::vector<TextureJob> jobs;
	jobs.reserve(infos.size());
	for (const auto& info : infos) {
		if (info.columns <= 1 && info.rows <= 1) {
			jobs.push_back({ info.path, GenerateStandardKey(info.path, "PARTICLE_"),
			                 "加载粒子纹理失败: " + info.path });
		}
		else {
			if (!LoadTiledTexture(info, "PARTICLE_")) {
				LOG_ERROR("ResourceManager") << "加载粒子分割贴图失败: " << info.path;
				success = false;
			}
		}
	}
	if (ParallelDecodeAndUpload(jobs) != jobs.size()) {
		success = false;
	}
	return success;
}

bool ResourceManager::LoadAllFonts()
{
	bool success = true;
	const auto& paths = configReader.GetFontPaths();
	for (const auto& path : paths)
	{
		if (!LoadFont(path))
		{
			LOG_ERROR("ResourceManager") << "注册字体失败: " << path;
			success = false;
		}
	}
	return success;
}

bool ResourceManager::LoadAllSounds()
{
	bool success = true;
	const auto& paths = configReader.GetSoundPaths();
	for (const auto& path : paths)
	{
		std::string key = GenerateStandardKey(path, "SOUND_");
		if (!LoadSound(path, key))
		{
			LOG_ERROR("ResourceManager") << "加载音效失败: " << path;
			success = false;
		}
	}
	return success;
}

bool ResourceManager::LoadAllMusic()
{
	bool success = true;
	const auto& paths = configReader.GetMusicPaths();
	for (const auto& path : paths)
	{
		std::string key = GenerateStandardKey(path, "MUSIC_");
		if (!LoadMusic(path, key))
		{
			LOG_ERROR("ResourceManager") << "加载音乐失败: " << path;
			success = false;
		}
	}
	return success;
}

bool ResourceManager::LoadAllReanimations()
{
	bool success = true;
	const auto& reanimPaths = configReader.GetReanimationPaths();
	for (const auto& reanimPair : reanimPaths) {
		const std::string& key = reanimPair.first;
		const std::string& path = reanimPair.second;

		if (LoadReanimation(key, path)) {
		}
		else {
			success = false;
			LOG_ERROR("ResourceManager") << "加载动画失败: " << key << " from " << path;
		}
	}
	return success;
}

void ResourceManager::BuildReanimAtlases() {
	// Vulkan 依靠 bindless 保持动画部件合批；传统 OpenGL 单 sampler 路径则把同一
	// reanim 的部件尽量放进同页，避免每个轨道都触发 texture flush。
	if (!mTextureBackend || mTextureBackend->Backend() != pvz::RendererBackend::OpenGL) return;

	std::vector<Texture*> sources;
	std::unordered_set<Texture*> seen;
	for (auto& pair : mReanimations) {
		const auto& reanim = pair.second;
		if (!reanim || !reanim->mTracks) continue;
		std::vector<Texture*> group;
		for (auto& track : *reanim->mTracks) {
			for (auto& frame : track.mFrames) {
				if (!frame.image) continue;
				auto* texture = const_cast<Texture*>(frame.image);
				if (texture->atlasPage || !texture->renderTexture
					|| texture->width <= 0 || texture->height <= 0) continue;
				if (seen.insert(texture).second) group.push_back(texture);
			}
		}
		std::stable_sort(group.begin(), group.end(), [](const Texture* lhs, const Texture* rhs) {
			return lhs->height > rhs->height;
		});
		sources.insert(sources.end(), group.begin(), group.end());
	}
	if (sources.empty()) return;

	constexpr int kAtlasPadding = 2; // 图块四周复制边缘像素的宽度，保护线性过滤和 mipmap。
	const int pageDimension = (std::min)(mTextureBackend->MaxTextureSize(), 4096);
	struct Placement {
		Texture* texture = nullptr;
		int x = 0;
		int y = 0;
	};
	std::vector<std::vector<Placement>> pages(1);
	int cursorX = kAtlasPadding;
	int cursorY = kAtlasPadding;
	int shelfHeight = 0;

	for (Texture* texture : sources) {
		const int width = texture->width;
		const int height = texture->height;
		if (width + 2 * kAtlasPadding > pageDimension
			|| height + 2 * kAtlasPadding > pageDimension) {
			LOG_WARN("ResourceManager") << "reanim 纹理超过 OpenGL 图集页，保留独立纹理: "
				<< width << "x" << height;
			continue;
		}
		if (cursorX + width + kAtlasPadding > pageDimension) {
			cursorX = kAtlasPadding;
			cursorY += shelfHeight + 2 * kAtlasPadding;
			shelfHeight = 0;
		}
		if (cursorY + height + kAtlasPadding > pageDimension) {
			pages.emplace_back();
			cursorX = kAtlasPadding;
			cursorY = kAtlasPadding;
			shelfHeight = 0;
		}
		pages.back().push_back({ texture, cursorX, cursorY });
		cursorX += width + 2 * kAtlasPadding;
		shelfHeight = (std::max)(shelfHeight, height);
	}

	std::size_t packedCount = 0;
	for (const auto& placements : pages) {
		if (placements.empty()) continue;
		std::vector<pvz::AtlasCopy> copies;
		copies.reserve(placements.size());
		for (const Placement& placement : placements) {
			copies.push_back({ placement.texture->renderTexture, placement.x, placement.y,
				placement.texture->width, placement.texture->height, kAtlasPadding });
		}
		pvz::RenderTexture* pageHandle = mTextureBackend->CreateAtlasPage(
			pageDimension, pageDimension, copies);
		if (!pageHandle) {
			LOG_ERROR("ResourceManager") << "OpenGL reanim 图集页创建失败，保留独立纹理";
			continue;
		}

		mAtlasPages.emplace_back();
		Texture& page = mAtlasPages.back();
		page.width = pageDimension;
		page.height = pageDimension;
		page.renderTexture = pageHandle;
		const float halfTexel = 0.5f / static_cast<float>(pageDimension);
		for (const Placement& placement : placements) {
			Texture* texture = placement.texture;
			texture->atlasPage = &page;
			texture->aU0 = static_cast<float>(placement.x) / pageDimension + halfTexel;
			texture->aV0 = static_cast<float>(placement.y) / pageDimension + halfTexel;
			texture->aU1 = static_cast<float>(placement.x + texture->width) / pageDimension - halfTexel;
			texture->aV1 = static_cast<float>(placement.y + texture->height) / pageDimension - halfTexel;
			++packedCount;
		}
	}

	LOG_WARN("ResourceManager") << "OpenGL reanim 图集: " << packedCount
		<< " 张源纹理 -> " << mAtlasPages.size() << " 页 (" << pageDimension << "x"
		<< pageDimension << ", padding=" << kAtlasPadding << ")";
}

bool ResourceManager::LoadAllImagesFromPath(const std::string& directory) {
	// 文件名来自构建期烘焙的清单（FileManager::ListResourceFiles 经 SDL_RWops 读，APK 可读），
	// 不再用 std::filesystem 枚举，使该路径在 Android 上也能列举 APK assets。
	// 返回的全路径与旧 directory_iterator 逐字一致，下游加载零差异。
	std::vector<std::string> entries = FileManager::ListResourceFiles(directory);

	std::vector<TextureJob> jobs;
	jobs.reserve(entries.size());
	for (const std::string& fullPath : entries) {
		std::string ext = FileManager::GetFileExtension(fullPath);
		// 转换为小写以统一判断
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
			// stem = 文件名去扩展名（ext 已小写但长度不变，故可直接按长度裁剪）
			std::string fileName = FileManager::GetFileName(fullPath);
			std::string key = fileName.substr(0, fileName.size() - ext.size());
			// 将 key 转换为大写
			std::transform(key.begin(), key.end(), key.begin(), ::toupper);
			jobs.push_back({ fullPath, "IMAGE_" + key, std::string() });
		}
	}

	const size_t loadedCount = ParallelDecodeAndUpload(jobs);

	if (loadedCount == 0) {
		LOG_ERROR("ResourceManager") << "在目录 " << directory << " 中没有找到任何 JPG/PNG 图片";
		return false;
	}

	return loadedCount == jobs.size();
}

std::shared_ptr<Reanimation> ResourceManager::LoadReanimation(const std::string& key, const std::string& path) {
	auto it = mReanimations.find(key);
	if (it != mReanimations.end()) {
		return it->second;
	}

	auto reanim = std::make_shared<Reanimation>();
	reanim->mResourceManager = this;
	if (!reanim->LoadFromFile(path)) {
		LOG_ERROR("ResourceManager") << "LoadReanimation 失败: " << path;
		return nullptr;
	}

	mReanimations[key] = reanim;
	return reanim;
}

std::shared_ptr<Reanimation> ResourceManager::GetReanimation(const std::string& key) {
	auto it = mReanimations.find(key);
	if (it != mReanimations.end()) {
		// 播放状态由 Animator 持有；Reanimation 实例只共享不可变的轨道资源与名称索引。
		auto cachedReanim = it->second;
		return std::make_shared<Reanimation>(*cachedReanim);
	}
	LOG_ERROR("ResourceManager") << "GetReanimation 未找到: " << key;
	return nullptr;
}

void ResourceManager::UnloadReanimation(const std::string& key) {
	mReanimations.erase(key);
}

bool ResourceManager::HasReanimation(const std::string& key) const {
	return mReanimations.contains(key);
}

std::string ResourceManager::AnimationTypeToString(AnimationType type) {
	return GameDataManager::GetInstance().GetAnimationName(type);
}

bool ResourceManager::LoadFont(const std::string& path, const std::string& key) {
	std::string actualKey = key.empty() ? path : key;
	if (fonts.contains(actualKey)) {
		return true; // 已注册
	}
	fonts[actualKey] = std::unordered_map<int, TTF_Font*>();
	return true;
}

TTF_Font* ResourceManager::GetFont(const std::string& key, int size) {
	if (key.empty() || size <= 0) {
		LOG_ERROR("ResourceManager") << "GetFont 无效的字体参数: key=" << key << ", size=" << size;
		return nullptr;
	}

	auto fontIt = fonts.find(key);
	if (fontIt == fonts.end()) {
		LOG_ERROR("ResourceManager") << "GetFont 字体未注册: '" << key << "'";
		return nullptr;
	}

	auto& sizeMap = fontIt->second;
	auto sizeIt = sizeMap.find(size);
	if (sizeIt != sizeMap.end()) {
		return sizeIt->second;
	}

	// 查找字体文件路径
	std::string fontPath;
	const auto& fontPaths = configReader.GetFontPaths();
	for (const auto& path : fontPaths) {
		std::string filename = path.substr(path.find_last_of("/\\") + 1);
		std::string fileKey = filename.substr(0, filename.find_last_of('.'));
		if (fileKey == key) {
			fontPath = path;
			break;
		}
	}
	if (fontPath.empty()) {
		// 尝试直接使用 key 作为路径
		fontPath = key;
	}

	SDL_RWops* fontRw = FileManager::OpenRead(fontPath);
	if (!fontRw) {
		LOG_ERROR("ResourceManager") << "GetFont 无法打开字体: " << fontPath << " size: " << size;
		return nullptr;
	}
	TTF_Font* font = TTF_OpenFontRW(fontRw, 1, size);   // freesrc=1
	if (!font) {
		LOG_ERROR("ResourceManager") << "GetFont 加载字体失败: " << fontPath << " size: " << size << " - " << TTF_GetError();
		return nullptr;
	}

	sizeMap[size] = font;
	return font;
}

void ResourceManager::UnloadFont(const std::string& key) {
	auto it = fonts.find(key);
	if (it != fonts.end()) {
		for (auto& sizePair : it->second) {
			TTF_CloseFont(sizePair.second);
		}
		fonts.erase(it);
		LOG_DEBUG("ResourceManager") << "卸载字体: " << key;
	}
}

void ResourceManager::UnloadFontSize(const std::string& key, int size) {
	auto fontIt = fonts.find(key);
	if (fontIt != fonts.end()) {
		auto& sizeMap = fontIt->second;
		auto sizeIt = sizeMap.find(size);
		if (sizeIt != sizeMap.end()) {
			TTF_CloseFont(sizeIt->second);
			sizeMap.erase(sizeIt);
			LOG_DEBUG("ResourceManager") << "卸载字体: " << key << " size: " << size;
			if (sizeMap.empty()) {
				fonts.erase(fontIt);
			}
		}
	}
}

void ResourceManager::CleanupUnusedFontSizes() {
	for (auto it = fonts.begin(); it != fonts.end(); ) {
		if (it->second.empty()) {
			it = fonts.erase(it);
		}
		else {
			++it;
		}
	}
}

bool ResourceManager::HasFont(const std::string& key) const {
	return fonts.contains(key);
}

std::vector<int> ResourceManager::GetLoadedFontSizes(const std::string& key) const {
	std::vector<int> sizes;
	auto it = fonts.find(key);
	if (it != fonts.end()) {
		for (const auto& sizePair : it->second) {
			sizes.push_back(sizePair.first);
		}
	}
	return sizes;
}

int ResourceManager::GetLoadedFontCount() const {
	int total = 0;
	for (const auto& fontPair : fonts) {
		total += static_cast<int>(fontPair.second.size());
	}
	return total;
}

Mix_Chunk* ResourceManager::LoadSound(const std::string& path, const std::string& key) {
	std::string actualKey = key.empty() ? path : key;
	if (sounds.contains(actualKey)) {
		return sounds[actualKey];
	}

	SDL_RWops* sndRw = FileManager::OpenRead(path);
	if (!sndRw) {
		LOG_ERROR("ResourceManager") << "LoadSound 无法打开音效: " << path;
		return nullptr;
	}
	Mix_Chunk* sound = Mix_LoadWAV_RW(sndRw, 1);   // freesrc=1
	if (!sound) {
		LOG_ERROR("ResourceManager") << "LoadSound 加载音效失败: " << path << " - " << Mix_GetError();
		return nullptr;
	}

	sounds[actualKey] = sound;
	return sound;
}

Mix_Chunk* ResourceManager::GetSound(const std::string& key) {
	auto it = sounds.find(key);
	if (it != sounds.end()) {
		return it->second;
	}
	LOG_WARN("ResourceManager") << "音效未找到: " << key;
	return nullptr;
}

void ResourceManager::UnloadSound(const std::string& key) {
	auto it = sounds.find(key);
	if (it != sounds.end()) {
		Mix_FreeChunk(it->second);
		sounds.erase(it);
		LOG_DEBUG("ResourceManager") << "卸载音效: " << key;
	}
}

bool ResourceManager::HasSound(const std::string& key) const {
	return sounds.contains(key);
}

Mix_Music* ResourceManager::LoadMusic(const std::string& path, const std::string& key) {
	std::string actualKey = key.empty() ? path : key;
	if (music.contains(actualKey)) {
		return music[actualKey];
	}

	SDL_RWops* musRw = FileManager::OpenRead(path);
	if (!musRw) {
		LOG_ERROR("ResourceManager") << "LoadMusic 无法打开音乐: " << path;
		return nullptr;
	}
	Mix_Music* mus = Mix_LoadMUS_RW(musRw, 1);   // freesrc=1
	if (!mus) {
		LOG_ERROR("ResourceManager") << "LoadMusic 加载音乐失败: " << path << " - " << Mix_GetError();
		return nullptr;
	}

	music[actualKey] = mus;
	return mus;
}

Mix_Music* ResourceManager::GetMusic(const std::string& key) {
	auto it = music.find(key);
	if (it != music.end()) {
		return it->second;
	}
	LOG_WARN("ResourceManager") << "音乐未找到: " << key;
	return nullptr;
}

void ResourceManager::UnloadMusic(const std::string& key) {
	auto it = music.find(key);
	if (it != music.end()) {
		Mix_FreeMusic(it->second);
		music.erase(it);
		LOG_DEBUG("ResourceManager") << "卸载音乐: " << key;
	}
}

bool ResourceManager::HasMusic(const std::string& key) const {
	return music.contains(key);
}

std::string ResourceManager::GenerateStandardKey(const std::string& path, const std::string& prefix) {
	std::string filename = path.substr(path.find_last_of("/\\") + 1);
	std::string nameWithoutExt = filename.substr(0, filename.find_last_of('.'));
	std::transform(nameWithoutExt.begin(), nameWithoutExt.end(), nameWithoutExt.begin(), ::toupper);
	for (char& c : nameWithoutExt) {
		if (!std::isalnum(c)) {
			c = '_';
		}
	}
	return prefix + nameWithoutExt;
}

void ResourceManager::UnloadAll() {
	// 先把每张纹理还给当前后端，再 clear；GameApp 保证纹理后端仍存活。
	if (mTextureBackend) {
		for (auto& kv : mTextures) {
			if (kv.second.renderTexture) mTextureBackend->DestroyTexture(kv.second.renderTexture);
		}
		for (auto& page : mAtlasPages) {
			if (page.renderTexture) mTextureBackend->DestroyTexture(page.renderTexture);
		}
	}
	mTextures.clear();
	mAtlasPages.clear();
	mTextureBackend = nullptr;

	// 清理动画
	mReanimations.clear();

	// 清理字体
	for (auto& fontPair : fonts) {
		for (auto& sizePair : fontPair.second) {
			TTF_CloseFont(sizePair.second);
		}
	}
	fonts.clear();

	// 清理音效
	for (auto& pair : sounds) {
		Mix_FreeChunk(pair.second);
	}
	sounds.clear();

	// 清理音乐
	for (auto& pair : music) {
		Mix_FreeMusic(pair.second);
	}
	music.clear();

	LOG_DEBUG("ResourceManager") << "已卸载所有资源";
}
