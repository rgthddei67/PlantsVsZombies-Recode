#include "Animator.h"
#include "../DeltaTime.h"
#include "../GameApp.h"
#include "../ResourceManager.h"
#include "../Logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
	thread_local AnimatorRenderProbe* gActiveRenderProbe = nullptr;

	/** 把最终 2x3 仿射单位四边形并入当前根 Animator 的世界包围盒。 */
	void RecordRenderQuad(float tA, float tB, float tC, float tD, float tx, float ty)
	{
		if (!gActiveRenderProbe) return;

		const float xs[4] = { tx, tx + tA, tx + tC, tx + tA + tC };
		const float ys[4] = { ty, ty + tB, ty + tD, ty + tB + tD };
		if (!gActiveRenderProbe->hasGeometry) {
			gActiveRenderProbe->minX = gActiveRenderProbe->maxX = xs[0];
			gActiveRenderProbe->minY = gActiveRenderProbe->maxY = ys[0];
			gActiveRenderProbe->hasGeometry = true;
		}
		for (int i = 0; i < 4; ++i) {
			gActiveRenderProbe->minX = std::min(gActiveRenderProbe->minX, xs[i]);
			gActiveRenderProbe->minY = std::min(gActiveRenderProbe->minY, ys[i]);
			gActiveRenderProbe->maxX = std::max(gActiveRenderProbe->maxX, xs[i]);
			gActiveRenderProbe->maxY = std::max(gActiveRenderProbe->maxY, ys[i]);
		}
		++gActiveRenderProbe->quadCount;
	}
}

Animator::Animator() {
	mReanim = nullptr;
}

Animator::Animator(std::shared_ptr<Reanimation> reanim) {
	Init(reanim);
}

Animator::~Animator() {
	Die();
}

void Animator::Die() {
	// 先让所有附加的子动画死亡
	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->Die();
			}
		}
		sparse.mAttachedReanims.clear();
	}
	mFrameEvents.clear();
	mIsPlaying = false;
}

void Animator::Init(std::shared_ptr<Reanimation> reanim) {
	mReanim = reanim;
	if (reanim) {
		mFPS = reanim->mFPS;
		mExtraInfos.clear();
		mSparseTrackStates.clear();
		mFrameEvents.clear();
		for (int i = 0; i < reanim->GetTrackCount(); i++) {
			auto track = reanim->GetTrack(i);
			if (track) {
				TrackExtraInfo extra;
				mExtraInfos.push_back(extra);
			}
		}

		mPlayingState = PlayState::PLAY_REPEAT;
		mIsPlaying = false;
		mTargetTrack = &InternRuntimeString("");
		mTargetTrackSpeed = 0.0f;
		mTargetTrackBlendTime = 0.5f;
	}
}

void Animator::AddFrameEventInternal(
	int frameIndex, InlineFrameCallback callback, bool persistent)
{
	if (!callback) return;
	// 现有 Animator 最多注册 4 个事件；首次预留一次即可覆盖普通僵尸的三个节点。
	if (mFrameEvents.empty()) mFrameEvents.reserve(4);
	const auto insertionPoint = std::upper_bound(
		mFrameEvents.begin(), mFrameEvents.end(), frameIndex,
		[](int frame, const FrameEvent& event) {
			return frame < event.frameIndex;
		});
	mFrameEvents.insert(insertionPoint,
		FrameEvent{ std::move(callback), frameIndex, persistent });
}

void Animator::ProcessFrameEventsAt(
	int frameIndex, std::vector<DeferredEvent>* outBuf)
{
	auto event = std::lower_bound(
		mFrameEvents.begin(), mFrameEvents.end(), frameIndex,
		[](const FrameEvent& candidate, int frame) {
			return candidate.frameIndex < frame;
		});
	while (event != mFrameEvents.end() && event->frameIndex == frameIndex) {
		InlineFrameCallback callback = event->callback;
		if (event->persistent) {
			++event;
		}
		else {
			event = mFrameEvents.erase(event);
		}

		if (outBuf) outBuf->push_back({ std::move(callback) });
		else callback();
	}
}

void Animator::ProcessFrameEventRange(
	int firstFrame, int lastFrame, std::vector<DeferredEvent>* outBuf)
{
	for (int frame = firstFrame; frame <= lastFrame; ++frame) {
		ProcessFrameEventsAt(frame, outBuf);
	}
}

bool Animator::PlayTrack(const std::string& trackName, float speed, float blendTime) {
	auto range = GetTrackRange(trackName);
	if (range.first == -1 || range.second == -1) {
		LOG_ERROR("Reanim") << "动画轨道不存在或为空: " << trackName;
		return false;
	}

	// 保存当前帧用于过渡
	mFrameIndexBlendBuffer = static_cast<int>(mFrameIndexNow);

	// 设置新的帧范围
	SetFrameRange(range.first, range.second);
	mFrameIndexNow = static_cast<float>(range.first);

	// 设置过渡效果
	if (blendTime > 0) {
		mReanimBlendCounterMax = blendTime;
		mReanimBlendCounter = blendTime;
	}
	else {
		mReanimBlendCounter = -1.0f;
	}

	// speed>0 → 本轨道绝对速度覆盖；speed==0 → 清除覆盖，回落到基础速度 mSpeed。
	// 递归到子动画，确保附加配件(铁桶/路障/报纸/头部)跟随同样的轨道速度。
	SetClipSpeed(speed > 0.0f ? speed : 0.0f);

	mIsPlaying = true;
	mPlayingState = PlayState::PLAY_REPEAT;
	mCurrentTrackName = &InternRuntimeString(trackName);

	return true;
}

bool Animator::PlayTrackOnce(const std::string& trackName, const std::string& returnTrack,
	float speed, float blendTime, float returnSpeed, float returnTrackBlendTime) {
	if (!PlayTrack(trackName, speed, blendTime)) {
		return false;
	}

	mPlayingState = PlayState::PLAY_ONCE_TO;
	mTargetTrack = &InternRuntimeString(returnTrack);
	mTargetTrackSpeed = returnSpeed;   // 回切时用，0=回落 base（保持旧行为）
	mTargetTrackBlendTime = returnTrackBlendTime;

	return true;
}

void Animator::Play(PlayState state) {
	mPlayingState = state;
	mIsPlaying = true;
}

void Animator::Pause() {
	mIsPlaying = false;
}

void Animator::PauseSubtree() {
	Pause();
	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			if (auto child = weakChild.lock()) {
				child->PauseSubtree();
			}
		}
	}
}

void Animator::Stop() {
	mIsPlaying = false;
	mFrameIndexNow = mFrameIndexBegin;
}

void Animator::Update() {
	if (!mIsPlaying || !mReanim) return;

	float deltaTime = DeltaTime::GetDeltaTime();
	float oldFrame = mFrameIndexNow;   // 记录更新前的帧索引

	// 帧索引前进：clip 覆盖优先于 base，再乘状态层(减速)
	float frameAdvance = deltaTime * mFPS * EffectiveSpeed();
	mFrameIndexNow += frameAdvance;

	// 处理动画结束/循环逻辑
	bool reachedEnd = mFrameIndexNow >= mFrameIndexEnd;
	if (reachedEnd) {
		switch (mPlayingState) {
		case PlayState::PLAY_REPEAT:
			mFrameIndexNow = mFrameIndexBegin;
			break;
		case PlayState::PLAY_ONCE:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			break;
		case PlayState::PLAY_ONCE_TO:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			if (!mTargetTrack->empty()) {
				PlayTrack(*mTargetTrack, mTargetTrackSpeed, mTargetTrackBlendTime);
			}
			mTargetTrack = &InternRuntimeString("");
			mTargetTrackSpeed = 0.0f;
			mTargetTrackBlendTime = 0.5f;
			break;
		case PlayState::PLAY_NONE:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			break;
		}
	}

	// 限制帧范围
	mFrameIndexNow = std::clamp(mFrameIndexNow, mFrameIndexBegin, mFrameIndexEnd);

	// ----- 触发帧事件（一次性触发后自动移除；持久事件保留）-----
	int oldInt = static_cast<int>(oldFrame);
	int newInt = static_cast<int>(mFrameIndexNow);

	if (newInt >= oldInt) {
		// 正常前进或不变
		ProcessFrameEventRange(oldInt + 1, newInt, nullptr);
	}
	else {
		// 发生了回绕（循环播放）
		int endInt = static_cast<int>(mFrameIndexEnd);
		ProcessFrameEventRange(oldInt + 1, endInt, nullptr);
		int beginInt = static_cast<int>(mFrameIndexBegin);
		ProcessFrameEventRange(beginInt, newInt, nullptr);
	}

	// 更新混合计时器
	if (mReanimBlendCounter > 0) {
		mReanimBlendCounter -= deltaTime;
		if (mReanimBlendCounter < 0) mReanimBlendCounter = 0;
	}

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->Update();
			}
		}
	}
}

void Animator::UpdateParallelDeferred(std::vector<DeferredEvent>& outBuf) {
	if (!mIsPlaying || !mReanim) return;

	float deltaTime = DeltaTime::GetDeltaTime();
	float oldFrame = mFrameIndexNow;

	float frameAdvance = deltaTime * mFPS * EffectiveSpeed();
	mFrameIndexNow += frameAdvance;

	bool reachedEnd = mFrameIndexNow >= mFrameIndexEnd;
	if (reachedEnd) {
		switch (mPlayingState) {
		case PlayState::PLAY_REPEAT:
			mFrameIndexNow = mFrameIndexBegin;
			break;
		case PlayState::PLAY_ONCE:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			break;
		case PlayState::PLAY_ONCE_TO:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			if (!mTargetTrack->empty()) {
				PlayTrack(*mTargetTrack, mTargetTrackSpeed, mTargetTrackBlendTime);
			}
			mTargetTrack = &InternRuntimeString("");
			mTargetTrackSpeed = 0.0f;
			mTargetTrackBlendTime = 0.5f;
			break;
		case PlayState::PLAY_NONE:
			mFrameIndexNow = mFrameIndexEnd;
			mIsPlaying = false;
			break;
		}
	}

	mFrameIndexNow = std::clamp(mFrameIndexNow, mFrameIndexBegin, mFrameIndexEnd);

	int oldInt = static_cast<int>(oldFrame);
	int newInt = static_cast<int>(mFrameIndexNow);

	if (newInt >= oldInt) {
		ProcessFrameEventRange(oldInt + 1, newInt, &outBuf);
	}
	else {
		int endInt = static_cast<int>(mFrameIndexEnd);
		ProcessFrameEventRange(oldInt + 1, endInt, &outBuf);
		int beginInt = static_cast<int>(mFrameIndexBegin);
		ProcessFrameEventRange(beginInt, newInt, &outBuf);
	}

	if (mReanimBlendCounter > 0) {
		mReanimBlendCounter -= deltaTime;
		if (mReanimBlendCounter < 0) mReanimBlendCounter = 0;
	}

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->UpdateParallelDeferred(outBuf);
			}
		}
	}
}

void Animator::Draw(Graphics* g, float baseX, float baseY, float Scale) {
	if (!mReanim || !g) return;

	// 附件通过 DrawInternal 递归而不会再次进入本函数；thread_local 让并行绘制的各根对象
	// 独立聚合，并保留嵌套调用时的旧探针以免污染外层。
	AnimatorRenderProbe* previousProbe = gActiveRenderProbe;
	if (GameAPP::mAutoTestMode) {
		mLastRenderProbe = {};
		mLastRenderProbe.baseX = baseX;
		mLastRenderProbe.baseY = baseY;
		mLastRenderProbe.objectScale = Scale;
		mLastRenderProbe.usedInstancePath = g->IsInstancePathEnabled();
		gActiveRenderProbe = &mLastRenderProbe;
	}

	// 保存当前变换栈，确保不叠加额外变换
	g->PushTransform(glm::mat4(1.0f));
	DrawInternal(g, baseX, baseY, Scale);
	g->PopTransform();
	if (GameAPP::mAutoTestMode)
		gActiveRenderProbe = previousProbe;
}

namespace {
	// Pack RGBA8 with r=lsb, a=msb — matches reanim_inst.vert.glsl unpack convention.
	inline uint32_t PackRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		return  static_cast<uint32_t>(r)
			| (static_cast<uint32_t>(g) << 8)
			| (static_cast<uint32_t>(b) << 16)
			| (static_cast<uint32_t>(a) << 24);
	}

	struct ReanimBasis {
		float tA;
		float tB;
		float tC;
		float tD;
	};

	struct ReanimSinCos {
		float sinValue;
		float cosValue;
	};

	constexpr int kReanimTrigTableSize = 2048;  // 线性插值三角表采样数；16 KiB 常驻、最大误差约 1.2e-6
	const std::array<ReanimSinCos, kReanimTrigTableSize + 1> kReanimTrigTable = [] {
		std::array<ReanimSinCos, kReanimTrigTableSize + 1> table{};
		constexpr float kTwoPi = 6.28318530717958647692f;
		for (int i = 0; i <= kReanimTrigTableSize; ++i) {
			const float radians = kTwoPi * static_cast<float>(i)
				/ static_cast<float>(kReanimTrigTableSize);
			table[i] = { sinf(radians), cosf(radians) };
		}
		return table;
	}();

	/** 查表并线性插值得到 sin(-degrees) / cos(-degrees)，避免每部件调用 CRT 三角函数。 */
	inline ReanimSinCos LookupNegativeDegreeSinCos(float degrees) {
		float wrapped = 360.0f - degrees;
		if (wrapped >= 360.0f) wrapped -= 360.0f;
		else if (wrapped < 0.0f) wrapped += 360.0f;
		// reanim 通常只略越过 0/360；异常大角度走低频兜底，避免查表越界。
		if (wrapped < 0.0f || wrapped >= 360.0f) {
			wrapped = fmodf(wrapped, 360.0f);
			if (wrapped < 0.0f) wrapped += 360.0f;
		}
		const float tablePosition = wrapped
			* (static_cast<float>(kReanimTrigTableSize) / 360.0f);
		const int index = static_cast<int>(tablePosition);
		const float fraction = tablePosition - static_cast<float>(index);
		const ReanimSinCos& a = kReanimTrigTable[index];
		const ReanimSinCos& b = kReanimTrigTable[index + 1];
		return {
			a.sinValue + (b.sinValue - a.sinValue) * fraction,
			a.cosValue + (b.cosValue - a.cosValue) * fraction,
		};
	}

	/**
	 * @brief 把 Reanim 的双轴角度与缩放转换成 2x2 仿射基。
	 *
	 * 普通旋转轨道会把同一角度同时写入 kx/ky；相等时复用同一组查表结果。
	 * 2048 点线性插值把理论位置误差压到远低于子像素，同时移除热路径 CRT 三角函数。
	 */
	inline ReanimBasis ComputeReanimBasis(const TrackFrameTransform& transform) {
		const ReanimSinCos trigX = LookupNegativeDegreeSinCos(transform.kx);
		const float cosX = trigX.cosValue;
		const float sinX = trigX.sinValue;

		float cosY = cosX;
		float sinY = sinX;
		if (transform.kx != transform.ky) {
			const ReanimSinCos trigY = LookupNegativeDegreeSinCos(transform.ky);
			cosY = trigY.cosValue;
			sinY = trigY.sinValue;
		}

		return {
			cosX * transform.sx,
			-sinX * transform.sx,
			sinY * transform.sy,
			cosY * transform.sy,
		};
	}
}

void Animator::DrawInternalInstanced(Graphics* g, float baseX, float baseY, float Scale) const {
	const BlendMode baseBlend = !mEnableWashedOutEffect ? BlendMode::Alpha
		: mUseLessWashedOutEffect ? BlendMode::LessWashedOut : BlendMode::WashedOut;
	const bool hasEnabledTrackGlow = mEnableExtraAdditiveDraw
		|| std::any_of(mExtraInfos.begin(), mExtraInfos.end(), [](const TrackExtraInfo& extra) {
			return extra.mHasGlowOverride && extra.mGlowOverrideEnabled;
		});
	// 无附件、覆盖层和发光的常规 Animator 全程只用一种混合模式。worker 可一次
	// 取得连续映射窗口，仍按轨道原顺序直写，避免每个部件重复进入 Graphics 状态机。
	Graphics::ReanimInstanceWriteSpan directSpan;
	const uint32_t trackCount = static_cast<uint32_t>(mReanim->GetTrackCount());
	const bool directWrite = mSparseTrackStates.empty()
		&& !mEnableExtraOverlayDraw && !hasEnabledTrackGlow
		&& g->BeginReanimInstanceWrite(trackCount, baseBlend, directSpan);
	uint32_t directCount = 0;
	struct DeferredFollowerInstance {
		InstanceRecord record;
		uint8_t overlayAlpha = 0;
		bool glowEnabled = false;
	};
	const auto submitFollower = [this, g, baseBlend](const DeferredFollowerInstance& draw) {
		g->AppendReanimInstance(draw.record, baseBlend);
		if (draw.overlayAlpha > 0) {
			InstanceRecord overlay = draw.record;
			overlay.colorRGBA8 = PackRGBA8(mExtraOverlayColor.r,
				mExtraOverlayColor.g, mExtraOverlayColor.b, draw.overlayAlpha);
			g->AppendReanimInstance(overlay, BlendMode::Alpha);
		}
		if (draw.glowEnabled) {
			InstanceRecord glow = draw.record;
			glow.colorRGBA8 = PackRGBA8(mExtraAdditiveColor.r,
				mExtraAdditiveColor.g, mExtraAdditiveColor.b, mExtraAdditiveColor.a);
			g->AppendReanimInstance(glow, BlendMode::Add);
		}
	};
	DeferredFollowerInstance firstDeferredFollowerInstance{};
	bool hasDeferredFollowerInstance = false;
	std::vector<DeferredFollowerInstance> deferredFollowerOverflow;
	size_t sparseIndex = 0;

	float blendRatio = 0.0f;
	if (mReanimBlendCounter > 0.0f)
		blendRatio = 1.0f - mReanimBlendCounter / mReanimBlendCounterMax;

	for (int i = 0; i < static_cast<int>(trackCount); ++i) {
		auto track = mReanim->GetTrack(i);
		if (!track || !track->mAvailable || track->mFrames.empty()) continue;
		while (sparseIndex < mSparseTrackStates.size()
			&& mSparseTrackStates[sparseIndex].mTrackIndex < i) {
			++sparseIndex;
		}
		const SparseTrackState* sparse = sparseIndex < mSparseTrackStates.size()
			&& mSparseTrackStates[sparseIndex].mTrackIndex == i
			? &mSparseTrackStates[sparseIndex] : nullptr;

		const TrackFrameTransform transform = GetInterpolatedTransform(i, blendRatio);
		const TrackExtraInfo* extra = i < static_cast<int>(mExtraInfos.size())
			? &mExtraInfos[i] : nullptr;
		const bool shouldDrawSelf = extra && extra->mVisible && transform.f != -1;
		const Texture* image = shouldDrawSelf
			? (extra->mImage ? extra->mImage : transform.image)
			: nullptr;
		const bool canDrawFollowers = extra && extra->mVisible && sparse
			&& transform.f != -1;
		const bool hasVisibleFollower = canDrawFollowers && std::any_of(
			sparse->mFollowers.begin(), sparse->mFollowers.end(),
			[](const TrackFollowerState& follower) {
				return follower.mVisible && follower.mImage;
			});

		// 无本体、跟随贴图和附件的轨道不需要仿射数据；保住隐藏轨道的实例快路径。
		if (!image && !hasVisibleFollower
			&& (!sparse || sparse->mAttachedReanims.empty())) continue;

		// GATE A 曾测得重复双轴三角计算约占 6 ms CPU sum；相等角度现在只算一组。
		// 附件定位继续复用同一结果，确保父轨道本体与子 Animator 不重复计算。
		const ReanimBasis basis = ComputeReanimBasis(transform);
		const float tA = basis.tA;
		const float tB = basis.tB;
		const float tC = basis.tC;
		const float tD = basis.tD;
		const float tx = transform.x + (extra ? extra->mOffsetX : 0.0f);
		const float ty = transform.y + (extra ? extra->mOffsetY : 0.0f);

		if (image) {
			const float w = static_cast<float>(image->width);
			const float h = static_cast<float>(image->height);

			InstanceRecord localRec;
			InstanceRecord& rec = directWrite
				? directSpan.records[directCount++] : localRec;
			// 把图像尺寸与全局缩放烘进 2x3 仿射列，保持与慢路径矩阵逐项等价。
			rec.tA = tA * w * Scale;
			rec.tB = tB * w * Scale;
			rec.tC = tC * h * Scale;
			rec.tD = tD * h * Scale;
			rec.tx = baseX + tx * Scale;
			rec.ty = baseY + ty * Scale;

			// 水平镜像：世界 x' = 2*(baseX + pivot*Scale) - x → x 行取负、平移分量绕 pivot 反射。
			// glow/overlay 复制 rec，翻转天然一并生效。
			if (mFlipX) {
				rec.tA = -rec.tA;
				rec.tC = -rec.tC;
				rec.tx = baseX + (2.0f * mFlipPivotX - tx) * Scale;
			}
			ApplyRenderScale(rec);
			RecordRenderQuad(rec.tA, rec.tB, rec.tC, rec.tD, rec.tx, rec.ty);

			// 图集子图只改 UV，实例仍绑定所属 atlas page 的 bindless 槽位。
			const Texture* bindTex = image->atlasPage ? image->atlasPage : image;
			rec.u0 = image->aU0; rec.v0 = image->aV0;
			rec.u1 = image->aU1; rec.v1 = image->aV1;
			rec.texSlot = bindTex->BindingId();

			// 本体 → overlay → glow 的相对顺序是视觉契约，子 Animator 必须排在三者之后。
			const SDL_Color trackColor = extra
				? extra->mColor : SDL_Color{ 255, 255, 255, 255 };
			const float baseAlpha = std::clamp(transform.a * mAlpha
				* (static_cast<float>(trackColor.a) / 255.0f), 0.0f, 1.0f);
			const uint8_t alpha8 = static_cast<uint8_t>(baseAlpha * 255.0f);
			rec.colorRGBA8 = PackRGBA8(
				trackColor.r, trackColor.g, trackColor.b, alpha8);
			if (!directWrite) {
				g->AppendReanimInstance(rec, baseBlend);
			}

			if (mEnableExtraOverlayDraw) {
				InstanceRecord ov = rec;
				const uint8_t ovAlpha = static_cast<uint8_t>(mExtraOverlayColor.a * baseAlpha);
				ov.colorRGBA8 = PackRGBA8(mExtraOverlayColor.r,
					mExtraOverlayColor.g,
					mExtraOverlayColor.b,
					ovAlpha);
				g->AppendReanimInstance(ov, BlendMode::Alpha);
			}

			if (hasEnabledTrackGlow && IsGlowEffectEnabledForTrack(i)) {
				InstanceRecord glow = rec;
				glow.colorRGBA8 = PackRGBA8(mExtraAdditiveColor.r,
					mExtraAdditiveColor.g,
					mExtraAdditiveColor.b,
					mExtraAdditiveColor.a);
				g->AppendReanimInstance(glow, BlendMode::Add);
			}
		}

		if (hasVisibleFollower) {
			for (const TrackFollowerState& follower : sparse->mFollowers) {
				const Texture* followerImage = follower.mVisible ? follower.mImage : nullptr;
				if (!followerImage) continue;
				const float followerX = tx + tA * follower.mOffsetX
					+ tC * follower.mOffsetY;
				const float followerY = ty + tB * follower.mOffsetX
					+ tD * follower.mOffsetY;
				const float w = static_cast<float>(followerImage->width);
				const float h = static_cast<float>(followerImage->height);

				InstanceRecord rec;
				rec.tA = tA * w * Scale * follower.mScaleX;
				rec.tB = tB * w * Scale * follower.mScaleX;
				rec.tC = tC * h * Scale * follower.mScaleY;
				rec.tD = tD * h * Scale * follower.mScaleY;
				rec.tx = baseX + followerX * Scale;
				rec.ty = baseY + followerY * Scale;
				if (mFlipX) {
					rec.tA = -rec.tA;
					rec.tC = -rec.tC;
					rec.tx = baseX + (2.0f * mFlipPivotX - followerX) * Scale;
				}
				ApplyRenderScale(rec);
				RecordRenderQuad(rec.tA, rec.tB, rec.tC, rec.tD, rec.tx, rec.ty);

				const Texture* bindTex = followerImage->atlasPage
					? followerImage->atlasPage : followerImage;
				rec.u0 = followerImage->aU0; rec.v0 = followerImage->aV0;
				rec.u1 = followerImage->aU1; rec.v1 = followerImage->aV1;
				rec.texSlot = bindTex->BindingId();
				const SDL_Color trackColor = extra
					? extra->mColor : SDL_Color{ 255, 255, 255, 255 };
				const float alpha = std::clamp(transform.a * mAlpha
					* (static_cast<float>(trackColor.a) / 255.0f), 0.0f, 1.0f);
				rec.colorRGBA8 = PackRGBA8(trackColor.r, trackColor.g, trackColor.b,
					static_cast<uint8_t>(alpha * 255.0f));
				const uint8_t followerOverlayAlpha = follower.mInheritOverlayEffect
					&& mEnableExtraOverlayDraw
					? static_cast<uint8_t>(mExtraOverlayColor.a * alpha) : 0;
				const bool followerGlowEnabled = follower.mInheritGlowEffect
					&& hasEnabledTrackGlow && IsGlowEffectEnabledForTrack(i);
				const DeferredFollowerInstance draw{
					rec, followerOverlayAlpha, followerGlowEnabled
				};
				if (follower.mDrawAfterAllTracks) {
					if (!hasDeferredFollowerInstance) {
						firstDeferredFollowerInstance = draw;
						hasDeferredFollowerInstance = true;
					}
					else {
						deferredFollowerOverflow.push_back(draw);
					}
				}
				else {
					submitFollower(draw);
				}
			}
		}

		if (!sparse) continue;

		// 在当前父轨道实例之后立即递归，既保持 reanim 轨道交错顺序，也让任意深度附件
		// 继续写入同一实例流；不可见的父轨道仍可作为附件锚点，与慢路径语义一致。
		for (const auto& weakChild : sparse->mAttachedReanims) {
			auto child = weakChild.lock();
			if (!child || !child->mReanim) continue;

			const float childX = child->mLocalPosX;
			const float childY = child->mLocalPosY;
			const float worldX = baseX + (tx + tA * childX + tC * childY) * Scale;
			const float worldY = baseY + (ty + tB * childX + tD * childY) * Scale;
			child->DrawInternalInstanced(g, worldX, worldY, 1.0f);
		}
	}

	if (hasDeferredFollowerInstance) {
		submitFollower(firstDeferredFollowerInstance);
	}
	for (const DeferredFollowerInstance& draw : deferredFollowerOverflow) {
		submitFollower(draw);
	}
	if (directWrite) g->EndReanimInstanceWrite(directSpan, directCount);
}

void Animator::DrawInternal(Graphics* g, float baseX, float baseY, float Scale) const {
	if (!mReanim) return;
	const BlendMode baseBlend = !mEnableWashedOutEffect ? BlendMode::Alpha
		: mUseLessWashedOutEffect ? BlendMode::LessWashedOut : BlendMode::WashedOut;

	// 生产路径统一递归实例化整棵 Animator 附件树；慢路径只由 -NoInstance 显式保留，
	// 用作视觉 A/B 与故障兜底，不再因存在子 Animator 让整棵父级退化成逐顶点提交。
	if (g->IsInstancePathEnabled() && !g->IsClipActive()) {
		DrawInternalInstanced(g, baseX, baseY, Scale);
		return;
	}

	struct DeferredFollowerDraw {
		const Texture* image;
		glm::mat4 transform;
		glm::vec4 color;
		float overlayAlpha = 0.0f;
		bool glowEnabled = false;
	};
	const auto submitFollower = [this, g, baseBlend](const DeferredFollowerDraw& draw) {
		g->DrawTextureMatrix(draw.image, draw.transform, 0.0f, 0.0f,
			draw.color, baseBlend);
		if (draw.overlayAlpha > 0.0f) {
			const glm::vec4 overlayColor(mExtraOverlayColor.r,
				mExtraOverlayColor.g, mExtraOverlayColor.b, draw.overlayAlpha);
			g->DrawTextureMatrix(draw.image, draw.transform, 0.0f, 0.0f,
				overlayColor, BlendMode::Alpha);
		}
		if (draw.glowEnabled) {
			const glm::vec4 glowColor(mExtraAdditiveColor.r,
				mExtraAdditiveColor.g, mExtraAdditiveColor.b, mExtraAdditiveColor.a);
			g->DrawTextureMatrix(draw.image, draw.transform, 0.0f, 0.0f,
				glowColor, BlendMode::Add);
		}
	};
	DeferredFollowerDraw firstDeferredFollowerDraw{};
	bool hasDeferredFollowerDraw = false;
	std::vector<DeferredFollowerDraw> deferredFollowerOverflow;
	size_t sparseIndex = 0;

	// 预计算混合比例，避免在轨道循环内每次做浮点除法
	float blendRatio = 0.0f;
	if (mReanimBlendCounter > 0.0f)
		blendRatio = 1.0f - mReanimBlendCounter / mReanimBlendCounterMax;

	for (int i = 0; i < static_cast<int>(mReanim->GetTrackCount()); ++i) {
		auto track = mReanim->GetTrack(i);
		if (!track || !track->mAvailable || track->mFrames.empty()) continue;
		while (sparseIndex < mSparseTrackStates.size()
			&& mSparseTrackStates[sparseIndex].mTrackIndex < i) {
			++sparseIndex;
		}
		const SparseTrackState* sparse = sparseIndex < mSparseTrackStates.size()
			&& mSparseTrackStates[sparseIndex].mTrackIndex == i
			? &mSparseTrackStates[sparseIndex] : nullptr;

		TrackFrameTransform transform = GetInterpolatedTransform(i, blendRatio);

		// 对本轨道 transform 只计算一次三角，自绘块和子动画块共用
		const ReanimBasis basis = ComputeReanimBasis(transform);
		const float tA = basis.tA;
		const float tB = basis.tB;
		const float tC = basis.tC;
		const float tD = basis.tD;

		const TrackExtraInfo* extra = i < static_cast<int>(mExtraInfos.size())
			? &mExtraInfos[i] : nullptr;
		bool shouldDrawSelf = extra && extra->mVisible && transform.f != -1;
		const Texture* image = nullptr;

		if (shouldDrawSelf) {
			image = extra->mImage ? extra->mImage : transform.image;
			shouldDrawSelf = (image != nullptr);
		}
		const bool canDrawFollowers = extra && extra->mVisible && sparse
			&& transform.f != -1;
		const bool hasVisibleFollower = canDrawFollowers && std::any_of(
			sparse->mFollowers.begin(), sparse->mFollowers.end(),
			[](const TrackFollowerState& follower) {
				return follower.mVisible && follower.mImage;
			});
		const float tx = transform.x + (extra ? extra->mOffsetX : 0.0f);
		const float ty = transform.y + (extra ? extra->mOffsetY : 0.0f);

		if (shouldDrawSelf) {
			int imgWidth = image->width;
			int imgHeight = image->height;
			float w = static_cast<float>(imgWidth);
			float h = static_cast<float>(imgHeight);

			// 直接构造仿射变换矩阵（将单位矩形映射到目标四边形，省去中间顶点计算）
			glm::mat4 mat(
				tA * w * Scale, tB * w * Scale, 0.0f, 0.0f,
				tC * h * Scale, tD * h * Scale, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				baseX + tx * Scale, baseY + ty * Scale, 0.0f, 1.0f
			);

			if (mFlipX) {
				mat[0][0] = -mat[0][0];
				mat[1][0] = -mat[1][0];
				mat[3][0] = baseX + (2.0f * mFlipPivotX - tx) * Scale;
			}
			ApplyRenderScale(mat);
			RecordRenderQuad(
				mat[0][0], mat[0][1], mat[1][0], mat[1][1], mat[3][0], mat[3][1]);

			const SDL_Color trackColor = extra
				? extra->mColor : SDL_Color{ 255, 255, 255, 255 };
			float combinedAlpha = transform.a * mAlpha
				* (static_cast<float>(trackColor.a) / 255.0f);
			float baseAlpha = std::clamp(combinedAlpha, 0.0f, 1.0f);

			// 模仿者直接替换本体采样颜色，避免在半透明边缘重复叠绘产生白边。
			glm::vec4 baseColor(trackColor.r, trackColor.g, trackColor.b,
				baseAlpha * 255.0f);
			g->DrawTextureMatrix(image, mat, 0.0f, 0.0f, baseColor, baseBlend);

			// 覆盖层效果（Alpha 混合，颜色需乘以基础透明度）。
			// 必须排在下面的发光之前：高 alpha 的覆盖层（如冰冻减速 a=240）若画在发光之后，
			// 会以接近不透明的 Alpha 叠绘把发光盖死。原版引擎（Reanimation.cs）把色调揉进
			// 正常绘制、additive 高亮永远最后一遍——此处“覆盖层→发光”的顺序即还原该语义。
			if (mEnableExtraOverlayDraw) {
				glm::vec4 overlayColor(mExtraOverlayColor.r,
					mExtraOverlayColor.g,
					mExtraOverlayColor.b,
					mExtraOverlayColor.a * baseAlpha);
				g->DrawTextureMatrix(image, mat, 0.0f, 0.0f, overlayColor, BlendMode::Alpha);
			}

			// 发光效果（叠加混合）——最后绘制，使加色提亮叠在覆盖层之上（减速白光保持可见）
			if (IsGlowEffectEnabledForTrack(i)) {
				glm::vec4 glowColor(mExtraAdditiveColor.r,
					mExtraAdditiveColor.g,
					mExtraAdditiveColor.b,
					mExtraAdditiveColor.a);
				g->DrawTextureMatrix(image, mat, 0.0f, 0.0f, glowColor, BlendMode::Add);
			}
		}

		if (hasVisibleFollower) {
			for (const TrackFollowerState& follower : sparse->mFollowers) {
				const Texture* followerImage = follower.mVisible ? follower.mImage : nullptr;
				if (!followerImage) continue;
				const float followerX = tx + tA * follower.mOffsetX
					+ tC * follower.mOffsetY;
				const float followerY = ty + tB * follower.mOffsetX
					+ tD * follower.mOffsetY;
				const float w = static_cast<float>(followerImage->width);
				const float h = static_cast<float>(followerImage->height);
				glm::mat4 mat(
					tA * w * Scale * follower.mScaleX,
					tB * w * Scale * follower.mScaleX, 0.0f, 0.0f,
					tC * h * Scale * follower.mScaleY,
					tD * h * Scale * follower.mScaleY, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					baseX + followerX * Scale, baseY + followerY * Scale, 0.0f, 1.0f);
				if (mFlipX) {
					mat[0][0] = -mat[0][0];
					mat[1][0] = -mat[1][0];
					mat[3][0] = baseX + (2.0f * mFlipPivotX - followerX) * Scale;
				}
				ApplyRenderScale(mat);
				RecordRenderQuad(
					mat[0][0], mat[0][1], mat[1][0], mat[1][1], mat[3][0], mat[3][1]);
				const SDL_Color trackColor = extra
					? extra->mColor : SDL_Color{ 255, 255, 255, 255 };
				const float alpha = std::clamp(transform.a * mAlpha
					* (static_cast<float>(trackColor.a) / 255.0f), 0.0f, 1.0f);
				const glm::vec4 color(trackColor.r, trackColor.g, trackColor.b,
					alpha * 255.0f);
				const float followerOverlayAlpha = follower.mInheritOverlayEffect
					&& mEnableExtraOverlayDraw
					? mExtraOverlayColor.a * alpha : 0.0f;
				const bool followerGlowEnabled = follower.mInheritGlowEffect
					&& IsGlowEffectEnabledForTrack(i);
				const DeferredFollowerDraw draw{
					followerImage, mat, color, followerOverlayAlpha,
					followerGlowEnabled
				};
				if (follower.mDrawAfterAllTracks) {
					if (!hasDeferredFollowerDraw) {
						firstDeferredFollowerDraw = draw;
						hasDeferredFollowerDraw = true;
					}
					else {
						deferredFollowerOverflow.push_back(draw);
					}
				}
				else {
					submitFollower(draw);
				}
			}
		}

		// 子动画
		if (sparse) {
			for (const auto& weakChild : sparse->mAttachedReanims) {
				auto child = weakChild.lock();
				if (!child || !child->mReanim) continue;

				// 复用本轨道已计算的 tA/tB/tC/tD，无需重复三角运算
				float childX = child->mLocalPosX;
				float childY = child->mLocalPosY;

				float worldX = tx + tA * childX + tC * childY;
				float worldY = ty + tB * childX + tD * childY;

				worldX = baseX + worldX * Scale;
				worldY = baseY + worldY * Scale;

				child->DrawInternal(g, worldX, worldY, 1.0f);
			}
		}
	}

	if (hasDeferredFollowerDraw) {
		submitFollower(firstDeferredFollowerDraw);
	}
	for (const DeferredFollowerDraw& draw : deferredFollowerOverflow) {
		submitFollower(draw);
	}
}

void Animator::SetSpeed(float speed) {
	this->mSpeed = speed;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetSpeed(speed);
			}
		}
	}
}

void Animator::SetClipSpeed(float clipSpeed) {
	this->mClipSpeed = clipSpeed;

	// 递归到附加子动画，复刻旧 SetSpeed 的传播语义：
	// 父轨道切到 eat(2.1)/walk(回落) 时，附加配件同步同样的速度
	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetClipSpeed(clipSpeed);
			}
		}
	}
}

void Animator::SetAlpha(float alpha) {
	this->mAlpha = alpha;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetAlpha(alpha);
			}
		}
	}
}

void Animator::SetGlowColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
	this->mExtraAdditiveColor = { r, g, b, a };

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetGlowColor(r, g, b, a);
			}
		}
	}
}

void Animator::SetOverlayColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
	this->mExtraOverlayColor = { r, g, b, a };

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetOverlayColor(r, g, b, a);
			}
		}
	}
}

void Animator::EnableGlowEffect(bool enable) {
	this->mEnableExtraAdditiveDraw = enable;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->EnableGlowEffect(enable);
			}
		}
	}
}

void Animator::EnableOverlayEffect(bool enable) {
	this->mEnableExtraOverlayDraw = enable;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->EnableOverlayEffect(enable);
			}
		}
	}
}

void Animator::SetTrackVisible(const std::string& trackName, bool visible) {
	for (auto& extra : GetTrackExtrasByName(trackName)) {
		extra->mVisible = visible;
	}
}

void Animator::SetTrackColor(const std::string& trackName, const SDL_Color& color) {
	for (auto& extra : GetTrackExtrasByName(trackName)) {
		extra->mColor = color;
	}
}

void Animator::SetTrackGlowOverride(const std::string& trackName, bool enable) {
	for (auto& extra : GetTrackExtrasByName(trackName)) {
		extra->mHasGlowOverride = true;
		extra->mGlowOverrideEnabled = enable;
	}
}

void Animator::SetTrackImage(const std::string& trackName, const Texture* image) {
	for (auto& extra : GetTrackExtrasByName(trackName)) {
		extra->mImage = image;
	}
}

void Animator::SetTrackOffset(const std::string& trackName, float x, float y) {
	for (auto& extra : GetTrackExtrasByName(trackName)) {
		extra->mOffsetX = x;
		extra->mOffsetY = y;
	}
}

void Animator::SetTrackFollowerImage(const std::string& trackName,
	const std::string& followerName, const Texture* image,
	float offsetX, float offsetY, float scaleX, float scaleY,
	bool drawAfterAllTracks, bool inheritOverlayEffect, bool inheritGlowEffect) {
	for (const int trackIndex : GetTrackIndicesByName(trackName)) {
		SparseTrackState* existing = FindSparseTrackState(trackIndex);
		if (!image && !existing) continue;
		SparseTrackState& sparse = existing ? *existing : GetOrCreateSparseTrackState(trackIndex);
		auto follower = std::find_if(sparse.mFollowers.begin(), sparse.mFollowers.end(),
			[&followerName](const TrackFollowerState& state) {
				return state.mName == followerName;
			});
		if (!image) {
			if (follower != sparse.mFollowers.end()) sparse.mFollowers.erase(follower);
			continue;
		}
		if (follower == sparse.mFollowers.end()) {
			sparse.mFollowers.push_back({});
			follower = std::prev(sparse.mFollowers.end());
			follower->mName = followerName;
		}
		follower->mImage = image;
		follower->mOffsetX = offsetX;
		follower->mOffsetY = offsetY;
		follower->mScaleX = scaleX;
		follower->mScaleY = scaleY;
		follower->mDrawAfterAllTracks = drawAfterAllTracks;
		follower->mInheritOverlayEffect = inheritOverlayEffect;
		follower->mInheritGlowEffect = inheritGlowEffect;
	}
}

void Animator::EnableWashedOutEffect(bool enable, bool lighter) {
	mEnableWashedOutEffect = enable;
	mUseLessWashedOutEffect = enable && lighter;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) child->EnableWashedOutEffect(enable, lighter);
		}
	}
}

void Animator::SetTrackFollowerVisible(const std::string& trackName,
	const std::string& followerName, bool visible) {
	for (const int trackIndex : GetTrackIndicesByName(trackName)) {
		SparseTrackState* sparse = FindSparseTrackState(trackIndex);
		if (!sparse) continue;
		auto follower = std::find_if(sparse->mFollowers.begin(), sparse->mFollowers.end(),
			[&followerName](const TrackFollowerState& state) {
				return state.mName == followerName;
			});
		if (follower != sparse->mFollowers.end()) {
			follower->mVisible = visible && follower->mImage;
		}
	}
}

void Animator::SetLocalPosition(float x, float y) {
	mLocalPosX = x;
	mLocalPosY = y;
}

void Animator::SetLocalPosition(const Vector& position) {
	this->SetLocalPosition(position.x, position.y);
}

void Animator::SetLocalScale(float sx, float sy) {
	mLocalScaleX = sx;
	mLocalScaleY = sy;
}

void Animator::SetRenderScale(float scaleX, float scaleY, float pivotX, float pivotY) {
	mRenderScaleX = scaleX;
	mRenderScaleY = scaleY;
	mRenderPivotX = pivotX;
	mRenderPivotY = pivotY;

	// 子 Animator 可能继续拥有自己的附件；递归同步后所有层级走同一世界锚点。
	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			if (auto child = weakChild.lock()) {
				child->SetRenderScale(scaleX, scaleY, pivotX, pivotY);
			}
		}
	}
}

void Animator::ApplyRenderScale(InstanceRecord& record) const {
	record.tA *= mRenderScaleX;
	record.tC *= mRenderScaleX;
	record.tx = mRenderPivotX + (record.tx - mRenderPivotX) * mRenderScaleX;

	record.tB *= mRenderScaleY;
	record.tD *= mRenderScaleY;
	record.ty = mRenderPivotY + (record.ty - mRenderPivotY) * mRenderScaleY;
}

void Animator::ApplyRenderScale(glm::mat4& matrix) const {
	matrix[0][0] *= mRenderScaleX;
	matrix[1][0] *= mRenderScaleX;
	matrix[3][0] = mRenderPivotX + (matrix[3][0] - mRenderPivotX) * mRenderScaleX;

	matrix[0][1] *= mRenderScaleY;
	matrix[1][1] *= mRenderScaleY;
	matrix[3][1] = mRenderPivotY + (matrix[3][1] - mRenderPivotY) * mRenderScaleY;
}

void Animator::SetLocalRotation(float rotation) {
	mLocalRotation = rotation;
}

void Animator::SetFlipX(bool flip, float pivotX) {
	mFlipX = flip;
	mFlipPivotX = pivotX;
}

bool Animator::AttachAnimator(const std::string& trackName, std::shared_ptr<Animator> child) {
	if (!mReanim || !child || child.get() == this) {
		return false;
	}

	auto trackIndices = GetTrackIndicesByName(trackName);
	if (trackIndices.empty()) {
		return false;
	}

	for (const int trackIndex : trackIndices) {
		auto& sparse = GetOrCreateSparseTrackState(trackIndex);
		// 避免重复添加
		bool alreadyExists = false;
		for (const auto& weak : sparse.mAttachedReanims) {
			if (auto existing = weak.lock()) {
				if (existing == child) {
					alreadyExists = true;
					break;
				}
			}
		}
		if (!alreadyExists) {
			sparse.mAttachedReanims.push_back(child);
		}
	}
	// 若父 Animator 已处于压扁等世界绘制变换，新挂件从第一帧起继承，避免短暂弹回原形。
	child->SetRenderScale(mRenderScaleX, mRenderScaleY, mRenderPivotX, mRenderPivotY);
	return true;
}

void Animator::DetachAnimator(const std::string& trackName, std::shared_ptr<Animator> child) {
	for (const int trackIndex : GetTrackIndicesByName(trackName)) {
		auto* sparse = FindSparseTrackState(trackIndex);
		if (!sparse) continue;
		auto& vec = sparse->mAttachedReanims;
		vec.erase(std::remove_if(vec.begin(), vec.end(),
			[&child](const std::weak_ptr<Animator>& weak) {
				auto sp = weak.lock();
				return sp == child || !sp; // 移除指定对象或已失效的
			}),
			vec.end());
	}
}

void Animator::DetachAllAnimators() {
	for (auto& sparse : mSparseTrackStates) {
		sparse.mAttachedReanims.clear();
	}
}

std::pair<int, int> Animator::GetTrackRange(const std::string& trackName) {
	if (!mReanim) {
		LOG_DEBUG("Reanim") << "GetTrackRange: mReanim is null";
		return { -1, -1 };
	}

	return mReanim->GetTrackFrameRange(trackName);
}

void Animator::SetFrameRange(int frameBegin, int frameEnd) {
	mFrameIndexBegin = static_cast<float>(frameBegin);
	mFrameIndexEnd = static_cast<float>(frameEnd);
	mFrameIndexNow = static_cast<float>(frameBegin);
}

void Animator::SetFrameRangeByTrackName(const std::string& trackName) {
	auto range = GetTrackRange(trackName);
	SetFrameRange(range.first, range.second);
}

void Animator::SetFrameRangeToDefault() {
	if (mReanim) {
		mFrameIndexBegin = 0;
		mFrameIndexEnd = static_cast<float>(mReanim->GetTotalFrames() - 1);
	}
}

float Animator::GetTrackVelocity(const std::string& trackName) const {
	int index = GetFirstTrackIndexByName(trackName);
	if (index < 0) return 0.0f;
	return GetTrackVelocity(index);
}

float Animator::GetTrackVelocity(int trackIndex) const {
	if (!mReanim) return 0.0f;

	auto* track = mReanim->GetTrack(trackIndex);
	if (!track || track->mFrames.empty()) return 0.0f;

	int frameBefore = static_cast<int>(mFrameIndexNow);
	int maxIndex = static_cast<int>(track->mFrames.size()) - 1;
	frameBefore = std::clamp(frameBefore, 0, maxIndex);
	int frameAfter = std::min(frameBefore + 1, maxIndex);

	float xBefore = track->mFrames[frameBefore].x;
	float xAfter = track->mFrames[frameAfter].x;
	float dx = xAfter - xBefore;

	float velocity = dx * EffectiveSpeed();
	return std::abs(velocity);
}

float Animator::GetTrackAverageVelocity(const std::string& trackName) const {
	const int index = GetFirstTrackIndexByName(trackName);
	if (index < 0) return 0.0f;
	return GetTrackAverageVelocity(index);
}

float Animator::GetTrackAverageVelocity(int trackIndex) const {
	if (!mReanim) return 0.0f;

	const auto* track = mReanim->GetTrack(trackIndex);
	if (!track || track->mFrames.empty()) return 0.0f;

	const int maxIndex = static_cast<int>(track->mFrames.size()) - 1;
	const int frameBegin = std::clamp(
		static_cast<int>(mFrameIndexBegin), 0, maxIndex);
	const int frameEnd = std::clamp(
		static_cast<int>(mFrameIndexEnd), 0, maxIndex);
	if (frameEnd <= frameBegin) return 0.0f;

	// 实际根运动逐帧取位移绝对值；预测也对同一片段积分，避免把步态停顿帧或跨步帧
	// 误当成未来整段时间的恒定速度。
	float totalDistance = 0.0f;
	for (int frame = frameBegin; frame < frameEnd; ++frame) {
		totalDistance += std::abs(
			track->mFrames[frame + 1].x - track->mFrames[frame].x);
	}
	const float averageFrameDistance =
		totalDistance / static_cast<float>(frameEnd - frameBegin);
	return averageFrameDistance * std::abs(EffectiveSpeed());
}

void Animator::SetExtraSpeedMultiplier(float mul) {
	mExtraSpeedMultiplier = mul;

	for (auto& sparse : mSparseTrackStates) {
		for (auto& weakChild : sparse.mAttachedReanims) {
			auto child = weakChild.lock();
			if (child) {
				child->SetExtraSpeedMultiplier(mul);
			}
		}
	}
}

std::vector<TrackInfo*> Animator::GetTracksByName(const std::string& trackName) const {
	std::vector<TrackInfo*> result;
	if (!mReanim) return result;

	for (int i = 0; i < mReanim->GetTrackCount(); i++) {
		auto track = mReanim->GetTrack(i);
		if (track && track->mTrackName == trackName) {
			result.push_back(track);
		}
	}
	return result;
}

Vector Animator::GetTrackPosition(const std::string& trackName) const {
	auto tracks = GetTracksByName(trackName);
	for (auto track : tracks) {
		if (!track->mFrames.empty()) {
			int frameIndex = static_cast<int>(mFrameIndexNow);
			if (frameIndex < static_cast<int>(track->mFrames.size())) {
				return Vector(track->mFrames[frameIndex].x, track->mFrames[frameIndex].y);
			}
		}
	}
	return Vector::zero();
}

float Animator::GetTrackRotation(const std::string& trackName) const {
	auto tracks = GetTracksByName(trackName);
	for (auto track : tracks) {
		if (!track->mFrames.empty()) {
			int frameIndex = static_cast<int>(mFrameIndexNow);
			if (frameIndex < static_cast<int>(track->mFrames.size())) {
				return track->mFrames[frameIndex].kx;
			}
		}
	}
	return 0.0f;
}

bool Animator::GetTrackVisible(const std::string& trackName) const {
	int index = GetFirstTrackIndexByName(trackName);
	if (index >= 0 && index < static_cast<int>(mExtraInfos.size())) {
		return mExtraInfos[index].mVisible;
	}
	return false;
}

bool Animator::GetTrackFollowerVisible(const std::string& trackName,
	const std::string& followerName) const {
	const int index = GetFirstTrackIndexByName(trackName);
	if (index >= 0 && index < static_cast<int>(mExtraInfos.size())) {
		const TrackExtraInfo& extra = mExtraInfos[index];
		const SparseTrackState* sparse = FindSparseTrackState(index);
		if (!extra.mVisible || !sparse) return false;
		const auto follower = std::find_if(
			sparse->mFollowers.begin(), sparse->mFollowers.end(),
			[&followerName](const TrackFollowerState& state) {
				return state.mName == followerName;
			});
		return follower != sparse->mFollowers.end()
			&& follower->mVisible && follower->mImage;
	}
	return false;
}

bool Animator::GetTrackFollowerInheritsOverlayEffect(const std::string& trackName,
	const std::string& followerName) const {
	const int index = GetFirstTrackIndexByName(trackName);
	const SparseTrackState* sparse = index >= 0 ? FindSparseTrackState(index) : nullptr;
	if (!sparse) return false;
	const auto follower = std::find_if(
		sparse->mFollowers.begin(), sparse->mFollowers.end(),
		[&followerName](const TrackFollowerState& state) {
			return state.mName == followerName;
		});
	return follower != sparse->mFollowers.end()
		&& follower->mInheritOverlayEffect;
}

bool Animator::GetTrackFollowerGlowEffectEnabled(const std::string& trackName,
	const std::string& followerName) const {
	const int index = GetFirstTrackIndexByName(trackName);
	const SparseTrackState* sparse = index >= 0 ? FindSparseTrackState(index) : nullptr;
	if (!sparse || !IsGlowEffectEnabledForTrack(index)) return false;
	const auto follower = std::find_if(
		sparse->mFollowers.begin(), sparse->mFollowers.end(),
		[&followerName](const TrackFollowerState& state) {
			return state.mName == followerName;
		});
	return follower != sparse->mFollowers.end()
		&& follower->mVisible && follower->mImage
		&& follower->mInheritGlowEffect;
}

bool Animator::GetTrackGlowEffectEnabled(const std::string& trackName) const {
	const int trackIndex = GetFirstTrackIndexByName(trackName);
	return trackIndex >= 0 && IsGlowEffectEnabledForTrack(trackIndex);
}

bool Animator::IsGlowEffectEnabledForTrack(int trackIndex) const {
	if (trackIndex >= 0 && trackIndex < static_cast<int>(mExtraInfos.size())) {
		const TrackExtraInfo& extra = mExtraInfos[trackIndex];
		if (extra.mHasGlowOverride) return extra.mGlowOverrideEnabled;
	}
	return mEnableExtraAdditiveDraw;
}

TrackFrameTransform Animator::GetInterpolatedTransform(int trackIndex, float blendRatio) const {
	TrackFrameTransform result;
	if (!mReanim) return result;

	auto track = mReanim->GetTrack(trackIndex);
	if (!track || track->mFrames.empty()) return result;

	int frameBefore = static_cast<int>(mFrameIndexNow);
	float fraction = mFrameIndexNow - frameBefore;
	int frameAfter = std::min(frameBefore + 1, static_cast<int>(track->mFrames.size() - 1));

	if (mReanimBlendCounter > 0) {
		// 过渡动画插值（blendRatio 由调用方预计算，避免此处重复做除法）
		GetDeltaTransform(track->mFrames[mFrameIndexBlendBuffer],
			track->mFrames[frameBefore],
			blendRatio,
			result, true);
	}
	else {
		// 正常帧间插值
		if (frameBefore >= 0 && frameAfter < static_cast<int>(track->mFrames.size())) {
			GetDeltaTransform(track->mFrames[frameBefore],
				track->mFrames[frameAfter],
				fraction, result);
		}
		else {
			result = track->mFrames[frameBefore];
		}
	}

	return result;
}

std::vector<TrackExtraInfo*> Animator::GetTrackExtrasByName(const std::string& trackName) {
	std::vector<TrackExtraInfo*> result;
	for (int i = 0; i < static_cast<int>(mExtraInfos.size()); i++) {
		auto track = mReanim->GetTrack(i);
		if (track && track->mTrackName == trackName) {
			result.push_back(&mExtraInfos[i]);
		}
	}
	return result;
}

SDL_Color Animator::GetTrackColor(const std::string& trackName) const {
	const int index = GetFirstTrackIndexByName(trackName);
	if (index >= 0 && index < static_cast<int>(mExtraInfos.size())) {
		return mExtraInfos[index].mColor;
	}
	return SDL_Color{ 255, 255, 255, 255 };
}

std::vector<int> Animator::GetTrackIndicesByName(const std::string& trackName) const {
	std::vector<int> result;
	if (!mReanim) return result;
	for (int i = 0; i < static_cast<int>(mReanim->GetTrackCount()); ++i) {
		const auto track = mReanim->GetTrack(i);
		if (track && track->mTrackName == trackName) result.push_back(i);
	}
	return result;
}

Animator::SparseTrackState* Animator::FindSparseTrackState(int trackIndex) {
	const auto it = std::lower_bound(mSparseTrackStates.begin(), mSparseTrackStates.end(), trackIndex,
		[](const SparseTrackState& sparse, int index) { return sparse.mTrackIndex < index; });
	return it != mSparseTrackStates.end() && it->mTrackIndex == trackIndex ? &*it : nullptr;
}

const Animator::SparseTrackState* Animator::FindSparseTrackState(int trackIndex) const {
	const auto it = std::lower_bound(mSparseTrackStates.begin(), mSparseTrackStates.end(), trackIndex,
		[](const SparseTrackState& sparse, int index) { return sparse.mTrackIndex < index; });
	return it != mSparseTrackStates.end() && it->mTrackIndex == trackIndex ? &*it : nullptr;
}

Animator::SparseTrackState& Animator::GetOrCreateSparseTrackState(int trackIndex) {
	const auto it = std::lower_bound(mSparseTrackStates.begin(), mSparseTrackStates.end(), trackIndex,
		[](const SparseTrackState& sparse, int index) { return sparse.mTrackIndex < index; });
	if (it != mSparseTrackStates.end() && it->mTrackIndex == trackIndex) return *it;
	SparseTrackState sparse;
	sparse.mTrackIndex = trackIndex;
	return *mSparseTrackStates.insert(it, std::move(sparse));
}

int Animator::GetFirstTrackIndexByName(const std::string& trackName) const {
	return mReanim ? mReanim->GetFirstTrackIndex(trackName) : -1;
}

bool Animator::HasTrack(const std::string& trackName) const {
	return GetFirstTrackIndexByName(trackName) >= 0;
}

// 颜色混合函数
int ColorComponentMultiply(int theColor1, int theColor2) {
	return std::clamp(theColor1 * theColor2 / 255, 0, 255);
}

SDL_Color ColorsMultiply(const SDL_Color& theColor1, const SDL_Color& theColor2) {
	return SDL_Color{
		static_cast<Uint8>(ColorComponentMultiply(theColor1.r, theColor2.r)),
		static_cast<Uint8>(ColorComponentMultiply(theColor1.g, theColor2.g)),
		static_cast<Uint8>(ColorComponentMultiply(theColor1.b, theColor2.b)),
		static_cast<Uint8>(ColorComponentMultiply(theColor1.a, theColor2.a))
	};
}
