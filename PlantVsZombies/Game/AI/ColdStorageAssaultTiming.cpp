#include "ColdStorageAssaultTiming.h"
#include "ColdStorageSearch.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace ColdStorageSearch {
namespace {
constexpr size_t kScheduleLimit=4; // 单个原编队最多四个时序变体，与自由探索共用原预算
constexpr float kAfterEvent=0.5f; // 事件后再留半秒入场，避免同一预测步的先后顺序歧义

/** 不排序成员；同刻出生的原提交顺序也属于候选身份。 */
bool SameSchedule(const std::vector<Action>& a,const std::vector<Action>& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const Action& x,const Action& y) {
        return x.option==y.option && x.delay==y.delay;
    });
}
}

std::vector<std::vector<Action>> BuildAssaultSchedules(const Snapshot& state,
    const std::vector<Action>& formation,float maxDelay) {
    std::vector<std::vector<Action>> schedules;
    if(formation.empty() || !std::isfinite(maxDelay) || maxDelay<=0) return schedules;
    std::map<int,int> counts;
    for(const auto& action:formation) {
        if(action.option<0 || static_cast<size_t>(action.option)>=state.options.size()) return {};
        if(state.options[action.option].device<0) ++counts[action.option];
    }
    if(counts.empty()) return schedules;

    std::vector<float> events;
    const auto addEvent=[&](float impact) {
        if(!std::isfinite(impact) || impact<0 || impact+kAfterEvent>maxDelay) return;
        const float after=impact+kAfterEvent;
        if(std::find(events.begin(),events.end(),after)==events.end()) events.push_back(after);
    };
    // 首次清除只等瞄准落地；结算后的45秒冷却不属于本次跟进等待。
    float precision=-1;
    if(state.precisionTargetID>0) precision=ColdStorageSkillRules::StrikeAimDuration
        +(state.precisionUnlockAfterPurchase ? std::max(0.0f,state.precisionUnlockAimStartSeconds) : 0);
    if(state.pendingPrecisionID>0) precision=std::max(precision,state.pendingPrecisionRemaining);
    addEvent(precision);

    float committed=-1, reusable=-1;
    bool coolingSource=false;
    std::map<int,float> sourceImpacts;
    for(const auto& counter:state.counters) {
        if(counter.consumesPlant || counter.blast.damage<=0) continue;
        if(counter.blast.committed) {
            // 已提交弹的ready已经是剩余爆炸时刻，来源死亡能否取消仍由完整推演决定。
            if(std::isfinite(counter.blast.ready)) committed=std::max(committed,counter.blast.ready);
        } else {
            if(counter.plantID>0 && std::none_of(state.plants.begin(),state.plants.end(),[&](const Plant& plant) {
                return plant.id==counter.plantID && plant.health>0;
            })) continue;
            coolingSource|=counter.plantID>0 && counter.blast.ready>0;
            // 同一炮的多个爆点共用一次装填；windup已包含飞行，不再加flightSeconds。
            const float impact=std::max(0.0f,counter.blast.ready)+std::max(0.0f,counter.windup);
            if(!std::isfinite(impact)) continue;
            const auto [at,inserted]=sourceImpacts.emplace(counter.source,impact);
            if(!inserted) at->second=std::min(at->second,impact);
        }
    }
    for(const auto& [source,impact]:sourceImpacts) {
        (void)source;
        reusable=reusable<0 ? impact : std::min(reusable,impact);
    }
    // 活炮已经装填时，先给当前空窗一个入场提案；不等待它装好才开始进攻。
    // 这不声称全场安全，也不重置其他就绪炮，所有来源仍由完整反制世界兑现。
    if(precision<0 && committed<0 && coolingSource) addEvent(0);
    addEvent(committed);
    addEvent(reusable);

    const auto append=[&](std::vector<Action> plan) {
        if(schedules.size()>=kScheduleLimit
            || std::any_of(schedules.begin(),schedules.end(),[&](const auto& old){return SameSchedule(old,plan);})) return;
        schedules.push_back(std::move(plan));
    };
    // 先覆盖各真实事件后的完整同步编队，避免分段候选抢掉清除后全队跟进的唯一名额。
    for(float after:events) {
        auto plan=formation;
        for(auto& action:plan) action.delay=state.options[action.option].device>=0 ? 0 : after;
        append(std::move(plan));
    }
    for(float after:events) {
        if(schedules.size()>=kScheduleLimit) break;
        auto plan=formation;
        std::map<int,int> seen;
        bool lead=false,follow=false;
        for(auto& action:plan) {
            if(state.options[action.option].device>=0) {action.delay=0;continue;}
            const int count=counts.at(action.option);
            // 从每种同路重复成员中抽少量先行，完整后队保留每种类型及全部单例搭档。
            // 不按肉盾/后排标签拆开整个兵种，也不复制支援或预先判定先行能骗掉炮。
            const int first=count>1 ? std::max(1,count/3) : 0;
            const bool early=seen[action.option]++<first;
            action.delay=early ? 0 : after;
            lead|=early; follow|=!early;
        }
        if(lead && follow) append(std::move(plan));
    }
    return schedules;
}
}
