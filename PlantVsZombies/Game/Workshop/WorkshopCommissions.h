#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Workshop {
enum class Blueprint { PressureGrower, HeavyGatling, BlazeTorch, Count };
enum class CommissionGoal { Produce, Deliver, Recycle, Defeat, Survive, Count };
enum class CommissionStatus { Offered, Running, Ready, Expired, Taken };
inline constexpr int BlueprintCount = static_cast<int>(Blueprint::Count);
inline constexpr int CommissionOfferCount = 6; // 每批简单、中等、困难各两项
inline constexpr int CommissionActiveLimit = 2; // 同时接取上限；完成待领取仍占名额，过期不占
inline constexpr float CommissionRefreshSeconds = 600; // 仅袭击推进的刷新周期，单位：游戏秒
inline constexpr std::array<float, 3> CommissionDeadlines{300, 480, 720}; // 三档接取后期限，单位：袭击游戏秒
inline constexpr std::array<int, 3> BlueprintChances{0, 5, 25}; // 各难度每条任务抽到蓝图的百分比，无保底
inline constexpr std::array<int, BlueprintCount> BlueprintUnlockWaves{6, 10, 14}; // 必须守住的波数

const char* BlueprintKey(Blueprint blueprint);
const char* BlueprintName(Blueprint blueprint);
const char* CommissionGoalName(CommissionGoal goal);
const char* CommissionDifficultyName(int difficulty);

struct Commission {
    std::uint64_t id = 0;
    CommissionGoal goal = CommissionGoal::Produce;
    CommissionStatus status = CommissionStatus::Offered;
    int difficulty = 0, target = 1, progress = 0, sunReward = 0;
    float remaining = 0;
    Blueprint blueprint = Blueprint::Count;
};

/** 只持有委托、随机序列和本基地蓝图；生产/战斗事务成功后才提交进度，独立于玩家教程档。 */
class Commissions {
public:
    void Reset(std::uint32_t seed = 0x4D595DF4u);
    void Tick(float seconds, int completedWaves);
    void Record(CommissionGoal goal, int amount = 1);
    bool Accept(int offer);
    bool Abandon(int active);
    /** 领奖原子消费该任务，返回阳光数；失败为 -1，不改变任务或蓝图。 */
    int Claim(int active);
    bool Has(Blueprint blueprint) const;
    int FreeUses(Blueprint blueprint) const;
    bool ConsumeFreeUse(Blueprint blueprint);
    float RefreshRemaining() const { return mRefreshRemaining; }
    int ActiveCount() const;
    const auto& Offers() const { return mOffers; }
    const auto& Active() const { return mActive; }
    nlohmann::json Save() const;
    /** 严格验证后提交；错误抛出，由 World 的临时对象事务边界处理。 */
    void Load(const nlohmann::json& document, int completedWaves);
private:
    std::array<Commission, CommissionOfferCount> mOffers{};
    std::vector<Commission> mActive;
    std::array<bool, BlueprintCount> mBlueprints{};
    std::array<int, BlueprintCount> mFreeUses{};
    float mRefreshRemaining = CommissionRefreshSeconds;
    std::uint32_t mRandom = 1;
    std::uint64_t mNextId = 1;
    std::uint32_t Random();
    void Refresh(int completedWaves);
};
}
