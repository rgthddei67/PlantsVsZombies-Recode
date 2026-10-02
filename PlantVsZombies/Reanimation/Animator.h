#pragma once
#ifndef _ANIMATOR_H
#define _ANIMATOR_H

#include "ReanimTypes.h"
#include "Reanimation.h"
#include "../InternedString.h"
#include "../Graphics.h"
#include "../Game/DeferredEvent.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <iostream>

/**
 * @brief 颜色分量乘法 (通道值范围 0-255)
 * @param theColor1 颜色1分量
 * @param theColor2 颜色2分量
 * @return 相乘后的分量 (clamp to 0-255)
 */
int ColorComponentMultiply(int theColor1, int theColor2);

/**
 * @brief SDL_Color 颜色乘法 (逐分量相乘)
 * @param theColor1 颜色1
 * @param theColor2 颜色2
 * @return 相乘后的颜色
 */
SDL_Color ColorsMultiply(const SDL_Color& theColor1, const SDL_Color& theColor2);

/**
 * Animator 最近一次实际绘制提交的世界空间几何快照。
 * 包围盒由默认实例化和 -NoInstance 慢路径共同使用的最终仿射四边形计算，不包含 shader 裁剪。
 */
struct AnimatorRenderProbe {
	bool hasGeometry = false;
	bool usedInstancePath = false;
	int quadCount = 0;
	float baseX = 0.0f;
	float baseY = 0.0f;
	float objectScale = 1.0f;
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
};

class Animator {
private:
	std::shared_ptr<Reanimation> mReanim;   ///< 关联的 Reanimation 数据
	float mFPS = 12.0f;                      ///< 帧率
	float mFrameIndexNow = 0.0f;             ///< 当前帧索引 (浮点，支持插值)
	float mFrameIndexBegin = 0.0f;           ///< 起始帧索引
	float mFrameIndexEnd = 0.0f;              ///< 结束帧索引
	float mSpeed = 1.0f;                      ///< 基础播放速度 (每实例随机的 base，PlayTrack 不再覆盖它)
	float mClipSpeed = 0.0f;                  ///< 当前轨道的绝对速度覆盖；0 = 回落到 mSpeed。每次 PlayTrack 重设，随轨道作用域
	float mExtraSpeedMultiplier = 1.0f;       ///< 额外速度倍率 (正交状态层，减速/冻结，跨 PlayTrack 存活)
	float mAlpha = 1.0f;                      ///< 整体透明度
	float mRenderScaleX = 1.0f;               ///< 最终世界绘制矩阵的 X 缩放，不影响逻辑坐标/动画推进
	float mRenderScaleY = 1.0f;               ///< 最终世界绘制矩阵的 Y 缩放
	float mRenderPivotX = 0.0f;               ///< 最终世界绘制缩放的 X 锚点
	float mRenderPivotY = 0.0f;               ///< 最终世界绘制缩放的 Y 锚点
	AnimatorRenderProbe mLastRenderProbe;      ///< 最近一次根 Draw 的最终世界几何，供 AutoTest 只读取证

	// 过渡动画相关
	float mReanimBlendCounter = -1.0f;        ///< 混合计数器，>0 时进行混合
	float mReanimBlendCounterMax = 100.0f;     ///< 混合总时长 (秒)
	int mFrameIndexBlendBuffer = 0;            ///< 混合起始帧索引

	bool mIsPlaying = false;                   ///< 是否正在播放
	PlayState mPlayingState = PlayState::PLAY_NONE;  ///< 播放状态
	std::vector<TrackExtraInfo> mExtraInfos;   ///< 每个轨道都会使用的热状态（可见性、自定义纹理与偏移）

	/** 同一父轨道上的命名 follower；vector 顺序同时定义同层贴图的稳定提交顺序。 */
	struct TrackFollowerState {
		std::string mName;
		bool mVisible = false;
		bool mDrawAfterAllTracks = false;
		bool mInheritOverlayEffect = true;
		bool mInheritGlowEffect = false;
		float mOffsetX = 0.0f;
		float mOffsetY = 0.0f;
		float mScaleX = 1.0f;
		float mScaleY = 1.0f;
		const Texture* mImage = nullptr;
	};

	/** 只有 follower 或子 Animator 的轨道才分配冷状态，按轨道索引排序。 */
	struct SparseTrackState {
		int mTrackIndex = -1;
		std::vector<TrackFollowerState> mFollowers;
		std::vector<std::weak_ptr<Animator>> mAttachedReanims;
	};
	std::vector<SparseTrackState> mSparseTrackStates;

	bool mEnableExtraAdditiveDraw = false;     ///< 是否启用高亮 (叠加混合) 效果
	bool mEnableExtraOverlayDraw = false;      ///< 是否启用附加覆盖效果
	bool mEnableWashedOutEffect = false;       ///< 是否用模仿者 HSL 滤镜绘制本体
	bool mUseLessWashedOutEffect = false;      ///< 是否使用原版 LessWashedOut 较弱参数
	SDL_Color mExtraAdditiveColor = { 255, 255, 255, 128 };  ///< 高亮颜色
	SDL_Color mExtraOverlayColor = { 255, 255, 255, 64 };    ///< 覆盖层颜色

	const std::string* mCurrentTrackName = &InternRuntimeString(""); ///< 当前正在播放的驻留轨道名

	// 过渡目标
	const std::string* mTargetTrack = &InternRuntimeString(""); ///< 播放一次后要切换到的驻留轨道名
	float mTargetTrackSpeed = 0.0f;             ///< 回切到 mTargetTrack 时用的 clip 速度（0=回落 base），由 PlayTrackOnce 指定
	float mTargetTrackBlendTime = 0.5f;         ///< 回切到 mTargetTrack 时的混合秒数，默认保留历史 0.5 秒

	struct FrameEvent {
		InlineFrameCallback callback;
		int frameIndex;
		bool persistent;   ///< false=一次性，触发后移除；true=每次穿过该帧都触发
	};
	std::vector<FrameEvent> mFrameEvents;  ///< 按帧号排序的小事件表；连续存储避免逐事件哈希节点

	void AddFrameEventInternal(
		int frameIndex, InlineFrameCallback callback, bool persistent);
	void ProcessFrameEventsAt(int frameIndex, std::vector<DeferredEvent>* outBuf);
	void ProcessFrameEventRange(
		int firstFrame, int lastFrame, std::vector<DeferredEvent>* outBuf);

public:
	/**
	 * @brief 默认构造函数
	 */
	Animator();

	/**
	 * @brief 构造函数，绑定 Reanimation
	 * @param reanim 要绑定的 Reanimation 共享指针
	 */
	explicit Animator(std::shared_ptr<Reanimation> reanim);

	/**
	 * @brief 析构函数，释放资源并清理子动画
	 */
	~Animator();

	/**
	 * @brief 初始化，绑定 Reanimation
	 * @param reanim 要绑定的 Reanimation 共享指针
	 */
	void Init(std::shared_ptr<Reanimation> reanim);

	/**
	 * @brief 销毁动画器，停止播放并清除所有子动画和事件
	 */
	void Die();

	// ---------- 播放控制 ----------
	/**
	 * @brief 开始播放
	 * @param state 播放状态 (默认重复播放)
	 */
	void Play(PlayState state = PlayState::PLAY_REPEAT);

	/**
	 * @brief 暂停播放
	 */
	void Pause();
	/** 暂停自身及当前全部附加 Animator；适用于不会恢复播放的终态表现。 */
	void PauseSubtree();

	/**
	 * @brief 停止播放，并将当前帧重置为起始帧
	 */
	void Stop();

	/**
	 * @brief 添加帧事件
	 * @param frameIndex 帧索引 (整数)
	 * @param callback 回调函数
	 * @param persistent false=一次性，触发后自动移除（默认）；true=持久事件，每次帧索引穿过该帧都触发，仅在 Die()/Init() 时清空
	 */
	template<typename Callback>
	void AddFrameEvent(int frameIndex, Callback&& callback, bool persistent = false)
	{
		AddFrameEventInternal(frameIndex,
			InlineFrameCallback(std::forward<Callback>(callback)), persistent);
	}

	/**
	 * @brief 播放指定轨道动画，支持过渡效果
	 * @param trackName 轨道名
	 * @param speed 轨道绝对播放速度，0.0=回落到基础速度(base)，>0=本轨道固定用该速度
	 * @param blendTime 过渡时间 (秒)，0表示无过渡
	 * @return 是否成功
	 */
	bool PlayTrack(const std::string& trackName, float speed = 0.0f, float blendTime = 0);

	/**
	 * @brief 播放指定轨道动画一次，播放完后可切换回另一轨道
	 * @param trackName 要播放的轨道名
	 * @param returnTrack 播放完后要返回的轨道名 (为空则不切换)
	 * @param speed 本轨道绝对播放速度，0.0=回落到基础速度(base)，>0=本轨道固定用该速度
	 * @param blendTime 过渡时间
	 * @param returnSpeed 回切到 returnTrack 时的 clip 速度，0.0=回落 base，>0=回切轨道固定用该速度
	 * @param returnTrackBlendTime 回切到 returnTrack 时的混合时间，0.0=直接切换
	 * @return 是否成功
	 */
	bool PlayTrackOnce(const std::string& trackName,
		const std::string& returnTrack = "",
		float speed = 0.0f,
		float blendTime = 0,
		float returnSpeed = 0.0f,
		float returnTrackBlendTime = 0.5f);


	// ---------- 轨道范围控制 ----------
	/**
	 * @brief 获取轨道对应的帧范围 (根据 f==0 的帧划分)
	 * @param trackName 轨道名
	 * @return pair {起始帧索引, 结束帧索引}，若无效返回 {-1,-1}
	 */
	std::pair<int, int> GetTrackRange(const std::string& trackName);

	/**
	 * @brief 手动设置帧范围
	 * @param frameBegin 起始帧索引
	 * @param frameEnd 结束帧索引
	 */
	void SetFrameRange(int frameBegin, int frameEnd);

	/**
	 * @brief 根据轨道名设置帧范围 (自动调用 GetTrackRange)
	 * @param trackName 轨道名
	 */
	void SetFrameRangeByTrackName(const std::string& trackName);

	/**
	 * @brief 将帧范围恢复为整个动画的范围 (0 到 总帧数-1)
	 */
	void SetFrameRangeToDefault();

	/**
	 * @brief 设置轨道自定义纹理 (会覆盖动画本身的纹理)
	 * @param trackName 轨道名
	 * @param image 纹理指针，nullptr 表示恢复默认
	 */
	void SetTrackImage(const std::string& trackName, const Texture* image);
	/** 返回首个同名轨道的实例换图；nullptr 表示无覆盖或轨道不存在，不解析默认帧图。 */
	const Texture* GetTrackImageOverride(const std::string& trackName) const;

	/**
	 * @brief 设置指定轨道叠加在 reanim 原始变换上的绘制偏移。
	 * @param trackName 轨道名。
	 * @param x 水平偏移，单位：动画局部像素。
	 * @param y 垂直偏移，单位：动画局部像素。
	 */
	void SetTrackOffset(const std::string& trackName, float x, float y);

	/**
	 * 配置跟随指定轨道完整插值仿射变换的附属贴图。
	 * @param trackName 父轨道名。
	 * @param followerName 同一父轨道内的稳定槽位名。
	 * @param image 附属纹理；nullptr 表示只移除该命名槽。
	 * @param offsetX 相对父轨道原点的局部 X 偏移，单位：动画像素。
	 * @param offsetY 相对父轨道原点的局部 Y 偏移，单位：动画像素。
	 * @param scaleX 相对父轨道的横向缩放倍率。
	 * @param scaleY 相对父轨道的纵向缩放倍率。
	 * @param drawAfterAllTracks true=延迟到本 Animator 全部轨道及附件之后提交；false=紧随父轨道。
	 * @param inheritOverlayEffect true=继承 Animator 的减速/冻结等覆盖色；状态贴图应传 false。
	 * @param inheritGlowEffect true=继承父轨道的 additive glow；不属于承伤外观的状态贴图应传 false。
	 */
	void SetTrackFollowerImage(const std::string& trackName,
		const std::string& followerName, const Texture* image,
		float offsetX, float offsetY, float scaleX, float scaleY,
		bool drawAfterAllTracks, bool inheritOverlayEffect = true,
		bool inheritGlowEffect = false);
	/** 控制指定轨道的一个命名 follower，不影响同轨道其他槽。 */
	void SetTrackFollowerVisible(const std::string& trackName,
		const std::string& followerName, bool visible);

	/**
	 * @brief 设置轨道可见性
	 * @param trackName 轨道名
	 * @param visible true=显示，false=隐藏
	 */
	void SetTrackVisible(const std::string& trackName, bool visible);

	/**
	 * 设置指定轨道纹理的乘色；纯白恢复原图，默认与 -NoInstance 路径语义一致。
	 * @param trackName 轨道名。
	 * @param color RGBA 乘色，通道范围 0～255。
	 */
	void SetTrackColor(const std::string& trackName, const SDL_Color& color);

	/**
	 * @brief 让指定轨道独立决定是否高亮，不再继承 Animator 整体高亮开关。
	 * @param trackName 轨道名
	 * @param enable true=该轨道高亮，false=该轨道不高亮
	 */
	void SetTrackGlowOverride(const std::string& trackName, bool enable);

	/**
	 * @brief 将子动画器附加到指定轨道 (子动画将跟随父轨道的变换)
	 * @param trackName 目标轨道名
	 * @param child 子动画器共享指针
	 * @return 是否成功 (轨道存在且不为自身)
	 */
	bool AttachAnimator(const std::string& trackName, std::shared_ptr<Animator> child);

	/**
	 * @brief 从指定轨道分离子动画器
	 * @param trackName 轨道名
	 * @param child 子动画器共享指针
	 */
	void DetachAnimator(const std::string& trackName, std::shared_ptr<Animator> child);

	/**
	 * @brief 分离所有轨道的所有子动画器
	 */
	void DetachAllAnimators();

	// ---------- 状态查询 ----------
	/**
	 * @brief 是否正在播放
	 */
	bool IsPlaying() const { return mIsPlaying; }

	/**
	 * @brief 获取当前帧索引 (浮点)
	 */
	float GetCurrentFrame() const { return mFrameIndexNow; }

	/**
	 * @brief 获取当前正在播放的轨道名
	 */
	const std::string& GetCurrentTrackName() const { return *mCurrentTrackName; }

	/**
	 * @brief 获取当前播放状态 (循环 / 一次性 / 一次性后切换)。
	 *        存档必须持久化它，否则读档统一走 PlayTrack 会把进行中的一次性轨道
	 *        误当 PLAY_REPEAT 永远循环、无法切回目标轨道。
	 */
	PlayState GetPlayingState() const { return mPlayingState; }

	/**
	 * @brief 获取 PlayTrackOnce 播完后要切换到的目标轨道名 (空=播完即停，不切换)
	 */
	const std::string& GetTargetTrack() const { return *mTargetTrack; }

	/**
	 * @brief 获取回切到目标轨道时使用的 clip 速度 (0=回落 base)
	 */
	float GetTargetTrackSpeed() const { return mTargetTrackSpeed; }

	/**
	 * @brief 获取回切到目标轨道时使用的混合时间，单位：秒。
	 */
	float GetTargetTrackBlendTime() const { return mTargetTrackBlendTime; }

	/**
	 * @brief 直接设置当前帧索引 (用于存档恢复)
	 */
	void SetCurrentFrame(float frameIndex) { mFrameIndexNow = frameIndex; }

	/**
	 * @brief 设置播放速度倍率
	 * @param speed 速度倍率
	 */
	void SetSpeed(float speed);

	/**
	 * @brief 获取播放速度倍率
	 */
	float GetSpeed() const { return mSpeed; }

	/**
	 * @brief 设置当前轨道的绝对速度覆盖 (0 = 回落到基础速度 mSpeed)。
	 *        会递归到所有附加子动画，复刻旧 SetSpeed 的递归语义。
	 */
	void SetClipSpeed(float clipSpeed);

	/**
	 * @brief 获取当前轨道速度覆盖值 (0 表示正使用基础速度)
	 */
	float GetClipSpeed() const { return mClipSpeed; }

	/**
	 * @brief 实际生效的播放速度 = (clip 覆盖优先, 否则 base) * 状态倍率
	 */
	float EffectiveSpeed() const {
		return (mClipSpeed != 0.0f ? mClipSpeed : mSpeed) * mExtraSpeedMultiplier;
	}

	/**
	 * @brief 设置额外速度倍率 (独立于 mSpeed，与 PlayTrack/SetSpeed 正交)
	 *        实际播放速度 = mSpeed * mExtraSpeedMultiplier，用于状态效果如减速
	 * @param mul 倍率
	 */
	void SetExtraSpeedMultiplier(float mul);

	/**
	 * @brief 获取额外速度倍率
	 */
	float GetExtraSpeedMultiplier() const { return mExtraSpeedMultiplier; }

	/**
	 * @brief 获取指定轨道的运动速度 (基于当前帧的前后位置差)
	 * @param trackName 轨道名
	 * @return 速度值 (像素/秒？实际为帧间位移乘以速度倍率)
	 */
	float GetTrackVelocity(const std::string& trackName) const;

	/**
	 * @brief 通过轨道索引获取运动速度 (跳过字符串查找，用于热路径)
	 * @param trackIndex 轨道索引
	 * @return 速度值
	 */
	float GetTrackVelocity(int trackIndex) const;

	/**
	 * @brief 获取当前动画片段内指定轨道的平均运动速度。
	 * @param trackName 轨道名
	 * @return 当前帧范围内逐帧位移绝对值的平均值，再乘实际播放倍率
	 */
	float GetTrackAverageVelocity(const std::string& trackName) const;

	/**
	 * @brief 通过轨道索引获取当前动画片段的平均运动速度。
	 * @param trackIndex 轨道索引
	 * @return 当前帧范围内的平均运动速度
	 */
	float GetTrackAverageVelocity(int trackIndex) const;

	/**
	 * @brief 根据轨道名获取第一个匹配的轨道索引 (O(1) 哈希查找)
	 * @param trackName 轨道名
	 * @return 索引，-1 表示未找到
	 */
	int GetFirstTrackIndexByName(const std::string& trackName) const;

	/**
	 * @brief 通过轨道名字获取一个track存不存在
	 * @param trackName 轨道名字
	 * @return 是否存在某个track，true=存在，false=不存在
	 */
	bool HasTrack(const std::string& trackName) const;

	// ---------- 透明度和颜色控制 ----------
	/**
	 * @brief 设置整体透明度
	 * @param alpha 透明度 (0~1)
	 */
	void SetAlpha(float alpha);

	/**
	 * @brief 获取整体透明度
	 */
	float GetAlpha() const { return mAlpha; }

	/**
	 * @brief 启用/禁用高亮 (叠加混合) 效果
	 * @param enable true=启用
	 */
	void EnableGlowEffect(bool enable);
	/** 获取 Animator 整体高亮开关；轨道仍可用独立覆盖替换该值。 */
	bool GetGlowEffectEnabled() const { return mEnableExtraAdditiveDraw; }

	/**
	 * @brief 设置高亮颜色 (叠加混合)
	 * @param r, g, b, a 颜色分量 (0-255)
	 */
	void SetGlowColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 128);

	/**
	 * @brief 启用/禁用覆盖层效果 (Alpha混合)
	 * @param enable true=启用
	 */
	void EnableOverlayEffect(bool enable);

	/**
	 * @brief 启用/禁用原版模仿者 HSL 滤镜，并递归应用到附件动画。
	 * @param enable true=启用，false=关闭
	 * @param lighter true=使用 LessWashedOut 参数，false=使用普通 WashedOut
	 */
	void EnableWashedOutEffect(bool enable, bool lighter = false);

	/**
	 * @brief 设置覆盖层颜色
	 * @param r, g, b, a 颜色分量 (0-255)
	 */
	void SetOverlayColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 64);

	// ---------- 更新和渲染 ----------
	/**
	 * @brief 更新动画逻辑 (帧前进、事件触发、子动画更新)
	 */
	void Update();

	/**
	 * @brief 阶段二并行段：完整推进帧 + 计时器 + 子节点递归；遇到帧事件 = 拷贝 callback 入 outBuf。
	 *        对象本地、worker 线程安全；不调用任何 callback。
	 *        前置：本 Animator 与其子 Animator 树不被其他线程并发访问（Task 1 audit PASS）。
	 */
	void UpdateParallelDeferred(std::vector<DeferredEvent>& outBuf);

	/**
	 * @brief 绘制动画 (现场计算变换并提交，递归绘制子动画)
	 * @param g Graphics 对象
	 * @param baseX 基准 X 坐标 (世界坐标)
	 * @param baseY 基准 Y 坐标
	 * @param Scale 全局缩放系数
	 */
	void Draw(Graphics* g, float baseX, float baseY, float Scale = 1.0f);

	/**
	 * 对最终世界绘制矩阵施加非等比缩放，并递归同步当前及以后附加的子 Animator。
	 * 与 Graphics 变换栈无关，因此 GPU 实例化快路径和任意层级子动画都保持一致。
	 */
	void SetRenderScale(float scaleX, float scaleY, float pivotX, float pivotY);
	float GetRenderScaleX() const { return mRenderScaleX; }
	float GetRenderScaleY() const { return mRenderScaleY; }
	float GetRenderPivotX() const { return mRenderPivotX; }
	float GetRenderPivotY() const { return mRenderPivotY; }
	const AnimatorRenderProbe& GetLastRenderProbe() const { return mLastRenderProbe; }

	/**
	 * @brief 获取底层 Reanimation 对象
	 */
	std::shared_ptr<Reanimation> GetReanimation() const { return mReanim; }

	// ---------- 轨道信息查询 ----------
	/**
	 * @brief 根据轨道名获取所有同名 TrackInfo 指针
	 * @param trackName 轨道名
	 * @return 指针数组
	 */
	std::vector<TrackInfo*> GetTracksByName(const std::string& trackName) const;

	/**
	 * @brief 获取轨道当前帧的位置
	 * @param trackName 轨道名
	 * @return 位置向量 (x,y)
	 */
	Vector GetTrackPosition(const std::string& trackName) const;

	/**
	 * @brief 获取轨道当前帧的旋转角度 (kx)
	 * @param trackName 轨道名
	 * @return 角度值
	 */
	float GetTrackRotation(const std::string& trackName) const;

	/**
	 * @brief 获取轨道的可见性
	 * @param trackName 轨道名
	 * @return true=可见
	 */
	bool GetTrackVisible(const std::string& trackName) const;
	/** 返回指定轨道的当前乘色；轨道不存在时返回纯白。 */
	SDL_Color GetTrackColor(const std::string& trackName) const;
	/** 返回指定命名 follower 与父轨道合并后的最终可见状态。 */
	bool GetTrackFollowerVisible(const std::string& trackName,
		const std::string& followerName) const;
	/** 返回指定命名 follower 是否继承 Animator 覆盖色。 */
	bool GetTrackFollowerInheritsOverlayEffect(const std::string& trackName,
		const std::string& followerName) const;
	/** 返回指定命名 follower 当前是否会随父轨道提交 additive glow。 */
	bool GetTrackFollowerGlowEffectEnabled(const std::string& trackName,
		const std::string& followerName) const;

	/**
	 * @brief 获取指定轨道合并整体开关与轨道覆盖后的实际高亮状态。
	 * @param trackName 轨道名
	 * @return true=绘制时会提交该轨道的高亮层
	 */
	bool GetTrackGlowEffectEnabled(const std::string& trackName) const;

	// ---------- 本地变换 (用于子动画相对父级) ----------
	/**
	 * @brief 设置本地位置偏移
	 * @param x, y 偏移量
	 */
	void SetLocalPosition(float x, float y);

	/**
	 * @brief 设置本地位置偏移 (Vector 版本)
	 * @param position 偏移向量
	 */
	void SetLocalPosition(const Vector& position);

	/**
	 * @brief 设置本地缩放
	 * @param sx, sy 缩放系数
	 */
	void SetLocalScale(float sx, float sy);

	/**
	 * @brief 设置本地旋转角度
	 * @param rotation 角度 (度)
	 */
	void SetLocalRotation(float rotation);

	/**
	 * 水平镜像（仅渲染）：绕动画局部 x = pivotX 的竖直轴翻转。
	 * 注意 SetLocalScale/mLocalScaleX 是历史死字段（绘制不读），翻转必须走本接口。
	 * 不影响碰撞箱/影子/_ground 轨道速度/帧事件（魅惑僵尸的移动方向由 ZombieMove 按 mIsMindControlled 处理）。
	 */
	void SetFlipX(bool flip, float pivotX = 0.0f);
	bool GetFlipX() const { return mFlipX; }
	/** 返回当前仅渲染水平镜像使用的动画局部 X 支点。 */
	float GetFlipPivotX() const { return mFlipPivotX; }

private:
	// 子动画相对于父轨道的本地变换
	float mLocalPosX = 0.0f;
	float mLocalPosY = 0.0f;
	float mLocalScaleX = 1.0f;
	float mLocalScaleY = 1.0f;
	float mLocalRotation = 0.0f;   // 角度制

	bool  mFlipX = false;
	float mFlipPivotX = 0.0f;

private:
	/**
	 * @brief 获取指定轨道的插值变换结果 (包含混合)
	 * @param trackIndex 轨道索引
	 * @return 插值后的帧变换
	 */
	TrackFrameTransform GetInterpolatedTransform(int trackIndex, float blendRatio) const;

	/**
	 * @brief 根据轨道名获取所有 TrackExtraInfo 指针
	 * @param trackName 轨道名
	 * @return 指针数组
	 */
	std::vector<TrackExtraInfo*> GetTrackExtrasByName(const std::string& trackName);
	/** 返回全部同名轨道索引，以保持旧接口对重复轨道名的广播语义。 */
	std::vector<int> GetTrackIndicesByName(const std::string& trackName) const;
	/** 查询已存在的轨道冷状态；不会在读取路径产生分配。 */
	SparseTrackState* FindSparseTrackState(int trackIndex);
	const SparseTrackState* FindSparseTrackState(int trackIndex) const;
	/** 按轨道索引创建或返回冷状态，并保持容器有序供绘制线性合并。 */
	SparseTrackState& GetOrCreateSparseTrackState(int trackIndex);

	/**
	 * @brief Draw 的内部递归分派：默认走实例化附件树，-NoInstance 时走矩阵慢路径。
	 * @param g Graphics 对象
	 * @param baseX 基准 X
	 * @param baseY 基准 Y
	 * @param Scale 全局缩放
	 */
	void DrawInternal(Graphics* g, float baseX, float baseY, float Scale) const;

	/**
	 * @brief 递归实例化当前 Animator 及全部附件。
	 *
	 * 每个可见轨道生成一个 InstanceRecord，并在该轨道之后立即递归附件，保持父子与
	 * 轨道间遮挡顺序；overlay/glow 仍紧跟本体。CPU 负责动画插值与附件定位，GPU 只展开
	 * 单位四边形，2x3 仿射已预乘图像尺寸和 Scale。
	 */
	void DrawInternalInstanced(Graphics* g, float baseX, float baseY, float Scale) const;
	/** 返回指定轨道合并整体开关与独立覆盖后的实际高亮状态。 */
	bool IsGlowEffectEnabledForTrack(int trackIndex) const;
	/** 把世界绘制缩放烘进实例化快路径的 2x3 仿射记录。 */
	void ApplyRenderScale(InstanceRecord& record) const;
	/** 把世界绘制缩放烘进慢路径的 4x4 仿射矩阵。 */
	void ApplyRenderScale(glm::mat4& matrix) const;
};

#endif
