#pragma once

#include "ColdStorageSearch.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

/** 独立行为世界的公开机会投影；不写Board历史，不决定付款或修改普通反制世界。 */
namespace ColdStorageObservedForecast {
struct Block { int row=0,column=0; float until=0; };
struct View {
    const ColdStorageSearch::Snapshot& snapshot;
    std::span<const ColdStorageSearch::Unit> units;
    std::span<const ColdStorageSearch::Plant> plants;
    std::span<const ColdStorageSearch::Counter> counters;
    std::span<const float> sourceReady;
    float time=0,sun=0,ice=0;
    std::span<const Block> blocks;
    std::span<const ColdStorageStrategy::BlastThreat> pending; // 仅当前仍未结算的已公开承诺
    const std::array<float,54>* fogAlpha=nullptr;
    std::span<const unsigned char> directPlacementLegal; // 可选：与counters同序的当前直接落种许可，不含铲种
    float sceneBottom=600;
};
struct Opportunity {
    ColdStorageResponse::Context context;
    bool eligible=false,visibleHostiles=false,censored=false,unknownGeometry=false;
    std::vector<unsigned char> directPaidCounters; // 与View.counters同序；仅有未承诺目标的确切落点，仍须同时获来源许可
};

/** 从本步已有单位/卡槽/钱包构造与Board一致的保守公开包络；未知情况不产生乐观许可。 */
Opportunity Inspect(const View& view);

/** 每个完整推演世界各持一份；同卡槽换上下文或删失不续期，真正失去机会才重置年龄。 */
class WorldClock {
public:
    explicit WorldClock(const ColdStorageResponse::Forecast& forecast);
    /** time是相对快照的游戏秒；第一次t=0保留已观察年龄，新机会从零开始。 */
    void Update(float time,const Opportunity& opportunity);
    /** 返回允许延后到已观察H窗口的真实paid来源位；免费炮、未知和已承诺动作无来源位。 */
    std::uint64_t DeferredSources(const ColdStorageResponse::Profile& profile,const Opportunity& opportunity) const;
    float SourceAge(int sourceKey) const;
private:
    std::array<float,64> mAges;
    std::uint64_t mAvailable=0;
    float mLastTime=0;
    bool mValid=true;
};
}
