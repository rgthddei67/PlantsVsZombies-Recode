#include "Game/AI/ColdStoragePhasedPlanning.h"
#include "Game/Board/ColdStorageDeploymentRules.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void CheckPhasedForecast(bool value,const char* message) {
    if(!value) {std::cerr<<"FAILED reconstructed phased forecast: "<<message<<'\n';std::exit(1);}
}
/** 静止普通单位排除战斗随机性；只检查未来采购的真实因果与账本。 */
ColdStorageSearch::Snapshot PhasedArena() {
    using namespace ColdStorageSearch;
    Snapshot s; s.rows=1; s.searchVersion=2; s.budget=40; s.capacity=4; s.houseX=-10000;
    Option o; o.type=71001; o.cost=10; o.unit.body.row=0; o.unit.body.x=900;
    o.unit.body.health=100; o.unit.maximumBody=100; o.unit.body.purchaseCost=10;
    o.unit.body.value=10; o.unit.playerRefund=5; o.unit.biteDps=0;
    s.options={o}; return s;
}
ColdStorageSearch::Weights Forecast(const ColdStorageSearch::Snapshot& s,const std::vector<ColdStorageSearch::Action>& first,
    const ColdStorageSearch::FollowupPolicy& policy,ColdStorageSearch::ConstructionStats& stats) {
    return ColdStorageSearch::Evaluate(s,first,&stats,0,0,0,false,false,-1,false,false,false,false,&policy);
}
}

/** 这些是重建后的新数值证据，不继承丢失实现或旧里程碑的测试成绩。 */
void RunColdStoragePhasedForecastTests() {
    using namespace ColdStorageSearch;
    {
        auto s=PhasedArena(); s.budget=9;
        auto worker=s.options[0]; worker.unit.body.economic=true; worker.unit.productionRemaining=.5f;
        worker.unit.nextYield=100; worker.unit.productionStopHealth=0; s.options={worker};
        FollowupPolicy p; p.actions={{0,0}}; p.expirySeconds=3;
        ConstructionStats stats; const auto f=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired && stats.followup.spent==0
            && stats.followup.purchasedAt==-1 && stats.followup.realizedActions.empty()
            && f[3]==0 && f[4]==0 && f[5]==0 && stats.opponentAssets==0,
            "unpaid future worker has zero health, asset, income, spend and death refund");
        CheckPhasedForecast(s.budget==9 && s.options[0].unit.body.health==100,
            "forecast never edits the captured wallet or birth image");
    }
    {
        auto s=PhasedArena(); s.budget=19;
        FollowupPolicy p; p.actions={{0,0},{0,0}}; p.expirySeconds=3;
        ConstructionStats stats; const auto f=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired && f[5]==0 && f[3]==0,
            "one affordable member cannot become a partial future transaction");
        s.supplyInterval=1000; s.supplyRemaining=2; s.supplyIce=1;
        const auto funded=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Purchased && stats.followup.purchasedAt==2
            && stats.followup.walletBefore==20 && stats.followup.walletAfter==0 && stats.followup.spent==20
            && funded[5]==20 && funded[3]==20 && stats.followup.realizedActions.size()==2,
            "the same world waits for actual supply and then pays the full bill once");
    }
    {
        auto s=PhasedArena(); s.budget=10;
        auto producer=s.options[0]; producer.unit.body.economic=true; producer.unit.productionRemaining=.5f;
        producer.unit.nextYield=20; producer.unit.productionStopHealth=0; s.options.push_back(producer);
        FollowupPolicy p; p.actions={{0,0},{0,0}}; p.earliestSeconds=2; p.expirySeconds=3;
        ConstructionStats stats; const auto f=Forecast(s,{{1,0}},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Purchased && stats.followup.purchasedAt==2
            && stats.followup.walletBefore>=20 && stats.followup.spent==20 && f[5]==30 && f[4]>=20,
            "only already realized first-stage production funds the same world's later bill");
        const auto noPrefix=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired && noPrefix[5]==0,
            "removing the producer removes its causal funding rather than granting forecast income upfront");
    }
    {
        auto s=PhasedArena();
        Counter flight; flight.consumesPlant=false; flight.blast.committed=true;
        flight.blast.ready=3; flight.blast.x=900; flight.blast.damage=1000; flight.blast.reach.fill(-1); flight.blast.reach[0]=150;
        s.counters={flight};
        FollowupPolicy p; p.actions={{0,3}}; p.earliestSeconds=2; p.expirySeconds=4;
        ConstructionStats stats; const auto delayed=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.purchasedAt==2 && stats.followup.realizedActions[0].delay==5
            && delayed[3]==10 && delayed[5]==10,
            "future member delay starts at actual payment and an earlier airborne hit cannot damage an unborn unit");
        p.actions[0].delay=0; s.traceCombat=true;
        const auto immediate=Forecast(s,{},p,stats);
        CheckPhasedForecast(immediate[3]==0 && immediate[5]==10 && stats.opponentAssets==5,
            "a paid member that enters before impact can die and refund exactly once");
        CheckPhasedForecast(!stats.combatTrace.empty() && std::any_of(stats.combatTrace.begin(),stats.combatTrace.end(),[](const auto& event) {
            return event.event=="unit_spawn" && event.reason=="followup_purchase" && event.at==2;
        }),"zero-delay future birth is traced in its actual payment step before possible same-step death");
        p.requireShotsClear=true;
        const auto clear=Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.purchasedAt==3.5f && clear[3]==10,
            "an existing independent shot gates payment until it actually resolves");
        p.requireObservedCobLaunch=true;
        Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired,
            "an already airborne snapshot is not misreported as a newly observed launch");
    }
    {
        auto s=PhasedArena();
        Plant cob; cob.id=17; cob.health=100; cob.x=200; s.plants={cob};
        Counter counter; counter.plantID=17; counter.consumesPlant=false; counter.source=0;
        counter.blast.x=900; counter.blast.damage=1000; counter.blast.reach.fill(-1); counter.blast.reach[0]=150;
        counter.windup=2; counter.flightSeconds=1; counter.recharge=1000; s.counters={counter,counter};
        FollowupPolicy p; p.actions={{0,0}}; p.requireObservedCobLaunch=true; p.requireShotsClear=true;
        p.maxReadyCobs=0; p.expirySeconds=8;
        ConstructionStats stats; const auto f=Forecast(s,{{0,0}},p,stats);
        CheckPhasedForecast(stats.observedCobLaunches==1 && stats.followup.purchasedAt==2.5f
            && stats.followup.spent==10 && f[3]==10 && f[5]==20,
            "duplicate aiming points share a source and one actual launch opens a causal post-impact purchase");
        Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired && stats.observedCobLaunches==0,
            "without the first-stage target no launch is invented to release the tail");
    }
    {
        auto s=PhasedArena(); s.capacity=1;
        FollowupPolicy p; p.actions={{0,0}}; p.earliestSeconds=2; p.expirySeconds=4;
        ConstructionStats stats; Forecast(s,{{0,0}},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired,
            "a live first-stage unit still occupies the fixed deployment capacity");
        Counter death; death.blast.committed=true; death.blast.ready=1; death.blast.x=900;
        death.blast.damage=1000; death.blast.reach.fill(-1); death.blast.reach[0]=150; s.counters={death};
        Forecast(s,{{0,0}},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Purchased && stats.followup.purchasedAt==2,
            "only a death that actually happened releases a future deployment slot");
        s.deploymentCapital=100; s.deploymentOccupied=ColdStorageDeploymentRules::BaseCapacity;
        s.current.clear(); s.counters.clear();
        Forecast(s,{},p,stats);
        CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Expired,
            "unprojected real occupied slots cannot disappear in a future purchase");
    }
    {
        auto s=PhasedArena(); FollowupPolicy p; p.actions={{0,0}}; p.expirySeconds=3;
        ConstructionStats stats;
        for(int invalid=0;invalid<4;++invalid) {
            auto state=s; auto policy=p;
            if(invalid==0) policy.actions[0].option=999;
            if(invalid==1) policy.actions[0].delay=std::numeric_limits<float>::quiet_NaN();
            if(invalid==2) policy.expirySeconds=-1;
            if(invalid==3) state.options[0].unit.clock.present=true;
            const auto f=Forecast(state,{},policy,stats);
            CheckPhasedForecast(stats.followup.status==FollowupForecast::Status::Invalid && f[5]==0 && f[3]==0,
                "invalid option, time or unsupported complex future capability cannot create speculative value");
        }
    }
    {
        auto s=PhasedArena(); s.traceCombat=true;
        Unit priest=s.options[0].unit; priest.id=71099; priest.body.health=1000;
        priest.ritual.present=priest.ritual.enabled=priest.ritual.winding=true;
        priest.ritual.armor=100; priest.ritual.stopHealth=0; priest.ritual.remaining=0;
        s.current={priest}; s.riftCells={{{0,0}},{{0,1}}};
        for(auto& summon:s.ritualSummons) summon=s.options[0].unit;
        Rift committed; committed.unit=s.options[0].unit; committed.remaining=1; committed.column=1; s.rifts={committed};
        ConstructionStats baseline, dormant;
        const auto original=Evaluate(s,{{0,0}},&baseline);
        FollowupPolicy p; p.actions={{0,0}}; p.requireObservedCobLaunch=true; p.expirySeconds=3;
        const auto extended=Forecast(s,{{0,0}},p,dormant);
        CheckPhasedForecast(original==extended && baseline.riftSummons>0 && baseline.riftSummons==dormant.riftSummons
            && baseline.combatTrace.size()==dormant.combatTrace.size(),
            "adding an unpaid dormant tail does not change existing rift allocation or combat results");
        for(size_t i=0;i<baseline.combatTrace.size();++i)
            CheckPhasedForecast(baseline.combatTrace[i].sourceID==dormant.combatTrace[i].sourceID
                && baseline.combatTrace[i].event==dormant.combatTrace[i].event,
                "pre-existing, current-cart and committed-rift source IDs stay stable across dormant-tail ablation");
    }
    std::cout<<"Reconstructed phased forecast: atomic same-world payment, dormant zero value, public gates and capacity verified\n";
}
