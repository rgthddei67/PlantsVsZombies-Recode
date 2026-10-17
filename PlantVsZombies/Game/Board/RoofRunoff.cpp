#include "RoofRunoff.h"
#include "GameRandom.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>

namespace {
	constexpr float kRoofRunoffChargeMaximum = 100.0f;    // 坡面径流进入锁行预警所需的满积累值
	constexpr float kRoofRunoffLightChargePerSecond = 1.00f; // 小雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffMediumChargePerSecond = 2.00f; // 中雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffHeavyChargePerSecond = 3.50f; // 大雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffClearDrainPerSecond = 0.30f; // 晴天每游戏秒自然排走的径流积累点数
	constexpr float kRoofRunoffWarningDuration = 3.0f;    // 满积累后锁行预警的持续游戏秒
	constexpr float kRoofRunoffFlowDuration = 2.20f;      // 单次目标行冲刷的持续游戏秒
	constexpr int kRoofRunoffRetainedChargeMin = 30;      // 冲刷结束后保留湿度的均匀随机下限（百分比）
	constexpr int kRoofRunoffRetainedChargeMax = 60;      // 冲刷结束后保留湿度的均匀随机上限（百分比）
	constexpr int kRoofRunoffOneRowWeight = 50;           // 满积累事件只冲刷一行的相对权重
	constexpr int kRoofRunoffTwoRowWeight = 35;           // 满积累事件同时冲刷两行的相对权重
	constexpr int kRoofRunoffThreeRowWeight = 15;         // 满积累事件同时冲刷三行的相对权重
}

bool RoofRunoff::Advance(float seconds, bool supported, int rows, RainIntensity rain)
{
	if (!supported) {
		mCharge = 0.0f;
		mRetainedCharge = 0.0f;
		mPhase = RoofRunoffPhase::IDLE;
		mTimer = 0.0f;
		mRowMask = 0;
		return false;
	}
	if (seconds <= 0.0f) return false;

	if (mPhase == RoofRunoffPhase::WARNING
		|| mPhase == RoofRunoffPhase::FLOWING) {
		mTimer = std::max(0.0f,
			mTimer - seconds);
		if (mTimer > 0.0f) return false;

		if (mPhase == RoofRunoffPhase::WARNING) {
			mPhase = RoofRunoffPhase::FLOWING;
			mTimer = kRoofRunoffFlowDuration;
			return false;
		}

		mCharge = mRetainedCharge;
		mRetainedCharge = 0.0f;
		mPhase = RoofRunoffPhase::IDLE;
		mRowMask = 0;
		return false;
	}

	float chargeDelta = 0.0f;
	switch (rain) {
	case RainIntensity::CLEAR:
		chargeDelta = -kRoofRunoffClearDrainPerSecond;
		break;
	case RainIntensity::LIGHT:
		chargeDelta = kRoofRunoffLightChargePerSecond;
		break;
	case RainIntensity::MEDIUM:
		chargeDelta = kRoofRunoffMediumChargePerSecond;
		break;
	case RainIntensity::HEAVY:
		chargeDelta = kRoofRunoffHeavyChargePerSecond;
		break;
	}
	mCharge = std::clamp(mCharge
		+ chargeDelta * seconds, 0.0f, kRoofRunoffChargeMaximum);
	if (mCharge < kRoofRunoffChargeMaximum || rows <= 0) return false;

	return true;
}

void RoofRunoff::LockRows(int rows, int guideRow)
{
    if (rows <= 0 || rows >= 31 || mPhase != RoofRunoffPhase::IDLE || mCharge < kRoofRunoffChargeMaximum) return;
	// 锁行组只抽一次并随档保存；不按敌我密度选行，避免系统暗中追打当前最拥挤防线。
	mPhase = RoofRunoffPhase::WARNING;
	mTimer = kRoofRunoffWarningDuration;
	// 下一轮起点和行组同时预抽并入档，避免活动阶段读档改变后续湿度。
	mRetainedCharge = static_cast<float>(GameRandom::Range(
		kRoofRunoffRetainedChargeMin, kRoofRunoffRetainedChargeMax));
	const int rowCountRoll = GameRandom::Range(1,
		kRoofRunoffOneRowWeight + kRoofRunoffTwoRowWeight + kRoofRunoffThreeRowWeight);
	int rowCount = rowCountRoll <= kRoofRunoffOneRowWeight ? 1
		: (rowCountRoll <= kRoofRunoffOneRowWeight + kRoofRunoffTwoRowWeight ? 2 : 3);
	rowCount = std::min(rowCount, rows);
	mRowMask = 0;
	while (RowCount() < rowCount) {
		mRowMask |= 1 << GameRandom::Range(0, rows - 1);
	}

	// 导流车只能替换本次已经抽中的一行，不能扩充冲刷规模；最终掩码仍是唯一入档事实。
	if (guideRow >= 0 && guideRow < rows && !RowSelected(guideRow)) {
		const int replacedIndex = GameRandom::Range(0, rowCount - 1);
		int selectedIndex = 0;
		for (int row = 0; row < rows; ++row) {
			if (!RowSelected(row)) continue;
			if (selectedIndex++ != replacedIndex) continue;
			mRowMask &= ~(1 << row);
			break;
		}
		mRowMask |= 1 << guideRow;
	}
}

void RoofRunoff::Restore(bool supported, int rows, float charge, RoofRunoffPhase phase,
    int rowMask, float timer, float retained)
{
	mCharge = 0.0f;
	mRetainedCharge = 0.0f;
	mPhase = RoofRunoffPhase::IDLE;
	mTimer = 0.0f;
	mRowMask = 0;
	if (!supported || !std::isfinite(charge)
		|| !std::isfinite(timer) || !std::isfinite(retained)) return;

	mCharge = std::clamp(charge, 0.0f, kRoofRunoffChargeMaximum);
	if (phase != RoofRunoffPhase::WARNING && phase != RoofRunoffPhase::FLOWING) return;
	const int validRowMask = rows > 0 && rows < 31 ? (1 << rows) - 1 : 0;
	rowMask &= validRowMask;
	if (rowMask == 0 || timer < 0.0f) {
		mCharge = 0.0f;
		return;
	}
	mCharge = kRoofRunoffChargeMaximum;
	mPhase = phase;
	mTimer = timer;
	mRowMask = rowMask;
	mRetainedCharge = std::clamp(retained,
		static_cast<float>(kRoofRunoffRetainedChargeMin),
		static_cast<float>(kRoofRunoffRetainedChargeMax));
}

void RoofRunoff::Save(nlohmann::json& document) const {
    document["roofRunoffCharge"] = mCharge;
    document["roofRunoffRetainedCharge"] = mRetainedCharge;
    document["roofRunoffPhase"] = static_cast<int>(mPhase);
    document["roofRunoffPhaseTimer"] = mTimer;
    document["roofRunoffRowMask"] = mRowMask;
}

/** 旧档缺字段时恢复空积累；锁定余时和行组只从存档恢复。 */
void RoofRunoff::Load(const nlohmann::json& document, bool supported, int rows) {
    const int phase = document.value("roofRunoffPhase", static_cast<int>(RoofRunoffPhase::IDLE));
    Restore(supported, rows, document.value("roofRunoffCharge", 0.0f),
        phase >= 0 && phase <= static_cast<int>(RoofRunoffPhase::FLOWING)
            ? static_cast<RoofRunoffPhase>(phase) : RoofRunoffPhase::IDLE,
        document.value("roofRunoffRowMask", 0), document.value("roofRunoffPhaseTimer", 0.0f),
        document.value("roofRunoffRetainedCharge", 30.0f));
}

int RoofRunoff::RowCount() const { return std::popcount(static_cast<unsigned>(mRowMask)); }

/** 冲刷动画只消费状态机已推进的归一化时间。 */
float RoofRunoff::FlowProgress() const {
    return mPhase == RoofRunoffPhase::FLOWING
        ? std::clamp(1.0f - mTimer / kRoofRunoffFlowDuration, 0.0f, 1.0f) : 0.0f;
}

float RoofRunoff::DefaultDuration(RoofRunoffPhase phase) {
    return phase == RoofRunoffPhase::WARNING ? kRoofRunoffWarningDuration : kRoofRunoffFlowDuration;
}
