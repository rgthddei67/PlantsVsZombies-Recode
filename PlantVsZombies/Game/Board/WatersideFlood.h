#pragma once

#include "Game/WeatherTypes.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace WatersideFloodRules {
    inline constexpr float MaximumWater = 100.0f; // 水域值上限；最高水位覆盖棋盘和四周背景
    inline constexpr float LightGain = 0.20f; // 小雨每游戏秒增加的水域值
    inline constexpr float MediumGain = 0.45f; // 中雨每游戏秒增加的水域值
    inline constexpr float HeavyGain = 0.90f; // 大雨每游戏秒增加的水域值
    inline constexpr float TorrentGain = 3.00f; // 暴雨每游戏秒增加的水域值
    inline constexpr float ClearDrain = LightGain / 5.0f; // 晴天排水率：一分钟小雨需五分钟晴天排回
    inline constexpr float ShoreStep = 18.0f; // 每远离原水岸一格所需的额外水域值
    inline constexpr float StoneRise = 12.0f; // 石地相对同距离天然土的额外淹没阈值
    inline constexpr float ShoreBlend = 8.0f; // 水面越过一个格子的连续过渡水域值跨度
    inline constexpr float OuterShoreRise = 18.0f; // 场外每延伸一格的地势增量；上限保留满水淹没余量
    inline constexpr float DrowningSeconds = 15.0f; // 陆生植物无睡莲时的溺亡宽限，游戏秒
    inline constexpr float PotGraceSeconds = 20.0f; // 退水后石地补花盆的宽限，游戏秒
    inline constexpr float AquaticDrySeconds = 20.0f; // 纯水生能力植物退水后的缺水宽限，游戏秒
    inline constexpr int TorrentChance = 35; // 水岸章节新大雨候选升级为暴雨的百分比
    inline constexpr float OverflowCapacity = 60.0f; // 一次冲击消耗的溢流水量；满水暴雨约每20游戏秒蓄满
    inline constexpr float SurgeWarningSeconds = 5.0f; // 锁定睡莲后的反应窗口，游戏秒
    inline constexpr int SurgeTargetLimit = 2; // 每次冲击最多损坏的睡莲数量，不是场上或全关累计上限
}

/** Board 独占的连续水域值；真实水格、表现和仪表均从同一数值派生。 */
class WatersideFlood {
public:
    void Advance(float seconds, RainIntensity rain);
    float GetWater() const { return mWater; }
    float GetOverflow() const { return mOverflow; }
    float GetWarningRemaining() const { return mWarningRemaining; }
    const std::vector<int>& GetTargets() const { return mTargets; }
    int GetSurgeCount() const { return mSurgeCount; }
    bool CanBeginSurge() const;
    /** 消费一份溢流并锁定实体 ID；预警结束后不重选已被救援或移除的目标。 */
    void BeginSurge(std::vector<int> targets);
    void FinishSurge();
    static float Rate(RainIntensity rain);
    void SetWater(float value);
    nlohmann::json Save() const;
    void Load(const nlohmann::json& value);
private:
    float mWater = 0.0f; // 当前累积水域值；零表示只剩原始水池
    float mOverflow = 0.0f; // 满水后的额外积累，水域值；晴天先排溢流再降水位
    float mWarningRemaining = 0.0f; // 当前预警余时，游戏秒
    int mSurgeCount = 0; // 本关已结算冲击次数，供持久化和诊断
    std::vector<int> mTargets; // 当前已锁定睡莲 ID，读档保留且最多两片
};
