#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "WorkshopChallenges.h"
#include "WorkshopTraits.h"

namespace Workshop {
enum class Blueprint { PressureGrower, HeavyGatling, BlazeTorch, RapidMagnet, HeavyThreepeater, EmergencyCotton, Count };
enum class CommissionGoal { Produce, Deliver, Recycle, Defeat, Survive, Battle, Count };
enum class CommissionStatus { Offered, Running, Ready, Expired, Taken };
inline constexpr int BlueprintCount = static_cast<int>(Blueprint::Count);
inline constexpr int CommissionOfferCount = 6; // 两简单、一中等经营、一中等战斗，另两格独立随机
inline constexpr int CommissionActiveLimit = 2; // 同时接取上限；完成待领取仍占名额，过期不占
inline constexpr int CommissionRefreshWaves = 5; // 每守住五波刷新未接任务，重试不计为新波
inline constexpr std::array<float, 2> CommissionDeadlines{90, 150}; // 经营任务时限，单位：袭击游戏秒；战斗只限目标波
inline constexpr int HardCommissionChance = 50; // 后两格各自抽困难的百分比，允许整批没有困难
inline constexpr std::array<int, 3> BlueprintChances{0, 5, 25}; // 各难度每条任务抽到蓝图的百分比，无保底
inline constexpr std::array<int, BlueprintCount> BlueprintUnlockWaves{6, 10, 14, 12, 18, 22}; // 必须守住的波数；新蓝图追加以保留既有 ID

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
    Challenge challenge = Challenge::None;
    int targetWave = 0;
    float timeLimit = 0;
    Trait trait = Trait::Count;
    bool legacy = false; // v8 未完成委托保留原目标/期限，不追溯新增战斗或词条奖励
    bool IsBattle() const { return challenge != Challenge::None; }
};

/** 持有委托、随机序列和本基地奖励；只接受已提交的经营/战斗事件，独立于玩家教程档。 */
class Commissions {
public:
    void Reset(std::uint32_t seed = 0x4D595DF4u, int completedWaves = 0);
    void Tick(float seconds);
    /** 仅在正式守波结算时调用一次；按累计波次刷新，保留已接任务。 */
    void OnWaveCompleted(int completedWaves);
    void Record(CommissionGoal goal, int amount = 1);
    bool CanAccept(int offer) const;
    bool Accept(int offer, int nextWave = 1);
    Challenge ChallengeForWave(int wave) const;
    void RecordBreach(int wave);
    bool Abandon(int active);
    /** 领奖原子消费该任务，返回阳光数；失败为 -1，不改变任务或蓝图。 */
    int Claim(int active);
    bool Has(Blueprint blueprint) const;
    int FreeUses(Blueprint blueprint) const;
    bool ConsumeFreeUse(Blueprint blueprint);
    static int RefreshWavesRemaining(int completedWaves) { return CommissionRefreshWaves - completedWaves % CommissionRefreshWaves; }
    int ActiveCount() const;
    const auto& Offers() const { return mOffers; }
    const auto& Active() const { return mActive; }
    const auto& Traits() const { return mTraits; }
    TraitBonuses Bonuses() const { return AggregateTraits(mTraits); }
    nlohmann::json Save() const;
    /** 严格验证后提交；错误抛出，由 World 的临时对象事务边界处理。 */
    void Load(const nlohmann::json& document, int completedWaves);
private:
    std::array<Commission, CommissionOfferCount> mOffers{};
    std::vector<Commission> mActive;
    std::array<bool, BlueprintCount> mBlueprints{};
    std::array<int, BlueprintCount> mFreeUses{};
    std::array<int, TraitCount> mTraits{};
    std::uint32_t mRandom = 1;
    std::uint64_t mNextId = 1;
    std::uint32_t Random();
    void Refresh(int completedWaves);
    void Complete(Commission& commission);
};
}
