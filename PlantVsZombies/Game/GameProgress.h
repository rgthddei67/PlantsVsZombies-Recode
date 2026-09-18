#pragma once
#include "GameObject.h"
#include "FlagMeter.h"
#include <memory>
#include "Transform.h"

class Board;

class GameProgress : public GameObject
{
public:
	explicit GameProgress(Board* board);
	~GameProgress();

	void Update() override;
	void Draw(Graphics* g) override;

	// 根据最大波数生成旗子，使用传入的图片键
	void SetupFlags(const Texture* stickTex, const Texture* flagTex);

	// 初始化升起状态（根据当前波数直接设置已升起的旗子）
	void InitializeRaisedFlags(float raiseY);

	// 立刻将进度条滑块对齐当前波数，跳过插值动画
	void SnapProgressToCurrentWave();

	// 将所有已升起的旗子平滑降回基准位（生存轮清时调用，避免上一轮的旗子一直悬着）
	void LowerAllFlags(float duration);

private:
	Board* mBoard = nullptr;
	float mUpdateTimer = 0.0f;

	float mCurrentSliderValue = 1.0f;   // 当前显示进度
	float mTargetSliderValue = 1.0f;    // 目标进度
	float mLerpSpeed = 1.1f;             // 插值速度（就地初始化=运行时真实生效值）

	int m_lastWave = 0;               // 上一帧波数，用于检测波次变化
	int m_flagCount = 0;              // 旗子总数

	Vector createPosition = Vector(870, 575);

	std::unique_ptr<FlagMeter> m_flagMeter;
	std::unique_ptr<FlagMeter> m_playerIceMeter;
};
