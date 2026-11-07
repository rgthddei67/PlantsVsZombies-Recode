#include "Game/Workshop/Workshop.h"
#include "Game/Workshop/WorkshopRoster.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using namespace Workshop;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED commissions: " << message << '\n'; std::exit(1); }
}
void Near(float value, float expected, const char* message) { Check(std::abs(value - expected) < .001f, message); }
void BattleOffer(nlohmann::json& document, Challenge challenge) {
    auto& c = document["state"]["commissions"]["offers"][4];
    c["difficulty"] = DescribeChallenge(challenge).difficulty; c["goal"] = static_cast<int>(CommissionGoal::Battle);
    c["challenge"] = static_cast<int>(challenge); c["target"] = 1; c["remaining"] = 0; c["timeLimit"] = 0;
    c["blueprint"] = BlueprintCount;
}
}

void TestWorkshopCommissions() {
    using namespace Workshop;
    Commissions tasks; tasks.Reset(42, 15);
    tasks.Record(CommissionGoal::Produce, 100000);
    Check(tasks.Accept(0) && tasks.Accept(2) && !tasks.Accept(1), "two simultaneous tasks, no pre-credit");
    Check(tasks.Active()[0].progress == 0, "activities before acceptance never count");
    const auto first = tasks.Active()[0]; tasks.Record(first.goal, first.target);
    const float remaining = tasks.Active()[0].remaining;
    tasks.Tick(1000); const auto beforeRefresh = tasks.Save();
    tasks.OnWaveCompleted(19); Check(tasks.Save() == beforeRefresh, "elapsed time and non-fifth waves cannot refresh");
    tasks.OnWaveCompleted(20);
    Check(tasks.Active()[0].status == CommissionStatus::Ready && tasks.Active()[0].remaining == remaining, "completion freezes deadline and survives refresh");
    Check(tasks.Claim(0) == first.sunReward && tasks.Claim(0) == -1, "manual claim is once-only");
    for (int tier = 0; tier < 2; ++tier) {
        tasks.Reset(); Check(tasks.Accept(tier == 0 ? 0 : 2), "accept timed tier");
        tasks.Tick(CommissionDeadlines[tier] - 1);
        Check(tasks.Active()[0].status == CommissionStatus::Running && tasks.Active()[0].remaining == 1, "last timed second remains playable");
        tasks.Tick(1);
        Check(tasks.Active()[0].status == CommissionStatus::Expired && tasks.ActiveCount() == 0 && tasks.Claim(0) == -1, "expiry frees slot without reward");
    }
    tasks.Reset(); Check(tasks.Accept(0) && tasks.Abandon(0) && !tasks.Accept(0), "abandoned offer cannot be retaken");
    tasks.Reset(99, 15); Check(tasks.Accept(3, 17), "battle accepted for next untouched wave");
    const auto battle = tasks.Active()[0]; tasks.Tick(10000); tasks.OnWaveCompleted(16);
    Check(tasks.Active()[0].status == CommissionStatus::Running && tasks.ChallengeForWave(16) == Challenge::None
        && tasks.ChallengeForWave(17) == battle.challenge, "current assault and elapsed minutes cannot finish future challenge");
    Commissions restored; restored.Load(tasks.Save(), 16); restored.OnWaveCompleted(17);
    Check(restored.Active()[0].status == CommissionStatus::Ready, "battle finishes at exact target clear");
    auto bad = restored.Save(); bad["traits"][0] = 6;
    bool rejected = false; try { tasks.Load(bad, 17); } catch (...) { rejected = true; }
    Check(rejected && tasks.Active()[0].status == CommissionStatus::Running, "corrupt trait load leaves live state intact");

    int noHard = 0, hard = 0, medium = 0, hardBlueprints = 0, mediumBlueprints = 0;
    std::array<int, 3> awards{}; int completed = 0;
    for (int seed = 1; seed <= 20000; ++seed) {
        tasks.Reset(seed, 15); int batchHard = 0, selected = -1;
        for (int i = 0; i < CommissionOfferCount; ++i) {
            const auto& c = tasks.Offers()[i];
            Check(c.difficulty != 2 || c.IsBattle(), "hard task is always a battle");
            if (i < 2) Check(c.difficulty == 0 && !c.IsBattle(), "first two slots are easy business");
            if (i == 2) Check(c.difficulty == 1 && !c.IsBattle(), "third slot is medium business");
            if (i == 3) Check(c.difficulty == 1 && c.IsBattle(), "fourth slot is medium battle");
            if (c.difficulty == 2) { ++hard; ++batchHard; selected = i; if (c.blueprint != Blueprint::Count) ++hardBlueprints; }
            if (c.difficulty == 1) { ++medium; if (c.blueprint != Blueprint::Count) ++mediumBlueprints; }
            if (c.difficulty == 0) Check(c.blueprint == Blueprint::Count, "easy never awards blueprints");
        }
        if (!batchHard) ++noHard;
        if (selected < 0) continue;
        const auto offered = tasks.Offers()[selected];
        Check(offered.sunReward == 900 * (DescribeChallenge(offered.challenge).mechanisms + 1) * 4, "hard sun contains threefold reward and stage scaling");
        Check(tasks.Accept(selected, 16) && !tasks.Accept(3, 16) && tasks.Accept(0, 16), "only one battle alongside business");
        restored.Load(tasks.Save(), 15); tasks.OnWaveCompleted(16); restored.OnWaveCompleted(16);
        Check(tasks.Save() == restored.Save(), "completion draw deterministic across restore");
        const auto prize = tasks.Active()[0].trait;
        if (prize != Trait::Count) ++awards[DescribeTrait(prize).rarity];
        const auto ready = tasks.Save(); restored.Load(ready, 16);
        Check(restored.Save() == ready, "ready reward not rerolled on load");
        Check(tasks.Claim(0) == offered.sunReward, "hard reward claimed exactly as displayed");
        if (prize != Trait::Count) Check(tasks.Traits()[static_cast<int>(prize)] == 1, "trait starts on claim");
        if (offered.blueprint != Blueprint::Count) Check(tasks.Has(offered.blueprint) && tasks.FreeUses(offered.blueprint) == 1, "blueprint and trait can coexist");
        ++completed;
    }
    Check(noHard > 4300 && noHard < 5700 && hard > 19000 && hard < 21000, "hard slots each fifty percent without pity");
    Check(hardBlueprints > hard * .22 && hardBlueprints < hard * .28 && mediumBlueprints > medium * .04 && mediumBlueprints < medium * .06, "blueprint chances retained");
    Check(awards[0] > completed * .22 && awards[0] < completed * .28 && awards[1] > completed * .025 && awards[1] < completed * .055
        && awards[2] > completed * .005 && awards[2] < completed * .017, "absolute common epic legendary drop chances");

    std::array<int, TraitCount> stacks{};
    auto b = AggregateTraits(stacks); Near(b.damage, 1, "zero traits neutral");
    stacks.fill(1); stacks[0] = 5; b = AggregateTraits(stacks);
    Near(b.damage, 4, "ordinary and epic add before legendary doubles"); Near(b.production, 1.7f, "industry adds production");
    Near(b.power, 2.8f, "eternal day doubles summed power"); Near(b.sunshine, 2.2f, "eternal day doubles summed sunlight");

    auto world = std::make_unique<World>(); auto document = world->Save();
    document["state"]["completedWaves"] = 50; document["state"]["energy"] = 0; BattleOffer(document, Challenge::TripleHorde);
    std::string error; Check(world->Load(document, error), "triple horde fixture");
    Check(world->AcceptCommission(4) && world->StartAssault() && world->RemainingSpawns() == MaxAssaultSpawns, "threefold wave expands full schedule to315");
    Check(world->AbandonCommission(0) && world->ChallengeRules().count == 3, "abandon cannot withdraw live modifiers");
    auto checkpoint = world->Save(); Check(world->Load(checkpoint, error) && world->ChallengeRules().count == 3, "abandoned challenge remains in save");
    world->RecordBreach(); world->RecordBreach(); world->RecordBreach();
    Check(world->Retry() && world->Tasks().Active().size() == 1, "retry restores pre-wave accepted challenge");
    Check(world->AbandonCommission(0) && world->ChallengeRules().count == 1, "can abandon before retry starts");

    world->Reset(); document = world->Save(); BattleOffer(document, Challenge::NoBreach);
    Check(world->Load(document, error) && world->AcceptCommission(4) && world->StartAssault(), "flawless fixture");
    world->RecordBreach(); Check(world->Health() == 2 && world->Tasks().Active()[0].status == CommissionStatus::Expired, "flawless fails without extra penalty");
    Check(world->ChallengeRules().noBreach, "failed challenge persists until wave end");

    world->Reset(); document = world->Save(); document["schemaVersion"] = 8;
    document["state"]["completedWaves"] = 14; document["state"].erase("waveChallenge");
    auto& oldTasks = document["state"]["commissions"]; oldTasks.erase("traits"); oldTasks["refreshRemaining"] = 57;
    for (int i = 0; i < CommissionOfferCount; ++i) {
        auto& c = oldTasks["offers"][i];
        c["difficulty"] = i / 2; c["goal"] = 0; c["target"] = 10; c["remaining"] = std::array<int, 3>{300, 480, 720}[i / 2];
        for (const char* key : {"challenge", "targetWave", "timeLimit", "trait", "legacy"}) c.erase(key);
    }
    auto accepted = oldTasks["offers"][0]; accepted["status"] = 1; accepted["remaining"] = 137;
    oldTasks["offers"][0]["status"] = 4; oldTasks["active"].push_back(accepted);
    Check(world->Load(document, error), "v8 task board and active work migrate");
    Check(world->Tasks().Active()[0].remaining == 137 && world->Tasks().Active()[0].legacy
        && world->Tasks().Bonuses().damage == 1, "migration preserves remaining time without retroactive rewards");
    world->Tasks().OnWaveCompleted(15);
    Check(world->Tasks().Active()[0].id == accepted["id"] && !world->Tasks().Offers()[0].legacy, "next fifth-wave refresh replaces only offers");
    world->Tasks().Record(CommissionGoal::Produce, 10); world->RefundEnergy(EnergyLimit);
    Check(!world->ClaimCommission(0) && world->Tasks().Active()[0].status == CommissionStatus::Ready, "insufficient storage retains entire reward");
    const int reward = world->Tasks().Active()[0].sunReward; world->SpendEnergy(static_cast<float>(reward));
    Check(world->ClaimCommission(0) && world->Energy() == EnergyLimit, "reward claims in full after making space");

    auto allOwned = world->Tasks().Save();
    for (int i = 0; i < TraitCount; ++i) allOwned["traits"][i] = TraitLimit(static_cast<Trait>(i));
    tasks.Load(allOwned, 15);
    for (int seed = 0; seed < 50; ++seed) {
        tasks.OnWaveCompleted(15);
        for (int i = 4; i < 6; ++i) if (tasks.Offers()[i].difficulty == 2) {
            Check(tasks.Accept(i, 16), "accept fully collected challenge"); tasks.OnWaveCompleted(16);
            Check(tasks.Active()[0].trait == Trait::Count, "full rarity never rerolls upward or exceeds cap");
            tasks.Claim(0); break;
        }
    }
    std::uint32_t ordinarySeed = 12, eliteSeed = 12;
    const auto ordinary = RollAssaultTypes(31, 105, ordinarySeed), elite = RollAssaultTypes(31, 105, eliteSeed, 3);
    auto countSpecial = [](const auto& types) { int count = 0; for (int type : types) count += type != 0; return count; };
    Check(countSpecial(elite) > countSpecial(ordinary), "elite gathering actually increases unlocked special composition");
    for (int i = 0; i < 105; i += 3) Check(elite[i] == 0, "elite gathering preserves ordinary slots");

    auto normalFactory = std::make_unique<World>(), traitFactory = std::make_unique<World>();
    auto boosted = traitFactory->Save();
    boosted["state"]["commissions"]["traits"][static_cast<int>(Trait::Industry)] = 1;
    boosted["state"]["commissions"]["traits"][static_cast<int>(Trait::EternalDay)] = 1;
    Check(traitFactory->Load(boosted, error), "factory trait fixture restores");
    Near(traitFactory->Power().capacity, normalFactory->Power().capacity * 2.6f, "industry and eternal day affect actual grid capacity");
    Near(traitFactory->SolarProduction(), normalFactory->SolarProduction() * 2, "eternal day affects actual solar rate");
    for (World* factory : {normalFactory.get(), traitFactory.get()}) {
        Check(factory->Build(1, Machine::Grower, 0) && factory->ConnectPower(StarterPowerBox, 1)
            && factory->Build(2, Machine::Feeder, 0) && factory->StartAssault(), "identical real factory lines run");
        factory->Tick(30);
    }
    Check(traitFactory->Produced() >= normalFactory->Produced() * 1.5f && traitFactory->Produced() <= normalFactory->Produced() * 1.7f,
        "industry accelerates actual processing near1.6x without changing recipe cost");
}
