#include "WorkshopCommissions.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace Workshop {
namespace {
constexpr std::array<int, 3> kSunRewards{75, 200, 450}; // 初期三档阳光奖励，后续随经营阶段增长
constexpr int kRewardStageWaves = 5, kRewardStageLimit = 10; // 每守住五波加一档，最多十档增长
constexpr std::array<std::array<int, 3>, 5> kTargets{{
    {60, 180, 420}, {45, 140, 320}, {8, 20, 45}, {8, 20, 45}, {1, 2, 3}
}}; // 生产/配送/回收/击杀为件或只，守波为接取后完整结算波数

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int Integer(const nlohmann::json& j, const char* key, int low, int high) {
    const auto& value = j.at(key);
    Require(value.is_number_integer() && value >= low && value <= high, "委托整数越界");
    return value.get<int>();
}
float Seconds(const nlohmann::json& j, const char* key, float maximum) {
    const auto& value = j.at(key);
    Require(value.is_number(), "委托计时类型错误");
    const double number = value.get<double>();
    Require(std::isfinite(number) && number >= 0 && number <= maximum, "委托计时越界");
    return static_cast<float>(number);
}
nlohmann::json SaveCommission(const Commission& c) {
    return {{"id", c.id}, {"goal", static_cast<int>(c.goal)}, {"status", static_cast<int>(c.status)},
        {"difficulty", c.difficulty}, {"target", c.target}, {"progress", c.progress},
        {"sunReward", c.sunReward}, {"remaining", c.remaining}, {"blueprint", static_cast<int>(c.blueprint)}};
}
Commission ReadCommission(const nlohmann::json& j, int waves) {
    Commission c;
    Require(j.at("id").is_number_integer() && j.at("id") > 0 && j.at("id") < 1000000000000ULL, "委托身份错误");
    c.id = j.at("id").get<std::uint64_t>();
    c.goal = static_cast<CommissionGoal>(Integer(j, "goal", 0, static_cast<int>(CommissionGoal::Count) - 1));
    c.status = static_cast<CommissionStatus>(Integer(j, "status", 0, static_cast<int>(CommissionStatus::Taken)));
    c.difficulty = Integer(j, "difficulty", 0, 2);
    c.target = Integer(j, "target", 1, 1000000); c.progress = Integer(j, "progress", 0, c.target);
    c.sunReward = Integer(j, "sunReward", 0, 1000000);
    c.remaining = Seconds(j, "remaining", CommissionDeadlines[c.difficulty]);
    c.blueprint = static_cast<Blueprint>(Integer(j, "blueprint", 0, BlueprintCount));
    if (c.blueprint != Blueprint::Count)
        Require(c.difficulty > 0 && waves >= BlueprintUnlockWaves[static_cast<int>(c.blueprint)], "蓝图任务尚未解锁");
    Require(c.status != CommissionStatus::Ready || c.progress == c.target, "未达标委托不可领奖");
    Require(c.status != CommissionStatus::Running || (c.progress < c.target && c.remaining > 0), "进行中委托状态错误");
    Require(c.status != CommissionStatus::Expired || (c.remaining == 0 && c.progress < c.target), "过期委托状态错误");
    return c;
}
}

const char* BlueprintKey(Blueprint blueprint) {
    constexpr std::array<const char*, BlueprintCount + 1> keys{"pressure_grower", "heavy_gatling", "blaze_torch", "none"};
    return keys.at(static_cast<int>(blueprint));
}
const char* BlueprintName(Blueprint blueprint) {
    constexpr std::array<const char*, BlueprintCount + 1> names{"高压育豆机", "重装机枪射手", "炽焰树桩", "无"};
    return names.at(static_cast<int>(blueprint));
}
const char* CommissionGoalName(CommissionGoal goal) {
    constexpr std::array<const char*, 5> names{"生产弹药", "送入前线弹仓", "回收残骸", "击杀僵尸", "守住波次"};
    return names.at(static_cast<int>(goal));
}
const char* CommissionDifficultyName(int difficulty) {
    constexpr std::array<const char*, 3> names{"简单", "中等", "困难"};
    return names.at(difficulty);
}
std::uint32_t Commissions::Random() {
    mRandom ^= mRandom << 13; mRandom ^= mRandom >> 17; mRandom ^= mRandom << 5;
    return mRandom;
}
void Commissions::Reset(std::uint32_t seed) {
    mBlueprints = {}; mFreeUses = {}; mActive.clear();
    mRandom = seed ? seed : 1; mNextId = 1;
    Refresh(0);
}
void Commissions::Refresh(int waves) {
    mRefreshRemaining = CommissionRefreshSeconds;
    std::vector<Blueprint> candidates;
    for (int b = 0; b < BlueprintCount; ++b) {
        const auto blueprint = static_cast<Blueprint>(b);
        if (waves < BlueprintUnlockWaves[b] || Has(blueprint)) continue;
        if (std::any_of(mActive.begin(), mActive.end(), [blueprint](const Commission& c) {
            return c.blueprint == blueprint && c.status != CommissionStatus::Expired;
        })) continue;
        candidates.push_back(blueprint);
    }
    const int stage = std::min(kRewardStageLimit, waves / kRewardStageWaves);
    for (int i = 0; i < CommissionOfferCount; ++i) {
        auto& c = mOffers[i]; c = {}; c.id = mNextId++; c.difficulty = i / 2;
        c.goal = static_cast<CommissionGoal>(Random() % static_cast<int>(CommissionGoal::Count));
        // 刚开局尚无回收设备；同档两条避免完全相同的目标。
        if (waves == 0 && c.goal == CommissionGoal::Recycle) c.goal = CommissionGoal::Produce;
        if (i % 2 && c.goal == mOffers[i - 1].goal)
            c.goal = c.goal == CommissionGoal::Deliver ? CommissionGoal::Defeat : CommissionGoal::Deliver;
        c.target = kTargets[static_cast<int>(c.goal)][c.difficulty];
        if (c.goal != CommissionGoal::Survive) c.target += c.target * stage / 2;
        c.sunReward = kSunRewards[c.difficulty] * (stage + 1);
        c.remaining = CommissionDeadlines[c.difficulty];
        if (Random() % 100 < static_cast<unsigned>(BlueprintChances[c.difficulty]) && !candidates.empty()) {
            const auto at = Random() % candidates.size();
            c.blueprint = candidates[at]; candidates.erase(candidates.begin() + at);
        }
    }
}
void Commissions::Tick(float seconds, int waves) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    for (auto& c : mActive) if (c.status == CommissionStatus::Running) {
        c.remaining = std::max(0.0f, c.remaining - seconds);
        if (c.remaining == 0) c.status = CommissionStatus::Expired;
    }
    mRefreshRemaining -= seconds;
    while (mRefreshRemaining <= 0) {
        const float overrun = -mRefreshRemaining;
        Refresh(waves); mRefreshRemaining -= overrun;
    }
}
void Commissions::Record(CommissionGoal goal, int amount) {
    if (amount <= 0) return;
    for (auto& c : mActive) if (c.status == CommissionStatus::Running && c.goal == goal) {
        c.progress += std::min(amount, c.target - c.progress);
        if (c.progress == c.target) c.status = CommissionStatus::Ready;
    }
}
int Commissions::ActiveCount() const {
    return static_cast<int>(std::count_if(mActive.begin(), mActive.end(), [](const Commission& c) { return c.status != CommissionStatus::Expired; }));
}
bool Commissions::Accept(int offer) {
    if (offer < 0 || offer >= CommissionOfferCount || mOffers[offer].status != CommissionStatus::Offered || ActiveCount() >= CommissionActiveLimit) return false;
    std::erase_if(mActive, [](const Commission& c) { return c.status == CommissionStatus::Expired; });
    auto c = mOffers[offer]; c.status = CommissionStatus::Running; c.progress = 0;
    mActive.push_back(c); mOffers[offer].status = CommissionStatus::Taken;
    return true;
}
bool Commissions::Abandon(int active) {
    if (active < 0 || active >= static_cast<int>(mActive.size())) return false;
    mActive.erase(mActive.begin() + active); return true;
}
int Commissions::Claim(int active) {
    if (active < 0 || active >= static_cast<int>(mActive.size()) || mActive[active].status != CommissionStatus::Ready) return -1;
    const auto c = mActive[active];
    if (c.blueprint != Blueprint::Count && !Has(c.blueprint)) {
        mBlueprints[static_cast<int>(c.blueprint)] = true;
        ++mFreeUses[static_cast<int>(c.blueprint)];
    }
    mActive.erase(mActive.begin() + active); return c.sunReward;
}
bool Commissions::Has(Blueprint blueprint) const {
    return blueprint >= Blueprint::PressureGrower && blueprint < Blueprint::Count && mBlueprints[static_cast<int>(blueprint)];
}
int Commissions::FreeUses(Blueprint blueprint) const { return Has(blueprint) ? mFreeUses[static_cast<int>(blueprint)] : 0; }
bool Commissions::ConsumeFreeUse(Blueprint blueprint) {
    if (FreeUses(blueprint) == 0) return false;
    --mFreeUses[static_cast<int>(blueprint)]; return true;
}
nlohmann::json Commissions::Save() const {
    auto offers = nlohmann::json::array(), active = nlohmann::json::array();
    for (const auto& c : mOffers) offers.push_back(SaveCommission(c));
    for (const auto& c : mActive) active.push_back(SaveCommission(c));
    return {{"offers", offers}, {"active", active}, {"blueprints", mBlueprints}, {"freeUses", mFreeUses},
        {"refreshRemaining", mRefreshRemaining}, {"random", mRandom}, {"nextId", mNextId}};
}
void Commissions::Load(const nlohmann::json& j, int waves) {
    Commissions candidate;
    candidate.mRefreshRemaining = Seconds(j, "refreshRemaining", CommissionRefreshSeconds);
    Require(candidate.mRefreshRemaining > 0, "刷新计时必须为正");
    Require(j.at("random").is_number_integer() && j.at("random") > 0 && j.at("random") <= UINT32_MAX, "委托随机状态错误");
    candidate.mRandom = j.at("random").get<std::uint32_t>();
    Require(j.at("nextId").is_number_integer() && j.at("nextId") > 0 && j.at("nextId") < 1000000000000ULL, "委托序号错误");
    candidate.mNextId = j.at("nextId").get<std::uint64_t>();
    Require(j.at("blueprints").is_array() && j.at("blueprints").size() == BlueprintCount
        && j.at("freeUses").is_array() && j.at("freeUses").size() == BlueprintCount, "蓝图库存长度错误");
    for (int b = 0; b < BlueprintCount; ++b) {
        const auto& owned = j.at("blueprints")[b]; const auto& free = j.at("freeUses")[b];
        Require(owned.is_boolean() && free.is_number_integer() && free >= 0 && free <= 1, "蓝图库存类型错误");
        candidate.mBlueprints[b] = owned.get<bool>(); candidate.mFreeUses[b] = free.get<int>();
        Require((!candidate.mBlueprints[b] || waves >= BlueprintUnlockWaves[b]) && (candidate.mBlueprints[b] || candidate.mFreeUses[b] == 0), "蓝图资格或免费次数错误");
    }
    Require(j.at("offers").is_array() && j.at("offers").size() == CommissionOfferCount, "委托列表长度错误");
    Require(j.at("active").is_array() && j.at("active").size() <= CommissionActiveLimit, "已接委托数量错误");
    std::unordered_set<std::uint64_t> ids;
    for (int i = 0; i < CommissionOfferCount; ++i) {
        auto c = ReadCommission(j.at("offers")[i], waves);
        Require(c.id < candidate.mNextId && ids.insert(c.id).second && c.difficulty == i / 2, "委托身份重复或分组错误");
        Require((c.status == CommissionStatus::Offered || c.status == CommissionStatus::Taken) && c.progress == 0
            && c.remaining == CommissionDeadlines[c.difficulty], "待接委托状态错误");
        candidate.mOffers[i] = c;
    }
    ids.clear();
    for (const auto& entry : j.at("active")) {
        auto c = ReadCommission(entry, waves);
        Require(c.id < candidate.mNextId && ids.insert(c.id).second && c.status != CommissionStatus::Offered && c.status != CommissionStatus::Taken, "已接委托身份或状态错误");
        for (const auto& offer : candidate.mOffers) if (offer.id == c.id)
            Require(offer.status == CommissionStatus::Taken && offer.goal == c.goal && offer.target == c.target && offer.difficulty == c.difficulty
                && offer.blueprint == c.blueprint && offer.sunReward == c.sunReward, "已接委托与任务板不一致");
        if (c.blueprint != Blueprint::Count && c.status != CommissionStatus::Expired) {
            Require(!candidate.Has(c.blueprint), "已接蓝图已经获得");
            for (const auto& previous : candidate.mActive) Require(previous.status == CommissionStatus::Expired || previous.blueprint != c.blueprint, "重复蓝图委托");
        }
        candidate.mActive.push_back(c);
    }
    *this = std::move(candidate);
}
}
