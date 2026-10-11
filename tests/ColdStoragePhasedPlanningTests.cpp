#include "Game/AI/ColdStoragePhasedPlanning.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void CheckPhasedPlanning(bool value,const char* message) {
    if(!value) {std::cerr<<"FAILED reconstructed phased planning: "<<message<<'\n';std::exit(1);}
}
/** 首批普通工人已兑现的产冰可解锁当前本就合法的后队付款；没有新兵解锁或额外补给。 */
ColdStorageSearch::Snapshot ProgramArena() {
    using namespace ColdStorageSearch;
    Snapshot s; s.rows=1; s.searchVersion=2; s.capacity=4; s.budget=10;
    Option worker; worker.type=72001; worker.cost=10; worker.unit.body.x=900; worker.unit.body.health=100;
    worker.unit.maximumBody=100; worker.unit.body.purchaseCost=10; worker.unit.body.economic=true;
    worker.unit.productionRemaining=.5f; worker.unit.nextYield=20; worker.unit.productionStopHealth=0; worker.unit.biteDps=0;
    auto troop=worker; troop.type=72002; troop.cost=20; troop.unit.body.purchaseCost=20;
    troop.unit.body.economic=false; troop.unit.body.speed=30;
    s.options={worker,troop}; return s;
}
bool SameActions(const std::vector<ColdStorageSearch::Action>& a,const std::vector<ColdStorageSearch::Action>& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y) {
        return x.option==y.option && x.delay==y.delay;
    });
}
}

/** 重建方案通过数值因果和公平等待验证；不把合成突破当作真实对局强度证据。 */
void RunColdStoragePhasedPlanningTests() {
    using namespace ColdStorageSearch;
    const Weights tactical{0,0,500,0,0,-1,0,0};
    FollowupPolicy policy; policy.actions={{1,0}}; policy.earliestSeconds=2; policy.expirySeconds=8;
    const PhasedPlan plan{{{0,0}},policy};
    {
        const auto s=ProgramArena();
        const auto comparison=ComparePhasedPlan(s,tactical,plan,plan.first);
        CheckPhasedPlanning(comparison.complete && comparison.full.features[2]>0
            && comparison.prefix.features[2]==0 && comparison.wait.features[2]==0 && comparison.deferred.features[2]==0,
            "first-stage realized production causally enables a tail breach that prefix, same-tail wait and whole-deferred wait cannot buy");
        CheckPhasedPlanning(comparison.full.features[5]==30 && comparison.full.actions.size()==1
            && comparison.full.followupPolicy.actions.size()==1 && comparison.full.construction.followup.spent==20,
            "full result reports total forecast expense separately from the only current cart");
        CheckPhasedPlanning(comparison.full.baselineFeatures[5]==0 && comparison.wait.baselineFeatures[5]==0
            && comparison.full.baselineFeatures==comparison.prefix.baselineFeatures
            && comparison.full.baselineFeatures==comparison.wait.baselineFeatures
            && comparison.full.baselineFeatures==comparison.deferred.baselineFeatures
            && comparison.full.effectiveWeights==comparison.deferred.effectiveWeights,
            "all complete arms use the same passive capital baseline and frozen weights");
        CheckPhasedPlanning(comparison.prefix.features==comparison.incumbent.features
            && comparison.prefix.score==comparison.incumbent.score,
            "exact same-window prefix may be reused as the incumbent comparison");
        CheckPhasedPlanning(s.budget==10 && s.options[0].unit.productionRemaining==.5f,
            "same-world funding never changes actual wallet, income timer or unit stats");
    }
    {
        auto s=ProgramArena(); s.budget=30;
        const auto comparison=ComparePhasedPlan(s,tactical,plan);
        CheckPhasedPlanning(comparison.complete && comparison.full.features[2]>0 && comparison.wait.features[2]>0
            && comparison.wait.score>comparison.full.score && comparison.wait.features[5]==20
            && comparison.full.features[5]==30,
            "when the tail is already affordable, fair waiting exposes the unnecessary first-stage cost without a bait bonus");
        auto finite=s; finite.houseX=-10000;
        const Weights assets{0,0,0,1,0,-1,0,0};
        const auto same=ComparePhasedPlan(finite,assets,plan);
        CheckPhasedPlanning(same.full.score==same.wait.score && same.full.score==same.deferred.score,
            "holding identical surviving assets does not manufacture incremental staged value");
    }
    {
        auto s=ProgramArena(); s.budget=30; s.houseX=-10000;
        const auto full=EvaluatePhasedCounterfactual(s,tactical,plan.first,policy,true,80);
        const auto prefix=EvaluatePhasedCounterfactual(s,tactical,plan.first,policy,false,80);
        auto impossible=policy; impossible.actions={{999,0}}; impossible.expirySeconds=std::numeric_limits<float>::quiet_NaN();
        const auto disabled=EvaluatePhasedCounterfactual(s,tactical,plan.first,impossible,false,80);
        CheckPhasedPlanning(prefix.features==disabled.features && prefix.score==disabled.score
            && prefix.construction.followup.status==FollowupForecast::Status::Disabled
            && prefix.followupPolicy.actions.empty() && prefix.followupWorldsEvaluated==0
            && prefix.features[5]==10 && full.features[5]==30,
            "disabling a tail removes all its assets, bills, policy and world counts even if the unused description is invalid");
        const auto original=plan.first;
        const auto invalid=EvaluatePhasedCounterfactual(s,tactical,{{0,0},{1,0},{1,0}},policy,false,80);
        CheckPhasedPlanning(invalid.construction.followup.status==FollowupForecast::Status::Invalid && invalid.actions.size()==3
            && SameActions(original,plan.first),
            "an unaffordable initial cart is rejected whole rather than silently repaired into a different plan");
        for(float envelope:{-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),301.0f})
            CheckPhasedPlanning(EvaluatePhasedCounterfactual(s,tactical,plan.first,policy,true,envelope).construction.followup.status
                ==FollowupForecast::Status::Invalid,"invalid shared response window cannot become a scoreable comparison");
    }
    {
        auto s=ProgramArena(); s.budget=100; s.houseX=-10000;
        Plant cob; cob.id=72003; cob.x=200; cob.health=1000; s.plants={cob};
        Counter c; c.consumesPlant=false; c.source=0; c.plantID=cob.id; c.recharge=1000; c.windup=2; c.flightSeconds=1;
        c.blast.x=900; c.blast.damage=1000; c.blast.reach.fill(-1); c.blast.reach[0]=150; s.counters={c};
        auto event=policy; event.requireObservedCobLaunch=true; event.requireShotsClear=true; event.maxReadyCobs=0;
        const auto result=EvaluatePhasedCandidate(s,tactical,plan.first,event);
        CheckPhasedPlanning(result.followupWorldsEvaluated>=3 && result.followupWorldsPurchased<result.followupWorldsEvaluated
            && result.followupWorldsPurchased>0,
            "every existing complete patience world executes its own conditional transaction; a launch in one cannot fund another");
        const auto comparison=ComparePhasedPlan(s,tactical,{plan.first,event});
        CheckPhasedPlanning(comparison.complete && comparison.wait.followupWorldsEvaluated>=3
            && comparison.deferred.followupWorldsEvaluated>=3,
            "fair waiting carries both the same tail and the whole deferred formation through the same response envelope");
    }
    {
        auto s=ProgramArena(); s.budget=30;
        Result incumbent; incumbent.actions={{0,0},{1,0}};
        const auto menu=BuildPhasedPlanningMenu(s,incumbent);
        CheckPhasedPlanning(!menu.plans.empty() && menu.plans.size()<=6 && menu.plans.front().first.size()==1
            && menu.plans.front().followup.actions.size()==1,"bounded generic menu splits complete legal current-price teams");
        s.budget=9;
        CheckPhasedPlanning(BuildPhasedPlanningMenu(s,incumbent).plans.empty(),
            "production menu never promises a two-stage seed by crediting future income upfront");
        s=ProgramArena(); s.budget=30; s.timeLimitedSearch=true; s.searchDeadline=std::chrono::steady_clock::now();
        const auto incomplete=ComparePhasedPlan(s,tactical,plan);
        CheckPhasedPlanning(!incomplete.complete && incomplete.full.followupWorldsEvaluated==0,
            "an expired shared deadline cannot return an incomplete branch as a winning program");
    }
    std::cout<<"Reconstructed phased planning: causal funding, full expense, fair waits, complete response worlds and deadline boundaries verified\n";
}
