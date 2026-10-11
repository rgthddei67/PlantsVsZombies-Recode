#include "Game/AI/ColdStorageObservedPlanning.h"
#include "Game/Board/ColdStorageDeploymentRules.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace ColdStorageSearch;
void CheckObservedIntegration(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED observed integration: "<<message<<'\n';std::exit(1);}
}
/** 合成但保留正式工人费用/生产间隔与灰烬伤害；只用于验证接线，不冒充实战成功。 */
Snapshot ResponseArena() {
    Snapshot s;s.rows=1;s.searchVersion=2;s.capacity=4;s.budget=144;s.recoveryReserve=48;
    s.capitalRiskAllowance=100;s.houseX=-10000;s.netEconomy=true;s.playerSun=10000;s.playerIce=10000;
    s.rowY[0]=150;for(int col=0;col<s.columns;++col) s.cellY[col]=150;
    Option worker;worker.type=75001;worker.cost=IceProduction::WorkerCost;
    worker.unit.body.x=900;worker.unit.body.health=IceProduction::WorkerHealth;
    worker.unit.maximumBody=IceProduction::WorkerHealth;worker.unit.body.purchaseCost=worker.cost;
    worker.unit.body.economic=true;worker.unit.biteDps=0;worker.unit.birthMovementKnown=true;
    worker.unit.responseNominalCapital=worker.cost;worker.unit.responseObjectYKnown=true;
    s.options={worker};
    Counter cherry;cherry.source=0;cherry.observedSourceKey=1;cherry.sunCost=150;cherry.iceCost=20;
    cherry.cellRow=0;cherry.cellColumn=8;cherry.blast.x=840;cherry.blast.damage=1800;
    cherry.blast.usesObjectX=true;cherry.blast.reach.fill(-1);cherry.blast.reach[0]=130;
    cherry.ladderClearRow=0;cherry.ladderClearRadius=1;cherry.windup=1;cherry.recharge=1;
    s.counters={cherry};
    ColdStorageResponse::Profile profile;profile.windowSeconds=24;profile.weight=.65f;
    profile.encounters=8;profile.nonResponses=8;profile.context.readyPaidAreaMask=1;
    profile.context.learnablePaidAreaMask=1;
    profile.context.nearestThreatBand=2;profile.context.economicPresent=true;
    s.observedResponse.profiles={profile};s.observedResponse.revision=19;
    s.observedResponse.sourceExposureSeconds.fill(-1);
    return s;
}
}

/** 正期望可以选择一次有界采购，但原完整最坏世界仍保持亏损，门禁和重新观察能撤销选择。 */
void RunColdStorageObservedIntegrationTests() {
    using namespace ColdStorageSearch;
    const Weights money{0,0,0,0,1,-1,0,0};
    auto state=ResponseArena();PhasedPlan plan;plan.first={{0,0}};
    const auto comparison=CompareObservedPlan(state,money,plan,{},state.observedResponse.profiles[0],false);
    CheckObservedIntegration(comparison.complete,"matching paid response completes both whole worlds");
    CheckObservedIntegration(comparison.diagnostics.robustDelayedWorlds==0
        && comparison.diagnostics.behaviorDelayedWorlds>0,
        "completed response-world delay counters explain causal effect without merging outcome features");
    const float robust=comparison.robust.full.score-comparison.robustPassive.score;
    const float behavior=comparison.behavior.full.score-comparison.behaviorPassive.score;
    CheckObservedIntegration(robust<0 && behavior>0 && comparison.behavior.full.construction.observedDelaySteps>0,
        "only actual delayed legal paid response changes worker survival and realized income");
    const auto result=Search(state,money,1);
    CheckObservedIntegration(result.observedDecision.selected && result.actions.size()==1
        && result.observedDecision.robustDelta<0 && result.observedDecision.combinedDelta>0,
        "whole-world bounded expected increment can select a purchase that robust-only ranking rejects");
    CheckObservedIntegration(result.features==comparison.robust.full.features && result.score==comparison.robust.full.score
        && result.construction.observedDelaySteps==0 && result.features[4]==0 && result.features[5]==24
        && result.observedDecision.plannedBill==24 && result.observedDecision.riskCap==24,
        "selected result preserves worst-world physical features and score with actual purchase bill and risk cap");
    CheckObservedIntegration(state.budget==144 && state.current.empty() && state.counters[0].blast.ready==0,
        "counterfactual production never changes real input wallet, units or cooldown");
    auto expired=state;expired.observedResponse.sourceExposureSeconds[0]=24;
    const auto old=Search(expired,money,1);
    CheckObservedIntegration(!old.observedDecision.selected && old.actions.empty(),
        "already elapsed source opportunity cannot gain another H by background replanning");
    auto capped=state;capped.capitalRiskAllowance=40;
    const auto denied=Search(capped,money,1);
    CheckObservedIntegration(!denied.observedDecision.selected && denied.actions.empty(),
        "positive behavior revenue cannot relax the independent actual risk allowance");
    auto disabled=state;disabled.observedRiskEnabled=false;
    const auto baseline=Search(disabled,money,1);
    CheckObservedIntegration(!baseline.observedDecision.selected && baseline.actions.empty(),
        "explicit ablation retains original robust decision with the same unchanged physical model");
    {
        // 同一合法工人仍有独立正期望；失格旧队不能占据唯一观察候选槽或冒充被缩小后的旧队。
        for(int invalid=0;invalid<6;++invalid) {
            auto stale=state;
            Proposal proposal(2,ProposalMember{state.options[0].type,0,-1,0,0});
            if(invalid==0) { // 总价在预算和风险额度内，但真实剩余名额只容一名。
                stale.budget=240; stale.capacity=64; stale.deploymentCapital=240;
                stale.deploymentOccupied=ColdStorageDeploymentRules::BaseCapacity-1;
            }
            if(invalid==1) proposal.resize(7,proposal.front()); // 整案超当前钱包。
            if(invalid==2) proposal[1].type=75999; // 旧身份已不在当前合法选项中。
            if(invalid==3) {stale.assaultContinuationRelativeTiming=true;proposal[1].delay=std::numeric_limits<float>::quiet_NaN();}
            if(invalid==4) stale.searchRound=3; // 分阶段插槽也必须能回到合法单购。
            // invalid==5 仅风险额度不够：48 冰旧案有钱且有名额，但本次额度只有 24。
            stale.assaultContinuation={proposal};
            const auto fallback=Search(stale,money,1);
            CheckObservedIntegration(fallback.observedDecision.evaluated==1
                && fallback.observedDecision.candidate.size()==1 && fallback.observedDecision.plannedBill==24
                && fallback.observedDecision.candidate.front().type==state.options[0].type
                && !fallback.observedDecision.phased && !fallback.assaultContinuationSelected,
                "invalid capacity, wallet, identity, timing or risk continuation leaves a separate bounded direct candidate fully evaluated");
            CheckObservedIntegration(stale.assaultContinuation.size()==1 && stale.assaultContinuation.front().size()==proposal.size()
                && stale.assaultContinuation.front()[1].type==proposal[1].type
                && (invalid==3 || stale.assaultContinuation.front()[1].delay==proposal[1].delay)
                && stale.options.size()==1 && stale.options.front().cost==24,
                "fallback does not shrink, rewrite, reprice or prepay the stored continuation awaiting ordinary revalidation");
            if(invalid==2 || invalid==4 || invalid==5)
                CheckObservedIntegration(fallback.observedDecision.selected && fallback.actions.size()==1,
                    "the fallback is actually selectable when the independently compared legal purchase wins");
        }
    }
    {
        auto staged=state; staged.budget=240; staged.searchRound=3;
        staged.assaultContinuation={Proposal(3,ProposalMember{state.options[0].type,0,-1,0,0})};
        const auto alternative=Search(staged,money,1);
        CheckObservedIntegration(alternative.observedDecision.evaluated==1 && alternative.observedDecision.phased
            && alternative.observedDecision.plannedBill==48 && alternative.observedDecision.candidate.size()==2
            && std::count_if(alternative.observedDecision.candidate.begin(),alternative.observedDecision.candidate.end(),
                [](const auto& member) {return member.future;})==1
            && !alternative.assaultContinuationSelected && staged.assaultContinuation.front().size()==3,
            "an over-risk continuation also permits the independent legal first-plus-tail comparison on a phased round");
    }
    {
        auto legal=state; legal.assaultContinuationRelativeTiming=true;
        legal.assaultContinuation={{{state.options[0].type,0,-1,0,2}}};
        const auto continued=Search(legal,money,1);
        CheckObservedIntegration(continued.observedDecision.evaluated==1
            && continued.observedDecision.candidate.size()==1 && continued.observedDecision.candidate.front().delay==2
            && continued.assaultDiagnostics.continuationEvaluated>0
            && legal.assaultContinuation.front().front().delay==2,
            "a wholly admissible continuation retains priority and its exact payment-relative timing");
    }
    {
        auto exhausted=state; exhausted.capacity=64; exhausted.deploymentCapital=state.budget;
        exhausted.deploymentOccupied=ColdStorageDeploymentRules::BaseCapacity;
        exhausted.assaultContinuation={{{state.options[0].type,0,-1,0,0}}};
        const auto noSlot=Search(exhausted,money,1);
        CheckObservedIntegration(BuildObservedPurchaseCandidate(exhausted,ObservedPurchaseAllowance(exhausted)).empty()
            && noSlot.observedDecision.evaluated==0 && !noSlot.observedDecision.selected && noSlot.actions.empty(),
            "a full authoritative deployment ledger admits no extra observed candidate despite stale positive snapshot capacity");
    }
    std::cout<<"Observed integration: legal response, real delayed income, mixed ranking, robust preservation and cap/expiry verified\n";
}
