#pragma once
#include "Game/WeatherTypes.h"
#include <nlohmann/json_fwd.hpp>

/** Board 独占的雷荷周期；实体选择、路线和伤害结算由 Board 在返回的边沿协调。 */
class NightRoofChargeCycle {
public:
    enum class Transition { None, DischargeStarted, CycleFinished };
    void Reset() { *this = NightRoofChargeCycle{}; }
    /** 正向输入：积累期返回跨满值的溢出，供 Board 锁路后保留；活动期直接截留余电。 */
    float Add(float amount);
    /** 锁路之后截留同笔输入的溢出，不改变本次预警强度。 */
    void RetainOverflow(float amount);
    /** 晴夜只泄漏积累期电荷，活动期余电不泄漏。 */
    void Leak(float amount);
    /** Board 已选择劫持者后提交唯一预警窗口。 */
    void BeginWarning(bool extended);
    /** 消耗阶段时间；先让 Board 处理最后一秒实体动作，再调用 CommitExpiredPhase。 */
    void Elapse(float seconds);
    /** 到期仅切换一次；调用方只在 DischargeStarted 边沿结算伤害。 */
    Transition CommitExpiredPhase();
    /** 恢复数值周期而不重放边沿；地图、行与实体组合由 Board 先校验。 */
    void Restore(float charge, NightRoofChargePhase phase, float seconds, float overcharge, bool extended);
    /** 使用原存档键保存周期数值；路线与实体 ID 由 Board 保存。 */
    void Save(nlohmann::json& document) const;
    float Charge() const { return mCharge; }
    float Overcharge() const { return mOvercharge; }
    NightRoofChargePhase Phase() const { return mPhase; }
    float Timer() const { return mTimer; }
private:
    float mCharge = 0.0f;
    float mOvercharge = 0.0f;
    NightRoofChargePhase mPhase = NightRoofChargePhase::CHARGING;
    float mTimer = 0.0f;
};
