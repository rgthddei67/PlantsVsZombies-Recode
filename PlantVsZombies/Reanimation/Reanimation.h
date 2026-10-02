#pragma once
#ifndef _REANIMATION_H
#define _REANIMATION_H

#include "ReanimTypes.h"
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>
#include <utility>

constexpr float REANIM_MISSING_FIELD_FLOAT = -1024;
constexpr int REANIM_MISSING_FIELD_INT = -1024;

class Reanimation {
private:
	std::shared_ptr<std::unordered_map<std::string, int>> mFirstTrackIndices;

public:
	float mFPS = 12.0f;
	std::shared_ptr<std::vector<TrackInfo>> mTracks = nullptr;
	bool mIsLoaded = false;
	class ResourceManager* mResourceManager = nullptr;

public:
	Reanimation();
	Reanimation(const Reanimation&) = default;
	~Reanimation();

	// 鍔犺浇reanim鏂囦欢
	bool LoadFromFile(const std::string& filePath);

	// 鑾峰彇杞ㄩ亾淇℃伅
	size_t GetTrackCount() const;
	TrackInfo* GetTrack(int index);
	TrackInfo* GetTrack(const std::string& trackName);
	/** 返回第一个同名轨道的索引；索引表由同一资源的全部实例共享。 */
	int GetFirstTrackIndex(const std::string& trackName) const;
	/** 读取连续 f=0 的动画片段范围；无有效片段返回 {-1,-1}，不创建播放实例或更改资源。 */
	std::pair<int,int> GetTrackFrameRange(const std::string& trackName) const;

	// 鑾峰彇鎬诲抚鏁?
	int GetTotalFrames() const;
};

/**
 * @brief 插值连续变换，并按播放模式选择不可插值的显示帧属性。
 * @param useDestFrame true 表示 blend，离散属性取 tDst；false 表示普通帧间插值，取 tSrc。
 */
void GetDeltaTransform(const TrackFrameTransform& tSrc, const TrackFrameTransform& tDst,
	float tDelta, TrackFrameTransform& tOutput, bool useDestFrame = false);

#endif
