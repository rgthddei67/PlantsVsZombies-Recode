#pragma once
#include "Game/WeatherTypes.h"
#include <nlohmann/json_fwd.hpp>

/** Board 独占的寒潮计划与温度曲线；不查询实体、雨势或绘制系统。 */
class WinterClimate {
public:
    /** 初始化随机周期或尚未消费的固定开幕；重复进入和读档不会重抽计划。 */
    void Initialize(bool supported, bool openingScript, bool finalOpening);
    /** 按游戏秒推进降温、低温和回暖；暂停不推进，不重放已提交计划。 */
    void Advance(float seconds);
    /** 仅标记预报被干扰；Board 在返回 true 后通知依赖预报的实体。 */
    bool DisruptForecast();
    bool HasForecast() const;
    bool IsForecastDisruptionVisible() const;
    bool IsActive() const;
    /** 沿用旧档字段、缺字段回退和校验范围；恢复不消费 RNG。 */
    void Save(nlohmann::json& document) const;
    void Load(const nlohmann::json& document, bool supported);
    /** 校验后一次替换测试计划；非法输入保持原状态。 */
    bool SetForTesting(float temperatureC, ColdWavePhase phase, float remaining,
        ColdWaveStrength strength, float targetTemperatureC, float coolingDuration,
        float holdDuration, float thawDuration, int frostVariant);
    static float WarmTemperature();
    static float ColdTemperature();
    static float FreezingTemperature();
    bool Initialized() const { return mWinterTemperatureInitialized; }
    bool OpeningInitialized() const { return mOpeningColdWavePlanInitialized; }
    ColdWavePhase Phase() const { return mColdWavePhase; }
    float Timer() const { return mColdWaveTimer; }
    float Temperature() const { return mAmbientTemperatureC; }
    ColdWaveStrength Strength() const { return mColdWaveStrength; }
    float Target() const { return mColdWaveTargetTemperatureC; }
    float CoolingDuration() const { return mColdWaveCoolingDuration; }
    float HoldDuration() const { return mColdWaveHoldDuration; }
    float ThawDuration() const { return mColdWaveThawDuration; }
    bool ForecastDisrupted() const { return mColdWaveForecastDisrupted; }
    int FrostVariant() const { return mWinterFrostVariant; }
private:
    /** 固定教学开幕不抽随机数，霜线由关卡语义选择。 */
    void InitializeOpeningPlan(bool finalOpening);
    /** 周期开始前一次锁定强度、温度、各阶段时长与霜线。 */
    void RollNext();
	bool mWinterTemperatureInitialized = false; // 冬日花园寒潮状态是否已初始化；旧档由 StartGame 补齐
	bool mOpeningColdWavePlanInitialized = false; // 7-8/7-9 开幕寒潮是否已锁定，读档后不得重复注入
	ColdWavePhase mColdWavePhase = ColdWavePhase::CALM; // 当前寒潮阶段，与雨势完全正交
	float mColdWaveTimer = 0.0f;        // 当前寒潮阶段剩余游戏秒
	float mAmbientTemperatureC = 6.0f;  // 当前环境温度，单位摄氏度；只由寒潮状态机写入
	ColdWaveStrength mColdWaveStrength = ColdWaveStrength::STRONG; // 已锁定寒潮强度；预报与揭晓共用
	float mColdWaveTargetTemperatureC = -12.0f; // 已锁定最低温，单位摄氏度
	float mColdWaveCoolingDuration = 20.0f; // 已锁定降温总时长，单位游戏秒
	float mColdWaveHoldDuration = 57.5f; // 已锁定低温维持总时长，单位游戏秒
	float mColdWaveThawDuration = 32.0f; // 已锁定回暖总时长，单位游戏秒
	bool mColdWaveForecastDisrupted = false; // 本轮准确预报是否已被干扰；下一轮重新开放
	int mWinterFrostVariant = 0;       // 本轮寒潮稳定使用的冻融线轮廓变体（0～2）

};
