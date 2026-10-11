#include "Game/AI/ColdStorageSearch.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string_view>

namespace {
using namespace ColdStorageSearch;

void CheckWorldCache(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED raw world cache: "<<message<<'\n';std::exit(1);}
}

/** 静止单位隔离原始积分和资源账；不依赖随机单位能力或墙钟采样。 */
Snapshot CacheArena() {
    Snapshot state;state.rows=1;state.searchVersion=2;state.capacity=4;state.budget=200;
    state.houseX=-10000;state.rowY[0]=150;
    for(int column=0;column<state.columns;++column) state.cellY[column]=150;
    Option first;first.type=78001;first.cost=10;first.unit.body.x=900;first.unit.body.health=100;
    first.unit.maximumBody=100;first.unit.body.purchaseCost=10;first.unit.biteDps=0;
    first.unit.birthMovementKnown=true;first.unit.responseNominalCapital=10;
    first.unit.responseObjectYKnown=true;
    auto second=first;second.type=78002;second.cost=20;second.unit.body.purchaseCost=20;
    second.unit.body.health=second.unit.maximumBody=200;second.unit.responseNominalCapital=20;
    state.options={first,second};return state;
}

/** 与公开响应接线专项同一真实生产/灰烬因果；行为证据仅延后当前合法付费牌。 */
Snapshot CacheResponseArena() {
    auto state=CacheArena();state.budget=144;state.recoveryReserve=48;state.capitalRiskAllowance=100;
    state.netEconomy=true;state.playerSun=state.playerIce=10000;
    auto worker=state.options.front();worker.cost=IceProduction::WorkerCost;
    worker.unit.body.health=worker.unit.maximumBody=IceProduction::WorkerHealth;
    worker.unit.body.purchaseCost=worker.unit.responseNominalCapital=worker.cost;worker.unit.body.economic=true;
    state.options={worker};
    Counter counter;counter.source=0;counter.observedSourceKey=1;counter.sunCost=150;counter.iceCost=20;
    counter.cellRow=0;counter.cellColumn=8;counter.blast.x=840;counter.blast.damage=1800;
    counter.blast.usesObjectX=true;counter.blast.reach.fill(-1);counter.blast.reach[0]=130;
    counter.ladderClearRow=0;counter.ladderClearRadius=1;counter.windup=1;counter.recharge=1;
    state.counters={counter};
    ColdStorageResponse::Profile profile;profile.windowSeconds=24;profile.weight=.65f;
    profile.encounters=profile.nonResponses=8;profile.context.readyPaidAreaMask=1;
    profile.context.learnablePaidAreaMask=1;profile.context.nearestThreatBand=2;profile.context.economicPresent=true;
    state.observedResponse.profiles={profile,profile};state.observedResponse.sourceExposureSeconds.fill(-1);
    state.observedResponse.revision=41;return state;
}

bool SameCacheActions(const std::vector<Action>& a,const std::vector<Action>& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y) {
        return x.option==y.option && x.delay==y.delay;
    });
}

/** 完整资源和公开轨迹都属于缓存值；命中不能只恢复评分特征而丢失实际付款/发射状态。 */
void SameCacheWorld(const RawWorldOutcome& a,const RawWorldOutcome& b) {
    const auto& x=a.construction;const auto& y=b.construction;
    CheckWorldCache(a.complete==b.complete && a.features==b.features && x.breachSeconds==y.breachSeconds
        && x.opponentAssets==y.opponentAssets && x.productionAfterWindow==y.productionAfterWindow
        && x.sunSpent==y.sunSpent && x.iceSpent==y.iceSpent && x.abilityIceSpent==y.abilityIceSpent
        && x.observedDelaySteps==y.observedDelaySteps && x.observedCobLaunches==y.observedCobLaunches
        && x.paidCounterCasts==y.paidCounterCasts && x.nextCombatAttackID==y.nextCombatAttackID
        && x.followup.status==y.followup.status && x.followup.purchasedAt==y.followup.purchasedAt
        && x.followup.spent==y.followup.spent && x.followup.walletBefore==y.followup.walletBefore
        && x.followup.walletAfter==y.followup.walletAfter
        && SameCacheActions(x.followup.realizedActions,y.followup.realizedActions)
        && x.workerTrace.size()==y.workerTrace.size() && x.counterTrace.size()==y.counterTrace.size()
        && x.combatTrace.size()==y.combatTrace.size() && x.combatTraceTruncated==y.combatTraceTruncated,
        "cache hit returns the complete same physical world and payment ledger");
    for(size_t i=0;i<x.combatTrace.size();++i) {
        const auto& p=x.combatTrace[i];const auto& q=y.combatTrace[i];
        CheckWorldCache(std::string_view(p.event)==q.event && std::string_view(p.reason)==q.reason
            && p.at==q.at && p.sourceID==q.sourceID && p.actionIndex==q.actionIndex && p.resolveAt==q.resolveAt
            && p.targets.size()==q.targets.size(),"cached combat events preserve their original ordering and stable identities");
        for(size_t j=0;j<p.targets.size();++j)
            CheckWorldCache(p.targets[j].id==q.targets[j].id && p.targets[j].before==q.targets[j].before
                && p.targets[j].after==q.targets[j].after,"cached combat recipients and actual damage remain unchanged");
    }
}

/** 只比较预测/选择语义，缓存计数和墙钟速度不参与胜负。 */
void SameCacheSearch(const Result& a,const Result& b) {
    CheckWorldCache(a.features==b.features && a.baselineFeatures==b.baselineFeatures && a.score==b.score
        && a.effectiveWeights==b.effectiveWeights && a.stateInputs==b.stateInputs
        && a.opponentAssets==b.opponentAssets && a.baselineOpponentAssets==b.baselineOpponentAssets
        && a.rawProduction==b.rawProduction && a.productionInputs==b.productionInputs
        && a.counterHoldSeconds==b.counterHoldSeconds && a.observedDelayedWorlds==b.observedDelayedWorlds
        && a.followupWorldsEvaluated==b.followupWorldsEvaluated && a.followupWorldsPurchased==b.followupWorldsPurchased
        && a.observedDecision.selected==b.observedDecision.selected && a.observedDecision.evaluated==b.observedDecision.evaluated
        && a.observedDecision.combinedDelta==b.observedDecision.combinedDelta
        && a.observedDecision.robustDelta==b.observedDecision.robustDelta
        && a.observedDecision.behaviorDelta==b.observedDecision.behaviorDelta
        && SameCacheActions(a.actions,b.actions) && SameCacheActions(a.followupPolicy.actions,b.followupPolicy.actions),
        "cached and uncached fixed searches preserve exact scores, response selection, worlds and chosen cart");
    SameCacheWorld({a.features,a.construction,true},{b.features,b.construction,true});
}
}

/** 只证明同源完整积分复用；不把缓存命中或诊断合成世界当作实战收益证据。 */
void RunColdStorageWorldCacheTests() {
    using namespace ColdStorageSearch;
    {
        auto state=CacheArena();state.traceCombat=true;
        RawWorldSession session(state);RawWorldRequest forward;forward.actions={{0,0},{1,0}};
        const auto first=session.Evaluate(forward);SameCacheWorld(first,session.Evaluate(forward));
        auto reverse=forward;std::reverse(reverse.actions.begin(),reverse.actions.end());
        const auto reversed=session.Evaluate(reverse);SameCacheWorld(reversed,session.Evaluate(reverse));
        const auto firstHealth=[](const RawWorldOutcome& world) {
            for(const auto& event:world.construction.combatTrace)
                if(std::string_view(event.event)=="unit_spawn" && event.actionIndex==0 && !event.targets.empty())
                    return event.targets.front().before;
            return -1.0f;
        };
        CheckWorldCache(firstHealth(first)==100 && firstHealth(reversed)==200
            && session.Counts().evaluations==2 && session.Counts().hits==2,
            "simultaneous action order is a different world with a different first spawn, not a sorted multiset key");
        auto plain=state;plain.traceCombat=false;RawWorldSession plainSession(plain);
        CheckWorldCache(plainSession.Evaluate(forward).construction.combatTrace.empty()
            && plainSession.Counts().evaluations==1 && plainSession.Counts().hits==0,
            "a new snapshot never borrows trace-bearing results from another session");
    }
    {
        auto state=CacheArena();state.budget=20;
        Counter shot;shot.blast.committed=true;shot.consumesPlant=false;shot.blast.ready=4;
        shot.blast.x=900;shot.blast.damage=1000;shot.blast.reach.fill(-1);shot.blast.reach[0]=150;state.counters={shot};
        RawWorldSession session(state);RawWorldRequest now;now.actions={{0,0}};now.enableFollowup=true;
        now.followup.actions={{0,0}};now.followup.earliestSeconds=2;now.followup.expirySeconds=3;
        const auto early=session.Evaluate(now);
        auto later=now;later.followup.actions.front().delay=3;const auto late=session.Evaluate(later);
        auto blocked=now;blocked.followup.requireObservedCobLaunch=true;const auto unpaid=session.Evaluate(blocked);
        auto prefix=now;prefix.enableFollowup=false;const auto alone=session.Evaluate(prefix);
        SameCacheWorld(early,session.Evaluate(now));SameCacheWorld(late,session.Evaluate(later));
        SameCacheWorld(unpaid,session.Evaluate(blocked));SameCacheWorld(alone,session.Evaluate(prefix));
        CheckWorldCache(early.features[3]==0 && late.features[3]==10 && early.features[5]==20 && late.features[5]==20
            && late.construction.followup.purchasedAt==2 && late.construction.followup.realizedActions.front().delay==5
            && unpaid.construction.followup.status==FollowupForecast::Status::Expired && unpaid.features[5]==10
            && alone.construction.followup.status==FollowupForecast::Status::Disabled
            && session.Counts().evaluations==4 && session.Counts().hits==4,
            "relative birth delay, trigger gates and disabled tail remain distinct complete same-wallet transactions");
    }
    {
        auto state=CacheResponseArena();state.observedResponse={};
        Plant cob;cob.id=78100;cob.x=300;cob.health=1000;state.plants={cob};
        auto& counter=state.counters.front();counter.plantID=cob.id;counter.consumesPlant=false;
        counter.sunCost=counter.iceCost=counter.observedSourceKey=0;counter.recharge=1000;
        RawWorldSession session(state);RawWorldRequest shortHold;shortHold.actions={{0,0}};
        shortHold.counterHoldSeconds=8;shortHold.storedHoldSeconds=shortHold.rowStrikeHoldSeconds=32;
        auto longHold=shortHold;longHold.storedHoldSeconds=longHold.rowStrikeHoldSeconds=60;
        const auto early=session.Evaluate(shortHold),late=session.Evaluate(longHold);
        SameCacheWorld(early,session.Evaluate(shortHold));SameCacheWorld(late,session.Evaluate(longHold));
        CheckWorldCache(late.features[4]>early.features[4] && session.Counts().evaluations==2 && session.Counts().hits==2,
            "a wider shared response envelope cannot reuse a shorter held-cannon world or its lower realized income");
    }
    {
        auto state=CacheResponseArena();RawWorldSession session(state);
        RawWorldRequest robust;robust.actions={{0,0}};
        auto behavior=robust;behavior.observedProfile=0;
        const auto full=session.Evaluate(robust),observed=session.Evaluate(behavior);
        auto equalProfile=behavior;equalProfile.observedProfile=1;
        SameCacheWorld(observed,session.Evaluate(equalProfile));SameCacheWorld(full,session.Evaluate(robust));
        CheckWorldCache(observed.construction.observedDelaySteps>0 && full.construction.observedDelaySteps==0
            && observed.features[4]>full.features[4] && session.Counts().evaluations==2 && session.Counts().hits==2,
            "behavior and robust resources remain separate while equal profile values may share only the identical behavior world");
        auto aged=session.State();
        CheckWorldCache(session.State().rawWorldBinding.Attached() && !aged.rawWorldBinding.Attached(),
            "an ordinary public Snapshot copy drops the internal cache binding before callers can mutate it");
        aged.observedResponse.sourceExposureSeconds[0]=24;RawWorldSession agedSession(aged);
        const auto expired=agedSession.Evaluate(behavior);
        CheckWorldCache(expired.construction.observedDelaySteps==0 && expired.features[4]==full.features[4]
            && agedSession.Counts().evaluations==1 && agedSession.Counts().hits==0,
            "changed source age at the same revision is a new physical snapshot and cannot renew cached patience");
        auto repriced=session.State();repriced.options.front().cost=40;
        RawWorldSession repricedSession(repriced);
        CheckWorldCache(repricedSession.Evaluate(robust).features[5]==40 && repricedSession.Counts().hits==0,
            "a copied then repriced options table never inherits the prior transaction cache");
        auto occupied=session.State();occupied.counters.clear();
        occupied.current={occupied.options.front().unit};occupied.current.front().body.spawnAt=50;
        RawWorldSession occupiedSession(occupied);RawWorldRequest passive;
        const auto paidQueue=occupiedSession.Evaluate(passive);
        CheckWorldCache(paidQueue.features[4]>0 && paidQueue.features[5]==0 && occupiedSession.Counts().hits==0,
            "changed already-paid queue timing recomputes actual income without charging it as a new purchase");
    }
    {
        auto state=CacheResponseArena();state.counters.clear();state.observedResponse={};
        RawWorldSession session(state);RawWorldRequest shortWorld;shortWorld.actions={{0,0}};shortWorld.searchVersion=1;
        auto longWorld=shortWorld;longWorld.searchVersion=2;
        const auto first=session.Evaluate(shortWorld),second=session.Evaluate(longWorld);
        CheckWorldCache(first.construction.productionAfterWindow==0 && second.construction.productionAfterWindow>0
            && session.Counts().evaluations==2,"sixty-second and full-portfolio worlds never share their raw horizon");
        SameCacheWorld(second,session.Evaluate(longWorld));
        auto visible=longWorld;visible.visibleCounterWitness=true;session.Evaluate(visible);
        CheckWorldCache(session.Counts().evaluations==3,"the explicit visible-cannon response is its own snapshot variant");
        auto targets=CacheArena();targets.precisionReady=true;targets.precisionTargetLimit=3;
        Plant a;a.id=78201;a.health=100;a.reward=20;a.assetValue=20;a.x=300;
        auto b=a;b.id=78202;b.reward=b.assetValue=40;b.x=400;targets.plants={a,b};
        RawWorldSession precision(targets);RawWorldRequest one;one.precisionTargetID=a.id;
        auto two=one;two.precisionAdditionalTargetIDs={b.id};
        const auto single=precision.Evaluate(one),both=precision.Evaluate(two);
        CheckWorldCache(single.construction.precisionHits==1 && both.construction.precisionHits==2
            && both.features[5]>single.features[5] && precision.Counts().evaluations==2,
            "complete precision target sets distinguish shared-wallet transactions within one source snapshot");
    }
    {
        std::atomic<bool> cancelled=false;
        auto state=CacheArena();state.cancellation=&cancelled;RawWorldSession session(state);RawWorldRequest request;
        const auto completed=session.Evaluate(request);SameCacheWorld(completed,session.Evaluate(request));
        cancelled=true;bool threw=false;
        try {session.Evaluate(request);} catch(...) {threw=true;}
        CheckWorldCache(threw && session.Counts().evaluations==1 && session.Counts().hits==1,
            "cancellation is checked before an otherwise exact cache hit can publish a result");
        cancelled=false;SameCacheWorld(completed,session.Evaluate(request));
        state.timeLimitedSearch=true;state.searchDeadline=std::chrono::steady_clock::now();
        RawWorldSession expired(state);const auto stopped=expired.Evaluate(request);
        CheckWorldCache(!stopped.complete && expired.Counts().evaluations==0 && expired.Counts().hits==0,
            "an already expired deadline cannot return an adoptable world or borrow another session's completed result");
    }
    {
        auto state=CacheArena();state.options.clear();RawWorldSession session(state);RawWorldRequest request;
        for(int i=0;i<RawWorldCacheEntryLimit+3;++i) {request.counterHoldSeconds=static_cast<float>(i);session.Evaluate(request);}
        const auto full=session.Counts();session.Evaluate(request);
        CheckWorldCache(full.entries==RawWorldCacheEntryLimit && full.bytes<=RawWorldCacheByteLimit
            && session.Counts().evaluations==full.evaluations+1 && session.Counts().entries==full.entries,
            "entry exhaustion recomputes new worlds without retaining unbounded session history");
        request.counterHoldSeconds=0;session.Evaluate(request);
        CheckWorldCache(session.Counts().hits==1,"early common passive worlds remain reusable after the entry cap");
        auto traced=CacheArena();traced.traceEconomy=true;
        auto worker=traced.options.front().unit;worker.body.economic=true;worker.productionRemaining=1000;
        for(int i=0;i<100;++i) {worker.id=78300+i;traced.current.push_back(worker);}
        RawWorldSession large(traced);
        for(int i=0;i<RawWorldCacheEntryLimit+1;++i) {request.counterHoldSeconds=static_cast<float>(i);large.Evaluate(request);}
        const auto bounded=large.Counts();const auto last=large.Evaluate(request);
        CheckWorldCache(bounded.entries>0 && bounded.entries<RawWorldCacheEntryLimit && bounded.bytes<=RawWorldCacheByteLimit
            && large.Counts().evaluations==bounded.evaluations+1 && last.complete && last.construction.workerTrace.size()>=6000,
            "full economic traces hit the byte cap before the entry cap while uncached worlds retain their complete diagnostics");
    }
    {
        const Weights money{0,0,0,0,1,-1,0,0};
        auto state=CacheResponseArena();const auto cached=Search(state,money,91);
        auto uncached=state;uncached.reuseRawWorlds=false;const auto plain=Search(uncached,money,91);
        SameCacheSearch(cached,plain);
        CheckWorldCache(cached.rawWorldCacheHits>0 && cached.rawWorldEvaluations<plain.rawWorldEvaluations
            && plain.rawWorldCacheHits==0 && plain.rawWorldCacheEntries==0 && plain.rawWorldCacheBytes==0,
            "one complete observed search saves actual raw integrations without changing fixed-candidate results");
        const Weights changed{.5f,.1f,200,.25f,.8f,-.7f,-.2f,.1f};
        StateModel model;model.coefficients[0][0]=.1f;state.stateModel=uncached.stateModel=&model;
        SameCacheSearch(Search(state,changed,92),Search(uncached,changed,92));
        CheckWorldCache(!state.rawWorldBinding.Attached() && !uncached.rawWorldBinding.Attached(),
            "completed searches leave no cache pointer on caller-owned snapshots");
    }
    {
        auto state=CacheArena();RawWorldSession session(state);RawWorldRequest request;request.actions={{0,0},{1,1}};
        auto first=std::async(std::launch::async,[&] {return session.Evaluate(request);});
        auto second=std::async(std::launch::async,[&] {return session.Evaluate(request);});
        const auto a=first.get(),b=second.get();SameCacheWorld(a,b);
        const auto before=session.Counts();SameCacheWorld(a,session.Evaluate(request));
        CheckWorldCache(before.evaluations+before.hits==2 && before.entries==1
            && session.Counts().hits==before.hits+1,
            "concurrent misses publish at most one immutable entry and never return a partially computed world");
    }
    std::cout<<"Raw world cache preserves exact source variants, order, payment, response windows, observation age, cancellation and bounded memory\n";
}
