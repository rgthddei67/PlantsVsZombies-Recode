#include "ColdStorageTrialPersistence.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdint>
#include <limits>

namespace ColdStorageTrials {
namespace {
bool Finite(double value) { return (std::bit_cast<std::uint64_t>(value)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }

/** 整数必须在JSON本来的整数类型中精确可表示，不截断小数、负数或超范围无符号数。 */
bool ReadInteger(const nlohmann::json& value,int minimum,int maximum,int& result) {
    if(value.is_number_unsigned()) {
        const auto number=value.get<std::uint64_t>();
        if(number<static_cast<std::uint64_t>(minimum) || number>static_cast<std::uint64_t>(maximum)) return false;
        result=static_cast<int>(number);
        return true;
    }
    if(!value.is_number_integer()) return false;
    const auto number=value.get<std::int64_t>();
    if(number<minimum || number>maximum) return false;
    result=static_cast<int>(number);
    return true;
}

/** 单条历史必须提供全部字段；签名严格保留无符号64位，不接受字符串或浮点近似身份。 */
bool ReadRecord(const nlohmann::json& value,int maxPaidWave,float elapsed,Record& record) {
    if(!value.is_object()) return false;
    for(const char* key:{"signature","latestPaidWave","cost","production","unresolved","startedAt","paidWaves"})
        if(!value.contains(key)) return false;
    if(!value.at("signature").is_number_unsigned() || !value.at("unresolved").is_boolean()
        || !value.at("startedAt").is_number()) return false;
    record.signature=value.at("signature").get<std::uint64_t>();
    if(!record.signature || !ReadInteger(value.at("latestPaidWave"),1,maxPaidWave,record.latestPaidWave)
        || !ReadInteger(value.at("cost"),1,MaximumCounter,record.cost)
        || !ReadInteger(value.at("production"),0,MaximumCounter,record.production)) return false;
    const double at=value.at("startedAt").get<double>();
    if(!Finite(at) || at<0 || at>static_cast<double>(elapsed)) return false;
    record.startedAt=static_cast<float>(at);
    record.unresolved=value.at("unresolved").get<bool>();
    const auto& waves=value.at("paidWaves");
    if(!waves.is_array() || waves.empty() || waves.size()>MaximumPaidWaves) return false;
    for(const auto& entry:waves) {
        int wave=0;
        if(!ReadInteger(entry,1,maxPaidWave,wave)) return false;
        record.paidWaves.push_back(wave);
    }
    return true;
}
}

bool MigrateLegacyV30(nlohmann::json& value,int maxPaidWave,float elapsed,
    int totalSpent,int totalProduction) {
    if(!value.is_object() || !value.contains("records") || !value.at("records").is_array()
        || value.at("records").size()>MaximumRecords) return false;
    auto migrated=value;
    for(auto& entry:migrated["records"]) {
        int wave=0;
        if(!entry.is_object() || entry.contains("paidWaves") || !entry.contains("latestPaidWave")
            || !ReadInteger(entry.at("latestPaidWave"),1,maxPaidWave,wave)) return false;
        entry["paidWaves"]=nlohmann::json::array({wave});
    }
    State checked;
    if(!Decode(migrated,checked,maxPaidWave,elapsed,totalSpent,totalProduction)) return false;
    // 全部原账验证通过后才归一化旧生命周期位；新格式的真实回本不再等待清场。
    for(std::size_t i=0;i<checked.Records().size();++i) {
        const auto& record=checked.Records()[i];
        if(record.production>=record.cost) migrated["records"][i]["unresolved"]=false;
    }
    value.swap(migrated);
    return true;
}

nlohmann::json Encode(const State& state) {
    nlohmann::json value{{"disabled",state.Disabled()},{"records",nlohmann::json::array()}};
    for(const auto& record:state.Records()) value["records"].push_back({
        {"signature",record.signature},{"latestPaidWave",record.latestPaidWave},
        {"cost",record.cost},{"production",record.production},
        {"unresolved",record.unresolved},{"startedAt",record.startedAt},{"paidWaves",record.paidWaves}});
    return value;
}

bool Decode(const nlohmann::json& value,State& state,int maxPaidWave,float elapsed,
    int totalSpent,int totalProduction) {
    const auto reject=[&] {state.Disable();return false;};
    if(maxPaidWave<0 || !Finite(elapsed) || elapsed<0 || totalSpent<0 || totalProduction<0
        || !value.is_object() || !value.contains("disabled") || !value.at("disabled").is_boolean()
        || !value.contains("records") || !value.at("records").is_array()
        || value.at("records").size()>MaximumRecords) return reject();

    // 先读入局部副本并核对全体总额，后面的坏记录不能导致前面的记录被部分采用。
    std::vector<Record> records;
    records.reserve(value.at("records").size());
    std::int64_t spent=0,production=0;
    for(const auto& entry:value.at("records")) {
        Record record;
        if(!ReadRecord(entry,maxPaidWave,elapsed,record)) return reject();
        spent+=record.cost;
        production+=record.production;
        if(spent>totalSpent || production>totalProduction) return reject();
        records.push_back(record);
    }
    State checked;
    if(!checked.Restore(records)) return reject();
    const bool disabled=value.at("disabled").get<bool>();
    if(state.Disabled()) {
        // 同一禁用存档可重复检查，任何不同记录或重新启用请求都不能替换旧失败历史。
        return disabled && state.Records()==records;
    }
    if(!state.Restore(records)) return reject();
    if(disabled) state.Disable();
    return true;
}
}
