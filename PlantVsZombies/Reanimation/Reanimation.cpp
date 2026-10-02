#include "Reanimation.h"
#include "../ResourceManager.h"
#include "../FileManager.h"
#include "../Logger.h"
#include <glm/glm.hpp>

Reanimation::Reanimation() {
	mTracks = std::make_shared<std::vector<TrackInfo>>();
	mFirstTrackIndices = std::make_shared<std::unordered_map<std::string, int>>();
}

Reanimation::~Reanimation() {
}

bool Reanimation::LoadFromFile(const std::string& filePath) {
	mTracks->clear();
	mFirstTrackIndices->clear();
	mIsLoaded = false;

	// 加载xml
	pugi::xml_document doc;
	if (!FileManager::LoadXMLFile(filePath, doc)) {
		LOG_ERROR("Reanim") << "LoadFromFile 加载 reanim 文件失败: " << filePath;
		return false;
	}

	// 解析xml
	for (pugi::xml_node node : doc.children()) {
		std::string tagName = node.name();
		if (tagName == "fps") {
			mFPS = node.text().as_float();
		}
		else if (tagName == "track") {
			TrackInfo track;

			// 初始化各个信息
			float prevX = 0.0f;
			float prevY = 0.0f;
			float prevKx = 0.0f;
			float prevKy = 0.0f;
			float prevSx = 1.0f;
			float prevSy = 1.0f;
			float prevA = 1.0f;
			int prevF = 0;
			const Texture* prevImage = nullptr;

			for (pugi::xml_node child : node.children()) {
				std::string childName = child.name();
				if (childName == "name") {
					track.mTrackName = child.text().as_string();
				}
				else if (childName == "t") {
					TrackFrameTransform frameTransform;

					// 先默认设置为占位符
					frameTransform.x = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.y = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.kx = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.ky = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.sx = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.sy = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.a = REANIM_MISSING_FIELD_FLOAT;
					frameTransform.f = REANIM_MISSING_FIELD_INT;
					frameTransform.image = nullptr;

					// 解析每个项
					for (pugi::xml_node prop : child.children()) {
						std::string propName = prop.name();
						std::string propValue = prop.text().as_string();

						if (propValue.empty()) continue;

						if (propName == "x") {
							frameTransform.x = prop.text().as_float();
						}
						else if (propName == "y") {
							frameTransform.y = prop.text().as_float();
						}
						else if (propName == "kx") {
							frameTransform.kx = prop.text().as_float();
						}
						else if (propName == "ky") {
							frameTransform.ky = prop.text().as_float();
						}
						else if (propName == "sx") {
							frameTransform.sx = prop.text().as_float();
						}
						else if (propName == "sy") {
							frameTransform.sy = prop.text().as_float();
						}
						else if (propName == "a") {
							frameTransform.a = prop.text().as_float();
						}
						else if (propName == "f") {
							frameTransform.f = prop.text().as_int();
						}
						else if (propName == "i") {
							std::string imageName = prop.text().as_string();
							if (!imageName.empty() && mResourceManager) {
								// 自动加载相关图片
								
								if (imageName.find("IMAGE_REANIM_") == 0) {
									std::string fileName = imageName.substr(13);
									// reanim 使用完整 imageName 作为缓存键；未缓存时再统一尝试 PNG/JPG，
									// 前一个合法候选不存在不应产生误导性的 ERROR。
									const Texture* tex = mResourceManager->GetTexture(imageName, /*warnOnMiss=*/false);
									if (!tex) {
										std::string filePath = "./resources/image/reanim/" + fileName;
										tex = mResourceManager->LoadTextureFromCandidates(
											{ filePath + ".png", filePath + ".jpg" }, imageName);

										if (tex) {
											// 新加载成功，更新 prevImage
											prevImage = tex;
										}
										else {
											LOG_WARN("Reanim") << "LoadFromFile 没有找到图片" << imageName;
										}
									}
									else {
										// 已存在，更新 prevImage
										prevImage = tex;
									}
									frameTransform.image = tex;
								}
								
							}
						}
					}

					if (frameTransform.x == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.x = prevX;
					else
						prevX = frameTransform.x;

					if (frameTransform.y == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.y = prevY;
					else
						prevY = frameTransform.y;

					if (frameTransform.kx == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.kx = prevKx;
					else
						prevKx = frameTransform.kx;

					if (frameTransform.ky == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.ky = prevKy;
					else
						prevKy = frameTransform.ky;

					if (frameTransform.sx == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.sx = prevSx;
					else
						prevSx = frameTransform.sx;

					if (frameTransform.sy == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.sy = prevSy;
					else
						prevSy = frameTransform.sy;

					if (frameTransform.a == REANIM_MISSING_FIELD_FLOAT)
						frameTransform.a = prevA;
					else
						prevA = frameTransform.a;

					if (frameTransform.f == REANIM_MISSING_FIELD_INT) frameTransform.f = prevF;
					else prevF = frameTransform.f;

					if (frameTransform.image == nullptr)
						frameTransform.image = prevImage;
					else
						prevImage = frameTransform.image;

					track.mFrames.push_back(frameTransform);
				}
			}

			// 判断是否可用
			track.mAvailable = !track.mFrames.empty();

			const int trackIndex = static_cast<int>(mTracks->size());
			mTracks->push_back(track);
			mFirstTrackIndices->try_emplace(track.mTrackName, trackIndex);
		}
	}

	mIsLoaded = true;

	LOG_DEBUG("Reanim") << "成功加载reanim: " << filePath << "   Track数量: " << mTracks->size() << "   总帧数" << GetTotalFrames();

	return true;
}

TrackInfo* Reanimation::GetTrack(int index) {
	if (!mTracks || index < 0 || index >= static_cast<int>(mTracks->size())) {
		return nullptr;
	}
	return &(*mTracks)[index];
}

TrackInfo* Reanimation::GetTrack(const std::string& trackName) {
	return GetTrack(GetFirstTrackIndex(trackName));
}

int Reanimation::GetFirstTrackIndex(const std::string& trackName) const {
	if (!mFirstTrackIndices) return -1;
	const auto it = mFirstTrackIndices->find(trackName);
	return it != mFirstTrackIndices->end() ? it->second : -1;
}

int Reanimation::GetTotalFrames() const {
	if (!mTracks || mTracks->empty()) return 0;
	return static_cast<int>((*mTracks)[0].mFrames.size());
}

std::pair<int,int> Reanimation::GetTrackFrameRange(const std::string& trackName) const
{
	const int index = GetFirstTrackIndex(trackName);
	if (!mTracks || index<0 || index>=static_cast<int>(mTracks->size())) return {-1,-1};
	const auto& frames = (*mTracks)[index].mFrames;
	int begin = -1;
	for (int i=0; i<static_cast<int>(frames.size()); ++i) if (frames[i].f==0) {begin=i; break;}
	if (begin<0) return {-1,-1};
	int end = begin;
	for (int i=begin+1; i<static_cast<int>(frames.size()) && frames[i].f==0; ++i) end=i;
	return {begin,end};
}

size_t Reanimation::GetTrackCount() const {
	return mTracks ? mTracks->size() : 0;
}

void GetDeltaTransform(const TrackFrameTransform& tSrc, const TrackFrameTransform& tDst,
	float tDelta, TrackFrameTransform& tOutput, bool useDestFrame) {
	tOutput.a = (tDst.a - tSrc.a) * tDelta + tSrc.a;
	tOutput.x = (tDst.x - tSrc.x) * tDelta + tSrc.x;
	tOutput.y = (tDst.y - tSrc.y) * tDelta + tSrc.y;
	tOutput.sx = (tDst.sx - tSrc.sx) * tDelta + tSrc.sx;
	tOutput.sy = (tDst.sy - tSrc.sy) * tDelta + tSrc.sy;

	if (useDestFrame) {
		// 复刻原版 ReanimatorXnaHelpers.BlendTransform：blend 期间若某轴需转过 >180°，
		// 不做平滑插值(否则像转半圈/穿过退化的扭曲态)，而是直接吸附到【当前帧 tDst】立即就位。
		// 旧实现误把吸附目标写成 tSrc(blend 起始旧帧) → 该关节整段 blend 卡在旧朝向、
		// 直到 blend 结束(走回最短弧分支)才弹回；当翻转编码在 ky(前后差 ~180°)时即"手反转"。
		if (tDst.kx > tSrc.kx + 180.0f || tDst.kx < tSrc.kx - 180.0f)
			tOutput.kx = tDst.kx;
		else
			tOutput.kx = (tDst.kx - tSrc.kx) * tDelta + tSrc.kx;
		if (tDst.ky > tSrc.ky + 180.0f || tDst.ky < tSrc.ky - 180.0f)
			tOutput.ky = tDst.ky;
		else
			tOutput.ky = (tDst.ky - tSrc.ky) * tDelta + tSrc.ky;

		// blend 的离散属性必须跟随新轨道当前帧，不能继承 blend 起点的旧图。
		tOutput.f = tDst.f;
		tOutput.image = tDst.image;
	}
	else {
		float kxDiff = tDst.kx - tSrc.kx;
		while (kxDiff > 180.0f) kxDiff -= 360.0f;
		while (kxDiff < -180.0f) kxDiff += 360.0f;
		tOutput.kx = tSrc.kx + kxDiff * tDelta;

		float kyDiff = tDst.ky - tSrc.ky;
		while (kyDiff > 180.0f) kyDiff -= 360.0f;
		while (kyDiff < -180.0f) kyDiff += 360.0f;
		tOutput.ky = tSrc.ky + kyDiff * tDelta;

		// 普通帧间插值在跨帧期间保持前一帧的离散属性。
		tOutput.f = tSrc.f;
		tOutput.image = tSrc.image;
	}
}
