#include "WorkshopCommissions.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace Workshop {
namespace {
constexpr std::array<int, 2> kSunRewards{75, 200}; // 两档经营基础阳光，后续随阶段增长
constexpr int kMediumBattleSun = 300; // 中等单机制战斗基础阳光
constexpr std::array<int, 3> kHardBattleSun{1800, 2700, 3600}; // 困难单/双/三机制基础阳光，已包含确认的三倍奖励
constexpr int kRewardStageWaves = 5, kRewardStageLimit = 10; // 每守住五波加一档，最多十档增长
constexpr std::array<std::array<int, 2>, 3> kTargets{{
    {60, 180}, {45, 140}, {8, 20}
}}; // 经营目标为接取后的生产件数、配送件数和回收份数；困难不再使用累计目标

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
        {"sunReward", c.sunReward}, {"remaining", c.remaining}, {"blueprint", static_cast<int>(c.blueprint)},
        {"challenge", static_cast<int>(c.challenge)}, {"targetWave", c.targetWave}, {"timeLimit", c.timeLimit},
        {"trait", static_cast<int>(c.trait)}, {"legacy", c.legacy}};
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
    c.timeLimit = Seconds(j, "timeLimit", 720);
    c.remaining = Seconds(j, "remaining", c.timeLimit);
    c.challenge = static_cast<Challenge>(Integer(j, "challenge", 0, static_cast<int>(Challenge::Count) - 1));
    c.targetWave = Integer(j, "targetWave", 0, 100002);
    c.trait = static_cast<Trait>(Integer(j, "trait", 0, TraitCount));
    Require(j.at("legacy").is_boolean(), "旧委托标记类型错误"); c.legacy = j.at("legacy").get<bool>();
    Require(c.IsBattle() == (c.goal == CommissionGoal::Battle), "战斗委托目标不一致");
    if (c.IsBattle()) {
        const auto& challenge = DescribeChallenge(c.challenge);
        Require(!c.legacy && c.target == 1 && c.timeLimit == 0 && challenge.difficulty == c.difficulty
            && challenge.unlockWave <= waves, "战斗委托配置错误");
        if (c.status == CommissionStatus::Running) Require(c.targetWave > waves && c.targetWave <= waves + 2, "挑战目标波错误");
    } else {
        Require(c.timeLimit > 0 && c.targetWave == 0 && c.trait == Trait::Count, "经营委托配置错误");
        if (!c.legacy) Require(c.difficulty < 2 && c.goal <= CommissionGoal::Recycle
            && c.timeLimit == CommissionDeadlines[c.difficulty], "经营任务分档错误");
    }
    Require(c.trait == Trait::Count || (c.IsBattle() && c.difficulty == 2 && c.status == CommissionStatus::Ready), "未完成困难挑战不可持有词条奖励");
    c.blueprint = static_cast<Blueprint>(Integer(j, "blueprint", 0, BlueprintCount));
    if (c.blueprint != Blueprint::Count)
        Require(c.difficulty > 0 && waves >= BlueprintUnlockWaves[static_cast<int>(c.blueprint)], "蓝图任务尚未解锁");
    Require(c.status != CommissionStatus::Ready || c.progress == c.target, "未达标委托不可领奖");
    Require(c.status != CommissionStatus::Running || (c.progress < c.target && (c.IsBattle() || c.remaining > 0)), "进行中委托状态错误");
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
    constexpr std::array<const char*, 6> names{"生产弹药", "送入前线弹仓", "回收残骸", "击杀僵尸", "守住波次", "战斗挑战"};
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
void Commissions::Reset(std::uint32_t seed, int completedWaves) {
    mBlueprints = {}; mFreeUses = {}; mTraits = {}; mActive.clear();
    mRandom = seed ? seed : 1; mNextId = 1;
    Refresh(completedWaves);
}
void Commissions::Refresh(int waves) {
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
        auto& c = mOffers[i]; c = {}; c.id = mNextId++;
        c.difficulty = i < 2 ? 0 : i < 4 ? 1 : Random() % 100 < HardCommissionChance ? 2 : Random() % 2;
        if (i == 3 || c.difficulty == 2) {
            auto challenges = ChallengeCandidates(c.difficulty, waves);
            // 同批不同困难尽量展示不同机制，不影响是否出现困难的独立抽取。
            if (i == 5 && challenges.size() > 1) std::erase(challenges, mOffers[4].challenge);
            c.challenge = challenges[Random() % challenges.size()]; c.goal = CommissionGoal::Battle;
            c.sunReward = (c.difficulty == 1 ? kMediumBattleSun : kHardBattleSun[DescribeChallenge(c.challenge).mechanisms - 1]) * (stage + 1);
        } else {
            c.goal = static_cast<CommissionGoal>(Random() % (waves == 0 ? 2 : 3));
            if (i == 1 && c.goal == mOffers[0].goal) c.goal = c.goal == CommissionGoal::Deliver ? CommissionGoal::Produce : CommissionGoal::Deliver;
            c.target = kTargets[static_cast<int>(c.goal)][c.difficulty];
            c.target += c.target * stage / 2;
            c.sunReward = kSunRewards[c.difficulty] * (stage + 1);
            c.remaining = c.timeLimit = CommissionDeadlines[c.difficulty];
        }
        if (Random() % 100 < static_cast<unsigned>(BlueprintChances[c.difficulty]) && !candidates.empty()) {
            const auto at = Random() % candidates.size();
            c.blueprint = candidates[at]; candidates.erase(candidates.begin() + at);
        }
    }
}
void Commissions::Tick(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    for (auto& c : mActive) if (c.status == CommissionStatus::Running && !c.IsBattle()) {
        c.remaining = std::max(0.0f, c.remaining - seconds);
        if (c.remaining == 0) c.status = CommissionStatus::Expired;
    }
}
void Commissions::OnWaveCompleted(int completedWaves) {
    for (auto& c : mActive) if (c.status == CommissionStatus::Running && c.IsBattle() && c.targetWave == completedWaves) Complete(c);
    if (completedWaves > 0 && completedWaves % CommissionRefreshWaves == 0) Refresh(completedWaves);
}
/** 在达标边沿锁定结果；领奖和读档不再消费随机数，已满档位不改抽其他档位。 */
void Commissions::Complete(Commission& c) {
    c.progress = c.target; c.status = CommissionStatus::Ready;
    if (!c.IsBattle() || c.difficulty != 2) return;
    int roll = Random() % 100;
    for (int rarity = 0; rarity < 3; ++rarity) {
        if (roll >= TraitChances[rarity]) { roll -= TraitChances[rarity]; continue; }
        std::vector<Trait> candidates;
        for (int i = 0; i < TraitCount; ++i) {
            const auto trait = static_cast<Trait>(i);
            if (DescribeTrait(trait).rarity == rarity && mTraits[i] < TraitLimit(trait)) candidates.push_back(trait);
        }
        if (!candidates.empty()) c.trait = candidates[Random() % candidates.size()];
        break;
    }
}
void Commissions::Record(CommissionGoal goal, int amount) {
    if (amount <= 0 || goal == CommissionGoal::Battle) return;
    for (auto& c : mActive) if (c.status == CommissionStatus::Running && c.goal == goal) {
        c.progress += std::min(amount, c.target - c.progress);
        if (c.progress == c.target) Complete(c);
    }
}
int Commissions::ActiveCount() const {
    return static_cast<int>(std::count_if(mActive.begin(), mActive.end(), [](const Commission& c) { return c.status != CommissionStatus::Expired; }));
}
bool Commissions::CanAccept(int offer) const {
    if (offer < 0 || offer >= CommissionOfferCount || mOffers[offer].status != CommissionStatus::Offered || ActiveCount() >= CommissionActiveLimit) return false;
    return !mOffers[offer].IsBattle() || std::none_of(mActive.begin(), mActive.end(), [](const Commission& c) {
        return c.IsBattle() && c.status != CommissionStatus::Expired;
    });
}
bool Commissions::Accept(int offer, int nextWave) {
    if (!CanAccept(offer) || nextWave < 1 || nextWave > 100002) return false;
    std::erase_if(mActive, [](const Commission& c) { return c.status == CommissionStatus::Expired; });
    auto c = mOffers[offer]; c.status = CommissionStatus::Running; c.progress = 0;
    if (c.IsBattle()) c.targetWave = nextWave;
    mActive.push_back(c); mOffers[offer].status = CommissionStatus::Taken;
    return true;
}
Challenge Commissions::ChallengeForWave(int wave) const {
    for (const auto& c : mActive) if (c.IsBattle() && c.status == CommissionStatus::Running && c.targetWave == wave) return c.challenge;
    return Challenge::None;
}
void Commissions::RecordBreach(int wave) {
    for (auto& c : mActive) if (c.IsBattle() && c.status == CommissionStatus::Running && c.targetWave == wave && DescribeChallenge(c.challenge).noBreach)
        c.status = CommissionStatus::Expired;
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
    if (c.trait != Trait::Count) mTraits[static_cast<int>(c.trait)] = std::min(TraitLimit(c.trait), mTraits[static_cast<int>(c.trait)] + 1);
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
    return {{"offers", offers}, {"active", active}, {"blueprints", mBlueprints}, {"freeUses", mFreeUses}, {"traits", mTraits},
        {"random", mRandom}, {"nextId", mNextId}};
}
void Commissions::Load(const nlohmann::json& j, int waves) {
    Commissions candidate;
    Require(j.at("random").is_number_integer() && j.at("random") > 0 && j.at("random") <= UINT32_MAX, "委托随机状态错误");
    candidate.mRandom = j.at("random").get<std::uint32_t>();
    Require(j.at("nextId").is_number_integer() && j.at("nextId") > 0 && j.at("nextId") < 1000000000000ULL, "委托序号错误");
    candidate.mNextId = j.at("nextId").get<std::uint64_t>();
    Require(j.at("traits").is_array() && j.at("traits").size() == TraitCount, "词条层数长度错误");
    for (int i = 0; i < TraitCount; ++i) {
        const auto& value = j.at("traits")[i];
        Require(value.is_number_integer() && value >= 0 && value <= TraitLimit(static_cast<Trait>(i)), "词条层数越界");
        candidate.mTraits[i] = value.get<int>();
    }
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
        Require(c.id < candidate.mNextId && ids.insert(c.id).second, "委托身份重复");
        Require((c.status == CommissionStatus::Offered || c.status == CommissionStatus::Taken) && c.progress == 0
            && c.remaining == c.timeLimit && c.targetWave == 0 && c.trait == Trait::Count, "待接委托状态错误");
        candidate.mOffers[i] = c;
    }
    ids.clear();
    for (const auto& entry : j.at("active")) {
        auto c = ReadCommission(entry, waves);
        Require(c.id < candidate.mNextId && ids.insert(c.id).second && c.status != CommissionStatus::Offered && c.status != CommissionStatus::Taken, "已接委托身份或状态错误");
        for (const auto& offer : candidate.mOffers) if (offer.id == c.id)
            Require(offer.status == CommissionStatus::Taken && offer.goal == c.goal && offer.target == c.target && offer.difficulty == c.difficulty
                && offer.blueprint == c.blueprint && offer.sunReward == c.sunReward && offer.timeLimit == c.timeLimit
                && offer.challenge == c.challenge && offer.legacy == c.legacy, "已接委托与任务板不一致");
        if (c.IsBattle() && c.status != CommissionStatus::Expired)
            for (const auto& previous : candidate.mActive) Require(!previous.IsBattle() || previous.status == CommissionStatus::Expired, "同时只能接一条战斗委托");
        if (c.trait != Trait::Count) Require(candidate.mTraits[static_cast<int>(c.trait)] < TraitLimit(c.trait), "待领取词条已满层");
        if (c.blueprint != Blueprint::Count && c.status != CommissionStatus::Expired) {
            Require(!candidate.Has(c.blueprint), "已接蓝图已经获得");
            for (const auto& previous : candidate.mActive) Require(previous.status == CommissionStatus::Expired || previous.blueprint != c.blueprint, "重复蓝图委托");
        }
        candidate.mActive.push_back(c);
    }
    *this = std::move(candidate);
}
}
