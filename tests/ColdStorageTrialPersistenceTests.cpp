#include "Game/AI/ColdStorageTrialPersistence.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void CheckTrialSave(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED trial persistence: "<<message<<'\n';std::exit(1);}
}

/** 同时包含失败、已回本和未决真实付费记录，签名覆盖完整无符号64位范围。 */
ColdStorageTrials::State SavedTrials() {
    using namespace ColdStorageTrials;
    State state;
    CheckTrialSave(state.Commit(std::numeric_limits<std::uint64_t>::max(),1,4,1)==Reason::Allowed,
        "first saved trial records a real payment");
    state.Settle(false);
    CheckTrialSave(state.Commit(std::uint64_t{1}<<63,2,24,3)==Reason::Allowed,
        "second saved trial records a distinct payment");
    state.CreditProduction(2,30);state.Settle(false);
    CheckTrialSave(state.Commit(77,3,16,4)==Reason::Allowed,"last saved trial remains unresolved");
    return state;
}

/** 每次拒绝均从独立有效旧状态开始，验证后部坏字段不会部分替换前面的合法记录。 */
bool RejectsWithoutReplacing(const nlohmann::json& value,int wave=3,float elapsed=4,
    int spent=44,int production=30) {
    using namespace ColdStorageTrials;
    State state;
    state.Commit(900,1,5,0);state.CreditProduction(1,2);state.Settle(false);
    const auto before=state.Records();
    return !Decode(value,state,wave,elapsed,spent,production)
        && state.Disabled() && state.Records()==before && state.Availability(901)==Reason::InvalidHistory;
}
}

/** 新格式必须自洽且受真实总账约束；无缺省放行、推算收益或静默裁剪。 */
void RunColdStorageTrialPersistenceTests() {
    using namespace ColdStorageTrials;
    const auto source=SavedTrials();
    const auto encoded=Encode(source);
    {
        // 经真实JSON文本往返，最大无符号签名不能变成浮点近似值。
        const auto parsed=nlohmann::json::parse(encoded.dump());
        State restored;
        CheckTrialSave(Decode(parsed,restored,3,4,44,30) && restored.Valid()
            && restored.Records()==source.Records() && !restored.Disabled(),
            "truthful failed, successful and unresolved history round-trips exactly");
        CheckTrialSave(restored.Records().front().signature==std::numeric_limits<std::uint64_t>::max()
            && restored.Records().back().unresolved,
            "unsigned identities and outstanding obligations survive serialization");
        restored.Settle(false);
        CheckTrialSave(restored.Availability(source.Records().front().signature)==Reason::FailedHypothesis,
            "loading and later settlement never erase a prior failed experiment");
    }
    {
        auto disabledSource=source;disabledSource.Disable();
        const auto disabledData=Encode(disabledSource);
        State restored;
        CheckTrialSave(Decode(disabledData,restored,3,4,44,30) && restored.Disabled()
            && restored.Records()==source.Records(),
            "a disabled snapshot retains every valid record while keeping trials disabled");
        CheckTrialSave(Decode(disabledData,restored,3,4,44,30) && Encode(restored)==disabledData,
            "an identical disabled snapshot may be checked repeatedly without changing state");
        CheckTrialSave(!Decode(encoded,restored,3,4,44,30) && restored.Disabled()
            && Encode(restored)==disabledData,"later loading cannot reopen an already disabled State");
        auto altered=disabledData;altered["records"][0]["cost"]=3;
        CheckTrialSave(!Decode(altered,restored,3,4,44,30) && Encode(restored)==disabledData,
            "a different disabled snapshot cannot silently replace old failed costs");
        const auto emptyDisabled=nlohmann::json{{"disabled",true},{"records",nlohmann::json::array()}};
        State missing;
        CheckTrialSave(!Decode(nlohmann::json{},missing,0,0,0,0) && missing.Disabled()
            && Decode(emptyDisabled,missing,0,0,0,0),
            "disabled empty history remains disabled and serializable after a missing-format rejection");
    }
    {
        State migrated;
        const auto empty=nlohmann::json{{"disabled",false},{"records",nlohmann::json::array()}};
        CheckTrialSave(Decode(empty,migrated,0,0,0,0) && migrated.Valid() && migrated.Records().empty(),
            "an explicit empty migrated format is valid without inventing old trial outcomes");
        CheckTrialSave(RejectsWithoutReplacing(nlohmann::json{})
            && RejectsWithoutReplacing(nlohmann::json::object()),
            "absent or empty new-format objects never silently become eligible empty history");
        for(const char* field:{"disabled","records"}) {
            auto invalid=encoded;invalid.erase(field);
            CheckTrialSave(RejectsWithoutReplacing(invalid),"every top-level format field is mandatory");
        }
        for(const auto& invalidBool:{nlohmann::json(0),nlohmann::json("false"),nlohmann::json(nullptr)}) {
            auto invalid=encoded;invalid["disabled"]=invalidBool;
            CheckTrialSave(RejectsWithoutReplacing(invalid),"disabled must have the actual JSON boolean type");
        }
        auto invalid=encoded;invalid["records"]=nlohmann::json::object();
        CheckTrialSave(RejectsWithoutReplacing(invalid),"records must be an array");
        invalid=encoded;
        while(invalid["records"].size()<=MaximumRecords) invalid["records"].push_back(encoded["records"][0]);
        CheckTrialSave(RejectsWithoutReplacing(invalid),"oversized history cannot drop old records to fit");
    }
    {
        for(const char* field:{"signature","latestPaidWave","cost","production","unresolved","startedAt","paidWaves"}) {
            auto invalid=encoded;invalid["records"][2].erase(field);
            CheckTrialSave(RejectsWithoutReplacing(invalid),"a missing late record field cannot partially replace state");
        }
        auto invalid=encoded;invalid["records"][2]=nullptr;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"every record must be an object");
        for(const auto& badSignature:{nlohmann::json(0ULL),nlohmann::json(-1),nlohmann::json(77),
            nlohmann::json(77.0),nlohmann::json("77"),nlohmann::json::parse("18446744073709551616")}) {
            invalid=encoded;invalid["records"][2]["signature"]=badSignature;
            CheckTrialSave(RejectsWithoutReplacing(invalid),"invalid, signed, approximate or overflowed signatures are rejected");
        }
        invalid=encoded;invalid["records"][2]["signature"]=invalid["records"][0]["signature"];
        CheckTrialSave(RejectsWithoutReplacing(invalid),"duplicate signatures cannot merge away old outcomes");
        invalid=encoded;invalid["records"][2]["latestPaidWave"]=2;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"duplicate waves cannot claim the same real production twice");
        invalid=encoded;invalid["records"][0]["unresolved"]=true;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"multiple unresolved records violate the controller lifecycle");
        invalid=encoded;invalid["records"][2]["unresolved"]=1;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"unresolved must have the actual JSON boolean type");
        invalid=encoded;invalid["records"][0]["startedAt"]=4;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"later paid waves cannot precede earlier records in game time");
    }
    {
        auto invalid=encoded;invalid["records"][2]["cost"]=16.0;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"floating point costs cannot be truncated into payments");
        invalid=encoded;invalid["records"][2]["cost"]=std::numeric_limits<std::uint64_t>::max();
        CheckTrialSave(RejectsWithoutReplacing(invalid),"integer payment overflow is rejected before conversion");
        invalid=encoded;invalid["records"][2]["cost"]=0;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"a record must have a positive actual payment");
        invalid=encoded;invalid["records"][2]["production"]=-1;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"negative production is not repaired or credited");
        invalid=encoded;invalid["records"][2]["production"]=MaximumCounter+1;
        CheckTrialSave(RejectsWithoutReplacing(invalid,3,4,44,MaximumCounter+30),
            "individual production counters cannot exceed the controller bound");
        invalid=encoded;invalid["records"][2]["latestPaidWave"]="3";
        CheckTrialSave(RejectsWithoutReplacing(invalid),"string wave identities cannot gain payment attribution");
        invalid=encoded;invalid["records"][2]["startedAt"]=true;
        CheckTrialSave(RejectsWithoutReplacing(invalid),"booleans cannot impersonate game time");
        for(double at:{-1.0,4.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            invalid=encoded;invalid["records"][2]["startedAt"]=at;
            CheckTrialSave(RejectsWithoutReplacing(invalid),"negative, future or nonfinite record time is rejected");
        }
    }
    {
        CheckTrialSave(RejectsWithoutReplacing(encoded,2,4,44,30),"trial waves cannot exceed actual paid decisions");
        CheckTrialSave(RejectsWithoutReplacing(encoded,3,3.5f,44,30),"trial time cannot exceed actual elapsed time");
        CheckTrialSave(RejectsWithoutReplacing(encoded,3,4,43,30),"summed trial costs cannot exceed all real spending");
        CheckTrialSave(RejectsWithoutReplacing(encoded,3,4,44,29),"summed trial receipts cannot exceed real worker income");
        CheckTrialSave(RejectsWithoutReplacing(encoded,-1,4,44,30)
            && RejectsWithoutReplacing(encoded,3,-1,44,30)
            && RejectsWithoutReplacing(encoded,3,std::numeric_limits<float>::quiet_NaN(),44,30)
            && RejectsWithoutReplacing(encoded,3,4,-1,30)
            && RejectsWithoutReplacing(encoded,3,4,44,-1),
            "invalid restored global accounts cannot authorize history");
    }
    {
        State overlapping;
        overlapping.Commit(201,1,24,1);overlapping.CreditProduction(1,30);
        CheckTrialSave(overlapping.Commit(201,2,24,2)==Reason::Allowed,
            "one profitable signature can retain two overlapping paid source waves");
        const auto saved=Encode(overlapping);
        State restored;
        CheckTrialSave(Decode(nlohmann::json::parse(saved.dump()),restored,2,2,48,30)
            && restored.Records()==overlapping.Records() && restored.CreditProduction(1,10)==10
            && restored.CreditProduction(2,8)==8 && !restored.Records().front().unresolved,
            "both old and new income sources survive a textual round-trip and genuinely pay the total cost");
        CheckTrialSave(restored.Records().front().production==48
            && restored.Records().front().paidWaves==std::vector<int>{1,2},
            "serialization never replays prior receipts or drops source identities");
    }
    {
        auto invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array();
        CheckTrialSave(RejectsWithoutReplacing(invalid),"an empty paid-wave list is invalid in v31");
        for(const auto& bad:{nlohmann::json(3.0),nlohmann::json("3"),nlohmann::json(true),
            nlohmann::json(-1),nlohmann::json(0),nlohmann::json(4),
            nlohmann::json(std::numeric_limits<std::uint64_t>::max())}) {
            invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array({bad});
            CheckTrialSave(RejectsWithoutReplacing(invalid),
                "paid sources must be exact positive integer waves within the real board boundary");
        }
        invalid=encoded;invalid["records"][2]["paidWaves"]="3";
        CheckTrialSave(RejectsWithoutReplacing(invalid),"a source list cannot be a string");
        invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array({3,3});
        CheckTrialSave(RejectsWithoutReplacing(invalid),"duplicate source waves in one record are invalid");
        invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array({3,2});
        CheckTrialSave(RejectsWithoutReplacing(invalid),"paid sources must remain in strict chronological order");
        invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array({2});
        CheckTrialSave(RejectsWithoutReplacing(invalid),"latestPaidWave must equal the final retained source");
        invalid=encoded;invalid["records"][2]["paidWaves"]=nlohmann::json::array({1,3});
        CheckTrialSave(RejectsWithoutReplacing(invalid),
            "a historical non-latest wave cannot belong to two different saved signatures");
        invalid=encoded;invalid["records"][2]["paidWaves"]=std::vector<int>(MaximumPaidWaves+1,3);
        CheckTrialSave(RejectsWithoutReplacing(invalid),"oversized sources cannot be silently truncated");
    }
    {
        auto legacy=encoded;
        for(auto& record:legacy["records"]) record.erase("paidWaves");
        CheckTrialSave(RejectsWithoutReplacing(legacy),"v31 decoding never guesses missing source fields");
        CheckTrialSave(MigrateLegacyV30(legacy,3,4,44,30) && legacy==encoded,
            "an explicit v30 upgrade records only the already known latest source and preserves the full ledger");
        auto migratedAgain=legacy;
        CheckTrialSave(!MigrateLegacyV30(migratedAgain,3,4,44,30) && migratedAgain==legacy,
            "a v30 document that already claims a source list must not be silently accepted or overwritten");
        auto cumulative=nlohmann::json{{"disabled",false},{"records",nlohmann::json::array({
            {{"signature",701ULL},{"latestPaidWave",9},{"cost",96},{"production",110},
                {"unresolved",true},{"startedAt",90.0}}})}};
        State restored;
        CheckTrialSave(MigrateLegacyV30(cumulative,9,90,96,110)
            && Decode(cumulative,restored,9,90,96,110)
            && restored.Records().front().cost==96 && restored.Records().front().production==110
            && restored.Records().front().paidWaves==std::vector<int>{9}
            && !restored.Records().front().unresolved && !restored.Settle(true)
            && restored.Availability(701)==Reason::Allowed,
            "migration retains large cumulative costs while normalizing only an already recovered unresolved flag");
        State roundTripped;
        CheckTrialSave(Decode(nlohmann::json::parse(Encode(restored).dump()),roundTripped,9,90,96,110)
            && roundTripped.Records()==restored.Records(),
            "the migrated cumulative ledger survives a subsequent v31 textual save and restore exactly");
        auto insufficient=cumulative;
        insufficient["records"][0].erase("paidWaves");
        const auto unmodified=insufficient;
        CheckTrialSave(!MigrateLegacyV30(insufficient,9,90,95,110) && insufficient==unmodified
            && !MigrateLegacyV30(insufficient,9,90,96,109) && insufficient==unmodified,
            "bad real spending or production totals reject legacy migration without a partial source-list write");
        auto disabledRecovered=unmodified;
        disabledRecovered["disabled"]=true;disabledRecovered["records"][0]["unresolved"]=true;
        CheckTrialSave(MigrateLegacyV30(disabledRecovered,9,90,96,110)
            && disabledRecovered["disabled"]==true && disabledRecovered["records"][0]["unresolved"]==false
            && disabledRecovered["records"][0]["cost"]==96 && disabledRecovered["records"][0]["production"]==110,
            "normalizing a recovered legacy flag cannot reopen a disabled ledger or rewrite its finances");
        auto bad=encoded;
        for(auto& record:bad["records"]) record.erase("paidWaves");
        bad["records"][2]["latestPaidWave"]=2;
        const auto before=bad;
        CheckTrialSave(!MigrateLegacyV30(bad,3,4,44,30) && bad==before,
            "a duplicate legacy wave fails migration transactionally without erasing a failed record");
        bad=before;bad["records"][2]["latestPaidWave"]=3;
        bad["records"][2]["paidWaves"]=nlohmann::json::array({1,3});
        const auto forged=bad;
        CheckTrialSave(!MigrateLegacyV30(bad,3,4,44,30) && bad==forged,
            "extra claimed legacy wave identities cannot expand income attribution during migration");
        const auto empty=nlohmann::json{{"disabled",true},{"records",nlohmann::json::array()}};
        auto disabledEmpty=empty;
        CheckTrialSave(MigrateLegacyV30(disabledEmpty,0,0,0,0) && disabledEmpty==empty,
            "empty disabled legacy history remains disabled without fabricated totals");
    }
    {
        State bounded;
        const std::array<Record,2> nearIncome{{{801,1,4,MaximumCounter-5,false,1,{1}},
            {802,2,8,0,true,2,{2}}}};
        CheckTrialSave(bounded.Restore(nearIncome) && bounded.CreditProduction(2,10)==5,
            "accepted production crossing the global feedback ceiling records only its representable remainder");
        const auto full=Encode(bounded);
        State restored;
        CheckTrialSave(Decode(full,restored,2,2,12,MaximumCounter) && restored.Valid()
            && restored.Records()==bounded.Records() && restored.Availability(803)==Reason::CounterLimit
            && restored.CreditProduction(1,10)==0,
            "global feedback exhaustion is a valid persistent stop for every new trial");
        auto overflow=full;overflow["records"][1]["production"]=6;
        State invalidIncome;
        CheckTrialSave(!Decode(overflow,invalidIncome,2,2,12,MaximumCounter*2) && invalidIncome.Disabled(),
            "a larger caller total cannot bypass the State global income ceiling");
        overflow=full;overflow["records"][0]["cost"]=MaximumCounter;
        State invalidCost;
        CheckTrialSave(!Decode(overflow,invalidCost,2,2,MaximumCounter*2,MaximumCounter) && invalidCost.Disabled(),
            "a larger caller total cannot bypass the State global spending ceiling");
    }
}
