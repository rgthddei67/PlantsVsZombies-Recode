#pragma once
#ifndef _RESOURCEMANAGER_H
#define _RESOURCEMANAGER_H

#include "./Reanimation/Reanimation.h"
#include "./Reanimation/AnimationTypes.h"
#include "ResourcesXMLConfigReader.h"
#include "Renderer/RenderBackend.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <list>
#include <vector>

// 后端无关纹理信息；具体 Vulkan/OpenGL 对象只由 TextureBackend 创建和销毁。
struct Texture {
	int width = 0;
	int height = 0;
	pvz::RenderTexture* renderTexture = nullptr;

	uint32_t BindingId() const { return renderTexture ? renderTexture->bindingId : 0; }

	// —— 图集映射 ——
	// 若 atlasPage != nullptr，说明本纹理已被打进 atlasPage 指向的图集页，
	// 绘制时应改用 atlasPage->BindingId()，并把 [0,1] UV 重映射到对应区域。
	const Texture* atlasPage = nullptr;
	float aU0 = 0.0f, aV0 = 0.0f, aU1 = 1.0f, aV1 = 1.0f;
	/// 仅 AutoTest 保留原子图逐像素 alpha；正式游戏为空，缺失时公开像素取证必须拒绝。
	std::vector<std::uint8_t> autoTestAlpha;
};

class ResourceManager {
private:
	// 后端无关纹理缓存
	std::unordered_map<std::string, Texture> mTextures;

	// 动画缓存
	std::unordered_map<std::string, std::shared_ptr<Reanimation>> mReanimations;

	// reanim 纹理图集页（用 list 保证元素地址稳定，Texture::atlasPage 会指向其中元素）
	std::list<Texture> mAtlasPages;

	// 字体缓存（按字体名 -> 大小 -> TTF_Font*）
	std::unordered_map<std::string, std::unordered_map<int, TTF_Font*>> fonts;

	// 音效缓存
	std::unordered_map<std::string, Mix_Chunk*> sounds;

	// 音乐缓存
	std::unordered_map<std::string, Mix_Music*> music;

	// 配置读取器
	ResourcesXMLConfigReader configReader;

	static ResourceManager* instance;
	pvz::TextureBackend* mTextureBackend = nullptr;
	ResourceManager() {}

	// 加载分割贴图
	bool LoadTiledTexture(const TiledImageInfo& info, const std::string& prefix);

	// 把已解码的 ABGR8888 surface 上传当前后端并插入 mTextures（接管 converted 所有权）。
	// 仅主线程调用；上传失败仍按原语义插入空 Texture 并返回其指针。
	const Texture* UploadDecodedTexture(SDL_Surface* converted, const std::string& key, const std::string& filepath);

	// 并行图片加载：worker 做 打开+解码+转格式，主线程严格按 jobs 原顺序做 去重/上传/插入/日志，
	// 与逐个调 LoadTexture 的串行语义逐位一致。failMsg 非空时该条目失败会额外记一行 ERROR。
	// 返回成功条目数（含"key 已存在跳过"的条目）。
	struct TextureJob {
		std::string path;
		std::string key;
		std::string failMsg;
	};
	size_t ParallelDecodeAndUpload(const std::vector<TextureJob>& jobs);

public:
	static ResourceManager& GetInstance();
	static void ReleaseInstance();

	bool Initialize(const std::string& configPath = "./resources/resources.xml");

	// 由 GameApp 在渲染后端就绪后注入；资源层不解释具体 GPU 句柄。
	void SetTextureBackend(pvz::TextureBackend* backend) { mTextureBackend = backend; }

	// 加载纹理，返回纹理信息指针，失败返回 nullptr
	const Texture* LoadTexture(const std::string& filepath, const std::string& key = "");
	/**
	 * @brief 按顺序尝试多个等价纹理路径，全部失败时才记录一条错误。
	 * @details 用于 `.png` / `.jpg` 等合法扩展名回退，避免把前一个候选不存在误报成错误。
	 */
	const Texture* LoadTextureFromCandidates(
		const std::vector<std::string>& filepaths, const std::string& key);
	// 获取已加载的纹理，不存在返回 nullptr。
	// warnOnMiss=false 供"存在性探测"调用方使用（如 Reanimation 加载时先查后载），
	// 这类 miss 是预期的正常路径，由调用方自行处理，不应记 WARN。
	const Texture* GetTexture(const std::string& key, bool warnOnMiss = true) const;
	// 卸载单个纹理
	void UnloadTexture(const std::string& key);
	// 检查纹理是否存在
	bool HasTexture(const std::string& key) const;

	// 批量加载（使用配置信息）
	bool LoadAllGameImages();
	bool LoadAllParticleTextures();
	bool LoadAllFonts();
	bool LoadAllSounds();
	bool LoadAllMusic();
	bool LoadAllReanimations();
	// 在支持图集页的后端上，把 reanim 部件纹理打进图集，降低单 sampler 的纹理切换。
	// 必须在纹理后端就绪、且 LoadAllReanimations 之后调用。
	void BuildReanimAtlases();
	/// @brief 加载 ./resources/image/reanim/ 目录下的所有 JPG/PNG 图片，使用文件名（不含扩展名）作为键名
	/// @param directory 要扫描的目录，默认为 "./resources/image/reanim/"
	/// @return 是否全部加载成功
	bool LoadAllImagesFromPath(const std::string& directory = "./resources/image/reanim/");

	// ---------- 动画管理 ----------
	std::shared_ptr<Reanimation> LoadReanimation(const std::string& key, const std::string& path);
	std::shared_ptr<Reanimation> GetReanimation(const std::string& key);
	void UnloadReanimation(const std::string& key);
	bool HasReanimation(const std::string& key) const;
	std::string AnimationTypeToString(AnimationType type);

	// ---------- 字体管理 ----------
	bool LoadFont(const std::string& path, const std::string& key = "");
	TTF_Font* GetFont(const std::string& key, int size);
	void UnloadFont(const std::string& key);
	void UnloadFontSize(const std::string& key, int size);
	void CleanupUnusedFontSizes();
	bool HasFont(const std::string& key) const;

	// ---------- 音效管理 ----------
	Mix_Chunk* LoadSound(const std::string& path, const std::string& key = "");
	Mix_Chunk* GetSound(const std::string& key);
	void UnloadSound(const std::string& key);
	bool HasSound(const std::string& key) const;

	// ---------- 音乐管理 ----------
	Mix_Music* LoadMusic(const std::string& path, const std::string& key = "");
	Mix_Music* GetMusic(const std::string& key);
	void UnloadMusic(const std::string& key);
	bool HasMusic(const std::string& key) const;

	// ---------- 辅助函数 ----------
	std::string GenerateStandardKey(const std::string& path, const std::string& prefix);
	std::vector<int> GetLoadedFontSizes(const std::string& key) const;
	int GetLoadedFontCount() const;

	// ---------- 获取配置信息（供外部使用）----------
	const std::vector<TiledImageInfo>& GetGameImageInfos() const { return configReader.GetGameImageInfos(); }
	const std::vector<TiledImageInfo>& GetParticleTextureInfos() const { return configReader.GetParticleTextureInfos(); }
	const std::vector<std::string>& GetSoundPaths() const { return configReader.GetSoundPaths(); }
	const std::vector<std::string>& GetMusicPaths() const { return configReader.GetMusicPaths(); }
	const std::unordered_map<std::string, std::string>& GetAnimationPaths() const { return configReader.GetReanimationPaths(); }

	// 清理所有资源
	void UnloadAll();

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;
};

#endif
