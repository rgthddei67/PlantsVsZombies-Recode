#pragma once
#include "Game/WeatherTypes.h"
#include <nlohmann/json_fwd.hpp>

/** Board 独占的径流状态机；不拥有实体、展示或雷荷状态。所有时间均为游戏秒。 */
class RoofRunoff {
public:
    void Reset() { *this = RoofRunoff{}; }
    /** 推进计时/积累；返回 true 时 Board 应在同一步提供导流候选并调用 LockRows。 */
    bool Advance(float seconds, bool supported, int rows, RainIntensity rain);
    /** 只在满积累边沿抽取行组和残留湿度；导流只能替换一行，不扩大规模。 */
    void LockRows(int rows, int guideRow);
    /** 恢复已提交状态并校验地形和组合；不消费随机数，不重放预警。 */
    void Restore(bool supported, int rows, float charge, RoofRunoffPhase phase,
        int rowMask, float timer, float retained);
    /** 沿用现有关卡 JSON 键；内部所有权变化不改变存档 schema。 */
    void Save(nlohmann::json& document) const;
    void Load(const nlohmann::json& document, bool supported, int rows);

    float Charge() const { return mCharge; }
    float RetainedCharge() const { return mRetainedCharge; }
    RoofRunoffPhase Phase() const { return mPhase; }
    float Timer() const { return mTimer; }
    int RowMask() const { return mRowMask; }
    bool RowSelected(int row) const { return row >= 0 && row < 31 && (mRowMask & (1 << row)) != 0; }
    int RowCount() const;
    float FlowProgress() const;
    static float DefaultDuration(RoofRunoffPhase phase);
private:
    float mCharge = 0.0f;
    float mRetainedCharge = 0.0f;
    RoofRunoffPhase mPhase = RoofRunoffPhase::IDLE;
    float mTimer = 0.0f;
    int mRowMask = 0;
};
