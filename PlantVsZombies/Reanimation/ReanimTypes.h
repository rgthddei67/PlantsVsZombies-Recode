#pragma once
#ifndef _REANIM_TYPES_H
#define _REANIM_TYPES_H

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "../Game/Definit.h"

struct Texture;

// 鍔ㄧ敾甯у彉鎹㈡暟鎹?
struct TrackFrameTransform {
	float x = 0.0f;
	float y = 0.0f;
	float kx = 0.0f;  // 鏃嬭浆X
	float ky = 0.0f;  // 鏃嬭浆Y
	float sx = 1.0f;  // 缂╂斁X
	float sy = 1.0f;  // 缂╂斁Y
	float a = 1.0f;   // 閫忔槑搴?
	int f = 0;        // 鏄剧ず鏍囧織锛?=鏄剧ず锛?1=绌虹櫧/鍒嗛殧锛?
	const Texture* image = nullptr; // 鍥惧儚

	TrackFrameTransform() = default;
};

// 鍔ㄧ敾杞ㄩ亾淇℃伅
struct TrackInfo {
	std::string mTrackName = "";
	bool mAvailable = true;
	std::vector<TrackFrameTransform> mFrames;

	TrackInfo() = default;
	explicit TrackInfo(const std::string& name) : mTrackName(name) {}
};

// 杞ㄩ亾棰濆鎺у埗淇℃伅
struct TrackExtraInfo {
	bool mVisible = true;
	bool mIgnoreTopClip = false;     // 仅本轨道纹理跳过最内层裁剪，保留外层场景裁剪
	bool mIgnoreColorEffects = false; // 水面等环境轨道不继承宿主的覆盖色和受击白光
	bool mHasGlowOverride = false;   // 是否由本轨道独立决定高亮，不再继承 Animator 整体开关
	bool mGlowOverrideEnabled = false; // 独立高亮的当前开关；仅在 mHasGlowOverride 时生效
	SDL_Color mColor = { 255, 255, 255, 255 }; // 轨道纹理乘色；默认纯白，不改变原图
	float mOffsetX = 0.0f;          // 杞ㄩ亾鑷韩缁樺埗鍋忕Щ X
	float mOffsetY = 0.0f;          // 杞ㄩ亾鑷韩缁樺埗鍋忕Щ Y
	const Texture* mImage = nullptr;  // 鎵嬪姩瑕嗙洊鍥剧墖璁剧疆
};

// 鍔ㄧ敾鎾斁鐘舵€?
enum class PlayState {
	PLAY_NONE,
	PLAY_REPEAT,
	PLAY_ONCE,
	PLAY_ONCE_TO,
};

#endif
