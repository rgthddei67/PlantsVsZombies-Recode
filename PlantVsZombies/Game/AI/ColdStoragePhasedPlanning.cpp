#include "ColdStoragePhasedPlanning.h"
#include "ColdStorageFormationSeeds.h"
#include "Game/Board/ColdStorageDeploymentRules.h"
#include <algorithm>
#include <cmath>
#include <bit>

namespace ColdStorageSearch {
namespace {
constexpr size_t kMenuLimit=6; // 每轮最多保留六个菜单项，正式搜索只完整比较其中一个
constexpr float kTimedEarliest=6; // 通用分批案的首次检查时刻，游戏秒；不改变对手择时
constexpr float kFollowupExpiry=30; // 未付款构成最多保留的预测游戏秒

/** 原案只作构成种子，整批按当前合法 option 重新映射，不补缺兵或借用旧价格。 */
std::vector<Action> MapProposal(const Snapshot& state,const Proposal& proposal) {
    std::vector<Action> result;
    for(const auto& member:proposal) {
        const auto option=std::find_if(state.options.begin(),state.options.end(),[&](const Option& candidate) {
            return candidate.type==member.type && candidate.row==member.row && candidate.device==member.device
                && candidate.setting==member.setting;
        });
        if(option==state.options.end()) return {};
        result.push_back({static_cast<int>(option-state.options.begin()),member.delay});
    }
    return result;
}
}

PhasedPlanningMenu BuildPhasedPlanningMenu(const Snapshot& state,const Result& incumbent) {
    PhasedPlanningMenu menu;
    if(state.capacity<1 || state.budget<=0) return menu;
    // 护卫前置案保留真正的支援分界，不能按总人数的三分之一把后排混进先遣队。
    const auto covers=BuildPressureCoverFormations(state,state.capacity,state.budget);
    for(size_t n=0;n<covers.size();++n) {
        const auto& cover=covers[(n+state.searchRound/2)%covers.size()];
        PhasedPlan staged;float tailAt=300;
        for(const auto& action:cover) {
            if(!CanForecastFollowup(state.options[action.option])) {staged={};break;}
            if(state.options[action.option].unit.pressureGuard) staged.first.push_back(action);
            else {staged.followup.actions.push_back(action);tailAt=std::min(tailAt,action.delay);}
        }
        if(staged.first.empty() || staged.followup.actions.empty()) continue;
        for(auto& action:staged.followup.actions) action.delay-=tailAt;
        staged.followup.earliestSeconds=tailAt;
        staged.followup.expirySeconds=std::min(45.0f,tailAt+15.0f);
        menu.plans.push_back(std::move(staged));
        if(menu.plans.size()>=2) break; // 保留通用分批/观察候选的菜单槽。
    }
    std::vector<std::vector<Action>> seeds;
    if(incumbent.actions.size()>1) seeds.push_back(incumbent.actions);
    for(const auto& proposal:incumbent.assaultProposals) seeds.push_back(MapProposal(state,proposal));
    for(const auto& proposal:incumbent.proposals) seeds.push_back(MapProposal(state,proposal));
    for(const auto& proposal:state.proposals) seeds.push_back(MapProposal(state,proposal));
    // 没有盈利原案时仍提供小而完整的能力构成，以真实比较决定等待或采购。
    std::vector<int> supported;
    for(size_t i=0;i<state.options.size();++i) if(CanForecastFollowup(state.options[i])) supported.push_back(static_cast<int>(i));
    if(!supported.empty()) {
        const size_t start=state.searchRound%supported.size();
        for(size_t n=0;n<std::min<size_t>(3,supported.size());++n) {
            const int first=supported[(start+n)%supported.size()];
            const int tail=supported[(start+n+1)%supported.size()];
            seeds.push_back({{first,0},{tail,0}});
        }
    }
    const bool hasCobs=std::any_of(state.counters.begin(),state.counters.end(),[](const Counter& c) { return !c.consumesPlant; })
        || std::any_of(state.construction.begin(),state.construction.end(),[](const Construction& c) {return c.cob.damage>0;});
    for(const auto& seed:seeds) {
        if(seed.size()<2 || seed.size()>static_cast<size_t>(state.capacity)) continue;
        const size_t split=std::max<size_t>(1,seed.size()/3);
        std::int64_t bill=0; bool legal=true;
        for(size_t n=0;n<seed.size();++n) {
            const auto& action=seed[n];
            if(action.option<0 || action.option>=static_cast<int>(state.options.size()) || (std::bit_cast<std::uint32_t>(action.delay)&0x7f800000u)==0x7f800000u
                || action.delay<0 || action.delay>30 || state.options[action.option].device>=0
                || state.options[action.option].cost<=0 || !CanForecastFollowup(state.options[action.option])) {legal=false;break;}
            bill+=state.options[action.option].cost;
        }
        if(!legal || bill>state.budget) continue;
        PhasedPlan plan;
        plan.first.assign(seed.begin(),seed.begin()+split);
        plan.followup.actions.assign(seed.begin()+split,seed.end());
        const float firstOrigin=plan.first.front().delay;
        const float tailOrigin=std::min_element(plan.followup.actions.begin(),plan.followup.actions.end(),[](const Action& a,const Action& b) {
            return a.delay<b.delay;
        })->delay;
        for(auto& action:plan.first) action.delay=std::max(0.0f,action.delay-firstOrigin);
        for(auto& action:plan.followup.actions) action.delay-=tailOrigin;
        plan.followup.earliestSeconds=kTimedEarliest; plan.followup.expirySeconds=kFollowupExpiry;
        menu.plans.push_back(plan);
        if(menu.plans.size()>=kMenuLimit) break;
        if(hasCobs) {
            plan.followup.earliestSeconds=0; plan.followup.requireObservedCobLaunch=true;
            plan.followup.requireShotsClear=true; plan.followup.maxReadyCobs=0;
            menu.plans.push_back(std::move(plan));
            if(menu.plans.size()>=kMenuLimit) break;
        }
    }
    return menu;
}
}
