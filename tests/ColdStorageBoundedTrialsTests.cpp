#include "Game/AI/ColdStorageBoundedTrials.h"
#include "Game/AI/ColdStorageSearch.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
void CheckTrial(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED bounded trials: "<<message<<'\n';std::exit(1);}
}
}

/** 真实账户约束与不可重置的实战历史；纯数值反事实不声称已验证自主胜率。 */
void RunColdStorageBoundedTrialsTests() {
    using namespace ColdStorageTrials;
    {
        State state;
        // R3真实付款前钱包230，初始与补给890；额外24付费资产全价计入仍无风险余额。
        const float remaining=ColdStorageSearch::RemainingCapitalRisk(890,254);
        const auto rejected=state.CheckPurchase(71,224,230,48,remaining,24);
        CheckTrial(remaining==0 && rejected.reason==Reason::NoRisk && rejected.cap==0,
            "the recorded 224-ice all-in cannot use depleted cumulative risk");
        CheckTrial(PurchaseCap(1000,48,350,0)==64,"single purchase ceiling applies to a rich wallet");
        CheckTrial(PurchaseCap(1000,48,350,150)==25 && PurchaseCap(1000,48,350,175)==0,
            "all outstanding paid capital consumes the shared allowance before a new trial");
        CheckTrial(PurchaseCap(48,48,350,0)==0 && PurchaseCap(60,48,350,0)==3,
            "recovery reserve is retained even when cumulative risk is available");
        CheckTrial(PurchaseCap(64,48,7.5f,0)==3,
            "fractional risk is rounded down before a real integer payment");
        CheckTrial(state.CheckPurchase(71,65,1000,48,350,0).reason==Reason::OverCap,
            "an expensive minimum viable trial never escalates to all-in");
        CheckTrial(state.CheckPurchase(71,24,400,48,140,0).Allowed(),"a current affordable quote may pass");
        CheckTrial(!state.CheckPurchase(71,24,100,48,0,0).Allowed() && state.Records().empty(),
            "a stale quote grants no authority after the current ledger changes and quotes do not commit history");
    }
    {
        State state;
        const float nan=std::numeric_limits<float>::quiet_NaN();
        const float infinity=std::numeric_limits<float>::infinity();
        for(float invalid:{nan,infinity,-1.0f}) {
            for(int column=0;column<4;++column) {
                std::array<float,4> values{400,48,140,0};values[column]=invalid;
                CheckTrial(PurchaseCap(values[0],values[1],values[2],values[3])==0,
                    "nonfinite or negative capital inputs fail closed");
                CheckTrial(state.CheckPurchase(1,4,values[0],values[1],values[2],values[3]).reason==Reason::InvalidInput,
                    "invalid capital inputs retain a diagnostic reason");
            }
        }
        CheckTrial(state.Availability(0)==Reason::InvalidInput
            && state.CheckPurchase(1,0,400,48,140,0).reason==Reason::InvalidInput,
            "zero identity or free purchase cannot create a trial");
        CheckTrial(state.Commit(1,0,4,0)==Reason::InvalidInput
            && state.Commit(1,1,0,0)==Reason::InvalidInput
            && state.Commit(1,1,4,nan)==Reason::InvalidInput
            && state.Commit(1,1,4,infinity)==Reason::InvalidInput
            && state.Commit(1,1,4,-1)==Reason::InvalidInput && state.Records().empty(),
            "invalid real transaction fields cannot create partial history");
        CheckTrial(state.Commit(1,1,MaximumPurchaseIce+1,0)==Reason::OverCap && state.Records().empty(),
            "the actual transaction ledger also refuses a purchase above the single-cart limit");
    }
    {
        State state;
        CheckTrial(state.Commit(701,3,24,60)==Reason::Allowed,"a paid economic hypothesis enters observation");
        const auto paid=state.Records();
        CheckTrial(state.Commit(701,3,24,60)==Reason::Unresolved && state.Records()==paid,
            "duplicate commit of the same payment changes neither cost nor history");
        CheckTrial(state.Availability(702)==Reason::Unresolved
            && state.Commit(702,4,4,61)==Reason::Unresolved,
            "another hypothesis cannot bypass the unresolved observation episode");
        CheckTrial(state.CreditProduction(2,100)==0 && state.CreditProduction(3,-1)==0
            && state.CreditProduction(3,12)==12,
            "only positive actual production tagged with the paid wave is credited");
        CheckTrial(!state.Settle(true) && state.Records().front().unresolved,
            "live troops, queues or returning transactions prevent premature settlement");
        CheckTrial(state.Settle(false) && !state.Settle(false),"a caller-confirmed empty episode settles exactly once");
        CheckTrial(state.Records().front().cost==24 && state.Records().front().production==12
            && state.Availability(701)==Reason::FailedHypothesis,
            "a losing hypothesis keeps its lifetime cost after troops disappear");
        CheckTrial(state.CheckPurchase(701,4,1000000,48,1000000,0).reason==Reason::FailedHypothesis,
            "a rich wallet or later supply cannot erase a failed hypothesis");
        CheckTrial(state.CreditProduction(3,100)==0 && state.Records().front().production==12,
            "a closed losing episode cannot be reopened by a stale source callback");
        CheckTrial(state.Commit(702,3,4,70)==Reason::WaveAlreadyUsed
            && state.Commit(702,4,4,59)==Reason::BeforePreviousTrial,
            "old waves and backwards game time cannot replay a paid trial");
        CheckTrial(state.Commit(702,4,4,70)==Reason::Allowed && state.Records().size()==2,
            "a distinct hypothesis may use a new paid wave after settlement");
    }
    {
        State state;
        CheckTrial(state.Commit(801,1,24,1)==Reason::Allowed,"successful history begins with actual cost");
        state.CreditProduction(1,24);
        CheckTrial(!state.Records().front().unresolved && state.Availability(802)==Reason::Allowed,
            "actual break-even releases the observation without waiting for any troop death");
        CheckTrial(state.CheckPurchase(801,16,1000,48,350,175).reason==Reason::NoRisk,
            "recovered history still reserves all live paid capital in the original risk cap");
        CheckTrial(state.Availability(801)==Reason::Allowed && state.Commit(801,2,16,2)==Reason::Allowed,
            "actual break-even allows the same hypothesis to be checked again");
        CheckTrial(state.Records().front().cost==40 && state.Records().front().production==24
            && state.Records().front().latestPaidWave==2 && state.Records().front().startedAt==2,
            "repeat success accumulates cost and receipts rather than resetting them");
        CheckTrial(state.Records().front().paidWaves==std::vector<int>{1,2}
            && state.Records().front().unresolved && state.CreditProduction(1,8)==8
            && state.CreditProduction(2,12)==12 && !state.Records().front().unresolved,
            "old and new paid waves keep their genuine income after profitable continuation");
        CheckTrial(state.Commit(801,3,4,3)==Reason::Allowed && !state.Records().front().unresolved,
            "an existing realized surplus that covers the next cart does not create an underwater window");
        CheckTrial(state.Commit(802,4,4,4)==Reason::Allowed && state.CreditProduction(1,6)==6
            && state.Records().front().production==50 && state.Records()[1].production==0
            && state.Availability(801)==Reason::Unresolved,
            "old paid production stays with its original hypothesis and cannot fund another trial's history");
        state.Settle(false);
        CheckTrial(state.Commit(801,5,8,5)==Reason::Allowed,"prior realized profit stays in the cumulative history");
        state.Settle(false);
        CheckTrial(state.Records().front().cost==52 && state.Records().front().production==50
            && state.Availability(801)==Reason::FailedHypothesis,
            "a later loss blocks an initially successful hypothesis when cumulative income no longer pays its cost");
    }
    {
        State state;
        std::vector<Record> full;
        for(std::size_t i=0;i<MaximumRecords;++i)
            full.push_back({100+i,static_cast<int>(i+1),4,i==0 ? 4 : 0,false,static_cast<float>(i+1),
                {static_cast<int>(i+1)}});
        CheckTrial(state.Restore(full) && state.Valid(),"bounded unique numeric history restores");
        CheckTrial(state.Availability(999)==Reason::HistoryFull
            && state.Commit(999,33,4,33)==Reason::HistoryFull && state.Records()==full,
            "full history rejects novelty instead of evicting a failed record");
        CheckTrial(state.Commit(100,33,4,33)==Reason::Allowed && state.Records().size()==MaximumRecords
            && state.Records().front().cost==8 && state.Records()[1].production==0,
            "full history may reuse a successful signature while retaining every failed one");
        const auto before=state.Records();
        auto invalid=before;invalid.push_back({999,34,4,0,false,34,{34}});
        CheckTrial(!state.Restore(invalid) && state.Records()==before,"oversized restore is transactional");
        const auto rejects=[&](const std::vector<Record>& records) {
            State restored;
            return restored.Restore(before) && !restored.Restore(records)
                && restored.Records()==before && restored.Disabled();
        };
        invalid=before;invalid[1].signature=invalid[0].signature;
        CheckTrial(rejects(invalid),"duplicate signatures cannot merge away costs");
        invalid=before;invalid[1].latestPaidWave=invalid[0].latestPaidWave;
        CheckTrial(rejects(invalid),"duplicate wave attribution is rejected");
        invalid=before;invalid[1].paidWaves.insert(invalid[1].paidWaves.begin(),1);
        CheckTrial(rejects(invalid),"a non-latest source wave cannot be attributed to two signatures");
        invalid=before;invalid[1].paidWaves.clear();
        CheckTrial(rejects(invalid),"missing source history cannot silently regain new-wave authority");
        invalid=before;invalid[1].unresolved=true;
        CheckTrial(rejects(invalid),"two unresolved episodes are invalid");
        invalid=before;invalid[1].startedAt=std::numeric_limits<float>::quiet_NaN();
        CheckTrial(rejects(invalid),"nonfinite saved time is rejected");
        invalid=before;invalid[1].startedAt=40;
        CheckTrial(rejects(invalid),"wave and time ordering must agree");
        invalid=before;invalid[1].cost=0;
        CheckTrial(rejects(invalid),"missing actual paid cost is invalid");
        invalid=before;invalid[1].production=-1;
        CheckTrial(rejects(invalid),"negative saved income is invalid");
        CheckTrial(state.Disabled() && !state.Valid() && state.Availability(100)==Reason::InvalidHistory
            && !state.Restore(before) && !state.Settle(false) && state.CreditProduction(33,10)==0,
            "a damaged restore cannot recover purchase authority by silently replacing history");
        State missing;
        missing.Disable();
        CheckTrial(missing.Availability(999)==Reason::InvalidHistory && !missing.Restore({}),
            "a missing required save field can explicitly disable trials without changing the wallet");
    }
    {
        State state;
        const std::array<Record,2> nearLimit{{{901,1,MaximumCounter-20,0,false,1,{1}},
            {902,2,10,10,false,2,{2}}}};
        CheckTrial(state.Restore(nearLimit),"legacy aggregate costs remain intact with only their known latest wave");
        const auto before=state.Records();
        CheckTrial(state.CheckPurchase(902,11,1000,48,350,0).reason==Reason::CounterLimit
            && state.Commit(902,3,11,3)==Reason::CounterLimit && state.Records()==before,
            "the global cost bound includes all old hypotheses before allowing a new payment");
        CheckTrial(state.Commit(902,3,10,3)==Reason::Allowed && state.Valid(),
            "the exact remaining global cost capacity may be paid once");
        CheckTrial(state.Availability(999)==Reason::CounterLimit && state.CreditProduction(3,20)==20
            && state.Availability(902)==Reason::CounterLimit,
            "full global cost closes every signature while still recording real receipts");
        State income;
        const std::array<Record,2> almostFull{{{903,1,8,MaximumCounter-8,false,1,{1}},
            {904,2,4,2,true,2,{2}}}};
        CheckTrial(income.Restore(almostFull)
            && income.CreditProduction(2,std::numeric_limits<int>::max())==6
            && income.Records()[1].production==8 && income.CreditProduction(1,1)==0
            && income.Availability(905)==Reason::CounterLimit && income.Valid(),
            "production feedback saturates globally without changing the external accepted wallet amount");
        auto invalid=std::vector<Record>(almostFull.begin(),almostFull.end());
        invalid[1].production=9;
        State badIncome;CheckTrial(!badIncome.Restore(invalid) && badIncome.Disabled(),
            "individually bounded receipts may not exceed the global saved income bound");
        invalid.assign(nearLimit.begin(),nearLimit.end());invalid[1].cost=21;
        State badCost;CheckTrial(!badCost.Restore(invalid) && badCost.Disabled(),
            "individually bounded costs may not exceed the global saved spending bound");
    }
    {
        State state;
        for(int wave=1;wave<=static_cast<int>(MaximumPaidWaves);++wave) {
            CheckTrial(state.Commit(1001,wave,1,static_cast<float>(wave))==Reason::Allowed
                && state.CreditProduction(wave,1)==1,"paid wave identities append without losing earlier income sources");
        }
        const auto before=state.Records();
        CheckTrial(state.Availability(1001)==Reason::WaveHistoryFull
            && state.Commit(1001,65,1,65)==Reason::WaveHistoryFull && state.Records()==before,
            "a full paid-wave history never evicts an old source to buy again");
        CheckTrial(state.Commit(1002,64,1,65)==Reason::WaveAlreadyUsed
            && state.Commit(1002,65,1,65)==Reason::Allowed && state.CreditProduction(1,2)==2
            && state.Records().front().production==66 && state.Records()[1].production==0,
            "global wave monotonicity and old income ownership survive a full successful record");
    }
    {
        const std::array<Record,1> legacyRecovered{{{1101,7,96,100,true,70,{7}}}};
        State settled;
        CheckTrial(settled.Restore(legacyRecovered) && settled.Availability(1102)==Reason::Allowed
            && settled.Settle(true) && !settled.Records().front().unresolved,
            "a restored recovered legacy window may settle while unrelated troops remain alive");
        State continued;
        CheckTrial(continued.Restore(legacyRecovered) && continued.Commit(1102,8,4,71)==Reason::Allowed
            && continued.Valid() && !continued.Records().front().unresolved
            && continued.Records()[1].unresolved,
            "committing after a legacy recovered window leaves at most one genuinely unresolved record");
    }
    CheckTrial(std::string_view(ReasonName(Reason::FailedHypothesis))=="failed_hypothesis",
        "diagnostic reasons are stable readable names");
}
