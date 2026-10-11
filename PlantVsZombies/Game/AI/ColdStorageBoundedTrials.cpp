#include "ColdStorageBoundedTrials.h"
#include "ColdStorageObservedResponse.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <utility>

namespace ColdStorageTrials {
namespace {
bool Finite(float value) { return (std::bit_cast<std::uint32_t>(value)&0x7f800000u)!=0x7f800000u; }

/** 非法真实账本必须拒绝，不能借负储备、无穷额度或非数值扩大试探预算。 */
bool ValidCapital(float wallet,float reserve,float remainingRisk,float outstanding) {
    return Finite(wallet) && Finite(reserve) && Finite(remainingRisk) && Finite(outstanding)
        && wallet>=0 && reserve>=0 && remainingRisk>=0 && outstanding>=0;
}

/** 逐项检查恢复边界，保留失败与未决记录的原始语义。 */
bool ValidRecords(std::span<const Record> records) {
    if(records.size()>MaximumRecords) return false;
    int unresolved=0;
    std::int64_t cost=0,production=0;
    std::array<int,MaximumRecords*MaximumPaidWaves> waves{};
    std::size_t waveCount=0;
    for(std::size_t i=0;i<records.size();++i) {
        const auto& record=records[i];
        if(!record.signature || record.latestPaidWave<=0 || record.cost<=0 || record.cost>MaximumCounter
            || record.production<0 || record.production>MaximumCounter
            || !Finite(record.startedAt) || record.startedAt<0 || (record.unresolved && ++unresolved>1)
            || record.paidWaves.empty() || record.paidWaves.size()>MaximumPaidWaves
            || record.paidWaves.size()>static_cast<std::size_t>(record.cost)
            || record.paidWaves.back()!=record.latestPaidWave) return false;
        cost+=record.cost;production+=record.production;
        if(cost>MaximumCounter || production>MaximumCounter) return false;
        int previousWave=0;
        for(int wave:record.paidWaves) {
            if(wave<=previousWave) return false;
            previousWave=wave;waves[waveCount++]=wave;
        }
        for(std::size_t j=0;j<i;++j) {
            const auto& previous=records[j];
            if(previous.signature==record.signature || previous.latestPaidWave==record.latestPaidWave) return false;
            if((previous.latestPaidWave<record.latestPaidWave && previous.startedAt>record.startedAt)
                || (previous.latestPaidWave>record.latestPaidWave && previous.startedAt<record.startedAt)) return false;
        }
    }
    // 最多2048个来源，排序核唯一性；避免在每次候选检查中逐对比较全部波号。
    std::sort(waves.begin(),waves.begin()+waveCount);
    if(std::adjacent_find(waves.begin(),waves.begin()+waveCount)!=waves.begin()+waveCount) return false;
    return true;
}

/** 调用前历史已经验证；合计不超过MaximumCounter，使用宽类型保留检查顺序的安全性。 */
std::pair<std::int64_t,std::int64_t> Totals(std::span<const Record> records) {
    std::int64_t cost=0,production=0;
    for(const auto& record:records) {cost+=record.cost;production+=record.production;}
    return {cost,production};
}
}

const char* ReasonName(Reason reason) {
    switch(reason) {
    case Reason::Allowed: return "allowed";
    case Reason::InvalidInput: return "invalid_input";
    case Reason::InvalidHistory: return "invalid_history";
    case Reason::Unresolved: return "unresolved";
    case Reason::FailedHypothesis: return "failed_hypothesis";
    case Reason::HistoryFull: return "history_full";
    case Reason::WaveAlreadyUsed: return "wave_already_used";
    case Reason::BeforePreviousTrial: return "before_previous_trial";
    case Reason::CounterLimit: return "counter_limit";
    case Reason::NoRisk: return "no_risk";
    case Reason::OverCap: return "over_cap";
    case Reason::WaveHistoryFull: return "wave_history_full";
    }
    return "invalid_reason";
}

int PurchaseCap(float wallet,float reserve,float remainingRisk,float outstandingPaidCapital) {
    if(!ValidCapital(wallet,reserve,remainingRisk,outstandingPaidCapital)) return 0;
    const float risk=ColdStorageResponse::PurchaseRiskCap(wallet,reserve,remainingRisk,outstandingPaidCapital);
    return static_cast<int>(std::floor(std::min(static_cast<float>(MaximumPurchaseIce),risk)));
}

bool State::Valid() const { return !mDisabled && ValidRecords(mRecords); }

bool State::Restore(std::span<const Record> records) {
    if(mDisabled) return false;
    if(!ValidRecords(records)) { Disable(); return false; }
    std::vector<Record> restored(records.begin(),records.end());
    mRecords.swap(restored);
    return true;
}

Reason State::Availability(std::uint64_t signature) const {
    if(!signature) return Reason::InvalidInput;
    if(!Valid()) return Reason::InvalidHistory;
    const auto [cost,production]=Totals(mRecords);
    if(cost>=MaximumCounter || production>=MaximumCounter) return Reason::CounterLimit;
    if(std::any_of(mRecords.begin(),mRecords.end(),[](const Record& record) {
        return record.unresolved && record.production<record.cost;
    }))
        return Reason::Unresolved;
    const auto found=std::find_if(mRecords.begin(),mRecords.end(),[&](const Record& record) {return record.signature==signature;});
    if(found==mRecords.end()) return mRecords.size()<MaximumRecords ? Reason::Allowed : Reason::HistoryFull;
    if(found->production<found->cost) return Reason::FailedHypothesis;
    return found->paidWaves.size()<MaximumPaidWaves ? Reason::Allowed : Reason::WaveHistoryFull;
}

Decision State::CheckPurchase(std::uint64_t signature,int bill,float wallet,float reserve,
    float remainingRisk,float outstandingPaidCapital) const {
    if(bill<=0 || bill>MaximumCounter || !ValidCapital(wallet,reserve,remainingRisk,outstandingPaidCapital))
        return {Reason::InvalidInput,0};
    Decision decision{Availability(signature),PurchaseCap(wallet,reserve,remainingRisk,outstandingPaidCapital)};
    if(!decision.Allowed()) return decision;
    if(Totals(mRecords).first>MaximumCounter-bill) {
        decision.reason=Reason::CounterLimit;
        return decision;
    }
    decision.reason=decision.cap<=0 ? Reason::NoRisk : bill>decision.cap ? Reason::OverCap : Reason::Allowed;
    return decision;
}

Reason State::Commit(std::uint64_t signature,int wave,int actualCost,float at) {
    if(wave<=0 || actualCost<=0 || actualCost>MaximumCounter || !Finite(at) || at<0) return Reason::InvalidInput;
    if(actualCost>MaximumPurchaseIce) return Reason::OverCap;
    const auto reason=Availability(signature);
    if(reason!=Reason::Allowed) return reason;
    if(Totals(mRecords).first>MaximumCounter-actualCost) return Reason::CounterLimit;
    for(const auto& record:mRecords) {
        if(wave<=record.latestPaidWave) return Reason::WaveAlreadyUsed;
        if(at<record.startedAt) return Reason::BeforePreviousTrial;
    }
    const auto found=std::find_if(mRecords.begin(),mRecords.end(),[&](const Record& record) {return record.signature==signature;});
    // 迁移的已回本未决位也在成功提交边沿释放，不能与新的未回本记录同时留存。
    for(auto& record:mRecords) if(record.production>=record.cost) record.unresolved=false;
    if(found==mRecords.end()) mRecords.push_back({signature,wave,actualCost,0,true,at,{wave}});
    else {
        found->cost+=actualCost;
        found->latestPaidWave=wave;
        found->paidWaves.push_back(wave);
        found->unresolved=found->production<found->cost;
        found->startedAt=at;
    }
    return Reason::Allowed;
}

int State::CreditProduction(int wave,int acceptedAmount) {
    if(wave<=0 || acceptedAmount<=0 || !Valid()) return 0;
    const auto found=std::find_if(mRecords.begin(),mRecords.end(),[&](const Record& record) {
        return std::binary_search(record.paidWaves.begin(),record.paidWaves.end(),wave);
    });
    // 清场后的亏损是假设终局；过期/重放的来源不能洗白失败记录。
    if(found==mRecords.end() || (!found->unresolved && found->production<found->cost)) return 0;
    const int remaining=static_cast<int>(MaximumCounter-Totals(mRecords).second);
    const int credited=std::min(acceptedAmount,remaining);
    found->production+=credited;
    if(found->production>=found->cost) found->unresolved=false;
    return credited;
}

bool State::Settle(bool hasTroopActivity) {
    if(!Valid()) return false;
    bool settled=false;
    for(auto& record:mRecords) if(record.unresolved && (!hasTroopActivity || record.production>=record.cost)) {
        record.unresolved=false;
        settled=true;
    }
    return settled;
}
}
