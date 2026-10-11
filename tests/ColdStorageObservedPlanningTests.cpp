#include "Game/AI/ColdStorageObservedPlanning.h"
#include "Game/Board/ColdStorageDeploymentRules.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace ColdStorageSearch;

/** 独立检查公开习惯的组合评分；这些合成世界不代表学习成功率或真人对局强度。 */
void CheckObservedPlan(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED observed planning: "<<message<<'\n';std::exit(1);}
}

bool SameObservedActions(const std::vector<Action>& a,const std::vector<Action>& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const Action& x,const Action& y) {
        return x.option==y.option && x.delay==y.delay;
    });
}

/** 同一策略不只包含成员；触发门槛和付款窗口也必须跨世界保持一致。 */
bool SameObservedPolicy(const FollowupPolicy& a,const FollowupPolicy& b) {
    return SameObservedActions(a.actions,b.actions) && a.earliestSeconds==b.earliestSeconds
        && a.expirySeconds==b.expirySeconds && a.requireObservedCobLaunch==b.requireObservedCobLaunch
        && a.requireShotsClear==b.requireShotsClear && a.maxReadyCobs==b.maxReadyCobs;
}

/** 缺少匹配的付费反制时，该Profile不产生任何等待优惠，但仍可检查两个完整世界的账本。 */
ColdStorageResponse::Profile InactiveProfile() {
    ColdStorageResponse::Profile p;
    p.windowSeconds=8; p.weight=.5f; p.encounters=8; p.nonResponses=8;
    p.context.readyPaidAreaMask=1; p.context.coveredCapitalBands[0]=3;
    p.context.learnablePaidAreaMask=1;p.context.uncoveredCoveredCapitalBands[0]=3;
    p.context.exposedCapitalBand=3; p.context.nearestThreatBand=3;
    p.context.uncoveredCapitalBand=3;
    return p;
}

/** 普通单位、静止既有生产和玩家资产给被动世界非零基线，不借用复杂能力或历史分数。 */
Snapshot ObservedArena(bool existingProduction=false) {
    Snapshot s; s.rows=1; s.searchVersion=2; s.capacity=6; s.budget=200;
    s.houseX=-10000; s.capitalRiskAllowance=400; s.sunIceValue=.2f; s.opponentWeight=.25f;
    s.playerSun=100; s.playerIce=10;
    Option unit; unit.type=74001; unit.cost=10; unit.unit.body.x=900;
    unit.unit.body.health=100; unit.unit.maximumBody=100; unit.unit.body.purchaseCost=10;
    unit.unit.playerRefund=5; unit.unit.biteDps=0; unit.unit.responseNominalCapital=10;
    auto second=unit; second.type=74002; second.cost=20; second.unit.body.purchaseCost=20;
    second.unit.responseNominalCapital=20;
    s.options={unit,second};
    Plant plant; plant.id=74003; plant.x=300; plant.health=100; plant.assetValue=30;
    plant.sunPerSecond=.5f; s.plants={plant};
    if(existingProduction) {
        auto paid=unit.unit; paid.id=74004; paid.body.economic=true;
        paid.productionRemaining=.5f; paid.nextYield=20; paid.productionStopHealth=0;
        s.current={paid};
    }
    s.observedResponse.profiles={InactiveProfile()}; s.observedResponse.revision=7;
    return s;
}

PhasedPlan ObservedProgram() {
    PhasedPlan p; p.first={{0,0}}; p.followup.actions={{1,1}};
    p.followup.earliestSeconds=2; p.followup.expirySeconds=8;
    return p;
}

/** 比较全部评分来源与付款轨迹；诊断代数不属于世界的物理结果。 */
void SameObservedOutcome(const Result& a,const Result& b,const char* message) {
    CheckObservedPlan(a.features==b.features && a.baselineFeatures==b.baselineFeatures
        && std::abs(a.score-b.score)<.001f && a.effectiveWeights==b.effectiveWeights
        && a.stateInputs==b.stateInputs && a.opponentAssets==b.opponentAssets
        && a.baselineOpponentAssets==b.baselineOpponentAssets && a.opponentScore==b.opponentScore
        && a.rawProduction==b.rawProduction && a.productionInputs==b.productionInputs
        && a.construction.breachSeconds==b.construction.breachSeconds
        && a.construction.productionAfterWindow==b.construction.productionAfterWindow
        && a.construction.sunSpent==b.construction.sunSpent && a.construction.iceSpent==b.construction.iceSpent
        && a.construction.abilityIceSpent==b.construction.abilityIceSpent
        && a.construction.paidCounterCasts==b.construction.paidCounterCasts
        && a.construction.followup.status==b.construction.followup.status
        && a.construction.followup.purchasedAt==b.construction.followup.purchasedAt
        && a.construction.followup.spent==b.construction.followup.spent
        && a.construction.followup.walletBefore==b.construction.followup.walletBefore
        && a.construction.followup.walletAfter==b.construction.followup.walletAfter
        && SameObservedActions(a.actions,b.actions)
        && SameObservedPolicy(a.followupPolicy,b.followupPolicy)
        && SameObservedActions(a.construction.followup.realizedActions,b.construction.followup.realizedActions),message);
}

/** 每个动作只相对自己所在世界的被动结果计增量，权重来源在整个对照中冻结。 */
void CheckWorldBaseline(const PhasedPlanComparison& arms,const Result& passive,const Weights& frozen,
    const Result& timedWait,const Result& timedDeferred) {
    float score=0; for(size_t i=0;i<frozen.size();++i) score+=passive.features[i]*frozen[i];
    CheckObservedPlan(passive.effectiveWeights==frozen && passive.opponentScore==0 && std::abs(passive.score-score)<.001f
        && passive.baselineFeatures==passive.features && passive.baselineOpponentAssets==passive.opponentAssets,
        "passive asset holdings are their own baseline and never become a duplicate negative opponent score");
    for(const Result* arm:{&arms.full,&arms.prefix,&arms.wait,&arms.deferred,&arms.incumbent,&timedWait,&timedDeferred}) {
        CheckObservedPlan(arm->baselineFeatures==passive.features && arm->baselineOpponentAssets==passive.opponentAssets
            && arm->effectiveWeights==frozen,
            "each completed arm uses its own world's passive features/assets and the one frozen weight vector");
    }
}
}

/** 先锁定无习惯优惠时的同世界账本和公平对照；动态延迟因果另由公开响应推进专项覆盖。 */
void RunColdStorageObservedPlanningTests() {
    using namespace ColdStorageSearch;
    const Weights weights{1,.2f,200,.5f,.25f,-1,-1,.1f};
    const auto profile=InactiveProfile();
    const auto program=ObservedProgram();
    {
        auto s=ObservedArena(true); s.netEconomy=true;
        StateModel model; model.coefficients[0][0]=.1f; s.stateModel=&model;
        const auto result=CompareObservedPlan(s,weights,program,program.first,profile,true);
        CheckObservedPlan(result.complete && result.robust.complete && result.behavior.complete
            && result.diagnostics.evaluated==1 && result.diagnostics.plannedBill==30,
            "a complete observed comparison accounts for the whole first-plus-tail bill");
        CheckObservedPlan(result.robustPassive.features[4]>0 && result.robustPassive.opponentAssets>0,
            "the test has real existing income and opponent assets rather than a vacuous all-zero baseline");
        const auto frozen=result.robust.full.effectiveWeights;
        CheckWorldBaseline(result.robust,result.robustPassive,frozen,result.robustTimedWait,result.robustTimedDeferred);
        CheckWorldBaseline(result.behavior,result.behaviorPassive,frozen,result.behaviorTimedWait,result.behaviorTimedDeferred);
        SameObservedOutcome(result.robust.full,result.behavior.full,
            "an unsupported response profile leaves the full world's resources, features and scores unchanged");
        SameObservedOutcome(result.robust.prefix,result.behavior.prefix,
            "the prefix also has independent equal robust and inactive-profile worlds");
        SameObservedOutcome(result.robust.wait,result.behavior.wait,
            "same-tail waiting cannot acquire a different wallet or baseline in an inactive-profile world");
        SameObservedOutcome(result.robust.deferred,result.behavior.deferred,
            "whole-deferred waiting retains the same physical plan across response worlds");
        SameObservedOutcome(result.robust.wait,result.robustTimedWait,
            "without event gates the conditional and timed same-tail controls are identical");
        SameObservedOutcome(result.behavior.deferred,result.behaviorTimedDeferred,
            "without event gates the conditional and timed whole-deferred controls are identical");
        CheckObservedPlan(result.robust.full.features[5]==30 && result.robust.prefix.features[5]==10
            && result.robust.wait.features[5]==20 && result.robust.deferred.features[5]==30
            && result.robust.full.actions.size()==1 && result.robust.full.construction.followup.spent==20
            && result.robust.full.construction.followup.realizedActions.front().delay==3,
            "future bill and actual future payment time stay separate from the real current shopping cart");
        auto robustState=s; robustState.observedResponse.profiles.clear();
        const auto ordinary=EvaluatePhasedCounterfactual(robustState,weights,program.first,program.followup,true,9);
        SameObservedOutcome(result.robust.full,ordinary,
            "the original robust result equals an independent ordinary counterfactual and is not replaced by behavioral features");
        CheckObservedPlan(s.budget==200 && s.playerSun==100 && s.playerIce==10
            && s.current.front().body.health==100 && s.current.front().productionRemaining==.5f
            && s.observedResponse.revision==7 && s.observedResponse.profiles.front().weight==.5f,
            "both forecast families leave sampled resources, production timers and evidence unchanged");
    }
    {
        auto s=ObservedArena();
        const auto before=CompareObservedPlan(s,weights,program,{},profile,true);
        s.playerSun+=500;
        const auto after=CompareObservedPlan(s,weights,program,{},profile,true);
        CheckObservedPlan(before.complete && after.complete
            && std::abs(after.robustPassive.opponentAssets-before.robustPassive.opponentAssets-100)<.01f
            && std::abs(after.behaviorPassive.opponentAssets-before.behaviorPassive.opponentAssets-100)<.01f
            && std::abs(after.robust.full.score-before.robust.full.score)<.01f
            && std::abs(after.behavior.full.score-before.behavior.full.score)<.01f,
            "adding unspent passive player capital shifts each matching baseline, not the action's incremental benefit");
    }
    {
        auto s=ObservedArena(); s.capitalRiskAllowance=100;
        auto live=s.options[0].unit; live.id=74101; live.body.health=1; live.body.purchaseCost=8;
        live.responseNominalCapital=1000;
        auto queued=live; queued.id=0; queued.body.health=100; queued.body.spawnAt=50; queued.body.purchaseCost=12;
        auto free=live; free.id=74103; free.body.purchaseCost=0;
        auto dead=live; dead.id=74104; dead.body.health=0; dead.body.purchaseCost=100;
        s.current={live,queued,free,dead}; s.committed.push_back({1,{true,false,false,false,false,false}});
        CheckObservedPlan(ObservedPurchaseAllowance(s)==30,
            "all live and already paid queued original prices occupy risk once; damage, nominal price, free summons and dead units do not distort it");
        s.deploymentCapital=240;
        CheckObservedPlan(ObservedPurchaseAllowance(s)==10,
            "authoritative deployment capital counts omitted live paid units such as headless bodies");
        s.deploymentCapital=210;
        CheckObservedPlan(ObservedPurchaseAllowance(s)==30,
            "an inconsistent lower authority snapshot cannot discard known outstanding paid commitments");
        s.deploymentCapital=-1;
        auto expensive=program; expensive.followup.actions={{0,0},{0,1},{0,2}};
        expensive.followup.requireObservedCobLaunch=true;
        const auto rejected=CompareObservedPlan(s,weights,expensive,{},profile,true);
        CheckObservedPlan(!rejected.complete && rejected.diagnostics.allowanceRejected==1
            && rejected.diagnostics.plannedBill==40 && rejected.diagnostics.riskCap==30
            && rejected.diagnostics.evaluated==0,
            "the risk ceiling includes every conditional tail member even when its trigger might never occur");
        s.supplyInterval=1; s.supplyRemaining=0; s.supplyIce=1000;
        CheckObservedPlan(ObservedPurchaseAllowance(s)==30
            && CompareObservedPlan(s,weights,expensive,{},profile,true).diagnostics.allowanceRejected==1,
            "future supply cannot prefinance the current bounded-risk purchase ceiling");
        s.recoveryReserve=200;
        CheckObservedPlan(ObservedPurchaseAllowance(s)==0,
            "cash needed for recovery is unavailable to the new behavioral purchase allowance");
    }
    {
        auto s=ObservedArena();
        auto unused=program; unused.followup.actions={{999,0}}; unused.followup.expirySeconds=-1;
        const auto ordinary=CompareObservedPlan(s,weights,unused,{},profile,false);
        CheckObservedPlan(ordinary.complete && ordinary.diagnostics.plannedBill==10
            && ordinary.robust.full.features[5]==10 && ordinary.behavior.full.features[5]==10
            && ordinary.robust.full.construction.followup.status==FollowupForecast::Status::Disabled
            && ordinary.robust.full.followupPolicy.actions.empty(),
            "a disabled tail contributes no price, projected unit or hidden policy even if its unused description is invalid");
    }
    {
        auto s=ObservedArena(); s.houseX=160; s.plants.clear(); s.options[1].unit.body.speed=30;
        auto p=program; p.followup.actions[0].delay=0;
        const Weights tactical{0,0,500,0,0,-1,0,0};
        const auto result=CompareObservedPlan(s,tactical,p,{},profile,true);
        CheckObservedPlan(result.complete && result.robust.full.features[2]>0 && result.robust.wait.features[2]>0
            && result.robust.full.construction.breachSeconds==result.robust.wait.construction.breachSeconds
            && result.robust.wait.score>result.robust.full.score && result.behavior.wait.score>result.behavior.full.score,
            "an already affordable identical tail wins at the same time without paying an unnecessary first-stage bait cost");
        p.followup.requireObservedCobLaunch=true; p.followup.requireShotsClear=true; p.followup.maxReadyCobs=0;
        const auto gated=CompareObservedPlan(s,tactical,p,{},profile,true);
        CheckObservedPlan(gated.complete && gated.robust.full.construction.followup.status==FollowupForecast::Status::Expired
            && gated.behavior.full.construction.followup.status==FollowupForecast::Status::Expired
            && gated.robust.wait.construction.followup.status==FollowupForecast::Status::Expired
            && gated.behavior.wait.construction.followup.status==FollowupForecast::Status::Expired
            && SameObservedPolicy(gated.robust.wait.followupPolicy,p.followup)
            && SameObservedPolicy(gated.robust.wait.followupPolicy,gated.behavior.wait.followupPolicy)
            && SameObservedPolicy(gated.robust.deferred.followupPolicy,gated.behavior.deferred.followupPolicy),
            "conditional wait/deferred controls retain the original public gates in both complete worlds");
        auto timed=p.followup; timed.requireObservedCobLaunch=false; timed.requireShotsClear=false; timed.maxReadyCobs=-1;
        auto timedDeferred=timed; timedDeferred.actions.insert(timedDeferred.actions.begin(),p.first.begin(),p.first.end());
        CheckObservedPlan(SameObservedPolicy(gated.robustTimedWait.followupPolicy,timed)
            && SameObservedPolicy(gated.robustTimedDeferred.followupPolicy,timedDeferred)
            && gated.robustTimedWait.construction.followup.status==FollowupForecast::Status::Purchased
            && gated.behaviorTimedWait.construction.followup.status==FollowupForecast::Status::Purchased,
            "timed controls independently remove only the event gates and can buy without an invented cannon launch");
        SameObservedOutcome(gated.robustTimedWait,gated.behaviorTimedWait,
            "the same timed tail policy is evaluated in both worlds rather than mixing distinct waiting policies");
        SameObservedOutcome(gated.robustTimedDeferred,gated.behaviorTimedDeferred,
            "the same timed whole-deferred policy is evaluated in both worlds");
    }
    {
        auto s=ObservedArena(); s.houseX=400; s.traceCombat=true; s.opponentWeight=0;
        s.options[1].unit.body.speed=30;
        auto old=s.options[0].unit; old.id=74201; old.body.x=450; s.current={old};
        Plant cob; cob.id=74202; cob.x=200; cob.health=1000; s.plants={cob};
        Counter c; c.plantID=cob.id; c.consumesPlant=false; c.recharge=1000;
        c.blast.ready=2; c.windup=4.5f; c.flightSeconds=3.5f;
        c.blast.x=700; c.blast.damage=1000; c.blast.reach.fill(-1); c.blast.reach[0]=300;
        s.counters={c};
        auto p=program; p.followup.actions[0].delay=0;
        p.followup.requireObservedCobLaunch=true; p.followup.requireShotsClear=true; p.followup.maxReadyCobs=0;
        const Weights tactical{0,0,500,0,0,-1,0,0};
        const auto result=CompareObservedPlan(s,tactical,p,{},profile,true);
        CheckObservedPlan(result.complete && result.robust.wait.followupWorldsEvaluated>=3
            && result.robust.wait.followupWorldsPurchased==result.robust.wait.followupWorldsEvaluated,
            "an already present urgent target makes each complete cannon-response world pay the conditional tail legally");
        const auto& wait=result.robust.wait;
        CheckObservedPlan(wait.construction.observedCobLaunches==1
            && std::any_of(wait.construction.combatTrace.begin(),wait.construction.combatTrace.end(),[](const auto& event) {
                return event.event=="counter_launch" && event.sourceID==74202 && event.at==3;
            }) && wait.construction.followup.purchasedAt==7 && wait.construction.followup.spent==20
            && wait.features[5]==20 && wait.actions.empty() && wait.features[2]>0,
            "the existing paid unit draws a real launch at three seconds and the tail pays once at seven after impact clears");
        CheckObservedPlan(result.robust.full.features[5]==30 && result.robust.full.features[2]>0
            && wait.construction.breachSeconds==result.robust.full.construction.breachSeconds
            && wait.score>result.robust.full.score && wait.score>result.robustTimedWait.score
            && result.robustTimedWait.construction.followup.purchasedAt==2 && result.robustTimedWait.features[2]==0,
            "free conditional waiting beats both unnecessary new bait and a funded timed tail killed by the already drawn shot");
        SameObservedOutcome(result.robust.wait,result.behavior.wait,
            "the conditional tail remains one identical complete strategy across robust and inactive-profile worlds");
        SameObservedOutcome(result.robust.deferred,result.behavior.deferred,
            "the conditional whole-deferred formation remains one identical complete strategy across worlds");
        SameObservedOutcome(result.robustTimedWait,result.behaviorTimedWait,
            "the timed tail remains its own identical complete strategy across worlds");
        SameObservedOutcome(result.robustTimedDeferred,result.behaviorTimedDeferred,
            "the timed whole-deferred formation remains its own identical complete strategy across worlds");
        const auto frozen=result.robust.full.effectiveWeights;
        CheckWorldBaseline(result.robust,result.robustPassive,frozen,result.robustTimedWait,result.robustTimedDeferred);
        CheckWorldBaseline(result.behavior,result.behaviorPassive,frozen,result.behaviorTimedWait,result.behaviorTimedDeferred);
        s.current.clear();
        const auto noOldTarget=CompareObservedPlan(s,tactical,p,{},profile,true);
        CheckObservedPlan(noOldTarget.complete && noOldTarget.robust.wait.construction.observedCobLaunches==0
            && noOldTarget.robust.wait.construction.followup.status==FollowupForecast::Status::Expired
            && noOldTarget.robust.wait.features[5]==0 && noOldTarget.behavior.wait.features[5]==0,
            "removing the existing target removes the conditional wait's actual launch and paid tail rather than granting them upfront");
    }
    {
        // 跳过实际分阶段轮，再检查完整笛卡尔积；偶数兵种和证据数量不能与每四轮插槽锁相。
        for(int typeCount:{4,6,8}) for(int profileCount:{2,8}) {
            auto s=ObservedArena(); s.rows=2; s.budget=120; s.options.clear();
            s.observedResponse.profiles.assign(profileCount,profile);
            auto option=ObservedArena().options.front();
            for(int type=0;type<typeCount;++type) for(int row=0;row<2;++row) {
                option.type=74300+type; option.row=option.unit.body.row=row; s.options.push_back(option);
            }
            const float allowance=ObservedPurchaseAllowance(s);
            CheckObservedPlan(allowance==30,"coverage fixture can legally buy one, two or three ten-ice members");
            std::vector<int> visits(typeCount*profileCount*2*3);
            int directCount=0;
            for(s.searchRound=0;directCount<typeCount*profileCount*2*3;++s.searchRound) {
                if(s.searchRound%4==3) continue;
                const auto actions=BuildObservedPurchaseCandidate(s,allowance);
                const auto evidence=ObservedPurchaseProfileIndex(s,allowance);
                CheckObservedPlan(!actions.empty() && actions.size()<=3 && evidence<static_cast<size_t>(profileCount),
                    "each direct round names a payable candidate and an existing profile");
                const auto& first=s.options[actions.front().option];
                const int type=first.type-74300;
                CheckObservedPlan(type>=0 && type<typeCount && first.row>=0 && first.row<2
                    && std::all_of(actions.begin(),actions.end(),[&](const Action& action) {
                        return s.options[action.option].type==first.type && action.delay==0;
                    }),"route/size rotation preserves one legal type and immediate current payment");
                ++visits[((evidence*typeCount+type)*2+first.row)*3+actions.size()-1];
                ++directCount;
            }
            CheckObservedPlan(std::all_of(visits.begin(),visits.end(),[](int count) {return count==1;}),
                "every 4/6/8-type and 2/8-profile pair covers both routes and every size exactly once across non-phase rounds");
        }
    }
    {
        auto s=ObservedArena(); s.budget=96; s.options.clear(); s.observedResponse.profiles.assign(8,profile);
        auto option=ObservedArena().options.front(); option.cost=24;
        option.unit.body.purchaseCost=24; option.unit.responseNominalCapital=24;
        for(int type=0;type<8;++type) {
            option.type=74400+type; option.unit.body.economic=type==7; s.options.push_back(option);
        }
        const float allowance=ObservedPurchaseAllowance(s);
        CheckObservedPlan(allowance==24,"a ninety-six-ice wallet exposes exactly twenty-four ice of current purchase allowance");
        std::vector<bool> workerProfiles(8);
        int directCount=0;
        for(s.searchRound=0;directCount<64;++s.searchRound) {
            if(s.searchRound%4==3) continue;
            const auto actions=BuildObservedPurchaseCandidate(s,allowance);
            const auto evidence=ObservedPurchaseProfileIndex(s,allowance);
            CheckObservedPlan(actions.size()==1 && s.options[actions.front().option].cost==24 && evidence<8,
                "the capped direct menu always pays only one twenty-four-ice member");
            if(s.options[actions.front().option].unit.body.economic) workerProfiles[evidence]=true;
            ++directCount;
        }
        CheckObservedPlan(std::all_of(workerProfiles.begin(),workerProfiles.end(),[](bool visited) {return visited;}),
            "a twenty-four-ice worker at the fourth-round residue is tested with every profile instead of disappearing behind phased rounds");
    }
    {
        auto s=ObservedArena();
        for(int invalid=0;invalid<6;++invalid) {
            PhasedPlan bad; bad.first={{0,0}};
            if(invalid==0) bad.first[0].option=-1;
            if(invalid==1) bad.first[0].option=static_cast<int>(s.options.size());
            if(invalid==2) bad.first.assign(21,{0,0});
            if(invalid==3) bad.first[0].delay=std::numeric_limits<float>::infinity();
            auto snapshot=s;
            if(invalid==4) snapshot.options[0].row=snapshot.rows;
            if(invalid==5) snapshot.options[0].unit.body.row=1;
            const auto rejected=CompareObservedPlan(snapshot,weights,bad,{},profile,false);
            CheckObservedPlan(!rejected.complete && rejected.diagnostics.evaluated==0 && !rejected.diagnostics.selected
                && rejected.robust.full.actions.empty() && rejected.behavior.full.actions.empty()
                && snapshot.budget==s.budget,
                "unknown indices, whole-cart overspend, nonfinite timing and invalid current routes fail before any world can execute them");
        }
        s.capacity=64; s.deploymentCapital=s.budget;
        s.deploymentOccupied=ColdStorageDeploymentRules::BaseCapacity-1;
        s.searchRound=5; // 本轮规模轮转本可请求三名，实际只剩一席。
        auto direct=BuildObservedPurchaseCandidate(s,ObservedPurchaseAllowance(s));
        CheckObservedPlan(direct.size()==1,
            "new bounded candidates use authoritative current deployment capacity rather than stale nominal capacity");
        const auto noRoom=CompareObservedPlan(s,weights,{{{0,0},{0,0}},{}},{},profile,false);
        CheckObservedPlan(!noRoom.complete && noRoom.diagnostics.evaluated==0,
            "preflight rejects an entire otherwise affordable current cart that exceeds true remaining capacity");
        s.deploymentOccupied=ColdStorageDeploymentRules::BaseCapacity;
        CheckObservedPlan(BuildObservedPurchaseCandidate(s,ObservedPurchaseAllowance(s)).empty(),
            "zero authoritative slots cannot manufacture a one-member observed candidate");
        s=ObservedArena(); s.options[0].row=s.rows;
        direct=BuildObservedPurchaseCandidate(s,ObservedPurchaseAllowance(s));
        CheckObservedPlan(!direct.empty() && direct.front().option==1,
            "an invalid route cannot conceal another legal bounded option during admission");
    }
    {
        auto s=ObservedArena(); s.observedResponse={}; s.timeLimitedSearch=false;
        const auto original=Search(s,weights,741);
        s.observedResponse.revision=999; s.observedResponse.currentOpportunity=true;
        s.observedResponse.currentContext=profile.context; s.observedResponse.currentExposureSeconds=100;
        s.observedResponse.sourceExposureSeconds.fill(100);
        const auto stillNoEvidence=Search(s,weights,741);
        SameObservedOutcome(original,stillNoEvidence,
            "current exposure metadata alone cannot change the original deterministic search without learned profiles");
        CheckObservedPlan(!original.observedDecision.selected && !stillNoEvidence.observedDecision.selected
            && original.observedDecision.evaluated==0 && stillNoEvidence.observedDecision.evaluated==0,
            "an empty evidence set never invokes or adopts the new behavioral selection");
    }
    {
        auto s=ObservedArena(); s.timeLimitedSearch=true; s.searchDeadline=std::chrono::steady_clock::now();
        const auto stopped=CompareObservedPlan(s,weights,program,{},profile,true);
        CheckObservedPlan(!stopped.complete && !stopped.robust.complete && !stopped.behavior.complete
            && stopped.diagnostics.incomplete==1 && stopped.diagnostics.evaluated==0 && !stopped.diagnostics.selected,
            "an expired shared deadline cannot produce an adoptable partial two-world comparison");
        s.timeLimitedSearch=false;
        for(int invalid=0;invalid<4;++invalid) {
            auto bad=profile;
            if(invalid==0) bad.weight=0;
            if(invalid==1) bad.weight=.66f;
            if(invalid==2) bad.windowSeconds=25;
            if(invalid==3) bad.windowSeconds=std::numeric_limits<float>::quiet_NaN();
            const auto rejected=CompareObservedPlan(s,weights,program,{},bad,true);
            CheckObservedPlan(!rejected.complete && rejected.diagnostics.evaluated==0 && !rejected.diagnostics.selected,
                "invalid confidence or observation window cannot become a scored behavioral comparison");
        }
    }
    std::cout<<"Observed planning preserves frozen weights, per-world baselines, robust resources, full-bill/outstanding limits, fair waits and completion gates\n";
}
