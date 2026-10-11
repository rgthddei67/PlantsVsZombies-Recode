#include "Game/AI/ColdStoragePlanner.h"
#include "Game/AI/ColdStorageSearch.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
/** 单独标明未付款增援契约；这些合成画像不代表真人对局胜率或必买阵容。 */
void CheckContinuation(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED assault continuation: "<<message<<'\n';std::exit(1);}
}

/** 无移动、无生产的两类合法士兵只用于观察整案映射、当前定价和真实炮弹损失。 */
ColdStorageSearch::Snapshot ContinuationArena() {
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1; state.searchVersion=2; state.netEconomy=true; state.houseX=-10000;
    state.budget=160; state.capacity=4; state.capitalRiskAllowance=0; state.traceEconomy=true;
    Option first;
    first.type=53101; first.cost=8; first.unit.body.x=900;
    first.unit.body.health=100; first.unit.maximumBody=100;
    first.unit.body.purchaseCost=8; first.unit.body.speed=0; first.unit.biteDps=0;
    auto second=first;
    second.type=53102; second.cost=11; second.unit.body.purchaseCost=11;
    state.options={first,second};
    state.assaultContinuation={{{first.type,0,-1,0,10},{second.type,0,-1,0,12},
        {first.type,0,-1,0,14},{second.type,0,-1,0,16}}};
    state.assaultIntentRevision=3;
    // 不设墙钟截止：测试实际完成的固定候选搜索，不由机器快慢决定能否看到早期槽。
    state.timeLimitedSearch=false;
    return state;
}

/** 已付前锋与独立在途弹进入同一新快照；是否保留炮体不影响已离膛弹。 */
ColdStorageSearch::Snapshot FlightArena(bool sourceAlive) {
    using namespace ColdStorageSearch;
    auto state=ContinuationArena();
    Unit paid=state.options[0].unit;
    paid.id=53201; paid.body.purchaseCost=7;
    state.current={paid};
    Counter flight;
    flight.plantID=0; flight.consumesPlant=false; flight.blast.committed=true;
    flight.blast.ready=2; flight.blast.x=900; flight.blast.damage=1800;
    flight.blast.reach.fill(-1); flight.blast.reach[0]=115;
    state.counters={flight};
    if(sourceAlive) {
        Plant source;
        source.id=53202; source.x=300; source.health=300;
        state.plants={source};
        auto reload=flight;
        reload.plantID=source.id; reload.blast.committed=false;
        reload.blast.ready=1000; reload.recharge=1000;
        state.counters.push_back(reload);
    }
    return state;
}

/** 只等待固定搜索结束；不以短超时、轮询次数或睡眠长度决定战术断言胜败。 */
std::unique_ptr<ColdStorageSearch::Planner::Work> TakeComplete(ColdStorageSearch::Planner& planner) {
    while(planner.Computing()) std::this_thread::yield();
    auto work=planner.TakeReady();
    CheckContinuation(work && !work->failed && !planner.Busy(),
        "a complete numeric worker result is joined before inspecting the fresh snapshot");
    CheckContinuation(work->budgetMilliseconds==0 && !work->snapshot.timeLimitedSearch,
        "integration coverage uses the deterministic fixed-candidate search without a wall-clock deadline");
    return work;
}

/** 即使完整增援被资金或价值拒绝，同窗等待仍合法且不计任何新购买费用。 */
void CheckWait(const ColdStorageSearch::Result& result) {
    CheckContinuation(result.actions.empty() && !result.assaultContinuationSelected
        && result.precisionTargetID==0 && result.forecastPrecisionTargetID==0
        && result.features[5]==0 && result.baselineFeatures[5]==0,
        "rejected unpaid continuation leaves a real zero-purchase wait rather than a forced partial cart");
}
}

/** 整队重映射、观察后的真实新快照和付款边界共同约束分阶段控制器。 */
void RunColdStorageAssaultContinuationTests() {
    using namespace ColdStorageSearch;
    {
        const auto original=ContinuationArena();
        const auto first=Search(original,InitialWeights,71);
        const auto repeated=Search(original,InitialWeights,71);
        CheckContinuation(first.assaultDiagnostics.continuationEvaluated==1
            && repeated.assaultDiagnostics.continuationEvaluated==1
            && first.assaultDiagnostics.continuationValueRejected==1
            && first.features==repeated.features && first.score==repeated.score,
            "each legal fresh search actually reevaluates one complete continuation against its own wait baseline");
        CheckWait(first); CheckWait(repeated);
        CheckContinuation(original.budget==160 && original.capacity==4
            && original.assaultContinuation.front().size()==4
            && original.assaultContinuation.front()[0].delay==10
            && original.options[0].cost==8 && original.options[1].cost==11,
            "evaluation neither prepays the old proposal nor rewrites its stored relative timing or source prices");

        auto reordered=original;
        std::reverse(reordered.options.begin(),reordered.options.end());
        reordered.options[0].cost=13; reordered.options[0].unit.body.purchaseCost=13;
        reordered.options[1].cost=9; reordered.options[1].unit.body.purchaseCost=9;
        reordered.searchRound=8;
        const auto remapped=Search(reordered,InitialWeights,71);
        CheckContinuation(remapped.assaultDiagnostics.continuationEvaluated==1,
            "current legal type and route identities remap after option order and prices change");
        CheckWait(remapped);

        for(int boundary=0;boundary<3;++boundary) {
            auto unavailable=original;
            if(boundary==0) unavailable.options.clear();
            if(boundary==1) unavailable.budget=0;
            if(boundary==2) unavailable.capacity=0;
            const auto rejected=Search(unavailable,InitialWeights,71);
            CheckContinuation(rejected.assaultDiagnostics.continuationEvaluated==0
                && rejected.assaultDiagnostics.continuationUnavailable==1,
                "no options, no cash and no slots are explicit terminal refusals rather than unconsumed deadline events");
            CheckWait(rejected);
        }

        auto poor=original;
        poor.budget=11;
        const auto shortWallet=Search(poor,InitialWeights,71);
        CheckContinuation(shortWallet.assaultDiagnostics.continuationEvaluated==0
            && shortWallet.assaultDiagnostics.continuationUnavailable==1,
            "a wallet that can buy one member cannot relabel a truncated formation as evaluated continuation");
        CheckWait(shortWallet);

        auto repriced=reordered;
        repriced.options[0].cost=90; repriced.options[0].unit.body.purchaseCost=90;
        const auto expensive=Search(repriced,InitialWeights,71);
        CheckContinuation(expensive.assaultDiagnostics.continuationEvaluated==0
            && expensive.assaultDiagnostics.continuationUnavailable==1,
            "new prices make the whole continuation unaffordable even when every required type remains individually legal");
        CheckWait(expensive);

        auto missing=original;
        missing.options.pop_back();
        const auto unavailable=Search(missing,InitialWeights,71);
        CheckContinuation(unavailable.assaultDiagnostics.continuationEvaluated==0
            && unavailable.assaultDiagnostics.continuationUnavailable==1,
            "losing a necessary type rejects the complete continuation rather than duplicating its surviving partner");
        CheckWait(unavailable);

        auto crowded=original;
        crowded.capacity=3;
        const auto shortCapacity=Search(crowded,InitialWeights,71);
        CheckContinuation(shortCapacity.assaultDiagnostics.continuationEvaluated==0
            && shortCapacity.assaultDiagnostics.continuationUnavailable==1,
            "current capacity cannot silently remove a required member from an unpaid continuation");
        CheckWait(shortCapacity);
    }
    {
        auto flight=FlightArena(false);
        const std::vector<Action> now{{0,0},{1,0},{0,0},{1,0}};
        const std::vector<Action> oldTiming{{0,10},{1,12},{0,14},{1,16}};
        const auto immediate=EvaluateCandidate(flight,InitialWeights,now);
        const auto delayed=EvaluateCandidate(flight,InitialWeights,oldTiming);
        CheckContinuation(immediate.features[3]==0 && delayed.features[3]==38
            && immediate.features[5]==38 && delayed.features[5]==38
            && immediate.baselineFeatures[5]==0 && immediate.construction.counterTrace.size()==1,
            "the independent shell kills immediate arrivals and paid existing troops without charging the existing troops again");
        CheckContinuation(ShouldConserveCapital(immediate,flight.budget,0,0)
            && !ShouldConserveCapital(delayed,flight.budget,0,0),
            "the fixture distinguishes current airborne risk from a stale proposal's later entry");
        const auto reviewed=Search(flight,InitialWeights,73);
        CheckContinuation(reviewed.assaultDiagnostics.continuationEvaluated==1
            && reviewed.assaultDiagnostics.continuationCapitalRejected==1
            && reviewed.assaultDiagnostics.continuationValueRejected==1
            && reviewed.baselineFeatures==immediate.baselineFeatures,
            "continuation really enters at current delay zero and uses the same committed-shell wait baseline and capital gate");
        CheckWait(reviewed);
        auto landed=flight;
        landed.counters.clear(); landed.current.clear();
        const auto safe=Search(landed,InitialWeights,73);
        CheckContinuation(safe.assaultDiagnostics.continuationEvaluated==1
            && safe.assaultDiagnostics.continuationCapitalRejected==0,
            "a genuinely new shell-free snapshot reevaluates the continuation without inheriting the old risk result");
        CheckWait(safe);
        CheckContinuation(flight.budget==160 && flight.current[0].body.health==100
            && flight.current[0].body.purchaseCost==7 && flight.counters[0].blast.committed
            && flight.counters[0].blast.ready==2 && flight.plants.empty(),
            "source removal, forecasted losses and rejected reinforcements do not mutate the captured wallet or airborne transaction");
    }
    {
        Planner planner;
        auto active=FlightArena(true);
        const auto proposals=active.assaultContinuation;
        active.assaultContinuation.clear(); active.assaultIntentRevision=0;
        const AssaultObservation ready{54001,1,1,0}, fired{54002,1,0,1}, removed{54003,0,0,1};
        planner.RememberAssault(10,ready,proposals);
        CheckContinuation(planner.AssaultIntent().Active() && !planner.AssaultIntent().NeedsReplan()
            && !planner.ObserveAssault(10,ready) && active.budget==160,
            "a confirmed advance payment only arms bounded observation and does not prepay or execute the remembered formation");
        CheckContinuation(planner.ObserveAssault(11,fired),"an actual launch requests fresh whole-battlefield evaluation");
        const auto firedRevision=planner.AssaultIntent().Revision();
        CheckContinuation(planner.Start(active,InitialWeights,79),"the first observed event starts a complete numeric review");
        CheckContinuation(!planner.Start(active,InitialWeights,80) && planner.AssaultIntent().NeedsReplan(),
            "an occupied planner cannot consume an event by declining a second start");
        CheckContinuation(planner.ObserveAssault(12,removed),
            "source removal while a result is pending leaves the already airborne shell as a newer event");
        const auto removedRevision=planner.AssaultIntent().Revision();
        auto first=TakeComplete(planner);
        CheckContinuation(first->snapshot.assaultIntentRevision==firedRevision && first->snapshot.searchRound==0
            && first->snapshot.assaultContinuation.size()==1 && first->snapshot.budget==160
            && first->result.assaultDiagnostics.continuationEvaluated==1,
            "the worker owns the captured event and unpaid proposal without borrowing a later observation or subtracting its bill");
        CheckWait(first->result);
        planner.CompleteAssaultReview(firedRevision,false);
        CheckContinuation(planner.AssaultIntent().NeedsReplan() && planner.AssaultIntent().Revision()==removedRevision
            && planner.AssaultIntent().Observation().committedShots==1,
            "accepting an older wait result cannot consume the newer source-removal event or erase its shell");

        auto removedState=FlightArena(false);
        removedState.assaultContinuation.clear(); removedState.assaultIntentRevision=0;
        CheckContinuation(planner.Start(removedState,InitialWeights,79),"the pending newer event can start after the prior result is joined");
        auto second=TakeComplete(planner);
        CheckContinuation(second->snapshot.assaultIntentRevision==removedRevision && second->snapshot.searchRound==1
            && second->snapshot.plants.empty() && second->snapshot.counters.size()==1
            && second->result.assaultDiagnostics.continuationEvaluated==1
            && second->result.assaultDiagnostics.continuationCapitalRejected==1,
            "the next full evaluation uses current source removal but still protects against the real independent impact");
        CheckWait(second->result);
        planner.CompleteAssaultReview(removedRevision,false);
        CheckContinuation(!planner.AssaultIntent().NeedsReplan() && planner.AssaultIntent().Active()
            && planner.AssaultIntent().ArmedAt()==10 && planner.AssaultIntent().ReplanCount()==2
            && !planner.ObserveAssault(54,removed),
            "two reviewed waits consume only their observed events and never renew the original payment lifetime");
        CheckContinuation(!planner.ObserveAssault(55,removed) && !planner.AssaultIntent().Active(),
            "the planner expires the unpaid continuation exactly forty-five game seconds after its actual advance payment");
        CheckContinuation(planner.Start(removedState,InitialWeights,79),"ordinary search remains available after observation expires");
        auto expired=TakeComplete(planner);
        CheckContinuation(expired->snapshot.assaultContinuation.empty() && expired->snapshot.assaultIntentRevision==0
            && expired->result.assaultDiagnostics.continuationEvaluated==0 && expired->snapshot.budget==160,
            "expiry clears proposal injection without changing the caller's wallet or ordinary waiting choices");
        CheckWait(expired->result);

        planner.RememberAssault(60,ready,proposals);
        CheckContinuation(planner.ObserveAssault(61,fired),"a later confirmed payment can arm a separate observation lifetime");
        planner.Cancel();
        CheckContinuation(!planner.AssaultIntent().Active() && planner.Start(active,InitialWeights,79),
            "cancellation clears the transient assault lifetime and leaves the planner reusable");
        auto reset=TakeComplete(planner);
        CheckContinuation(reset->snapshot.assaultContinuation.empty() && reset->snapshot.assaultIntentRevision==0
            && reset->snapshot.searchRound==0 && reset->result.assaultDiagnostics.continuationEvaluated==0,
            "a reset planner cannot import a prior board's unpaid formation, event token or round cursor");
        CheckWait(reset->result);
    }
    {
        Planner planner;
        const auto state=ContinuationArena();
        const AssaultObservation ready{55001,1,1,0}, fired{55002,1,0,1}, removed{55003,0,0,1};
        planner.RememberAssault(10,ready,state.assaultContinuation);
        CheckContinuation(planner.ObserveAssault(11,fired),"the old payment has an independently captured review token");
        const auto oldPaymentRevision=planner.AssaultIntent().Revision();
        planner.RememberAssault(20,ready,state.assaultContinuation);
        CheckContinuation(planner.ObserveAssault(21,fired),"a newer payment owns its own unconsumed event");
        const auto currentRevision=planner.AssaultIntent().Revision();
        planner.CompleteAssaultReview(oldPaymentRevision,true);
        CheckContinuation(planner.AssaultIntent().Active() && planner.AssaultIntent().NeedsReplan()
            && planner.AssaultIntent().ArmedAt()==20 && planner.AssaultIntent().Revision()==currentRevision
            && planner.AssaultIntent().ReplanCount()==0,
            "a purchased result from an old payment lifetime cannot clear or consume the new unpaid intent");
        CheckContinuation(planner.ObserveAssault(22,removed),"a newer observation can arrive before an older current-lifetime result");
        const auto newerRevision=planner.AssaultIntent().Revision();
        planner.CompleteAssaultReview(currentRevision,true);
        CheckContinuation(planner.AssaultIntent().Active() && planner.AssaultIntent().NeedsReplan()
            && planner.AssaultIntent().Revision()==newerRevision
            && planner.AssaultIntent().Observation().committedShots==1,
            "even a purchased older event cannot erase a newer source change and its surviving airborne shell");
        planner.CompleteAssaultReview(newerRevision,true);
        CheckContinuation(!planner.AssaultIntent().Active() && !planner.AssaultIntent().NeedsReplan()
            && state.budget==160 && state.assaultContinuation.front().size()==4,
            "only a matching current purchase consumes its intent without editing the source snapshot or proposal");
    }
    {
        Planner planner;
        auto state=ContinuationArena(); state.assaultContinuation.clear(); state.assaultIntentRevision=0;
        Result selected;
        selected.phasedPlanning.selected=true; selected.phasedPlanning.evaluated=1;
        selected.actions={{0,0}};
        selected.followupPolicy.actions={{1,2},{0,4}};
        selected.followupPolicy.earliestSeconds=3; selected.followupPolicy.expirySeconds=20;
        selected.followupPolicy.requireObservedCobLaunch=true;
        selected.followupPolicy.requireShotsClear=true; selected.followupPolicy.maxReadyCobs=0;
        const AssaultObservation ready{56000,2,2,0,0}, windup{56001,2,1,1,0};
        CheckContinuation(!planner.RememberConditionalAssault(10,ready,state,selected,false)
            && !planner.AssaultIntent().Active() && state.budget==160,
            "an unpaid first stage cannot arm a lead-payment policy or charge either cart");
        CheckContinuation(planner.RememberConditionalAssault(10,ready,state,selected,true)
            && planner.AssaultIntent().ArmedAt()==10 && planner.AssaultIntent().ExpiresAt()==30
            && !planner.AssaultIntent().NeedsReplan() && state.budget==160 && state.current.empty(),
            "confirmed current payment stores only an unpaid conditional tail with its own exact bounded window");
        CheckContinuation(planner.ObserveAssault(11,windup) && !planner.AssaultIntent().Eligible()
            && !planner.AssaultIntent().ObservedLaunch(),"a real windup event wakes ordinary search without pretending launch eligibility");
        CheckContinuation(planner.Start(state,InitialWeights,83),"trigger-blocked public changes still permit ordinary fresh search");
        auto blocked=TakeComplete(planner);
        CheckContinuation(blocked->snapshot.assaultContinuation.empty()
            && blocked->snapshot.assaultIntentRevision==planner.AssaultIntent().Revision()
            && blocked->snapshot.budget==160 && blocked->result.assaultDiagnostics.continuationEvaluated==0,
            "a fresh snapshot carries the checked event token but no gated tail, payment or old score");
        planner.CompleteAssaultReview(blocked->snapshot.assaultIntentRevision,false);
        CheckContinuation(!planner.AssaultIntent().NeedsReplan() && planner.ObserveAssault(13,windup),
            "after the blocked event is reviewed the one-shot earliest-time edge can request another fresh snapshot");
        planner.CompleteAssaultReview(planner.AssaultIntent().Revision(),false);
        CheckContinuation(!planner.ObserveAssault(13,windup) && planner.AssaultIntent().ArmedAt()==10,
            "repeated time observations cannot renew the anchor or retrigger its timer");
        CheckContinuation(planner.ObserveAssault(14,{56002,2,1,1,10}) && !planner.AssaultIntent().Eligible(),
            "a true new shell does not excuse the remaining ready cannon");
        CheckContinuation(planner.ObserveAssault(15,{56003,0,0,1,10}) && !planner.AssaultIntent().Eligible(),
            "destroying every cannon does not erase its already launched shell");
        CheckContinuation(planner.ObserveAssault(16,{56004,0,0,0,0}) && planner.AssaultIntent().Eligible(),
            "the live proposal is admitted only after the witnessed shell is actually gone and all current gates pass");
        auto repriced=state;
        std::reverse(repriced.options.begin(),repriced.options.end());
        repriced.options[0].cost=17; repriced.options[0].unit.body.purchaseCost=17;
        repriced.options[1].cost=13; repriced.options[1].unit.body.purchaseCost=13;
        repriced.budget=100;
        CheckContinuation(planner.Start(repriced,InitialWeights,83),"an eligible tail enters the next current-price full evaluation");
        auto eligible=TakeComplete(planner);
        CheckContinuation(eligible->snapshot.assaultContinuation.size()==1
            && eligible->snapshot.assaultContinuationRelativeTiming
            && eligible->snapshot.assaultContinuation[0].size()==2
            && eligible->snapshot.assaultContinuation[0][0].type==53102
            && eligible->snapshot.assaultContinuation[0][0].delay==2
            && eligible->snapshot.assaultContinuation[0][1].delay==4
            && eligible->snapshot.budget==100 && eligible->snapshot.options[0].cost==17
            && eligible->result.assaultDiagnostics.continuationEvaluated==1,
            "a two-member tail remaps its type identities and exact payment-relative delays without old prices or future prepayment");
        CheckWait(eligible->result);
        planner.CompleteAssaultReview(eligible->snapshot.assaultIntentRevision,false);
        CheckContinuation(!planner.AssaultIntent().NeedsReplan() && planner.Start(repriced,InitialWeights,83),
            "ordinary fresh search can still compare an eligible unpaid tail after its dedicated wake was consumed");
        auto ordinary=TakeComplete(planner);
        CheckContinuation(ordinary->snapshot.assaultContinuation.size()==1
            && ordinary->result.assaultDiagnostics.continuationEvaluated==1
            && planner.AssaultIntent().ArmedAt()==10 && planner.AssaultIntent().ExpiresAt()==30,
            "reviewed waits keep only the original bounded unpaid construction and never renew its deadline");
        planner.CompleteAssaultReview(ordinary->snapshot.assaultIntentRevision,true);
        CheckContinuation(!planner.AssaultIntent().Active() && planner.Start(state,InitialWeights,83),
            "a matching actual purchase consumes the tail even when it came from an ordinary search without a new event");
        auto consumed=TakeComplete(planner);
        CheckContinuation(consumed->snapshot.assaultContinuation.empty() && consumed->snapshot.assaultIntentRevision==0
            && state.budget==160 && selected.followupPolicy.actions[0].delay==2,
            "a consumed tail cannot be injected or paid twice, and its source snapshot and policy remain unchanged");
    }
    {
        Planner planner;
        auto state=ContinuationArena(); state.assaultContinuation.clear(); state.assaultIntentRevision=0;
        Result wait;
        wait.phasedPlanning.selected=true; wait.phasedPlanning.waitSelected=true; wait.phasedPlanning.evaluated=1;
        wait.followupPolicy.actions={{0,0},{1,2}};
        wait.followupPolicy.earliestSeconds=2; wait.followupPolicy.expirySeconds=10;
        const AssaultObservation quiet{57000,0,0,0,0};
        CheckContinuation(planner.RememberConditionalAssault(100,quiet,state,wait,false)
            && state.budget==160 && state.current.empty() && !planner.AssaultIntent().NeedsReplan(),
            "a fully evaluated winning wait establishes one explicit unpaid anchor without first-stage purchase");
        CheckContinuation(!planner.RememberConditionalAssault(101,quiet,state,wait,false)
            && planner.AssaultIntent().ArmedAt()==100 && planner.AssaultIntent().ExpiresAt()==110,
            "a repeated winning wait cannot replace an existing anchor or extend its deadline");
        CheckContinuation(planner.ObserveAssault(102,quiet),"the explicit wait anchor wakes once at its copied earliest time");
        planner.CompleteAssaultReview(planner.AssaultIntent().Revision(),false);
        for(int frame=0;frame<30;++frame) CheckContinuation(!planner.ObserveAssault(102,quiet),
            "paused game-clock repeats do not spend another wake or extend the deferred cart");
        CheckContinuation(!planner.ObserveAssault(110,quiet) && !planner.AssaultIntent().Active()
            && !planner.RememberConditionalAssault(111,quiet,state,wait,false),
            "expiry drops the entire deferred cart and its explicit wait cannot perpetually rearm itself");
        CheckContinuation(planner.Start(state,InitialWeights,89),"expired wait anchors never block normal fresh search");
        auto expired=TakeComplete(planner);
        CheckContinuation(expired->snapshot.assaultContinuation.empty() && expired->snapshot.assaultIntentRevision==0,
            "the expired cart and old event token do not escape into ordinary search");
        planner.Cancel();
        CheckContinuation(planner.RememberConditionalAssault(0,quiet,state,wait,false),
            "cancel or load/new-board reset clears the transient wait latch and permits a genuinely new anchor at time zero");
        CheckContinuation(!planner.ObserveAssault(1,quiet) && !planner.ObserveAssault(.5f,quiet)
            && !planner.AssaultIntent().Active(),"backward game time clears the waiting intent instead of moving its deadline");
        planner.ClearAssault();
        auto unpaidLead=wait; unpaidLead.phasedPlanning.waitSelected=false; unpaidLead.actions={{0,0}};
        CheckContinuation(planner.RememberConditionalAssault(5,quiet,state,unpaidLead,true),
            "only a new confirmed current payment can open a new paid-anchor cycle");
        planner.ClearAssault();
        auto incomplete=wait; incomplete.phasedPlanning.evaluated=0;
        CheckContinuation(!planner.RememberConditionalAssault(6,quiet,state,incomplete,false)
            && !planner.AssaultIntent().Active(),"generated or incomplete phased waits cannot establish a live unpaid cart");
        auto bad=wait; bad.followupPolicy.actions[0].option=999;
        CheckContinuation(!planner.RememberConditionalAssault(6,quiet,state,bad,false)
            && !planner.AssaultIntent().Active(),"a missing required current type cannot create a partial remembered tail");
    }
    std::cout<<"Assault continuation preserves current-price teams, conditional triggers, bounded wait anchors and unpaid wallets\n";
}
