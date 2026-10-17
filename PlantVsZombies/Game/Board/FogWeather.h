#pragma once
#include "Game/WeatherTypes.h"
#include <nlohmann/json_fwd.hpp>

/** Board 独占的雾势、预报与风致驱散状态；不查询实体或提交绘制，时间均为游戏秒。 */
class FogWeather {
public:
    /** 按地图资格初始化；重复调用保留当前阶段，不支持时清空状态。 */
    void Initialize(bool supported);
    /** 自然周期推进；返回雾势是否改变，供 Board 在该边沿发送展示提示。 */
    bool AdvanceWeather(float seconds, float directorFactor);
    /** 推进风致驱散与漂移；完全吹散增强雾时结束事件并返回 true。 */
    bool AdvanceDispersal(float seconds, TyphoonStrength strength, WindDirection direction,
        bool hasTyphoon, float directorFactor);
    /** 强制或锁定雾势；可选择重置驱散，保留独立的纹理呼吸时钟。 */
    bool SetFixedWeather(FogWeatherIntensity intensity, float duration, bool resetDispersal);
    /** 设备接管当前雾势并停用自然预报；保留原有倒计时、驱散与漂移。 */
    void ApplyDeviceControl(bool supported, FogWeatherIntensity intensity);
    /** 仅隐藏公开预报，不改变已经锁定的真实结果和揭晓时刻。 */
    void DisruptForecast();
    /** 注入已提交预报供确定性验证；不消费随机数。 */
    void SetForecast(FogWeatherIntensity forecast, FogWeatherIntensity actual, float revealIn);
    /** 注入有限的驱散比例；非法浮点值拒绝写入。 */
    bool SetDispersal(float dispersal);
    /** 推进不入档的纹理呼吸时钟。 */
    void AdvanceAnimation(float seconds);
    /** 沿用既有 JSON 键和缺字段语义；恢复不抽随机数、不发送提示。 */
    void Save(nlohmann::json& document) const;
    void Load(const nlohmann::json& document, bool supported);
    /** 按 Board 提供的 0..1 天气导演压力插值大雾概率。 */
    static int DenseChance(float directorFactor);

    FogWeatherIntensity Intensity() const { return mFogWeatherIntensity; }
    FogWeatherIntensity Forecast() const { return mForecastFogWeatherIntensity; }
    FogWeatherIntensity ActualForecast() const { return mActualForecastFogWeatherIntensity; }
    float Timer() const { return mFogWeatherTimer; }
    float Dispersal() const { return mFogDispersal; }
    float VisualOffsetX() const { return mFogVisualOffsetX; }
    float AnimationTime() const { return mFogAnimationTime; }
    bool Initialized() const { return mFogWeatherInitialized; }
    bool HasForecast() const { return mFogWeatherForecastReady && !mFogWeatherForecastDisrupted; }
    bool ForecastDisrupted() const { return mFogWeatherForecastReady && mFogWeatherForecastDisrupted; }
private:
    /** 清除公开与真实预报；不改变当前阶段。 */
    void ClearForecast();
    /** 按当前阶段抽取下一雾势；增强雾之后固定回到默认雾且不抽签。 */
    FogWeatherIntensity RollNext(float directorFactor);
    /** 在揭晓窗口锁定唯一真实结果。 */
    void PrepareForecast(float directorFactor);
    /** 消费已提交结果并抽取持续时间，返回雾势是否改变。 */
    bool ConsumeForecast(float directorFactor);
    /** 设置阶段并清除上一份预报，返回雾势是否改变。 */
    bool Begin(FogWeatherIntensity intensity, float duration);
    /** 天气导演缩短后期默认雾休整窗口。 */
    static float RandomDefaultDuration(float directorFactor);
    /** 校验恢复状态；不重新抽取未来事件。 */
    void Restore(bool supported, bool initialized, FogWeatherIntensity intensity,
        FogWeatherIntensity forecast, FogWeatherIntensity actual,
        float timer, bool forecastReady, float dispersal, float visualOffsetX);
	FogWeatherIntensity mFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	FogWeatherIntensity mForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	FogWeatherIntensity mActualForecastFogWeatherIntensity = FogWeatherIntensity::DEFAULT;
	float mFogWeatherTimer = 0.0f;      // DEFAULT 时距下一次增强雾势，其余档位为本次事件剩余游戏秒
	float mFogDispersal = 0.0f;         // 台风累积驱散比例（0～1）；停风后缓慢回落，基础雾随之回流
	float mFogVisualOffsetX = 0.0f;     // 随实时风向平滑漂移的纯视觉横向偏移，单位像素
	float mFogAnimationTime = 0.0f;     // 雾纹理轻微呼吸的游戏秒计时；纯视觉，不入存档
	bool mFogWeatherInitialized = false; // 旧档缺雾势字段时由 StartGame 首次初始化
	bool mFogWeatherForecastReady = false; // 已锁定公开/真实下一雾势，等待独立雾势倒计时揭晓
	bool mFogWeatherForecastDisrupted = false; // 当前雾势预报已被隐藏；真实下一雾势仍按原计时揭晓
};
