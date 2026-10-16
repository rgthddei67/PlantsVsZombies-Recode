#include "Game/AI/ColdStorageSearch.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void CheckTrial(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED informational trial: "<<message<<'\n';std::exit(1);}
}

/** 冷启动只有一张公开付费范围牌；不提供历史画像或固定不反制概率。 */
ColdStorageSearch::Snapshot TrialArena(int version=2) {
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1;state.houseX=-10000;state.gridLeft=0;state.cellWidth=100;
    state.searchVersion=version;state.netEconomy=true;state.experiencedFormations=false;
    state.budget=1000;state.capacity=1;state.recoveryReserve=48;state.capitalRiskAllowance=350;
    state.informationalTrials=true;state.observedRiskEnabled=true;
    Option worker;
    worker.type=54102;worker.cost=24;worker.unit.body.x=850;
    worker.unit.body.health=500;worker.unit.body.purchaseCost=24;
    worker.unit.body.economic=true;worker.unit.biteDps=0;
    state.options={worker};
    Counter ash;
    ash.source=1;ash.observedSourceKey=1;ash.sunCost=100;ash.recharge=1;ash.windup=1;
    ash.blast.x=850;ash.blast.reach.fill(1000);ash.blast.damage=1800;
    ash.clearsLadderRow=true;ash.ladderClearRow=0;
    state.counters={ash};state.playerSun=100000;
    return state;
}

/** 原量级实时预算，不假定机器必须刚好完成若干随机候选。 */
ColdStorageSearch::Result SearchWithin(ColdStorageSearch::Snapshot state,int milliseconds,unsigned seed=71) {
    state.timeLimitedSearch=true;
    state.searchDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    return ColdStorageSearch::Search(state,ColdStorageSearch::InitialWeights,seed);
}

int PurchaseCost(const ColdStorageSearch::Snapshot& state,const ColdStorageSearch::Result& result) {
    int bill=0;
    for(const auto& action:result.actions) {
        CheckTrial(action.option>=0 && action.option<static_cast<int>(state.options.size()),"actions use legal real options");
        bill+=state.options[action.option].cost;
    }
    return bill;
}

/** 即使不交任何新灰烬也无收入、资产损失或突破的阵地。 */
void AddLethalFire(ColdStorageSearch::Snapshot& state) {
    ColdStorageSearch::Plant hazard;
    hazard.x=300;hazard.health=100000;hazard.initialHealth=100000;
    hazard.dps=100000;hazard.multiTarget=true;hazard.hitDamage=100;
    hazard.range=10000;hazard.edible=false;hazard.assetValue=100;
    state.plants={hazard};
}

/** 显式诊断四案与实时试探复用同一完整评估，不把原始短窗或既有军团收入误当新案潜力。 */
void CheckPaidAreaResponseComparison() {
    using namespace ColdStorageSearch;
    {
        auto state=TrialArena(1);state.capacity=16;state.resumePortfolio=true;state.traceEconomy=true;
        const std::vector<Action> actions{{0,0}};
        const auto comparison=ComparePaidAreaResponse(state,InitialWeights,actions);
        CheckTrial(comparison.complete && comparison.matchesRequested && comparison.robust.complete && comparison.potential.complete
            && comparison.requestedSearchVersion==1 && comparison.effectiveSearchVersion==2 && comparison.expandedForecast
            && comparison.horizonSeconds==120 && comparison.productionWindowSeconds==60,
            "four-arm diagnosis expands v1 once and reports the actual shared full horizon");
        const auto& potential=comparison.potential.cart;
        float tracedProduction=0;
        for(const auto& point:potential.construction.workerTrace) tracedProduction+=point.income;
        CheckTrial(potential.rawProduction>0 && potential.construction.productionAfterWindow>0
            && potential.features[4]==potential.rawProduction+potential.construction.productionAfterWindow
            && comparison.potential.cash==potential.rawProduction+potential.construction.productionAfterWindow,
            "diagnostic production separates the calibration window from same-horizon later production");
        CheckTrial(!potential.construction.workerTrace.empty()
            && tracedProduction==potential.rawProduction+potential.construction.productionAfterWindow
            && potential.construction.workerTrace.back().at>comparison.productionWindowSeconds
            && potential.construction.workerTrace.back().at<=comparison.horizonSeconds,
            "full worker traces reconcile to raw plus after-window production");
        auto full=state;full.searchVersion=2;
        const auto independent=EvaluateCandidate(full,InitialWeights,actions);
        CheckTrial(comparison.robust.cart.features==independent.features && comparison.robust.cart.score==independent.score,
            "shared diagnostic comparison preserves the complete ordinary robust cart score");
        for(const auto* outcome:{&comparison.robust.cart,&comparison.robust.wait,&comparison.potential.cart,&comparison.potential.wait})
            CheckTrial(outcome->effectiveWeights==comparison.robust.cart.effectiveWeights && outcome->expandedForecast
                && outcome->fallbackMode==0 && !outcome->trialDiagnostics.selected && outcome->followupPolicy.actions.empty(),
                "all four arms share weights and expansion without granting production selection flags");
        const auto trial=SearchWithin(state,360);
        const auto selectedComparison=ComparePaidAreaResponse(state,InitialWeights,trial.actions,trial.expandedForecast);
        CheckTrial(trial.trialDiagnostics.selected && selectedComparison.complete && trial.score==selectedComparison.robust.cart.score
            && trial.features==selectedComparison.robust.cart.features && trial.trialDiagnostics.potentialCash==selectedComparison.potential.cash,
            "the production trial uses the diagnostic four-arm implementation with unchanged forecast values");
        CheckTrial(state.searchVersion==1 && state.counters.size()==1 && state.trialHistory.Records().empty(),
            "four-arm comparison never mutates source version, counters or real trial history");
        const std::vector<Action> larger(3,{0,0});
        const auto diagnosticOnly=ComparePaidAreaResponse(state,InitialWeights,larger);
        CheckTrial(diagnosticOnly.complete && diagnosticOnly.bill==72 && diagnosticOnly.robust.cart.actions.size()==3
            && diagnosticOnly.robust.cart.fallbackMode==0,
            "explicit legal diagnosis may compare a full bill above the information-trial cap without granting purchase eligibility");
    }
    {
        auto state=TrialArena();state.netEconomy=false;state.capacity=4;state.traceEconomy=true;
        Plant source;source.id=10;source.x=800;source.health=300;state.plants={source};
        Counter stored;stored.source=2;stored.plantID=source.id;stored.stored=true;
        stored.sunCost=75;stored.windup=3.4f;stored.recharge=10000;
        stored.blast.x=800;stored.blast.damage=1800;stored.blast.reach.fill(-1);stored.blast.reach[0]=275;
        state.counters.front().blast.damage=0;state.counters.push_back(stored);
        auto worker=state.options.front().unit;worker.body.x=900;
        state.current={worker,worker,worker};state.current[1].body.spawnAt=24;state.current[2].body.spawnAt=30;
        state.options.front().unit=worker;
        Weights losses{};losses[6]=-1;
        const auto comparison=ComparePaidAreaResponse(state,losses,{{0,55}});
        CheckTrial(comparison.complete && comparison.responseEnvelopeSeconds==55
            && comparison.storedResponseEnvelopeSeconds==55 && comparison.patientResponseHoldSeconds==8,
            "cart delay extends one shared response envelope beyond the default stored hold");
        for(const auto* pair:{&comparison.robust,&comparison.potential})
            CheckTrial(pair->cart.counterHoldSeconds==55 && pair->wait.counterHoldSeconds==55
                && pair->cart.features[6]==96 && pair->wait.features[6]==72,
                "both full cart and same-army wait compare the late stored response in both counter domains");
    }
    for(bool committed:{false,true}) {
        auto state=TrialArena();state.traceEconomy=true;
        auto retained=state.counters.front();retained.source=2;retained.observedSourceKey=2;
        if(committed) retained.blast.committed=true;
        else {retained.consumesPlant=false;retained.sunCost=retained.iceCost=0;}
        state.counters.push_back(retained);
        const auto comparison=ComparePaidAreaResponse(state,InitialWeights,{{0,0}});
        CheckTrial(comparison.complete && comparison.removedCounterCount==1 && comparison.removedSourceKeys==std::vector<int>{1}
            && comparison.potential.cart.rawProduction==0 && comparison.potential.cart.features[3]==0
            && comparison.potential.cash==0 && !comparison.potential.cart.construction.counterTrace.empty(),
            "the shared narrow deletion retains a free reusable or already committed lethal counter");
    }
    {
        auto state=TrialArena();state.rows=2;state.netEconomy=false;
        auto existing=state.options.front().unit;existing.body.purchaseCost=4;existing.id=9123;state.current={existing};
        state.options.front().row=state.options.front().unit.body.row=1;
        AddLethalFire(state);state.plants.front().row=1;
        state.counters.front().blast.reach.fill(-1);state.counters.front().blast.reach[1]=1000;
        ProductionCalibration calibration;
        calibration.nodes={{2,1,2,.5f,0},{-1,-1,-1,0,.2f},{-1,-1,-1,0,1}};
        state.productionCalibration=&calibration;
        const Weights materialOnly{0,0,0,0,0,-1,0,0};
        const auto comparison=ComparePaidAreaResponse(state,materialOnly,{{0,0}});
        CheckTrial(comparison.complete,"existing-army attribution comparison completes without a realtime machine-speed assertion");
        for(const auto* pair:{&comparison.robust,&comparison.potential})
            CheckTrial(pair->cart.features[4]-pair->wait.features[4]>24 && pair->cart.rawProduction>0
                && pair->cart.rawProduction==pair->wait.rawProduction
                && pair->cart.construction.productionAfterWindow==pair->wait.construction.productionAfterWindow
                && pair->cash==0,
                "cart-dependent calibration cannot count unchanged existing-worker production as new physical return");
    }
    {
        auto state=TrialArena();state.timeLimitedSearch=true;
        state.searchDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        const auto expired=ComparePaidAreaResponse(state,InitialWeights,{{0,0}});
        CheckTrial(!expired.complete && !expired.robust.complete && !expired.potential.complete,
            "the public comparison preserves its caller's deadline instead of silently permitting unlimited work");
        std::atomic<bool> cancellation=true;state.cancellation=&cancellation;
        bool cancelled=false;
        try {(void)ComparePaidAreaResponse(state,InitialWeights,{{0,0}});} catch(...) {cancelled=true;}
        CheckTrial(cancelled,"the public comparison checks cancellation before declaring any arm complete");
    }
}
}

/** 试探必须有真实可区分潜力、完整四案与持久新假设；正常价值搜索和原风险边界优先。 */
void RunColdStorageStagnationSearchTests() {
    using namespace ColdStorageSearch;
    // 最终复查只留下动作身份。当前反制、价格或合法池变化必须使旧案失效。
    {
        auto state=TrialArena();state.counters.clear();state.informationalTrials=false;
        state.stallProposal={{state.options[0].type,0,-1,0,0}};
        auto accepted=Search(state,InitialWeights,71);
        CheckTrial(accepted.evaluated==1 && accepted.actions.size()==1 && accepted.score>0,
            "a deep-review proposal is fully revalued and adopted on the fresh world");
        auto defended=state;AddLethalFire(defended);
        CheckTrial(Search(defended,InitialWeights,71).actions.empty(),
            "new lethal defense rejects an old profitable proposal");
        auto unaffordable=state;unaffordable.options[0].cost=2000;
        CheckTrial(Search(unaffordable,InitialWeights,71).actions.empty(),
            "old proposal uses the current price and never its former bill");
        auto removed=state;removed.options.clear();
        CheckTrial(Search(removed,InitialWeights,71).actions.empty(),
            "an unavailable old type cannot be restored into the legal pool");
        auto expired=state;expired.timeLimitedSearch=true;expired.searchDeadline=std::chrono::steady_clock::now();
        CheckTrial(Search(expired,InitialWeights,71).actions.empty(),
            "expired confirmation cannot buy the cached proposal");
    }
    CheckPaidAreaResponseComparison();
    for(int version:{1,2}) for(int milliseconds:{120,240,360}) {
        const auto state=TrialArena(version);
        const auto result=SearchWithin(state,milliseconds);
        const auto& trial=result.trialDiagnostics;
        CheckTrial(result.fallbackMode==1 && trial.selected && trial.complete && trial.signature!=0,
            "a complete novel cold trial fits the unchanged realtime budgets");
        CheckTrial(result.actions.size()==1 && PurchaseCost(state,result)==24 && trial.bill==24 && trial.cap==64,
            "one real worker is eligible without an obsolete minimum-three-troop rule");
        CheckTrial(result.score<0 && result.score==trial.robustScore && result.features[4]==0
            && trial.robustCash==0 && trial.potentialCash>=trial.bill,
            "the selected result retains its actual negative robust forecast; potential is diagnostic only");
        CheckTrial(state.observedResponse.profiles.empty() && result.observedDecision.profiles==0
            && result.observedDelayedWorlds==0 && state.trialHistory.Records().empty(),
            "search neither manufactures a cold response profile nor commits real trial history");
    }
    {
        auto state=TrialArena(1);state.capacity=16;state.resumePortfolio=true;
        const auto result=SearchWithin(state,360);
        CheckTrial(result.trialDiagnostics.selected && result.expandedForecast,
            "funded trial cart and both waits share the expanded horizon");
        auto full=state;full.searchVersion=2;
        const auto robust=EvaluateCandidate(full,InitialWeights,result.actions);
        CheckTrial(std::abs(robust.score-result.score)<.001f && robust.features==result.features,
            "full-horizon robust score is the independently evaluated real cart score");
        auto contrast=full;std::erase_if(contrast.counters,IsFreshKnownPaidAreaCounter);
        const auto potential=EvaluateCandidate(contrast,InitialWeights,result.actions);
        CheckTrial(std::abs(result.trialDiagnostics.potentialCash-potential.features[4])<.001f,
            "potential does not accidentally use the shorter original horizon");
    }
    {
        auto state=TrialArena();state.capitalRiskAllowance=0;
        const auto result=SearchWithin(state,120);
        CheckTrial(result.actions.empty() && result.fallbackMode==0 && result.trialDiagnostics.cap==0
            && result.trialDiagnostics.reason==TrialRejectReason::NoRisk,
            "a zero remaining global risk allowance cannot fund an informational purchase");
        state.capitalRiskAllowance=350;state.budget=state.recoveryReserve;
        CheckTrial(SearchWithin(state,120).actions.empty(),"recovery reserve cannot be spent on a trial");
    }
    {
        auto state=TrialArena();AddLethalFire(state);
        const auto result=SearchWithin(state,120);
        CheckTrial(result.actions.empty() && result.fallbackMode==0 && result.trialDiagnostics.complete
            && result.trialDiagnostics.reason==TrialRejectReason::NoMaterialPotential
            && result.trialDiagnostics.potentialCash==0 && result.trialDiagnostics.potentialPlantAssets==0,
            "zero material outcomes in both worlds cannot justify a purchase");
        state.informationalTrials=false;state.fallbackProbeBudget=64;state.fallbackAllIn=true;
        const auto legacy=SearchWithin(state,120);
        CheckTrial(legacy.actions.empty() && legacy.fallbackMode==0,
            "legacy all-in and probe flags cannot force a destructive purchase");
    }
    {
        auto state=TrialArena();
        auto cob=state.counters.front();cob.source=2;cob.observedSourceKey=0;
        cob.consumesPlant=false;cob.sunCost=0;cob.iceCost=0;
        state.counters.push_back(cob);
        const auto result=SearchWithin(state,120);
        CheckTrial(!result.trialDiagnostics.selected && result.actions.empty() && result.trialDiagnostics.complete
            && result.trialDiagnostics.removedCounters==1 && result.trialDiagnostics.potentialCash==0,
            "the narrow contrast keeps free repeatable cannon fire and rejects an ineffective trial");
        state.counters={cob};
        CheckTrial(SearchWithin(state,120).trialDiagnostics.reason==TrialRejectReason::NoPaidReaction,
            "a free counter alone supplies no paid-reaction information hypothesis");
    }
    {
        const auto original=TrialArena().counters.front();
        CheckTrial(IsFreshKnownPaidAreaCounter(original),"the public fresh paid area card is identified");
        for(int change=0;change<8;++change) {
            auto retained=original;
            if(change==0) retained.blast.committed=true;
            if(change==1) retained.stored=true;
            if(change==2) retained.targeted=true;
            if(change==3) retained.plantID=77;
            if(change==4) retained.flightSeconds=.5f;
            if(change==5) retained.consumesPlant=false;
            if(change==6) retained.sunCost=retained.iceCost=0;
            if(change==7) retained.observedSourceKey=0;
            CheckTrial(!IsFreshKnownPaidAreaCounter(retained),"committed, stored, free and unrelated paid defenses remain in contrast");
        }
    }
    {
        auto state=TrialArena();const auto first=SearchWithin(state,120);
        const auto signature=first.trialDiagnostics.signature;
        CheckTrial(state.trialHistory.Commit(signature,1,24,1)==ColdStorageTrials::Reason::Allowed
            && state.trialHistory.Settle(false),"a real failed trial can be recorded and settled");
        state.searchRound=937;state.noProgressSeconds=10000;
        state.counters.front().source=27;state.counters.front().observedSourceKey=19;
        state.counters.front().blast.ready=.123f;state.options.front().type=99999;
        CheckTrial(InformationalTrialSignature(state,{{0,0}})==signature,
            "elapsed time, round, type/source identity and fractional cooldown do not invent novelty");
        const auto rejected=SearchWithin(state,120);
        CheckTrial(rejected.actions.empty() && rejected.trialDiagnostics.reason==TrialRejectReason::History
            && rejected.trialDiagnostics.historyReason==ColdStorageTrials::Reason::FailedHypothesis,
            "the same failed hypothesis remains blocked across ordinary search rounds");
        CheckTrial(state.trialHistory.CreditProduction(1,24)==0
            && SearchWithin(state,120).trialDiagnostics.historyReason==ColdStorageTrials::Reason::FailedHypothesis,
            "a closed failed wave cannot be revived by a late production callback");
        auto active=TrialArena();
        CheckTrial(active.trialHistory.Commit(signature,1,24,1)==ColdStorageTrials::Reason::Allowed
            && active.trialHistory.CreditProduction(1,24)==24 && !active.trialHistory.Records().front().unresolved,
            "real production repays an active trial before an all-troops-empty settlement");
        CheckTrial(SearchWithin(active,120).trialDiagnostics.selected,
            "actually repaid active production can permit the same bounded hypothesis again");
    }
    {
        auto state=TrialArena();state.counters.front().blast.damage=1;
        const auto result=SearchWithin(state,120);
        CheckTrial(!result.actions.empty() && result.fallbackMode==0 && !result.trialDiagnostics.selected
            && result.trialDiagnostics.reason==TrialRejectReason::NormalPriority
            && result.features[4]>result.features[5],
            "a normally profitable real plan always keeps priority over information gathering");
    }
    for(int capability=0;capability<3;++capability) {
        auto state=TrialArena();auto& unit=state.options.front().unit;
        if(capability==0) {unit.burst.cost=1;unit.burst.range=500;}
        if(capability==1) {unit.repair.cost=1;unit.repair.amount=50;unit.repair.maximum=100;}
        if(capability==2) unit.engineer=true;
        const auto result=SearchWithin(state,240);
        CheckTrial(result.fallbackMode==0 && !result.trialDiagnostics.selected
            && result.trialDiagnostics.reason==TrialRejectReason::NoCandidate,
            "automatic paid burst, repair or reload capabilities cannot enter the one-bill trial ledger");
        CheckTrial(state.options.front().cost==24 && (unit.burst.cost>0 || unit.repair.cost>0 || unit.engineer),
            "trial exclusion leaves the real legal price and ordinary unit capability unchanged");
        auto supported=TrialArena();supported.current={unit};
        const auto existing=SearchWithin(supported,240);
        CheckTrial(!existing.trialDiagnostics.selected && existing.trialDiagnostics.reason==TrialRejectReason::Disabled
            && HasInformationalTrialAutomaticFees(unit),
            "supporting an existing automatic spender cannot bypass the one-bill trial boundary");
    }
    {
        auto state=TrialArena();
        state.options.front().unit.body.economic=false;
        Construction build;build.source=2;
        build.plant.x=800;build.plant.column=8;build.plant.health=100;
        build.plant.dps=1;build.plant.assetValue=200;
        state.construction.push_back(build);
        auto contrast=state;contrast.counters.clear();
        const auto wait=EvaluateCandidate(contrast,InitialWeights,{});
        const auto cart=EvaluateCandidate(contrast,InitialWeights,{{0,0}});
        CheckTrial(wait.construction.plantAssets-cart.construction.plantAssets>=24
            && cart.features[0]==0 && cart.features[1]==0 && cart.construction.plantAttackAssets==0,
            "an immobile harmless troop can prevent construction without destroying any plant assets");
        const auto result=SearchWithin(state,240);
        CheckTrial(result.actions.empty() && result.trialDiagnostics.complete
            && result.trialDiagnostics.reason==TrialRejectReason::NoMaterialPotential
            && result.trialDiagnostics.potentialPlantAssets==0,
            "deferred or cancelled construction is not actual attack damage and cannot fund an information trial");
    }
    {
        auto state=TrialArena();state.rows=2;
        auto existing=state.options.front().unit;existing.body.purchaseCost=4;existing.id=9123;
        state.current={existing};
        state.options.front().row=state.options.front().unit.body.row=1;
        AddLethalFire(state);state.plants.front().row=1;
        state.counters.front().blast.reach.fill(-1);state.counters.front().blast.reach[1]=1000;
        ProductionCalibration calibration;
        calibration.nodes={{2,1,2,.5f,0},{-1,-1,-1,0,.2f},{-1,-1,-1,0,1}};
        CheckTrial(calibration.IsValid(),"the attribution counterexample uses a valid production calibration tree");
        state.productionCalibration=&calibration;
        // 此处隔离信息门禁；普通评分不奖励生产，避免把既有校准差直接当普通优案。
        state.netEconomy=false;
        const Weights materialOnly{0,0,0,0,0,-1,0,0};
        auto contrast=state;contrast.counters.clear();
        const auto wait=EvaluateCandidate(contrast,materialOnly,{});
        const auto cart=EvaluateCandidate(contrast,materialOnly,{{0,0}});
        CheckTrial(cart.features[4]-wait.features[4]>24 && cart.rawProduction==wait.rawProduction
            && cart.construction.productionAfterWindow==wait.construction.productionAfterWindow,
            "purchased-worker calibration can change predicted old production without any new physical yield");
        state.timeLimitedSearch=true;state.searchDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(240);
        const auto result=Search(state,materialOnly,71);
        CheckTrial(result.actions.empty() && result.trialDiagnostics.complete
            && result.trialDiagnostics.reason==TrialRejectReason::NoMaterialPotential
            && result.trialDiagnostics.potentialCash==0,
            "same-army wait subtraction uses actual simulated production, not cart-dependent calibration gains");
    }
    {
        auto state=TrialArena();state.options.front().unit.body.economic=false;
        state.options.front().unit.biteDps=25;
        Plant source;source.id=77;source.x=800;source.column=8;source.health=500;
        source.assetValue=1000;source.reward=4;
        state.plants={source};
        WindCounter wind;wind.plantID=source.id;wind.committed=true;wind.ready=2;
        state.windCounters={wind};
        auto contrast=state;contrast.counters.clear();
        const auto cart=EvaluateCandidate(contrast,InitialWeights,{{0,0}});
        CheckTrial(cart.features[1]>0 && cart.features[0]==0 && cart.construction.plantAttackAssets==0,
            "temporary damage to a source that still consumes itself is not lasting attack asset loss");
        const auto result=SearchWithin(state,240);
        CheckTrial(result.actions.empty() && result.trialDiagnostics.complete
            && result.trialDiagnostics.reason==TrialRejectReason::NoMaterialPotential
            && result.trialDiagnostics.potentialPlantAssets==0,
            "a committed self-consuming transaction cannot fund a trial with unrecovered transient damage");
    }
    {
        auto state=TrialArena();state.timeLimitedSearch=true;
        state.searchDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        const auto expired=Search(state,InitialWeights,71);
        CheckTrial(expired.actions.empty() && !expired.trialDiagnostics.selected && !expired.trialDiagnostics.complete,
            "an expired budget cannot publish a generated but incomplete trial");
        std::atomic<bool> cancellation=true;state.cancellation=&cancellation;
        bool cancelled=false;
        try { (void)Search(state,InitialWeights,71); } catch(...) { cancelled=true; }
        CheckTrial(cancelled,"cancellation throws before any trial result can be submitted");
    }
    {
        auto state=TrialArena();const auto cached=SearchWithin(state,120);
        state.reuseRawWorlds=false;const auto uncached=SearchWithin(state,360);
        CheckTrial(cached.trialDiagnostics.selected && uncached.trialDiagnostics.selected
            && cached.features==uncached.features && cached.score==uncached.score
            && cached.trialDiagnostics.potentialCash==uncached.trialDiagnostics.potentialCash,
            "isolated robust and deleted-counter cache domains preserve complete trial outcomes");
    }
    std::cout<<"Informational trial bounds, material contrasts, novelty, ordinary priority and cancellation passed\n";
}
