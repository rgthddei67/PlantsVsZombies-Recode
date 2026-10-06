#include "Game/Board/ColdStorageDeploymentRules.h"
#include "Game/AI/ColdStorageSearch.h"
#include "Game/AI/ColdStorageFormationSeeds.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/Zombie/CrystalDrummerRules.h"
#include "Game/Zombie/ColdChainGuardRules.h"
#include "Game/Zombie/AuroraPriestRules.h"
#include "Game/Zombie/AdaptiveHelmetRules.h"
#include "Game/Zombie/PolarClockRules.h"
#include "Game/Zombie/CatapultRules.h"
#include "Game/Zombie/ZombieBirthVitalsRules.h"
#include "Game/AI/ColdStoragePlanner.h"
#include "Game/AI/ColdStoragePlanEvaluator.h"
#include "Game/Plant/IceStorageNutRules.h"
#include "Game/Plant/EchoWaveRules.h"
#include <chrono>
#include <algorithm>
#include <thread>
#include <limits>
#include "Game/AI/ColdStorageStrategy.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void RunColdStorageHealerForecastTests();
void RunColdStorageLadderForecastTests();
void RunColdStorageJackBalloonForecastTests();
void RunColdStorageThunderTimingTests();
void RunColdStorageDiggerForecastTests();
void RunColdStorageDiggerIntegrationTests();
void RunColdStorageCapitalUtilityTests();
void RunColdStorageCounterBudgetTests();
void RunColdStorageExtendedIncomeTests();
void RunColdStorageSiegePreparationTests();
void RunColdStorageMultiPrecisionTests();
void RunColdStorageDeploymentTransactionTests();
void RunColdStoragePrecisionUnlockForecastTests();
void RunColdStorageDeepCompositionTests();
void RunColdStorageAssaultExplorationTests();

/** Deterministic counterfactuals: triggering splash, existing targets, spacing and paid arrivals. */
int main()
{
	RunColdStorageHealerForecastTests();
	RunColdStorageLadderForecastTests();
	RunColdStorageJackBalloonForecastTests();
	RunColdStorageThunderTimingTests();
	RunColdStorageDiggerForecastTests();
	RunColdStorageDiggerIntegrationTests();
	RunColdStorageCapitalUtilityTests();
	RunColdStorageCounterBudgetTests();
	RunColdStorageExtendedIncomeTests();
	RunColdStorageSiegePreparationTests();
	RunColdStorageMultiPrecisionTests();
	RunColdStorageDeploymentTransactionTests();
	RunColdStoragePrecisionUnlockForecastTests();
	RunColdStorageDeepCompositionTests();
	RunColdStorageAssaultExplorationTests();
	auto check = [](bool condition, const char* name) {
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; std::exit(1); }
	};
    {
        using namespace ColdStorageSearch;
        Snapshot rain;rain.rows=5;rain.columns=9;rain.gridLeft=100;rain.cellWidth=100;rain.cellHeight=100;
        rain.rightEdge=1100;rain.weatherStation=true;rain.rainPlant.fill(1);rain.rainZombie.fill(1);
        Plant wall;wall.id=1;wall.row=2;wall.column=3;wall.x=450;wall.health=wall.maximumHealth=10000;wall.reward=100;
        rain.plants={wall};
        Unit mortar;mortar.id=2;mortar.body.row=2;mortar.body.x=850;mortar.body.health=1800;
        mortar.maximumBody=1800;mortar.floodMortar=true;mortar.body.speed=10;mortar.floodStopHealth=600;
        rain.current={mortar};
        const float clear=Evaluate(rain,{})[1];
        rain.station.controls[0].value=3;
        const float heavy=Evaluate(rain,{})[1];
        check(clear>0 && heavy>clear*2,"heavy rain increases real mortar forecast pressure");
        rain.plants[0].airborneDefenseRadius=1;
        check(Evaluate(rain,{})[1]==0,"umbrella removes forecast mortar damage");
        rain.current.clear();rain.plants[0].airborneDefenseRadius=-1;
        rain.floodShots={{2,3,.5f,250}};
        check(Evaluate(rain,{})[1]>0,"launched water bomb survives without source in snapshot");
    }
	for(int type=0;type<static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES);++type) {
		const auto value=ZombieBirthVitalsRules::Get(static_cast<ZombieType>(type));
		check(value.known && value.body>0 && value.helm>=0 && value.shield>=0,
			"every implemented type must explicitly declare its actual birth layers");
	}
	const auto scaledBirth=ZombieBirthVitalsRules::Scaled(ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_PINK_FOOTBALL),1.1,.75);
	check(scaledBirth.body==242 && scaledBirth.helm==743 && scaledBirth.bite==40,
		"birth scaling rounds body and armor separately without changing attack damage");
	check(ZombieBirthVitalsRules::ScaleHealth(15000,1e20)==std::numeric_limits<int>::max(),
		"shared entity and forecast health scaling saturates before narrowing to integer");
	{
	using namespace ColdStorageSearch;
	Snapshot dance; dance.houseX=-10000;
	Unit leader; leader.id=1; leader.body.row=2; leader.body.x=900; leader.body.health=DancerRules::BodyHealth;
	leader.dance.leader=true; leader.dance.remaining=1; leader.dance.snapSeconds=1;
	leader.dance.stopHealth=DancerRules::BodyHealth/3.0f;
	dance.current={leader};
	dance.dancerBackup.body.health=DancerRules::BackupBodyHealth;
	dance.dancerBackup.dance.backup=true; dance.dancerBackup.dance.phase=DancerRules::Forecast::Phase::HOLD;
	dance.dancerBackup.dance.remaining=DancerRules::HoldSeconds;
	ConstructionStats stats;
	check(Evaluate(dance,{},&stats)[5]==0 && stats.dancerSummons==4,
		"dancer creates four independent free followers without inventing purchase costs");
	check(dance.current[0].dance.remaining==1 && dance.current[0].dance.followers[0]==0,
		"numeric summoning cannot mutate the live portrait or its follower identities");
	auto edge=dance; edge.current[0].body.row=4; Evaluate(edge,{},&stats);
	check(stats.dancerSummons==3,"last-row dancer cannot summon into an absent sixth lane");
	auto headless=dance; headless.current[0].body.health=160; Evaluate(headless,{},&stats);
	check(stats.dancerSummons==0,"headless dancer cannot start or complete another summon");
	Counter fatal; fatal.blast.committed=true; fatal.blast.ready=1; fatal.blast.damage=1800;
	fatal.blast.x=900; fatal.blast.reach.fill(-1); fatal.blast.reach[2]=0;
	auto before=dance; before.counters={fatal}; Evaluate(before,{},&stats);
	check(stats.dancerSummons==0,"leader death before snap completion cancels the uncommitted summon");
	auto after=before; after.current[0].dance.phase=DancerRules::Forecast::Phase::SNAP;
	after.current[0].dance.remaining=0;
	Plant target; target.id=10; target.row=1; target.x=900; target.health=100; target.reward=10;
	after.plants={target};
	check(Evaluate(after,{},&stats)[0]==10 && stats.dancerSummons==4,
		"already committed free followers continue attacking after their leader dies");
	Snapshot rising; rising.houseX=-10000; rising.current={dance.dancerBackup};
	rising.current[0].body.x=900; rising.current[0].dance.walkSpeed=100;
	target.row=0; target.x=500; rising.plants={target};
	check(Evaluate(rising,{})[0]==10,"purchased backup resumes walking after its rise instead of remaining stationary forever");
	rising.current[0].dance.backup=false;
	check(Evaluate(rising,{})[0]==0,"stationary body alone cannot invent the later attack");
	}
	using namespace ColdStorageStrategy;
	SplashField field;
	field.directDps[1] = field.melonDps[1] = 27;
	field.plantX[1] = 300;
	SplashUnit guard;
	guard.row = 0; guard.x = 1080; guard.speed = 20; guard.health = 270; guard.value = 40;
	SplashUnit worker = guard;
	worker.x = 1120; worker.health = 500; worker.value = 150;
	SplashUnit bucket = guard;
	bucket.row = 1; bucket.x = 1140; bucket.health = 1500; bucket.value = 8; bucket.spawnAt = 1;

	{
		using namespace IceStorageNutRules;
		Protection protection;
		protection.RecordDamage(600); protection.Advance(1.0f); protection.RecordDamage(399);
		check(protection.invulnerable==0 && protection.RecentDamage()==999,"subthreshold damage does not trigger protection");
		protection.Advance(0.6f); protection.RecordDamage(600);
		check(protection.invulnerable==0 && protection.RecentDamage()==999,"rolling window expires each hit independently");
		protection.RecordDamage(1);
		check(protection.invulnerable==5 && protection.cooldown==0,"threshold triggers five seconds with no concurrent cooldown");
		protection.Advance(4); protection.RecordDamage(1000);
		check(protection.invulnerable==1 && protection.hits.empty(),"immune damage cannot refresh protection or accumulate");
		protection.Advance(1.25f);
		check(protection.invulnerable==0 && protection.cooldown==7.25f,"cooldown starts after immunity including partial step");
		protection.RecordDamage(1000); protection.Advance(7.25f);
		check(protection.cooldown==0 && protection.hits.empty(),"cooldown damage is discarded on becoming ready");
		protection.RecordDamage(999);
		check(protection.invulnerable==0,"fresh ready window requires the full threshold again");
		protection.RecordDamage(1);
		check(protection.invulnerable==5,"protection can trigger again after cooldown");
	}
	const float ordinary = ForecastSplashExternality(field, {guard, worker}, {bucket});
	field.melonSlowDuty[1] = field.directSlowDuty[1] = 1;
	field.slowDuration[1] = 7.5f;
	const float winter = ForecastSplashExternality(field, {guard, worker}, {bucket});
	check(ordinary > 0 && winter > ordinary, "winter splash costs include lost progress");
	SplashUnit ahead = bucket;
	ahead.x = 650; ahead.spawnAt = 0;
	check(ForecastSplashExternality(field, {guard, worker, ahead}, {bucket}) == 0, "existing nearer target keeps splash away");
	SplashUnit alreadyTargeted = ahead;
	alreadyTargeted.x = 1100;
	check(ForecastSplashExternality(field, {guard, worker, alreadyTargeted}, {bucket}) == 0,
		"splash already hitting the same allies is not charged again");
	ahead.row = 0;
	check(ForecastSplashExternality(field, {ahead}, {bucket}) == 0, "far forward ally is outside splash");
	bucket.spawnAt = 25;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) == 0, "later arrival does not immediately draw fire");
	bucket.spawnAt = 1;
	guard.spawnAt = worker.spawnAt = 1;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) > 0, "paid waiting allies count after spawning");
	field.melonDps[1] = 0;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) == 0, "single lane fire has no melon collateral");
	std::cout << "Splash counterfactuals passed; melon=" << ordinary << ", winter=" << winter << '\n';

	BlastThreat cherry;
	cherry.x = 850; cherry.damage = 1800; cherry.reach = {130, 130, 130, -1, -1, -1};
	SplashUnit football;
	football.row = 1; football.x = 850; football.health = 1700; football.purchaseCost = 16;
	std::vector<SplashUnit> cluster(8, football);
	std::array<float, 6> noSlow{};
	check(ForecastBlastRisk(cluster, 0, {cherry}, noSlow, 1100).loss == 128,
		"all eight football investments lost, no three-unit damage cap");
	cluster.resize(3); cluster[0].row = 0; cluster[2].row = 2;
	check(ForecastBlastRisk(cluster, 0, {cherry}, noSlow, 1100).loss == 48,
		"three neighboring rows still share one blast");
	cluster[2].row = 4;
	check(ForecastBlastRisk(cluster, 0, {cherry}, noSlow, 1100).loss == 32, "distant row lies outside blast");
	cherry.committed = true; cherry.ready = 2;
	football.spawnAt = 5;
	check(ForecastBlastRisk({football}, 0, {cherry}, noSlow, 1100).loss == 0, "spawn after committed blast is safe");
	cherry.committed = false;
	check(ForecastBlastRisk({football}, 0, {cherry}, noSlow, 1100).loss == 16, "holding a ready bomb can catch later arrivals");
	cherry.ready = 40;
	check(ForecastBlastRisk({football}, 0, {cherry}, noSlow, 1100).loss == 0, "long cooldown leaves an attack window");
	cherry.ready = 0;
	football.x = 1140; football.speed = 40; football.spawnAt = 0;
	SplashUnit later = football;
	later.spawnAt = 10;
	check(ForecastBlastRisk({football, later}, 0, {cherry}, noSlow, 1100).loss == 16,
		"moving echelons can leave the same blast area at different times");
	football.stopX = later.stopX = 850;
	check(ForecastBlastRisk({football, later}, 0, {cherry}, noSlow, 1100).loss == 32,
		"a blocking nut makes delayed echelons bunch up again");

	Raid raid;
	raid.spawnX = 850; raid.targets.push_back({800, 4000, 0, 20});
	raid.members.assign(4, {1700, 40, 16, 0});
	raid.blastTime = 0; raid.blastDamage.assign(4, 1800);
	check(ForecastRaid(raid).cellsBroken == 0, "bomb-killed footballs cannot finish breaking a nut");
	raid.members[0] = raid.members[1] = {3000, 20, 16, 4};
	check(ForecastRaid(raid).cellsBroken == 1, "surviving breachers can finish after followers die");
	raid.members[0].health = raid.members[1].health = 1700;
	raid.blastDamage[2] = raid.blastDamage[3] = 0;
	check(ForecastRaid(raid).cellsBroken == 0, "living followers cannot inherit a killed breacher's smash");
	std::cout << "Blast and breaching formation counterfactuals passed\n";

	ColdStorageSearch::Snapshot search;
	search.budget = 40; search.capacity = 4;
	ColdStorageSearch::Option producer;
	producer.type = 1; producer.cost = 24;
	producer.unit.body.x = 1100; producer.unit.body.row = 0; producer.unit.body.speed = 20;
	producer.unit.body.health = 500; producer.unit.body.purchaseCost = 24; producer.unit.body.economic = true;
	ColdStorageSearch::Option tank = producer;
	tank.type = 2; tank.cost = 16; tank.unit.body.health = 3000;
	tank.unit.body.purchaseCost = 16; tank.unit.body.economic = false; tank.unit.body.smashSeconds = 4;
	search.options = {producer,tank};
	ColdStorageSearch::Plant gun;
	gun.x = 800; gun.row = 0; gun.health = 4000; gun.dps = 150; gun.reward = 20;
	search.plants.push_back(gun);
	const auto naked = ColdStorageSearch::Evaluate(search, {{0,0}});
	const auto protectedIncome = ColdStorageSearch::Evaluate(search, {{1,0},{0,8}});
	check(protectedIncome[4] > naked[4], "joint plan values actual forward protection of worker");
	ColdStorageSearch::Result recovery;
	recovery.actions = {{1,0},{0,8}};
	recovery.baselineFeatures = ColdStorageSearch::Evaluate(search, {});
	recovery.features = protectedIncome;
	check(!ColdStorageSearch::ShouldRegroup(recovery,40,48), "low inventory still funds productive escort and worker combinations");
	// 现有工人在另一行赚钱，不能掩盖新巨人冲入火力后的纯亏损。
	auto doomed = search;
	doomed.plants[0].dps = 10000;
	auto safeWorker = producer.unit;
	safeWorker.body.row = 1; safeWorker.body.speed = 0;
	doomed.current.push_back(safeWorker);
	recovery.actions = {{1,0}};
	recovery.baselineFeatures = ColdStorageSearch::Evaluate(doomed, {});
	recovery.features = ColdStorageSearch::Evaluate(doomed,recovery.actions);
	check(ColdStorageSearch::ShouldRegroup(recovery,47,48), "existing income cannot justify a doomed low-inventory reinforcement");
	check(!ColdStorageSearch::ShouldRegroup(recovery,48,48), "sufficient reserves leave investment choice to the search");
	doomed.plants.clear();
	recovery.baselineFeatures = ColdStorageSearch::Evaluate(doomed, {});
	recovery.features = ColdStorageSearch::Evaluate(doomed,recovery.actions);
	check(!ColdStorageSearch::ShouldRegroup(recovery,16,48), "affordable house breach is not delayed for economy");
	doomed.plants = {gun}; doomed.plants[0].dps = 10000;
	doomed.budget = 40; doomed.capacity = 1; doomed.recoveryReserve = 48; doomed.allowWait = false;
	doomed.options = {tank,producer}; doomed.options[0].preference[0] = 500;
	doomed.options[1].row = 1; doomed.options[1].unit.body.row = 1;
	const auto rebuilt = ColdStorageSearch::Search(doomed,ColdStorageSearch::InitialWeights,123);
	check(rebuilt.actions.size() == 1 && rebuilt.actions[0].option == 1,
		"regroup filter retains profitable alternatives instead of rejecting only the winning wasteful plan");
	const auto chosen = ColdStorageSearch::Search(search, ColdStorageSearch::InitialWeights, 123);
	const auto repeated = ColdStorageSearch::Search(search, ColdStorageSearch::InitialWeights, 123);
	check(chosen.baselineFeatures == ColdStorageSearch::Evaluate(search, {}), "decision records the no-purchase counterfactual");
	float explained = chosen.preferenceScore;
	for (int i = 0; i < ColdStorageSearch::FeatureCount; ++i) explained += chosen.features[i] * ColdStorageSearch::InitialWeights[i];
	check(std::abs(explained-chosen.score)<0.01f, "diagnostic contributions reconstruct the decision score");
	check(chosen.score == repeated.score && chosen.actions.size() == repeated.actions.size(), "local search reproducibility");
	float paid = 0;
	for (const auto& action : chosen.actions) paid += search.options[action.option].cost;
	check(paid <= search.budget && chosen.actions.size() <= 4, "search cannot spend forecast income or exceed capacity");
	search.capacity = 0;
	check(ColdStorageSearch::Search(search, ColdStorageSearch::InitialWeights, 123).actions.empty(), "full board can only wait");
	auto invalid = ColdStorageSearch::InitialWeights; invalid[0] = std::numeric_limits<float>::infinity();
	check(!ColdStorageSearch::ValidWeights(invalid), "nonfinite learned artifact rejected");
	std::cout << "Free plan search contracts passed\n";

	// 两张不同的清场牌可以先后消耗同一支重甲队；同一卡的候选格位不能复制次数。
	ColdStorageSearch::Snapshot countered;
	countered.houseX = 0; countered.playerSun = 300; countered.playerIce = 200;
	ColdStorageSearch::Unit heavy;
	heavy.body.x = 800; heavy.body.health = 3000; heavy.body.purchaseCost = 16;
	countered.current.assign(4,heavy);
	ColdStorageSearch::Counter response;
	response.blast.x = 800; response.blast.reach.fill(-1); response.blast.reach[0] = 130;
	response.blast.damage = 1800; response.recharge = 100; response.sunCost = 125; response.iceCost = 90;
	countered.counters = {response,response};
	const auto singleCounter = ColdStorageSearch::Evaluate(countered,{});
	countered.counters[1].source = 1;
	const auto twoCounters = ColdStorageSearch::Evaluate(countered,{});
	check(twoCounters[6] > singleCounter[6] && twoCounters[3] < singleCounter[3],
		"independent response cards hit survivors; alternative cells cannot duplicate a card");
	countered.playerSun = 125;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == singleCounter[6], "counter sequence shares player sun budget");
	countered.playerSun = 300; countered.playerIce = 90;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == singleCounter[6], "counter sequence shares player ice budget");
	countered.playerIce = 200; countered.counters[1].blast.ready = 70;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == singleCounter[6], "cooldown beyond horizon cannot clear reinforcements");
	countered.current.assign(1,heavy); countered.current[0].body.health = 1700;
	countered.counters.resize(1); countered.counters[0].targeted = true; countered.counters[0].blast.x = 710;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == 16, "squash can counter one valuable nearby target");
	countered.counters[0].blast.x = 500;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == 0, "squash cannot target across half the lawn");
	countered.counters[0].blast.x = 710;
	countered.counters[0].windup = 1.7f;
	heavy.body.health = 1700; heavy.body.speed = 43;
	countered.current.assign(6,heavy);
	check(ColdStorageSearch::Evaluate(countered,{})[6] == 96,
		"squash refreshes its target before jumping and catches a moving football cluster");
	countered.counters[0].targeted = false;
	countered.counters[0].blast.x = 800;
	countered.counters[0].blast.reach[0] = 50;
	countered.counters[0].blast.ready = 1.7f;
	countered.counters[0].blast.committed = true;
	check(ColdStorageSearch::Evaluate(countered,{})[6] == 0,
		"an already committed fixed blast cannot chase targets that move out of its area");
	// 新生工人的首次计时也必须走共享规则，防止调参只更新后续批次。
	ColdStorageSearch::Snapshot production;
	production.current.push_back(producer.unit);
	production.current.back().body.speed = 0;
	const auto predicted = ColdStorageSearch::Evaluate(production, {});
	check(predicted[4] == IceProduction::Forecast(IceProduction::Interval, IceProduction::InitialYield, 60),
		"search and live production share the complete first-minute schedule");
	auto headlessProduction = production;
	headlessProduction.current[0].body.health = IceProduction::WorkerHealth/3;
	check(ColdStorageSearch::Evaluate(headlessProduction,{})[4] == 0,"head-loss health threshold stops predicted income before body death");
	headlessProduction.current[0].body.health += 1;
	check(ColdStorageSearch::Evaluate(headlessProduction,{})[4] == predicted[4],"worker above head-loss threshold still produces");
	// 单行高威胁直击可以越过普通前排，站在工人前面不等于能挡住主伤害。
	ColdStorageSearch::Snapshot dawn = production;
	ColdStorageSearch::Plant sourcePlant; sourcePlant.id = 1; sourcePlant.health = 500; sourcePlant.edible = false;
	dawn.plants.push_back(sourcePlant);
	dawn.rowStrikes.push_back({1,0,100,1400,300,120});
	auto weakEscort = dawn.current[0]; weakEscort.body.economic = false;
	weakEscort.body.health = 270; weakEscort.body.purchaseCost = 4; weakEscort.body.x -= 200;
	dawn.current.push_back(weakEscort);
	check(ColdStorageSearch::Evaluate(dawn,{})[4] == 0,"normal in front does not shield higher-health worker from charged row strike");
	dawn.current[1].body.health = 3000;
	const auto separatedEscort = ColdStorageSearch::Evaluate(dawn,{});
	check(separatedEscort[4] == predicted[4],"strong separated escort can absorb main strike for worker");
	dawn.current[1].body.x = dawn.current[0].body.x - 30;
	check(ColdStorageSearch::Evaluate(dawn,{})[3] < separatedEscort[3],"nearby protected worker still takes splash");
	dawn.current[1].body.x = dawn.current[0].body.x - 200; dawn.rowStrikes[0].recharge = 20;
	check(ColdStorageSearch::Evaluate(dawn,{})[4] < predicted[4],"recharging strike eventually stops worker income after the escort weakens");
	dawn.rowStrikes[0].recharge = 100;
	dawn.current.resize(1); dawn.rowStrikes[0].ready = 20;
	const auto delayedStrike = ColdStorageSearch::Evaluate(dawn,{});
	check(delayedStrike[4] > 0 && delayedStrike[4] < predicted[4],"spent active ability leaves a finite production window");
	dawn.plants[0].health = 0;
	check(ColdStorageSearch::Evaluate(dawn,{})[4] == predicted[4],"destroyed ability source cannot cast later");
	dawn.plants[0].health = 500; dawn.rowStrikes[0].ready = 0; dawn.rowStrikes[0].recharge = 100;
	dawn.current.push_back(dawn.current[0]); dawn.current[1].body.row = 1; dawn.current[1].body.spawnAt = 1;
	check(ColdStorageSearch::Evaluate(dawn,{})[4] > 0,"all rows share one cooldown; a later spawn does not receive another free cast");
	// 真人可以蓄满后等新工人出生；即时释放的冷却窗口不能自动算成后续工人的安全期。
	auto dawnTiming = dawn;
	dawnTiming.current[1].body.spawnAt = 4;
	const auto dawnImmediate = ColdStorageSearch::Evaluate(dawnTiming,{});
	const auto dawnPatient = ColdStorageSearch::Evaluate(dawnTiming,{},nullptr,0,0,8);
	check(dawnPatient[4] < dawnImmediate[4] && dawnPatient[6] == 48 && dawnPatient[3] == 0,
		"holding one charged row strike catches a later worker in another lane without duplicating the charge");
	ColdStorageSearch::Weights dawnIncome{}; dawnIncome[4] = 1;
	const auto robustDawn = ColdStorageSearch::Search(dawnTiming,dawnIncome,31);
	check(robustDawn.counterHoldSeconds == 8 && robustDawn.features == dawnPatient && robustDawn.baselineFeatures == dawnPatient,
		"row-strike patience is compared without ash cards and uses a complete common waiting baseline");
	check(dawnTiming.rowStrikes[0].ready == 0 && dawnTiming.current[0].body.health == 500,
		"numerical holding does not spend the live charge or mutate sampled units");
	auto farDawn = dawnTiming;
	farDawn.current[1].body.spawnAt = 40;
	ColdStorageSearch::Weights dawnLoss{}; dawnLoss[6] = -1;
	const auto robustFarDawn = ColdStorageSearch::Search(farDawn,dawnLoss,31);
	check(robustFarDawn.counterHoldSeconds == 40
		&& robustFarDawn.features == ColdStorageSearch::Evaluate(farDawn,{},nullptr,0,0,40),
		"held active strikes cover known later paid arrivals instead of assuming charge was spent before birth");
	auto urgentDawn = dawnTiming;
	urgentDawn.current[0].body.x = urgentDawn.houseX+100;
	check(ColdStorageSearch::Evaluate(urgentDawn,{}) == ColdStorageSearch::Evaluate(urgentDawn,{},nullptr,0,0,8),
		"held row strike releases immediately when a current target threatens the house");
	auto lostDawn = dawnTiming;
	lostDawn.plants[0].health = 0;
	check(ColdStorageSearch::Evaluate(lostDawn,{}) == ColdStorageSearch::Evaluate(lostDawn,{},nullptr,0,0,8),
		"a destroyed source cannot release a held row strike");
	auto snipedDawn = dawnTiming;
	snipedDawn.pendingPrecisionID = 1; snipedDawn.pendingPrecisionRemaining = 2;
	auto noStrikeDawn = snipedDawn; noStrikeDawn.rowStrikes.clear();
	check(ColdStorageSearch::Evaluate(snipedDawn,{},nullptr,0,0,8) == ColdStorageSearch::Evaluate(noStrikeDawn,{}),
		"destroying the source during its hold cancels release instead of leaving an independent delayed strike");
	auto noTargetDawn = dawnTiming;
	noTargetDawn.current.resize(1); noTargetDawn.current[0].body.spawnAt = 59;
	check(ColdStorageSearch::Evaluate(noTargetDawn,{},nullptr,0,0,8)[6] == 0,
		"row-strike hold begins with a born target and cannot spend charge on future units");
	auto builtDawn = dawnTiming;
	builtDawn.plants.clear(); builtDawn.rowStrikes.clear();
	ColdStorageSearch::Construction dawnCard;
	dawnCard.plant = sourcePlant; dawnCard.plant.x = 400;
	dawnCard.strike = dawnTiming.rowStrikes[0]; dawnCard.remainingUses = 1;
	builtDawn.construction.push_back(dawnCard);
	ColdStorageSearch::ConstructionStats builtStats;
	const auto builtPatient = ColdStorageSearch::Evaluate(builtDawn,{},&builtStats,0,0,8);
	check(builtStats.planted == 1 && builtPatient[6] == 48
		&& builtPatient[4] < ColdStorageSearch::Evaluate(builtDawn,{})[4],
		"a legally constructed source gains its own hold timer and one shared charge");
	const auto robustBuiltDawn = ColdStorageSearch::Search(builtDawn,dawnIncome,31);
	check(robustBuiltDawn.counterHoldSeconds == 8 && robustBuiltDawn.features == builtPatient,
		"search includes active-strike holding for possible construction as well as living sources");
	ColdStorageSearch::ProductionCalibration calibration;
	calibration.nodes = {{2,1,2,0.5f,1},{-1,-1,-1,0,0.25f},{-1,-1,-1,0,0.75f}};
	check(calibration.IsValid(), "finite forward-only calibration tree accepted");
	production.productionCalibration = &calibration;
	const auto calibrated = ColdStorageSearch::Search(production,ColdStorageSearch::InitialWeights,17);
	check(calibrated.rawProduction == predicted[4] && calibrated.features[4] == predicted[4]*0.25f
		&& calibrated.baselineFeatures[4] == calibrated.features[4], "calibration discounts forecast and baseline, preserves raw evidence");
	ColdStorageSearch::ProductionFeatures inputs{}; inputs[2] = 1;
	check(calibration.Predict(inputs) == 0.75f, "production feature selects independent leaf");
	calibration.nodes[0].right = 0;
	check(!calibration.IsValid(), "cyclic calibration rejected");
	calibration.nodes[0].right = 2; calibration.nodes[2].value = 1.1f;
	check(!calibration.IsValid(), "calibration cannot invent additional production");

	ColdStorageSearch::Snapshot contextual;
	contextual.budget = 4; contextual.capacity = 1;
	tank.cost = 4; tank.unit.body.purchaseCost = 4;
	contextual.options = {tank,tank};
	contextual.options[0].preference[1] = 20;
	contextual.context[0][1] = 1;
	ColdStorageSearch::Weights contextScore{}; contextScore[5] = 1;
	const auto support = ColdStorageSearch::Search(contextual, contextScore, 77);
	check(support.actions.size() == 1 && support.actions[0].option == 0,
		"learned type value responds to wounds without a scripted healer rule");
	contextual.context[0][1] = 0;
	check(ColdStorageSearch::Search(contextual, contextScore, 77).score < support.score,
		"healthy allies do not receive wounded-ally preference");
	contextual.options[0].preference = {}; contextScore[5] = -1;
	contextual.houseX = -10000; // 观望夹具排除真实获胜路径，避免把胜利优先误判成强制出兵。
	check(ColdStorageSearch::Search(contextual,contextScore,77).actions.empty(),"waiting remains a legal scored choice");
	contextual.allowWait = false;
	const auto resume = ColdStorageSearch::Search(contextual,contextScore,77);
	check(!resume.actions.empty() && resume.actions.front().delay == 0,"explicit unlock exploration can require a paid first action");
	contextual.budget = 0;
	check(ColdStorageSearch::Search(contextual,contextScore,77).actions.empty(),"unlock exploration cannot authorize unpaid troops");

	// 逐行打击有利于集中，而一张整行灰烬有利于分散；不能把主攻一路变成固定命令。
	ColdStorageSearch::Snapshot formation;
	formation.budget = 96; formation.capacity = 2; formation.houseX = 0;
	formation.plants.push_back(sourcePlant);
	formation.rowStrikes.push_back({1,0,20,1400,300,120});
	for (int row = 0; row < 5; ++row) {
		auto giant = tank;
		giant.type = 10; giant.row = giant.unit.body.row = row; giant.cost = 48;
		giant.unit.body.purchaseCost = 48; giant.unit.body.health = 3000;
		giant.unit.body.x = 900; giant.unit.body.speed = 0; giant.preference = {};
		formation.options.push_back(giant);
	}
	ColdStorageSearch::Weights formationWeights{};
	formationWeights[3] = 1; formationWeights[5] = 2; formationWeights[6] = -1;
	for (unsigned seed = 0; seed < 16; ++seed) {
		const auto focused = ColdStorageSearch::Search(formation,formationWeights,seed);
		check(focused.actions.size() == 2 && focused.actions[0].option == focused.actions[1].option,
			"repeated row strikes favor concentrating heavy troops instead of exposing every lane");
		// 重复提案复用后实际积分数可下降；五条合法行仍完整对照，预算上界不能增加。
		check(focused.formationTested == 31 && focused.evaluated>0
			&& focused.evaluated<=101+focused.routeEvaluated+focused.combinationEvaluated && focused.duplicatesSkipped>0,
			"every legal lane is compared within a bounded extra search budget");
		check(focused.score + 0.002f >= focused.formationBaseScore,
			"formation refinement never replaces the free plan with a worse scored plan");
		for (int row = 0; row < 5; ++row)
			check(focused.score + 0.002f >= focused.formationScores[row],"no better legal concentration is missed");
	}
	formation.rowStrikes.clear(); formation.playerSun = 150; formation.playerIce = 100;
	for (int row = 0; row < 5; ++row) {
		ColdStorageSearch::Counter ash;
		ash.blast.x = 900; ash.blast.reach.fill(-1); ash.blast.reach[row] = 10000;
		ash.blast.damage = 10000; ash.blast.ready = 20; ash.recharge = 100; ash.sunCost = 150; ash.iceCost = 100;
		formation.counters.push_back(ash);
	}
	const auto spread = ColdStorageSearch::Search(formation,formationWeights,19);
	check(spread.actions.size() == 2 && spread.actions[0].option != spread.actions[1].option,
		"shared ready lane-clear keeps a split plan when concentrating would lose both investments");
	check(spread.formationChosenRow == -1 && spread.formationTested == 31,
		"diagnostics distinguish rejecting concentration from failing to compare it");
	formation.options.resize(1);
	const auto restricted = ColdStorageSearch::Search(formation,formationWeights,19);
	check(restricted.formationTested == 1,"no fabricated row option when the spawn pool restricts a unit");

	ColdStorageSearch::Snapshot followup;
	followup.budget = 24; followup.capacity = 1; followup.houseX = 0;
	auto liveGuard = tank.unit;
	liveGuard.body.row = 3; liveGuard.body.x = 600; liveGuard.body.speed = 0; liveGuard.body.health = 10000;
	followup.current.push_back(liveGuard);
	for (int row = 0; row < 5; ++row) {
		auto workerOption = producer;
		workerOption.row = workerOption.unit.body.row = row;
		workerOption.unit.body.x = 900; workerOption.unit.body.speed = 0;
		followup.options.push_back(workerOption);
		auto fire = gun; fire.row = row; fire.x = 400; fire.edible = false;
		followup.plants.push_back(fire);
	}
	ColdStorageSearch::Weights incomeWeights{}; incomeWeights[4] = 1;
	const auto funded = ColdStorageSearch::Search(followup,incomeWeights,13);
	check(funded.actions.size() == 1 && followup.options[funded.actions[0].option].row == 3,
		"worker followup uses the surviving existing guard without relocating it or buying a new one");
	check(funded.features[5] == 24 && funded.formationTested == 31,"followup respects wallet and compares every legal lane");
	std::cout << "Concentration, ash dispersal and guarded economy comparisons passed\n";

	// 同一个基础评分通过局势输入改变偏好，而不是在运行逻辑中写固定进攻库存线。
	ColdStorageSearch::StateModel adaptive;
	adaptive.coefficients[0][5] = 1;
	ColdStorageSearch::Snapshot adaptiveState;
	adaptiveState.capacity = 32; adaptiveState.allowWait = true;
	adaptiveState.stateModel = &adaptive; adaptiveState.budget = 4;
	auto cheap = producer; cheap.unit.body.economic = false; cheap.unit.body.speed = 0;
	cheap.type = 0; cheap.cost = 4; cheap.unit.body.purchaseCost = 4;
	adaptiveState.options = {cheap};
	ColdStorageSearch::Weights adaptiveBase{}; adaptiveBase[5] = -1;
	const auto saving = ColdStorageSearch::Search(adaptiveState,adaptiveBase,11);
	check(saving.actions.empty(),"learned state layer can prefer waiting at low funds without a forced purchase");
	adaptiveState.budget = 1800;
	const auto investing = ColdStorageSearch::Search(adaptiveState,adaptiveBase,11);
	check(investing.actions.size() > 8 && investing.actions.size() <= 32,
		"same model can choose a larger affordable formation at higher funds");
	check(investing.effectiveWeights[5] > saving.effectiveWeights[5],"logged weights reflect actual budget conditioning");
	check(investing.features[5] <= adaptiveState.budget,"expanded search cannot borrow predicted income");
	adaptiveState.capacity = 3;
	check(ColdStorageSearch::Search(adaptiveState,adaptiveBase,11).actions.size() <= 3,"adaptive formation respects remaining real slots");
	adaptiveState.capacity = 32; adaptive.coefficients[0][5] = 0;
	check(ColdStorageSearch::Search(adaptiveState,adaptiveBase,11).actions.empty(),"high funds alone never force attack");
	adaptiveState.stateModel = nullptr;
	check(ColdStorageSearch::Search(adaptiveState,adaptiveBase,11).effectiveWeights == adaptiveBase,"legacy model preserves fixed weights");
	adaptive.coefficients[1][0] = std::numeric_limits<float>::infinity();
	check(!adaptive.IsValid(),"adaptive layer rejects nonfinite coefficients");
	adaptive = {};
	production.stateModel = &adaptive;
	check(ColdStorageSearch::Evaluate(production,{})[4] == predicted[4],"longer battle projection preserves the calibrated 60-second income window");
	ColdStorageSearch::Snapshot readiness;
	ColdStorageSearch::Counter readyCounter; readyCounter.source = 0; readyCounter.sunCost = 100;
	readiness.counters = {readyCounter,readyCounter}; readiness.playerSun = 100;
	check(std::abs(ColdStorageSearch::DescribeState(readiness,{})[4]-1.0f/3)<.001f,"alternative counter cells share one state feature source");
	readiness.playerSun = 0;
	check(ColdStorageSearch::DescribeState(readiness,{})[4] == 0,"unaffordable cards are not treated as ready pressure");
	std::cout << "Adaptive state scoring and voluntary large formations passed\n";

	// 不指定打法，只锁定账目：同一块冰的收入与支出必须同价，不能把亏损采购本身计成收益。
	ColdStorageSearch::Weights malformedEconomy{};
	malformedEconomy[3] = 500; malformedEconomy[4] = 2; malformedEconomy[5] = 500; malformedEconomy[6] = 500;
	const auto accounting = ColdStorageSearch::AccountForIce(malformedEconomy);
	check(accounting[4] == 2 && accounting[5] == -2 && accounting[3] == 2,
		"income and expenses share one currency value; remaining investment cannot exceed purchase price");
	check(64*accounting[4]+600*accounting[5] < 0,"600 ice for 64 income and no combat benefit is a loss");
	check(accounting[6] < 0 && 600*accounting[5]+600*accounting[6] < 0,
		"forecast blast casualties cannot act as an indirect spending reward");
	check(600*accounting[4]+64*accounting[5] > 0,"profitable production remains a legitimate investment");
	adaptiveState.netEconomy = true; adaptiveState.stateModel = &adaptive;
	adaptive.coefficients[0][5] = 500; adaptiveBase[4] = 1;
	check(ColdStorageSearch::Search(adaptiveState,adaptiveBase,11).actions.empty(),
		"large budget and positive spend coefficients cannot buy immobile troops for score alone");
	auto economicFollowup = followup; economicFollowup.netEconomy = true;
	const auto guardedProfit = ColdStorageSearch::Search(economicFollowup,incomeWeights,13);
	check(!guardedProfit.actions.empty() && economicFollowup.options[guardedProfit.actions[0].option].row == 3,
		"net accounting still invests behind an existing guard when production justifies its cost");
	economicFollowup.current.clear();
	for (auto& fire : economicFollowup.plants) fire.dps = 10000;
	check(ColdStorageSearch::Search(economicFollowup,incomeWeights,13).actions.empty(),
		"unprotected workers destroyed before earning income are not rewarded for their expense");
	std::cout << "Net ice accounting and protected investment passed\n";

	ColdStorageSearch::Snapshot crowded;
	ColdStorageSearch::Plant melon;
	melon.melon = true; melon.rowRadius = 1; melon.dps = 1; melon.health = 100; melon.edible = false;
	crowded.plants.push_back(melon);
	ColdStorageSearch::Unit stationary;
	stationary.body.health = stationary.body.purchaseCost = 10000;
	stationary.body.x = 900;
	crowded.current.assign(29,stationary);
	const auto melonBudget = ColdStorageSearch::Evaluate(crowded,{});
	check(std::abs(melonBudget[3]-(290000-60*8)) < 1,
		"crowded melon secondary damage is capped at seven times direct DPS, excluding the primary target");
	crowded.current.resize(2); crowded.current[1].body.row = 1; crowded.current[1].body.x = 960;
	check(std::abs(ColdStorageSearch::Evaluate(crowded,{})[3]-(20000-60)) < 1,
		"adjacent unit outside the real 60-pixel splash window is not hit by a phantom wider blast");
	crowded.current[0].body.boundsOffset = 0;
	check(std::abs(ColdStorageSearch::Evaluate(crowded,{})[3]-(20000-80)) < 1,
		"splash uses the primary collision center and the secondary bounding box");
	std::cout << "Melon splash geometry and crowd budget passed\n";

	ColdStorageSearch::Snapshot rebuilding;
	stationary.body.health = 1000; stationary.body.purchaseCost = 100;
	rebuilding.current = {stationary};
	ColdStorageSearch::Construction build;
	build.plant.x = 300; build.plant.health = 300; build.plant.dps = 100;
	build.sunCost = 100; build.iceCost = 10; build.recharge = 1000;
	rebuilding.construction = {build};
	build.plant.column = 1; build.plant.x = 400;
	rebuilding.construction.push_back(build); // 同卡两个格位，不能得到两次独立冷却
	rebuilding.playerSun = 1000; rebuilding.playerIce = 100;
	ColdStorageSearch::ConstructionStats built;
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 0 && built.planted == 1
		&& built.sunSpent == 100 && built.iceSpent == 10,"future fire uses one paid card and its shared cooldown");
	rebuilding.playerIce = 9;
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 100 && built.planted == 0,
		"future construction cannot spend unavailable ice");
	rebuilding.playerIce = 100; rebuilding.playerSun = 99;
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 100 && built.planted == 0,
		"future construction cannot spend unavailable sun");
	rebuilding.playerSun = 1000;
	for (auto& c : rebuilding.construction) c.ready = 90;
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 100 && built.planted == 0,
		"future construction respects actual card cooldown");
	build.ready = 0; build.plant.dps = 0; build.plant.health = 500;
	build.strike = {0,20,20,1400,300,150}; build.recharge = 2;
	rebuilding.construction = {build};
	build.plant.column = 2; build.plant.x = 500;
	rebuilding.construction.push_back(build);
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 0 && built.planted == 1,
		"newly built row-strike plant charges before its ability starts");
	build.strike.ready = 90; rebuilding.construction = {build};
	check(ColdStorageSearch::Evaluate(rebuilding,{},&built)[3] == 100,
		"an ability beyond the horizon cannot damage units immediately after planting");
	std::cout << "Paid future construction, shared cooldown and charge delay passed\n";
	ColdStorageSearch::Snapshot constructionReturn;
	constructionReturn.searchVersion = 2; constructionReturn.playerSun = 100; constructionReturn.playerIce = 10;
	ColdStorageSearch::Unit buyerThreat;
	buyerThreat.body.x = 500; buyerThreat.body.health = 10000; buyerThreat.body.speed = 20;
	constructionReturn.current = {buyerThreat};
	ColdStorageSearch::Construction rebuiltWall;
	rebuiltWall.plant.x = 400; rebuiltWall.plant.health = 100; rebuiltWall.plant.reward = 12;
	rebuiltWall.sunCost = 100; rebuiltWall.iceCost = 10; rebuiltWall.recharge = 1000;
	constructionReturn.construction = {rebuiltWall};
	check(ColdStorageSearch::Evaluate(constructionReturn,{},&built)[0] == 12 && built.planted == 1,
		"forecast counts income only after a paid future plant is actually destroyed in the rollout");
	constructionReturn.playerIce = 9;
	check(ColdStorageSearch::Evaluate(constructionReturn,{},&built)[0] == 0 && built.planted == 0,
		"unaffordable future construction cannot invent kill income");
	constructionReturn.playerIce = 10; constructionReturn.current[0].biteDps = 0;
	check(ColdStorageSearch::Evaluate(constructionReturn,{},&built)[0] == 0 && built.planted == 1,
		"merely building a future plant grants no kill income");

	// 单只/小队都过不了的连续火力，必须独立比较可负担的完整队伍；不借助正支出奖励。
	ColdStorageSearch::Snapshot portfolio;
	portfolio.budget = portfolio.capacity = 64; portfolio.houseX = 100;
	ColdStorageSearch::Option portfolioUnit;
	portfolioUnit.cost = 1; portfolioUnit.unit.body.purchaseCost = 1;
	portfolioUnit.unit.body.health = 100; portfolioUnit.unit.body.x = 900; portfolioUnit.unit.body.speed = 40;
	portfolio.options = {portfolioUnit};
	ColdStorageSearch::Plant battery;
	battery.health = 1000; battery.x = 50; battery.dps = 200; battery.edible = false;
	portfolio.plants = {battery};
	ColdStorageSearch::Weights breakthrough{}; breakthrough[2] = 100; breakthrough[5] = -1;
	portfolio.capacity = 8;
	check(ColdStorageSearch::Search(portfolio,breakthrough,731).actions.empty(),"small formations cannot cross this fire lane");
	portfolio.capacity = 64;
	const auto broad = ColdStorageSearch::Search(portfolio,breakthrough,731);
	check(broad.largestPlan == 64 && broad.actions.size() > 8 && broad.features[2] == 1,
		"an unproductive small search can escalate to a complete team without a spending reward");
	check(broad.expandedForecast && broad.features[5] <= portfolio.budget
		&& broad.evaluated-broad.routeEvaluated-broad.combinationEvaluated <= 300,
		"expanded search is explicit in diagnostics and remains bounded and paid");
	portfolio.searchVersion = 2;
	const auto large = ColdStorageSearch::Search(portfolio,breakthrough,731);
	check(large.largestPlan == 64 && large.actions.size() > 8 && large.features[2] > 0,
		"portfolio search finds a paid large-team breakthrough without a state layer or spending reward");
	check(large.features[5] <= portfolio.budget && large.evaluated-large.routeEvaluated-large.combinationEvaluated <= 198,
		"larger search obeys money and bounded evaluation limits");
	portfolio.capacity = 8;
	check(ColdStorageSearch::Search(portfolio,breakthrough,731).actions.empty(),"search cannot exceed remaining board capacity");
	portfolio.capacity = 64; portfolio.plants[0].dps = 100000; portfolio.plants[0].multiTarget = true;
	check(ColdStorageSearch::Search(portfolio,breakthrough,731).actions.empty(),"large budget does not force a hopeless attack");
	portfolio.budget = 0;
	check(ColdStorageSearch::Search(portfolio,breakthrough,731).actions.empty(),"new search cannot purchase unpaid troops");
	std::cout << "Portfolio breakthrough, optional waiting and capacity constraints passed\n";

	ColdStorageSearch::Snapshot throwTest;
	throwTest.searchVersion = 2;
	ColdStorageSearch::Unit thrower;
	thrower.body.x = 1000; thrower.body.health = 1000;
	thrower.throwHealth = 1000; thrower.throwAnchorX = 682;
	throwTest.current = {thrower};
	ColdStorageSearch::Plant frontWall, rearTarget;
	frontWall.x = 800; frontWall.column = 6; frontWall.health = 4000;
	rearTarget.x = 400; rearTarget.column = 1; rearTarget.health = 100; rearTarget.reward = 10;
	throwTest.plants = {frontWall,rearTarget};
	const auto thrown = ColdStorageSearch::Evaluate(throwTest,{});
	check(thrown[0] == 10 && thrown[2] == 1 && thrown[3] == 0 && thrown[5] == 0,
		"one free child bypasses the wall without inventing purchase assets or repeated throws");
	throwTest.current[0].throwHealth = 500;
	check(ColdStorageSearch::Evaluate(throwTest,{})[0] == 0,"unhurt thrower cannot release its child early");
	throwTest.current[0].throwHealth = 1000;
	ColdStorageSearch::Counter killParent;
	killParent.blast.committed = true; killParent.blast.x = 1000; killParent.blast.damage = 2000;
	killParent.blast.reach.fill(-1); killParent.blast.reach[0] = 30;
	throwTest.counters = {killParent};
	check(ColdStorageSearch::Evaluate(throwTest,{})[0] == 0,"death before release cancels the uncommitted child");
	throwTest.counters[0].blast.ready = 3;
	check(ColdStorageSearch::Evaluate(throwTest,{})[0] == 10,"death after release cannot cancel an airborne child");
	ColdStorageSearch::Snapshot freeThreat;
	freeThreat.searchVersion = 2; freeThreat.houseX = 160;
	thrower.throwHealth = 0; thrower.body.x = 300; thrower.body.health = 270; thrower.body.speed = 20;
	freeThreat.current = {thrower};
	killParent.blast.committed = false; killParent.blast.ready = 0; killParent.blast.x = 300;
	killParent.blast.reach[0] = 1000; killParent.recharge = 1000;
	freeThreat.counters = {killParent};
	const auto counteredFree = ColdStorageSearch::Evaluate(freeThreat,{});
	check(counteredFree[2] == 0 && counteredFree[3] == 0 && counteredFree[6] == 0,
		"free summons trigger urgent counters without creating paid assets or paid blast losses");

	ColdStorageSearch::Snapshot layeredSmash;
	thrower.throwHealth = 0; thrower.body.health = 1000; thrower.body.speed = 0; thrower.body.x = 450; thrower.body.smashSeconds = 3;
	layeredSmash.current = {thrower};
	rearTarget.health = 4000; layeredSmash.plants = {rearTarget,rearTarget};
	layeredSmash.plants[1].layer = 2;
	killParent.blast.committed = true; killParent.blast.reach[0] = 30; killParent.blast.x = 450; killParent.blast.ready = 3.5f;
	layeredSmash.counters = {killParent};
	check(ColdStorageSearch::Evaluate(layeredSmash,{})[0] == 10,"legacy prediction remains a single-layer smash");
	layeredSmash.searchVersion = 2;
	check(ColdStorageSearch::Evaluate(layeredSmash,{})[0] == 20,"one completed smash affects shell and host in the same cell");
	std::cout << "Thrown child commitment, free summons and same-cell smash layers passed\n";
	// 队友先吃掉砸击目标后，动作仍应结束；残留进度不能永久封住后续投小鬼。
	ColdStorageSearch::Snapshot lostSmashTarget;
	lostSmashTarget.searchVersion = 2;
	ColdStorageSearch::Unit smashingGiant, bitingAlly;
	smashingGiant.body.x = 1000; smashingGiant.body.health = 600; smashingGiant.body.speed = 5;
	smashingGiant.body.smashSeconds = 3; smashingGiant.throwHealth = 500; smashingGiant.throwAnchorX = 682;
	bitingAlly.body.x = 990; bitingAlly.body.health = 10000; bitingAlly.biteDps = 80;
	lostSmashTarget.current = {smashingGiant,bitingAlly};
	ColdStorageSearch::Plant weakWall, coveringFire;
	weakWall.x = 950; weakWall.column = 7; weakWall.health = 40; weakWall.reward = 10;
	coveringFire.x = 200; coveringFire.health = 100000; coveringFire.dps = 10;
	coveringFire.multiTarget = true; coveringFire.edible = false;
	lostSmashTarget.plants = {weakWall,coveringFire};
	check(ColdStorageSearch::Evaluate(lostSmashTarget,{})[2] == 1,
		"a teammate removing the smash target cannot permanently suppress the giant's later imp throw");
	lostSmashTarget.plants[1].dps = 0;
	ColdStorageSearch::Counter injureDuringSmash = killParent;
	injureDuringSmash.blast.x = 1000; injureDuringSmash.blast.reach[0] = 30;
	injureDuringSmash.blast.ready = .5f; injureDuringSmash.blast.damage = 100;
	killParent.blast.x = 1000; killParent.blast.ready = 2;
	lostSmashTarget.counters = {injureDuringSmash,killParent};
	check(ColdStorageSearch::Evaluate(lostSmashTarget,{})[2] == 0,
		"losing a target cannot cancel the remaining smash windup and release an imp before lethal damage");
	lostSmashTarget.counters[1].blast.ready = 4;
	check(ColdStorageSearch::Evaluate(lostSmashTarget,{})[2] == 1,
		"the completed empty smash unlocks a subsequent throw before the later lethal hit");
	lostSmashTarget.current[0].body.stopped = 2;
	check(ColdStorageSearch::Evaluate(lostSmashTarget,{})[2] == 0,
		"immobilization still delays an empty smash instead of advancing its recovery on wall time");
	for (int version : {1,2}) {
		auto differentCell = lostSmashTarget;
		differentCell.searchVersion = version;
		differentCell.current[0].body.stopped = 0; differentCell.current[0].throwHealth = 0;
		differentCell.current[0].body.speed = 0; differentCell.current[1].body.x = 1005;
		auto nextWall = weakWall;
		nextWall.x = 945; nextWall.column = 6; nextWall.health = 4000; nextWall.reward = 50;
		differentCell.plants = {weakWall,nextWall};
		differentCell.counters = {killParent}; differentCell.counters[0].blast.ready = 4;
		check(ColdStorageSearch::Evaluate(differentCell,{})[0] == 10,
			"an unfinished smash cannot transfer its charge to another cell after the original target dies");
	}
	std::cout << "Lost smash target recovery, windup and immobilization passed\n";
	for (int version : {1,2}) {
		ColdStorageSearch::Snapshot mowerTest;
		mowerTest.searchVersion = version; mowerTest.houseX = 100;
		portfolioUnit.row = portfolioUnit.unit.body.row = 0;
		portfolioUnit.unit.body.x = 300; portfolioUnit.unit.body.speed = 20;
		mowerTest.options = {portfolioUnit};
		mowerTest.mowers.push_back({0,200,60,230});
		check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{0,0}})[2] == 0,
			"a ready mower clears the first cohort instead of awarding two false breakthroughs");
		check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{0,20}})[2] == 1,
			"a spent mower cannot clear reinforcements which have not spawned during its sweep");
		// 真人局回归：前排免费小鬼已经足以触发车，立即跟入的高血量增援同样会被清掉。
		auto followup = mowerTest;
		ColdStorageSearch::Unit trigger;
		trigger.body.x = 250; trigger.body.health = 110; trigger.body.speed = 20;
		followup.current = {trigger};
		followup.options[0].unit.body.x = 1000;
		followup.options[0].unit.body.speed = 0;
		followup.options[0].unit.body.health = 3000;
		check(ColdStorageSearch::Evaluate(followup,{{0,0},{0,2}})[3] == 0,
			"an existing trigger makes immediate high-health reinforcements lose their terminal assets");
		check(ColdStorageSearch::Evaluate(followup,{{0,6}})[3] > 0,
			"waiting until the mower passes preserves a reinforcement without forcing another lane");
		mowerTest.options[0].unit.mowerImmune = true;
		check(ColdStorageSearch::Evaluate(mowerTest,{{0,0}})[2] == 1,"mower immunity comes from the unit capability");
		mowerTest.options[0].unit.mowerImmune = false;
		mowerTest.options[0].unit.consumesOtherMowers = true;
		portfolioUnit.row = portfolioUnit.unit.body.row = 1;
		mowerTest.options.push_back(portfolioUnit); mowerTest.mowers.push_back({1,200,60,230});
		check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{1,20}})[2] == 1,
			"a mower-consuming unit removes other lanes' mowers without granting itself immunity");
	}
	std::cout << "Mower clearing, delayed reinforcements and mower abilities passed\n";
	ColdStorageSearch::Snapshot wonLane;
	for (int version : {1,2}) {
		wonLane.searchVersion = version; wonLane.houseX = 100;
		wonLane.current.clear();
		portfolioUnit.unit.body.x = 300;
		portfolioUnit.row = portfolioUnit.unit.body.row = 0;
		wonLane.options = {portfolioUnit};
		const auto oneVictory = ColdStorageSearch::Evaluate(wonLane,{{0,0}});
		const auto repeatedVictory = ColdStorageSearch::Evaluate(wonLane,{{0,0},{0,0},{0,10}});
		check(oneVictory[2] == 1 && repeatedVictory[2] == 1,
			"additional intruders cannot multiply the value of one already won game");
		check(repeatedVictory[5] == 3*oneVictory[5],"redundant invasion still pays every purchase");
		wonLane.options[0].unit.body.x = 101;
		ColdStorageSearch::Unit lateIncome;
		lateIncome.body.health = 500; lateIncome.body.x = 900; lateIncome.body.economic = true;
		wonLane.current = {lateIncome};
		const auto endedGame = ColdStorageSearch::Evaluate(wonLane,{{0,0}});
		check(endedGame[2] == 1 && endedGame[4] == 0,"the game cannot keep producing ice after an actual projected victory");
	}
	ColdStorageSearch::Snapshot resourcePressure;
	resourcePressure.searchVersion = 2; resourcePressure.budget = 24; resourcePressure.capacity = 6;
	resourcePressure.recoveryReserve = 48; resourcePressure.playerSun = 100; resourcePressure.playerIce = 40;
	resourcePressure.sunIceValue = 100.0f/225;
	ColdStorageSearch::Option probe;
	probe.cost = 4; probe.unit.body.purchaseCost = 4; probe.unit.playerRefund = 3;
	probe.unit.body.health = 270; probe.unit.body.x = 500; probe.unit.body.speed = 0;
	resourcePressure.options = {probe};
	ColdStorageSearch::Counter paidCounter;
	paidCounter.blast.x = 500; paidCounter.blast.damage = 1800;
	paidCounter.blast.reach.fill(-1); paidCounter.blast.reach[0] = 200;
	paidCounter.sunCost = 100; paidCounter.iceCost = 40;
	resourcePressure.counters = {paidCounter};
	ColdStorageSearch::Weights costOnly{}; costOnly[5] = -1;
	check(ColdStorageSearch::Search(resourcePressure,costOnly,17).actions.empty(),
		"legacy scoring sees no immediate zombie income in a counter-consuming trade");
	resourcePressure.opponentWeight = 1;
	const auto pressured = ColdStorageSearch::Search(resourcePressure,costOnly,17);
	check(pressured.actions.empty(),
		"optional terminal value cannot assume a paid counter against harmless stationary troops");
	ColdStorageSearch::ConstructionStats paidTrade;
	ColdStorageSearch::Evaluate(resourcePressure,{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},&paidTrade);
	check(std::abs(paidTrade.opponentAssets-18) < .001f,
		"all dead paid zombies refund the player; attrition cannot ignore that returned currency");
	resourcePressure.playerSun = 99;
	check(ColdStorageSearch::Search(resourcePressure,costOnly,17).actions.empty(),
		"an unaffordable counter cannot generate imaginary resource depletion");
	resourcePressure.playerSun = 100;
	resourcePressure.counters[0].sunCost = resourcePressure.counters[0].iceCost = 0;
	check(ColdStorageSearch::Search(resourcePressure,costOnly,17).actions.empty(),
		"free counters plus death refunds do not become a profitable resource trade");
	resourcePressure.counters[0].sunCost = 100; resourcePressure.counters[0].iceCost = 40;
	resourcePressure.playerSunLimit = 100; resourcePressure.playerIceLimit = 40;
	ColdStorageSearch::Plant fullBankProducer;
	fullBankProducer.row = 1; fullBankProducer.health = 300; fullBankProducer.sunPerSecond = 100;
	resourcePressure.plants = {fullBankProducer};
	check(ColdStorageSearch::Search(resourcePressure,costOnly,17).actions.empty(),
		"a capped economy that recovers its sun cannot be credited with fictitious permanent sun depletion");
	resourcePressure.counters[0].sunCost = resourcePressure.counters[0].iceCost = 0;
	ColdStorageSearch::ConstructionStats cappedRefund;
	ColdStorageSearch::Evaluate(resourcePressure,{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},&cappedRefund);
	check(std::abs(cappedRefund.opponentAssets-(40+100*resourcePressure.sunIceValue)) < .001f,
		"refunds and passive income respect the actual player resource capacities");
	// 付费反制不是承诺：无战果单兵不能靠假定玩家一定交灰烬赚取消耗分。
	{
		using namespace ColdStorageSearch;
		Snapshot bait; bait.houseX=-10000; bait.budget=24; bait.capacity=1; bait.netEconomy=true;
		bait.playerSun=1000; bait.playerIce=100; bait.sunIceValue=100.0f/225; bait.opponentWeight=1;
		Option unit; unit.type=701; unit.cost=24; unit.unit.body.x=900; unit.unit.body.health=500;
		bait.options={unit};
		Counter ash; ash.sunCost=125; ash.iceCost=20; ash.recharge=50;
		ash.blast.x=900; ash.blast.reach[0]=10000; ash.blast.damage=1800; bait.counters={ash};
		const Weights value{0,0,100,0,1,-1,0,0};
		const auto idle=Search(bait,value,72);
		check(idle.actions.empty(),"harmless bait cannot profit from a player forced to spend a paid counter");
		ConstructionStats retained;
		Evaluate(bait,{{0,0}},&retained,0,0,0,false,false,-1,false,false,true);
		check(retained.paidCounterCasts==0 && retained.paidDefensesRetained
			&& retained.opponentAssets==bait.playerIce+bait.playerSun*bait.sunIceValue,
			"retained paid defenses preserve actual cash rather than inventing a mandatory cast");
		bait.houseX=160; bait.budget=240; bait.options[0].unit.body.x=450; bait.options[0].unit.body.speed=40;
		const auto stopped=Search(bait,value,72);
		std::cout << "paid defense response: actions=" << stopped.actions.size() << " breach=" << stopped.features[2]
			<< " casts=" << stopped.construction.paidCounterCasts << " retained=" << stopped.construction.paidDefensesRetained << " score=" << stopped.score << "\n";
		check(!stopped.actions.empty() && stopped.features[2]==0 && stopped.construction.paidCounterCasts>0,
			"genuine breach pressure can still force a paid counter and earn a valid resource exchange");
		bait.current={bait.options[0].unit}; bait.counters[0].blast.committed=true; bait.counters[0].blast.ready=1;
		const auto committed=Evaluate(bait,{},&retained,0,0,0,false,false,-1,false,false,true);
		check(committed[2]==0 && retained.paidCounterCasts==0,
			"holding future paid defenses does not cancel an already committed explosion or charge it twice");
	}
	// 单独高档照明会被后续补阵堵住灰烬格，单独留格则看不见边路；联合应对仍可清场。
	{
		using namespace ColdStorageSearch;
		Snapshot lit; lit.houseX=-10000; lit.weatherStation=true; lit.searchVersion=2; lit.stationWave=79;
		lit.playerSun=1000; lit.playerIce=100; lit.station.controls[WeatherStationRules::FOG].value=2;
		lit.station.controls[WeatherStationRules::FOG].protection=30;
		for(int row=0;row<lit.rows;++row) for(int col=4;col<lit.columns;++col) lit.stationFogAlpha[row*lit.columns+col]=255;
		Unit worker; worker.body.row=4; worker.body.x=840; worker.body.health=500;
		worker.body.value=worker.body.purchaseCost=24; worker.body.economic=true; lit.current.assign(7,worker);
		Option profile; profile.type=702; profile.row=4; profile.cost=24; profile.unit=worker; lit.options={profile};
		Plant lamp; lamp.id=1; lamp.row=2; lamp.column=4; lamp.x=520; lamp.health=300;
		lamp.plantern=true; lamp.lightFuel=100; lit.plants={lamp};
		Construction filler; filler.plant.row=4; filler.plant.column=7; filler.plant.x=760;
		filler.plant.health=100000; filler.plant.layer=1; filler.plant.edible=false;
		filler.sunCost=1; filler.recharge=1000; lit.construction={filler};
		Counter ash; ash.cellRow=4; ash.cellColumn=7; ash.blast.x=760; ash.blast.reach[4]=10000;
		ash.blast.damage=1800; ash.blast.ready=8; ash.windup=1; ash.sunCost=125; ash.iceCost=20; ash.recharge=50;
		for(int row=0;row<lit.rows;++row) {
			ash.cellRow=row; ash.blast.reach.fill(-1); ash.blast.reach[row]=10000; lit.counters.push_back(ash);
		}
		const Weights income{0,0,0,0,1,0,-1,0};
		const auto joint=Search(lit,income,73);
		check(joint.construction.counterSpaceReserved && joint.construction.planternResponseGear>=3
			&& joint.construction.paidCounterCasts==1 && joint.features[4]<Evaluate(lit,{})[4],
			"an existing high-gear lamp plus reserved counter space exposes and clears a hidden side-lane worker cohort");
		std::cout << "joint high-light counter: production=" << joint.features[4] << " casts=" << joint.construction.paidCounterCasts << "\n";
	}
	ColdStorageSearch::Snapshot assetTransfer;
	assetTransfer.playerSun = 100; assetTransfer.playerIce = 40; assetTransfer.sunIceValue = 100.0f/225;
	ColdStorageSearch::Construction capital;
	capital.sunCost = 100; capital.iceCost = 40; capital.recharge = 1000;
	capital.plant.x = 400; capital.plant.health = 300; capital.plant.dps = 1;
	capital.plant.assetValue = 40+100*assetTransfer.sunIceValue;
	assetTransfer.construction = {capital};
	ColdStorageSearch::ConstructionStats capitalStats;
	ColdStorageSearch::Evaluate(assetTransfer,{},&capitalStats);
	check(capitalStats.planted == 1 && std::abs(capitalStats.opponentAssets-capital.plant.assetValue) < .001f,
		"buying a surviving plant transfers cash to assets instead of rewarding fake attrition");
	std::cout << "Opponent capital, counter costs, refunds and optional attrition scoring passed\n";
	ColdStorageSearch::Snapshot trading;
	trading.playerIce = 20; trading.sunIceValue = 100.0f/225;
	ColdStorageSearch::SunExchange exchange;
	exchange.sunGain = 100; exchange.iceCost = 10; exchange.recharge = 120; exchange.cells = {{{0,0}}};
	trading.exchanges = {exchange,exchange};
	ColdStorageSearch::ConstructionStats traded;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.exchanges == 0,"player trading stays disabled for legacy policy snapshots");
	trading.anticipateEconomy = true;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.exchanges == 2 && traded.exchangeSun == 200 && traded.exchangeIce == 20,
		"normal and imitater economy cards have independent cooldowns but share actual ice and a reusable cell");
	trading.playerIce = 9;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.exchanges == 0,"negative sun prices cannot waive a card's ice cost");
	trading.playerIce = 20;
	trading.exchanges[0].ready = trading.exchanges[1].ready = 61;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.exchanges == 0,"economy cards beyond the horizon cannot produce early");
	trading.exchanges[0].ready = trading.exchanges[1].ready = 0;
	ColdStorageSearch::Plant occupiedCell;
	occupiedCell.row = occupiedCell.column = 0; occupiedCell.health = 300;
	trading.plants = {occupiedCell}; trading.playerSun = 400;
	trading.shop = {{100,40,5},{225,100,10}};
	trading.playerIce = 0;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.exchanges == 0 && traded.orders == 0,"occupied economy cells do not cause imaginary farming or orders");
	trading.plants.clear(); trading.exchanges.clear();
	ColdStorageSearch::Unit target;
	target.body.health = 100; target.body.x = 500; target.body.speed = 0; target.body.purchaseCost = 50;
	trading.current = {target};
	ColdStorageSearch::Counter futureCounter;
	futureCounter.blast.x = 500; futureCounter.blast.damage = 1800; futureCounter.blast.reach.fill(-1);
	futureCounter.blast.reach[0] = 200; futureCounter.sunCost = 150; futureCounter.iceCost = 20;
	trading.counters = {futureCounter};
	trading.anticipateEconomy = false;
	check(ColdStorageSearch::Evaluate(trading,{})[3] == 50,"without a future order the ice-starved counter is unavailable");
	trading.anticipateEconomy = true;
	check(ColdStorageSearch::Evaluate(trading,{},&traded)[3] == 0 && traded.orders == 1
		&& traded.orderSun == 225 && traded.orderIce == 100,"a paid delivered order can fund a later real-price counter");
	trading.playerSun = 99;
	check(ColdStorageSearch::Evaluate(trading,{},&traded)[3] == 50 && traded.orders == 0,
		"an unaffordable delivery cannot provide counter ice");
	trading.playerSun = 400; trading.incomingIce = 40; trading.incomingIceAt = 3;
	check(ColdStorageSearch::Evaluate(trading,{},&traded)[3] == 0 && traded.orders == 0,
		"an already pending delivery arrives once and blocks a duplicate order");
	trading.playerSun = 0; trading.incomingIceAt = 70;
	ColdStorageSearch::Evaluate(trading,{},&traded);
	check(traded.pendingIce == 40 && traded.opponentAssets == 40,"paid goods beyond the horizon remain assets without arriving early");
	std::cout << "Player economy cards, shared ice and delayed paid orders passed\n";
	ColdStorageSearch::Snapshot affordableTeam;
	affordableTeam.searchVersion = 2; affordableTeam.budget = 40; affordableTeam.capacity = 64;
	tank.type = 10; tank.cost = 16; tank.unit.body.purchaseCost = 16;
	tank.unit.body.x = 850; tank.unit.body.health = 10000; tank.unit.body.speed = 0; tank.preference = {};
	producer.type = 20; producer.cost = 24; producer.unit.body.purchaseCost = 24;
	producer.unit.body.x = 900; producer.unit.body.health = 500; producer.unit.body.speed = 0;
	affordableTeam.options = {tank,producer};
	gun.row = 0; gun.x = 200; gun.health = 10000; gun.dps = 80; gun.edible = false;
	affordableTeam.plants = {gun};
	ColdStorageSearch::Weights returnOnTeam{}; returnOnTeam[4] = 1; returnOnTeam[5] = -1;
	for (unsigned seed = 0; seed < 12; ++seed) {
		const auto team = ColdStorageSearch::Search(affordableTeam,returnOnTeam,seed);
		check(team.actions.size() == 2 && team.features[5] == 40 && team.features[4] > 40,
			"affordable large-space samples retain profitable cooperation instead of buying only the first cohort");
	}
	// 自由混编搜索仍需保留灰烬长冷却时在安全路集中大量工人的经营机会。
	{
		using namespace ColdStorageSearch;
		Snapshot window; window.houseX=-10000; window.searchVersion=2;
		window.budget=1600; window.capacity=48; window.netEconomy=true;
		window.playerSun=5000; window.playerIce=3000;
		for (int row=0;row<5;++row) {
			Option worker; worker.type=709; worker.row=worker.unit.body.row=row; worker.cost=24;
			worker.unit.body.x=900; worker.unit.body.health=500; worker.unit.body.value=24;
			worker.unit.body.economic=true; worker.unit.productionStopHealth=500.0f/3;
			window.options.push_back(worker);
			if (row!=2) {
				Plant fire; fire.row=row; fire.x=300; fire.health=100000; fire.dps=1000; fire.edible=false;
				window.plants.push_back(fire);
			}
		}
		Counter coolingAsh; coolingAsh.blast.ready=90; coolingAsh.recharge=35;
		coolingAsh.blast.x=900; coolingAsh.blast.reach.fill(130); coolingAsh.blast.damage=1800;
		coolingAsh.sunCost=150; coolingAsh.iceCost=20; window.counters={coolingAsh};
		const Weights profit{0,0,0,0,1,-1,0,0};
		size_t largestWorkers=0;
		for (std::uint32_t seed=1;seed<=4;++seed) {
			const auto plan=Search(window,profit,seed);
			largestWorkers=std::max(largestWorkers,plan.actions.size());
			check(plan.actions.size()>=20 && plan.actions.size()<=48 && plan.features[4]>plan.features[5]
				&& std::all_of(plan.actions.begin(),plan.actions.end(),[&](const auto& action) {
					return window.options[action.option].row==2;
				}),"long ash cooldown and a safe lane still allow profitable mass worker concentration");
		}
		check(largestWorkers>=40,"free composition search retains forty-plus worker investment opportunities");
		std::cout << "safe-lane worker concentration: " << largestWorkers << " units\n";
	}
	// 不同数量的两类协作必须通过真实能力和钱包胜出，改类型编号不能改变搜索结果。
	{
		using namespace ColdStorageSearch;
		Snapshot cooperation; cooperation.houseX=-10000; cooperation.searchVersion=2;
		cooperation.budget=117; cooperation.capacity=4; cooperation.netEconomy=true;
		Option support; support.type=700; support.cost=35;
		support.unit.engineer=true; support.unit.body.x=1010; support.unit.body.health=1000; support.unit.body.value=35;
		Option income; income.type=701; income.cost=24;
		income.unit.body.economic=true; income.unit.body.x=900; income.unit.body.health=500;
		income.unit.body.value=24; income.unit.productionStopHealth=500.0f/3;
		cooperation.options={support,income};
		Counter blast; blast.blast.committed=true; blast.blast.x=900;
		blast.blast.reach.fill(40); blast.blast.damage=1800;
		for (float at=1;at<120;at+=7) { blast.blast.ready=at; cooperation.counters.push_back(blast); }
		const Weights profit{0,0,0,0,1,-1,0,0};
		const auto feasible=Evaluate(cooperation,{{0,0},{1,0},{1,0},{1,0}});
		for (std::uint32_t seed=1;seed<=8;++seed) {
			const auto choice=Search(cooperation,profit,seed);
			check(choice.unevenMixEvaluated>0 && choice.duplicatesSkipped>0
				&& choice.score>=feasible[4]-feasible[5]-.01f,
				"generic ratios discover profitable non-equal cooperation without named type templates");
			int cost=0;
			for (const auto& action:choice.actions) cost+=cooperation.options[action.option].cost;
			check(cost<=cooperation.budget && choice.actions.size()<=static_cast<size_t>(cooperation.capacity),
				"expanded composition search preserves actual wallet and capacity");
		}
		const auto original=Search(cooperation,profit,7);
		cooperation.options[0].type=42; cooperation.options[1].type=444;
		const auto renamed=Search(cooperation,profit,7);
		check(original.actions.size()==renamed.actions.size() && original.score==renamed.score
			&& std::equal(original.actions.begin(),original.actions.end(),renamed.actions.begin(),[](const auto& a,const auto& b) {
				return a.option==b.option && a.delay==b.delay;
			}),"composition exploration depends on legal profiles rather than particular type identifiers");
		cooperation.counters.clear();
		const auto unneeded=Search(cooperation,profit,7);
		check(std::none_of(unneeded.actions.begin(),unneeded.actions.end(),[&](const auto& action) {
			return cooperation.options[action.option].unit.engineer;
		}),"free exploration drops extra support when unprotected income is better");
	}
	// 新能力必须兑现真实护工、费用及离散控制，不修改来源快照。
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.searchVersion=2;
	Unit worker; worker.body.x=900; worker.body.health=500; worker.body.economic=true;
	worker.productionStopHealth=500.0f/3;
	s.current={worker,worker,worker};
	Counter blast; blast.blast.committed=true; blast.blast.ready=1;
	blast.blast.x=900; blast.blast.reach.fill(-1); blast.blast.reach[0]=130; blast.blast.damage=1800;
	s.counters={blast};
	const auto exposed=Evaluate(s,{});
	Unit engineer; engineer.engineer=true; engineer.body.x=920; engineer.body.health=1000;
	s.current.push_back(engineer); ConstructionStats protectedStats;
	const auto protectedIncome=Evaluate(s,{},&protectedStats);
	check(protectedIncome[4]>exposed[4] && protectedStats.engineerBlocks==3,
		"same blast may kill engineer while preserving its three nearby workers");
	check(s.current.back().canisterFull && s.current[0].body.health==500,
		"engineering forecast does not consume live canister or worker health");
	s.budget=10; s.current.back().body.x=1010; s.counters[0].blast.reach[0]=30;
	blast=s.counters[0]; blast.blast.ready=8; s.counters.push_back(blast);
	ConstructionStats reload; Evaluate(s,{},&reload);
	check(reload.engineerBlocks==6 && reload.engineerReloadIce>=20,
		"surviving engineer can pay for six-second reload and protect a later separate blast");
	s.budget=0;
	for (size_t i=0;i<3;++i) s.current[i].body.stopped=600;
	ConstructionStats broke; Evaluate(s,{},&broke);
	check(broke.engineerBlocks==3 && broke.engineerReloadIce==0,
		"empty wallet and stopped production cannot invent a replacement canister");
	worker.body.stopped=600; engineer.body.x=1010;
	s.current={worker,engineer,engineer}; s.counters[1].blast.ready=1.5f;
	ConstructionStats overlap; Evaluate(s,{},&overlap);
	check(overlap.engineerBlocks==2 && overlap.engineerReloadIce==0,
		"overlapping engineers retain their second canister for the next separate ash event");
	s.current.pop_back(); Evaluate(s,{},&overlap);
	check(overlap.engineerBlocks==1,
		"one broke engineer cannot repeat protection before completing a paid reload");
	s.counters.clear(); s.current.clear();
	Unit crowd; crowd.body.x=900; crowd.body.health=100000;
	for (int i=0;i<9;++i) { crowd.id=i+1; s.current.push_back(crowd); }
	Plant flower; flower.thunder=true; flower.x=200; flower.health=300; flower.dps=10; flower.edible=false;
	s.plants={flower}; ConstructionStats single, multiple;
	Evaluate(s,{},&single);
	check(single.thunderStuns>0 && single.thunderStuns<=6*60,
		"thunder control is discrete and capped at six successful recipients per attack");
	s.current.resize(6); Evaluate(s,{},&single); s.plants.push_back(flower); Evaluate(s,{},&multiple);
	check(multiple.thunderStuns==single.thunderStuns,
		"synchronized flowers cannot refresh paralysis or stack their per-target resistance");
	s.plants.clear(); s.thunderRays={{850,0,PlantDamageOrigin::FromPlant(PlantType::PLANT_THUNDERFLOWER)}};
	ConstructionStats inFlight; Evaluate(s,{},&inFlight);
	check(inFlight.thunderStuns==6,"an already-fired ray completes its capped control after its plant is gone");
	// 邻行有敌人、本行尚未到场时不能预发雷种；真正到场后的首批生产仍可兑现。
	Snapshot delayed; delayed.houseX=-10000; delayed.traceEconomy=true;
	flower.rowRadius=1; flower.thunderAttack.cooldownRemaining=flower.thunderAttack.checkRemaining=0; delayed.plants={flower};
	crowd.body.row=1; crowd.body.x=1000; delayed.current={crowd};
	worker.body.row=0; worker.body.x=1000; worker.body.health=20; worker.body.stopped=0;
	worker.body.spawnAt=2; worker.productionRemaining=1; worker.productionStopHealth=0; worker.nextYield=4;
	delayed.current.push_back(worker);
	const auto arrival=Evaluate(delayed,{});
	check(arrival[4]>=4,"neighboring targets cannot prelaunch a ray that kills a later own-row worker before production");
	// 本行触发后的电弧仍可伤及邻行；修正的是发射资格而非电弧范围。
	delayed.current.back().body.spawnAt=0; delayed.current.back().body.health=100000;
	delayed.current.front().body.health=100000; delayed.current.front().body.row=1;
	ConstructionStats arc; Evaluate(delayed,{},&arc);
	check(arc.thunderStuns>0,"a legitimate own-row impact keeps adjacent-row arc control");
	// 诊断购物车必须采用正式搜索会选择的长时域；第70秒清场不可藏在小队窗口外。
	Snapshot funded; funded.houseX=-10000; funded.netEconomy=true; funded.budget=3000; funded.capacity=48;
	Option purchase; purchase.cost=24; purchase.unit=worker;
	purchase.unit.body.health=500; purchase.unit.body.spawnAt=0; purchase.unit.body.purchaseCost=24;
	funded.options={purchase}; blast.blast.ready=70; blast.blast.reach.fill(10000); funded.counters={blast};
	Weights value{}; value[3]=value[4]=1;
	const auto diagnosed=EvaluateCandidate(funded,value,{{0,0}});
	auto longWorld=funded; longWorld.searchVersion=2;
	const auto explicitLong=EvaluateCandidate(longWorld,value,{{0,0}});
	check(diagnosed.expandedForecast && diagnosed.features==explicitLong.features
		&& diagnosed.score==explicitLong.score && diagnosed.features[3]==0 && funded.searchVersion==1,
		"candidate diagnosis shares funded long forecast and cannot mutate the input or hide a late clearing");
	}
	// 已付款队列不属于新购物车：只能提前/改合法路线，不能再次扣费或修改在场实体。
	ColdStorageSearch::Snapshot queued;
	queued.searchVersion = 2;
	ColdStorageSearch::Unit live, queuedUnit;
	live.id = 42; live.body.row = 2; live.body.x = 900; live.body.health = 100; live.body.speed = 0;
	queuedUnit.body.row = 0; queuedUnit.body.x = 900; queuedUnit.body.health = 500; queuedUnit.body.speed = 0;
	queuedUnit.body.purchaseCost = 24; queuedUnit.body.spawnAt = 2; queuedUnit.playerRefund = 18;
	queued.current = {live,queuedUnit};
	queued.committed = {{1,{{true,true,false,false,false,false}}}};
	ColdStorageSearch::Counter committedBlast;
	committedBlast.blast.x = 900; committedBlast.blast.reach.fill(-1);
	committedBlast.blast.reach[0] = 200; committedBlast.blast.ready = 5;
	committedBlast.blast.damage = 1800; committedBlast.blast.committed = true;
	queued.counters = {committedBlast};
	ColdStorageSearch::Weights preservePaid{}; preservePaid[3] = 1; preservePaid[6] = -1;
	const auto routed = ColdStorageSearch::ReplanCommitted(queued,preservePaid,31);
	check(routed.changed == 1 && routed.afterScore > routed.beforeScore && queued.current[1].body.row == 1,
		"paid reinforcements can avoid a newly committed blast by choosing a legal different row");
	check(queued.current[0].id == 42 && queued.current[0].body.row == 2 && queued.current[0].body.x == 900
		&& queued.current[1].body.purchaseCost == 24 && queued.current[1].playerRefund == 18 && queued.current.size() == 2
		&& queued.current[1].body.spawnAt <= 2 && ColdStorageSearch::Evaluate(queued,{})[5] == 0,
		"queue revision preserves live entities, paid cost, count, refund and original deadline without new spending");
	queued.current[1] = queuedUnit; queued.committed[0].legalRows[1] = false;
	const auto locked = ColdStorageSearch::ReplanCommitted(queued,preservePaid,31);
	check(locked.changed == 0 && queued.current[1].body.row == 0 && queued.current[1].body.spawnAt == 2,
		"a losing queue cannot escape via an illegal row or by extending its deadline");
	queued.current[1].body.spawnAt = 10;
	check(ColdStorageSearch::Evaluate(queued,{})[3] == 24,
		"a future paid unit is not present for a blast that resolves before its birth");
	queued.counters.clear(); queued.current[1].body.economic = true;
	queued.current[1].body.spawnAt = 30;
	ColdStorageSearch::Weights earn{}; earn[4] = 1;
	const auto earlier = ColdStorageSearch::ReplanCommitted(queued,earn,31);
	check(earlier.changed == 1 && earlier.afterScore > earlier.beforeScore && queued.current[1].body.spawnAt == 0,
		"an already paid worker can start earlier when the current battlefield makes that more productive");
	const auto noFunds = ColdStorageSearch::Search(queued,earn,31);
	check(noFunds.actions.empty() && noFunds.features[4] > 0 && noFunds.features[5] == 0,
		"zero new budget still forecasts owned queued production without charging it again");
	queued.committed = {{0,{{true,true,true,false,false,false}}}};
	check(ColdStorageSearch::ReplanCommitted(queued,earn,31).evaluated == 0 && queued.current[0].body.row == 2,
		"a real entity ID accidentally marked as pending cannot be repositioned");
	queued.searchVersion = 1; queued.committed = {{1,{{true,false,false,false,false,false}}}};
	queued.current[1] = queuedUnit; queued.current[1].body.spawnAt = 50;
	committedBlast.blast.ready = 80; queued.counters = {committedBlast};
	check(ColdStorageSearch::Evaluate(queued,{})[3] == 0,
		"small purchase searches still evaluate a late paid queue's full combat window");
	queued.committed.clear();
	check(ColdStorageSearch::Evaluate(queued,{})[3] == 24,
		"ordinary v1 snapshots retain their original forecast horizon");
	std::cout << "Paid queue replanning, legal routes, deadlines and unchanged live entities passed\n";
	ColdStorageSearch::Snapshot outsideBlast;
	ColdStorageSearch::Unit outsideWorker;
	outsideWorker.body.row = 0; outsideWorker.body.x = 1140; outsideWorker.body.health = 500;
	outsideWorker.body.economic = true; outsideWorker.body.purchaseCost = 24;
	outsideBlast.current = {outsideWorker};
	ColdStorageSearch::Counter rowFire;
	rowFire.blast.x = 400; rowFire.blast.reach.fill(-1); rowFire.blast.reach[0] = 10000;
	rowFire.blast.ready = 1; rowFire.blast.damage = 1800; rowFire.blast.committed = true;
	outsideBlast.counters = {rowFire};
	const auto burnedOutside = ColdStorageSearch::Evaluate(outsideBlast,{});
	check(burnedOutside[4] == 0 && burnedOutside[6] == 24 && burnedOutside[3] == 0,
		"row fire destroys an already born offscreen worker instead of granting imaginary production");
	outsideBlast.current[0].body.spawnAt = 3;
	check(ColdStorageSearch::Evaluate(outsideBlast,{})[4] > 0,
		"row fire cannot destroy a paid worker whose birth is after the explosion");
	outsideBlast.current[0].body.spawnAt = 0;
	outsideBlast.counters[0].blast.x = 1050; outsideBlast.counters[0].blast.reach[0] = 130;
	check(ColdStorageSearch::Evaluate(outsideBlast,{})[4] == 0,
		"a finite blast reaches an offscreen worker inside its actual radius");
	outsideBlast.counters[0].blast.x = 900;
	check(ColdStorageSearch::Evaluate(outsideBlast,{})[4] > 0,
		"removing the screen edge cutoff does not extend the blast radius");
	std::cout << "Offscreen ash geometry and future birth boundaries passed\n";
	ColdStorageSearch::Snapshot counterTiming;
	outsideWorker.body.x = 900; outsideWorker.body.spawnAt = 0;
	counterTiming.current = {outsideWorker,outsideWorker};
	counterTiming.current[1].body.spawnAt = 4;
	rowFire.blast.committed = false; rowFire.blast.ready = 0;
	rowFire.recharge = 1000; rowFire.windup = 1;
	counterTiming.counters = {rowFire};
	const auto immediate = ColdStorageSearch::Evaluate(counterTiming,{});
	const auto patient = ColdStorageSearch::Evaluate(counterTiming,{},nullptr,8);
	check(patient[4] < immediate[4] && patient[6] > immediate[6],
		"a patient counter can catch a following worker that survives an immediate early cast");
	const auto robustTiming = ColdStorageSearch::Search(counterTiming,earn,31);
	check(robustTiming.counterHoldSeconds == 8 && robustTiming.features == patient && robustTiming.baselineFeatures == patient,
		"search and waiting baseline use one complete conservative counter outcome, without mixing hypothetical worlds");
	counterTiming.counters[0].blast.committed = true;
	check(ColdStorageSearch::Evaluate(counterTiming,{}) == ColdStorageSearch::Evaluate(counterTiming,{},nullptr,8),
		"the patient model cannot postpone an explosion already committed by the real player");
	counterTiming.counters[0].blast.committed = false;
	counterTiming.current[0].body.x = counterTiming.houseX+100;
	counterTiming.current[1].body.x = counterTiming.houseX+100;
	check(ColdStorageSearch::Evaluate(counterTiming,{}) == ColdStorageSearch::Evaluate(counterTiming,{},nullptr,8),
		"a patient player still uses an urgent counter instead of waiting for a larger crowd near the house");
	counterTiming.counters[0].sunCost = 150; counterTiming.playerSun = 149;
	check(ColdStorageSearch::Evaluate(counterTiming,{},nullptr,8)[6] == 0,
		"counter patience never waives the player's real resource requirement");
	std::cout << "Immediate and patient counter models, common baseline and committed casts passed\n";
	ColdStorageSearch::Snapshot threaded = counterTiming;
	threaded.capacity = 8; threaded.budget = 200;
	threaded.options.push_back({0,0,24,outsideWorker,{}});
	threaded.committed.push_back({1,{true,true,false,false,false,false}});
	ColdStorageSearch::StateModel ownedModel;
	threaded.stateModel = &ownedModel;
	ColdStorageSearch::Planner planner;
	check(planner.Start(threaded,earn,81), "background planner accepts one snapshot");
	check(!planner.Start(threaded,earn,82), "a busy planner never queues a duplicate purchase search");
	auto expectedQueue = threaded;
	const auto expectedRevision = ColdStorageSearch::ReplanCommitted(expectedQueue,earn,81 ^ 0x91A7u);
	ownedModel.coefficients[0][4] = 99; // 修改来源，不能改变已复制到工作线程的模型。
	threaded.current.clear();
	std::unique_ptr<ColdStorageSearch::Planner::Work> completed;
	const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10);
	while (!completed && std::chrono::steady_clock::now() < deadline) {
		completed = planner.TakeReady();
		if (!completed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	check(completed && !completed->failed && !planner.Busy(), "background result is completed and consumed exactly once");
	check(completed->snapshot.stateModel->coefficients[0][4] == 0 && completed->snapshot.current.size() == 2,
		"the worker owns policy models and entity values independently of its caller");
	check(completed->revision.afterScore == expectedRevision.afterScore
		&& completed->revision.changed == expectedRevision.changed
		&& completed->snapshot.current[1].body.row == expectedQueue.current[1].body.row
		&& completed->snapshot.current[1].body.spawnAt == expectedQueue.current[1].body.spawnAt,
		"background paid-queue replanning matches the synchronous route and deadline exactly");
	const auto synchronous = ColdStorageSearch::Search(completed->snapshot,earn,81);
	check(synchronous.features == completed->result.features && synchronous.score == completed->result.score
		&& synchronous.evaluated == completed->result.evaluated && synchronous.actions.size() == completed->result.actions.size(),
		"background and synchronous searches retain exactly the same score, candidate budget and result");
	for (size_t i=0; i<synchronous.actions.size(); ++i)
		check(synchronous.actions[i].option == completed->result.actions[i].option
			&& synchronous.actions[i].delay == completed->result.actions[i].delay, "thread scheduling never changes the selected actions");
	check(!planner.TakeReady(), "a result cannot be delivered twice");
	check(planner.Start(threaded,earn,82), "planner can restart after completion");
	planner.Cancel();
	check(!planner.Busy() && !planner.TakeReady(), "cancellation releases worker ownership and publishes no partial result");
	std::cout << "Owned background snapshots, deterministic search and cancellation passed\n";
	{
		// 明确阻塞辅助线程，证明协调线程可继续独立计算；不靠睡眠猜测是否并行。
		ColdStorageSearch::PlanEvaluator helper;
		std::promise<void> entered, release;
		auto unlocked=release.get_future();
		const auto caller=std::this_thread::get_id();
		auto candidate=helper.Submit(std::packaged_task<ColdStorageSearch::Result()>([&] {
			const bool different=std::this_thread::get_id()!=caller;
			entered.set_value(); unlocked.wait();
			ColdStorageSearch::Result result; result.score=different ? 17 : -1; return result;
		}));
		entered.get_future().wait();
		const auto independent=ColdStorageSearch::Evaluate(counterTiming,{});
		check(independent==ColdStorageSearch::Evaluate(counterTiming,{})
			&& candidate.wait_for(std::chrono::seconds(0))!=std::future_status::ready,
			"coordinator can evaluate a whole world while the auxiliary thread is still occupied");
		release.set_value();
		check(candidate.get().score==17,"auxiliary computation runs on a different thread and delivers a whole result");
		auto failure=helper.Submit(std::packaged_task<ColdStorageSearch::Result()>([]() -> ColdStorageSearch::Result {
			throw std::runtime_error("candidate failed");
		}));
		bool propagated=false;
		try { failure.get(); } catch(const std::runtime_error&) { propagated=true; }
		check(propagated && helper.Submitted()==2,"candidate failure is delivered without killing the auxiliary worker");
	}
	{
		using namespace ColdStorageSearch;
		Snapshot live; live.houseX=-10000; live.searchVersion=2; live.netEconomy=true;
		live.budget=600; live.capacity=24;
		Option income; income.type=901; income.cost=24; income.unit.body.x=900;
		income.unit.body.health=500; income.unit.body.economic=true; income.unit.body.value=24;
		Option support; support.type=902; support.cost=35; support.unit.engineer=true;
		support.unit.body.x=1010; support.unit.body.health=1000; support.unit.body.value=35;
		live.options={income,support};
		Counter blast; blast.blast.committed=true; blast.blast.x=900;
		blast.blast.ready=10; blast.blast.reach.fill(40); blast.blast.damage=1800; live.counters={blast};
		const Weights profit{0,0,0,0,1,-1,0,0};
		Planner pair;
		check(pair.Start(live,profit,7,600),"real-time planner starts a two-worker search under the existing budget");
		std::unique_ptr<Planner::Work> result;
		const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);
		while(!result && std::chrono::steady_clock::now()<limit) {
			result=pair.TakeReady();
			if(!result) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(result && !result->failed && result->workerThreads==2 && result->parallelPlans>0
			&& result->budgetMilliseconds==600,"two cooperating workers retain one shared wall-clock budget and result");
		check(result->result.refinementEvaluated>0 && result->result.largestPlan>=24,
			"a full-capacity cohort receives generic small replacement comparisons");
		int spent=0; bool incomeChosen=false, supportChosen=false;
		for(const auto& action:result->result.actions) {
			spent+=live.options[action.option].cost;
			incomeChosen|=action.option==0; supportChosen|=action.option==1;
		}
		check(spent<=live.budget && result->result.actions.size()<=static_cast<size_t>(live.capacity)
			&& incomeChosen && supportChosen,"parallel exploration finds payable cooperation without a fixed composition");
		check(live.budget==600 && live.options[1].unit.canisterFull && live.options[1].unit.body.health==1000,
			"parallel forecasts do not pay from or consume canisters in the caller snapshot");
		check(pair.Start(live,profit,8,600),"two-worker task can restart");
		pair.Cancel();
		check(!pair.Busy() && !pair.TakeReady(),"cancel joins both workers and delivers no partial purchase");
	}
	std::cout << "Two-worker candidate delivery, exception propagation, refinement and cancellation passed\n";

	{
	// Same total health has different meaning for a shield and a helmet against lobbed splash.
	ColdStorageSearch::Snapshot armorCase;
	ColdStorageSearch::Unit door;
	door.body.x = 900; door.body.health = 1370; door.shieldHealth = 1100; door.body.purchaseCost = 8;
	ColdStorageSearch::Plant melon;
	melon.x = 300; melon.health = 300; melon.dps = 40; melon.melon = true; melon.hitDamage = 120;
	armorCase.plants = {melon}; armorCase.current = {door};
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] == 0, "melon kills a door's body despite intact shield health");
	auto reinforced = door;
	reinforced.body.health = 1300; reinforced.shieldHealth = 1030;
	reinforced.shieldedHitCap = 10; reinforced.shieldedAshCap = 320;
	reinforced.blocksShieldBypass = reinforced.blocksFumePiercing = true; reinforced.fumeMultiplier = 2;
	armorCase.current = {reinforced};
	const float reinforcedAssets = ColdStorageSearch::Evaluate(armorCase,{})[3];
	check(std::abs(reinforcedAssets-8.0f*900/1300) < .001f,
		"reinforced door melon hit is capped at ten on BOTH shield and body");
	armorCase.current[0].body.health = 1130; // 100 body + 1030 shield, dies during the forecast.
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] == 0, "reinforced door's surviving shield cannot keep its dead body alive");
	armorCase.current = {reinforced}; armorCase.current[0].shieldHealth = 0; armorCase.current[0].body.health = 270;
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] == 0, "broken reinforced door loses its per-hit cap");
	armorCase.current = {door}; armorCase.plants[0].melon = false; armorCase.plants[0].dps = 10;
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] > 0, "ordinary door still absorbs frontal non-piercing fire");
	armorCase.plants[0].bypassShield = true;
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] == 0, "lobbed shield bypass reaches the ordinary door body");
	armorCase.current = {reinforced};
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] > 0, "reinforced shield rejects a lobbed bypass request");
	armorCase.plants.clear(); armorCase.current = {reinforced};
	ColdStorageSearch::Counter ash;
	ash.blast.x=900; ash.blast.reach.fill(-1); ash.blast.reach[0]=100; ash.blast.damage=1800; ash.blast.committed=true;
	armorCase.counters = {ash};
	check(std::abs(ColdStorageSearch::Evaluate(armorCase,{})[3]-8.0f*980/1300) < .001f,
		"reinforced shield caps one ash hit at 320 without erasing its body");
	armorCase.current = {door};
	check(ColdStorageSearch::Evaluate(armorCase,{})[3] == 0, "ordinary shield has no reinforced ash resistance");
	armorCase.counters.clear(); armorCase.plants = {melon};
	armorCase.current = {reinforced,reinforced}; armorCase.current[1].body.row=1;
	check(std::abs(ColdStorageSearch::Evaluate(armorCase,{})[3]-16.0f*900/1300) < .002f,
		"reinforced cap applies separately to direct melon and adjacent splash hits");
	check(ColdStorageSearch::ShieldProtectionFraction(door,melon)==0
		&& ColdStorageSearch::ShieldProtectionFraction(reinforced,melon)>.9f,
		"ordinary door has no anti-melon protection preference but reinforced damage cap remains valuable");
	armorCase.current = {reinforced}; armorCase.plants[0].melon=false; armorCase.plants[0].fume=true;
	armorCase.plants[0].multiTarget=true; armorCase.plants[0].dps=20; armorCase.plants[0].hitDamage=20;
	ColdStorageSearch::Unit rearWorker;
	rearWorker.body.x=1000; rearWorker.body.health=500; rearWorker.body.economic=true;
	armorCase.current.push_back(rearWorker);
	check(ColdStorageSearch::Evaluate(armorCase,{})[4] > 100,
		"reinforced shield interrupts linear fume before it reaches a rear worker");
	armorCase.plants[0].around=true;
	check(ColdStorageSearch::Evaluate(armorCase,{})[4] < 100,
		"gloom's surrounding cloud is not stopped by a reinforced shield in another direction");
	std::cout << "Shield layers, melon parallel damage and reinforced door resistance passed\n";

	}
	{
	using namespace ColdStorageSearch;
	Snapshot echoCase; echoCase.houseX=-10000; echoCase.gridLeft=0; echoCase.cellWidth=100;
	Plant echo; echo.x=550; echo.column=5; echo.health=300; echo.dps=50;
	echo.multiTarget=echo.echo=true; echo.range=600; echo.hitDamage=EchoWaveRules::Damage;
	echoCase.plants={echo};
	Unit inside; inside.body.x=850; inside.body.health=1000; inside.body.purchaseCost=10;
	Unit outside=inside; outside.body.x=880; outside.body.blastAnchorOffset=30;
	echoCase.current={inside,outside};
	check(Evaluate(echoCase,{})[3]==10,
		"Echo hits the board target but cannot apply collateral to an object outside the last column");
	echoCase.current={outside};
	check(Evaluate(echoCase,{})[3]==10,"an outside object cannot trigger Echo damage by its collider anchor");
	echoCase.current[0].body.x=905; echoCase.current[0].body.blastAnchorOffset=-10;
	check(Evaluate(echoCase,{})[3]==0,"Echo uses object position when its collider anchor remains outside");
	echoCase.current[0].body.x=540; echoCase.current[0].body.blastAnchorOffset=0;
	check(Evaluate(echoCase,{})[3]==10,"Echo cannot damage an object behind its launch cell center");
	echoCase.current={inside}; echoCase.current[0].body.health=5000;
	echoCase.current[0].shieldHealth=3000; echoCase.current[0].shieldedHitCap=10;
	check(std::abs(Evaluate(echoCase,{})[3]-9.4f)<.001f,
		"Echo's 100 damage per hit yields five capped DPS and does not bypass a shield");
	std::cout<<"Echo board entry, object anchors, collateral and single-hit caps passed\n";
	}
	{
	ColdStorageSearch::Snapshot broad;
	broad.capacity = 1; broad.budget = 24; broad.netEconomy = true;
	ColdStorageSearch::Unit harmless;
	harmless.body.x = 1150; harmless.body.health = 500;
	harmless.body.purchaseCost = 24;
	// No preferred type ids, every type has the same row count. Only the last type can earn anything.
	for (int type=0; type<48; ++type) for (int row=0; row<5; ++row) {
		auto unit = harmless; unit.body.row = row; unit.body.economic = type==47;
		broad.options.push_back({type,row,24,unit,{}});
	}
	const ColdStorageSearch::Weights returns{0,0,0,0,1,-1,0,0};
	for (unsigned seed=1; seed<=64; ++seed) {
		const auto chosen = ColdStorageSearch::Search(broad,returns,seed);
		check(chosen.actions.size()==1 && broad.options[chosen.actions[0].option].unit.body.economic,
			"wide roster evaluates every affordable type instead of randomly missing its sole profitable investment");
	}
	broad.plants.push_back({}); broad.plants[0].x=300; broad.plants[0].health=300; broad.plants[0].dps=10000;
	broad.plants[0].rowRadius=5; broad.plants[0].multiTarget=true;
	for (auto& option : broad.options) { option.unit.body.x=900; option.unit.productionRemaining=10; }
	check(ColdStorageSearch::Search(broad,returns,8).actions.empty(),
		"coverage does not force an economic unit when every investment dies before production");
	std::cout << "Wide roster opportunity coverage and optional economy passed\n";
	}

	{
	auto crowded = followup;
	crowded.plants.clear(); crowded.options.clear();
	for (int row=0; row<5; ++row) {
		ColdStorageSearch::Plant fire;
		fire.row=row; fire.x=300; fire.health=300; fire.dps=200; fire.edible=false;
		crowded.plants.push_back(fire);
	}
	crowded.current[0].body.health=20000;
	for (int type=0; type<48; ++type) for (int row=0; row<5; ++row) {
		auto option=producer;
		option.type=type; option.row=option.unit.body.row=row; option.preference={};
		option.unit.body.x=900; option.unit.body.speed=0; option.unit.body.economic=type==47;
		crowded.options.push_back(option);
	}
	ColdStorageSearch::Weights returnWeights{}; returnWeights[4]=1; returnWeights[5]=-1;
	int missed=0;
	for (unsigned seed=1; seed<=64; ++seed) {
		const auto result=ColdStorageSearch::Search(crowded,returnWeights,seed);
		if (result.actions.empty()) ++missed;
		check(result.routeEvaluated>0 && result.routeEvaluated<=256 && result.combinationEvaluated<=80,"generic route and pair exploration have bounded budgets");
		check((result.combinationBestBreach && !result.combinationBaseBreach)
			|| result.combinationBestScore>=result.combinationBaseScore,"generic refinement retains the incumbent unless it improves the goal");
	}
	check(missed==0,"wide-roster search must compare protected economic routes even if a random unsafe sample was discarded");
	std::cout << "Protected investment in 48-type roster missed " << missed << "/64; known safe production="
		<< ColdStorageSearch::Evaluate(crowded,{{47*5+3,0}})[4] << "\n";
	ColdStorageSearch::Counter wipe;
	wipe.blast.committed=true; wipe.blast.damage=100000; wipe.blast.x=800;
	wipe.blast.reach.fill(-1); wipe.blast.reach[3]=10000;
	crowded.counters={wipe};
	check(ColdStorageSearch::Search(crowded,returnWeights,13).actions.empty(),"a committed wipe removes the escort before new production can be justified");
	crowded.counters.clear(); crowded.current.clear();
	check(ColdStorageSearch::Search(crowded,returnWeights,13).actions.empty(),"unprotected production still loses to waiting");
	crowded.plants.clear();
	for (int row=0; row<5; ++row) { crowded.options[row].unit.body.x=1; crowded.options[row].unit.body.speed=20; }
	returnWeights[2]=500;
	const auto immediateWin=ColdStorageSearch::Search(crowded,returnWeights,13);
	check(immediateWin.features[2]>0 && !crowded.options[immediateWin.actions[0].option].unit.body.economic,
		"a winning attack is retained instead of forcing an available economic investment");
	}


	// Paid abilities share the actual forecast wallet; candidate purchase and activation are separate edges.
	{
	using namespace ColdStorageSearch;
	Snapshot battle; battle.houseX = -1000; battle.budget = 20; battle.capacity = 1;
	Plant wall; wall.id=1; wall.row=0; wall.column=4; wall.x=800; wall.health=100000; wall.reward=100;
	battle.plants={wall};
	Unit boiler; boiler.body.row=0; boiler.body.x=850; boiler.body.speed=20; boiler.body.health=1000;
	boiler.body.purchaseCost=15; boiler.biteDps=50;
	auto& b=boiler.burst;
	b.range=400; b.cost=5; b.stopHealth=333; b.windup=2; b.duration=8; b.recovery=5; b.retry=1;
	b.moveMultiplier=5; b.biteMultiplier=10; b.recoveryMoveMultiplier=.5f;
	Option option; option.cost=15; option.unit=boiler; battle.options={option};
	ConstructionStats paid, broke;
	const auto full=Evaluate(battle,{{0,0}},&paid);
	battle.budget=15;
	const auto empty=Evaluate(battle,{{0,0}},&broke);
	check(paid.burstActivations==1 && paid.abilityIceSpent==5 && full[5]==20,"purchase leaves exactly one skill fee; commit only once");
	check(broke.burstActivations==0 && empty[5]==15 && full[1]>empty[1],"cannot borrow hypothetical skill money after purchase");
	check(battle.options[0].unit.burst.stage==PaidBurst::Stage::READY && battle.budget==15,"forecast never mutates authoritative snapshot");
	battle.supplyRemaining=10; battle.supplyInterval=30; battle.supplyIce=5;
	Evaluate(battle,{{0,0}},&paid);
	check(paid.burstActivations==1 && paid.abilityIceSpent==5,"later real supply funds a retry rather than free initial burst");
	battle.supplyInterval=0; battle.budget=5; battle.options.clear(); battle.current={boiler,boiler};
	Evaluate(battle,{},&paid);
	check(paid.burstActivations==1,"two boilers must compete for the same remaining five ice");
	battle.current={boiler}; battle.budget=0;
	Unit maker; maker.body.row=1; maker.body.x=900; maker.body.health=500; maker.body.economic=true;
	maker.productionRemaining=4; maker.nextYield=5;
	battle.current.push_back(maker); Evaluate(battle,{},&paid);
	check(paid.burstActivations==1,"surviving economic ally can fund a future paid burst");
	battle.current={boiler}; battle.budget=5; battle.current[0].body.health=333;
	Evaluate(battle,{},&paid);
	check(paid.burstActivations==0,"headless boiler cannot pay or activate");
	battle.current[0]=boiler; battle.current[0].body.stopped=60;
	Evaluate(battle,{},&paid);
	check(paid.burstActivations==0,"hard control prevents starting windup and premature payment");
	battle.current[0].burst.stage=PaidBurst::Stage::WINDUP; battle.current[0].burst.remaining=1;
	Evaluate(battle,{},&paid); check(paid.burstActivations==0,"hard control also pauses an already started windup");
	battle.current[0]=boiler; battle.plants[0].dps=10000;
	Evaluate(battle,{},&paid); check(paid.burstActivations==0,"death before commit never pays the fee");
	battle.plants[0].dps=0;
	battle.budget=0; battle.current[0]=boiler;
	battle.current[0].burst.stage=PaidBurst::Stage::ACTIVE; battle.current[0].burst.remaining=8;
	const auto committed=Evaluate(battle,{},&paid);
	check(paid.burstActivations==0 && paid.abilityIceSpent==0 && committed[1]>empty[1],"live active snapshot keeps paid power without charging twice");
	battle.current[0].body.stopped=8;
	const auto expired=Evaluate(battle,{},&paid);
	battle.current[0].burst.stage=PaidBurst::Stage::SPENT;
	const auto ordinaryAfterFreeze=Evaluate(battle,{});
	check(expired[1]<ordinaryAfterFreeze[1],"paid burst expires while frozen, followed by no-bite recovery");
	battle.current[0]=boiler; battle.current[0].burst.stage=PaidBurst::Stage::RECOVERY; battle.current[0].burst.remaining=60;
	const auto venting=Evaluate(battle,{});
	check(venting[0]==0 && venting[1]==0 && venting[2]==0,"recovery cannot eat or cross a blocking plant");
	battle.current[0]=boiler; battle.current[0].body.x=1400; battle.current[0].body.speed=0; battle.budget=5;
	Evaluate(battle,{},&paid);
	check(paid.burstActivations==0,"out of trigger range cannot spend skill fee");
	battle.current[0].body.x=1220; battle.current[0].body.blastAnchorOffset=-30;
	Evaluate(battle,{},&paid);
	check(paid.burstActivations==1,"trigger range follows object origin rather than offset collision center");
	Result incremental; incremental.actions={{0,0}}; incremental.features[5]=5; incremental.baselineFeatures[5]=5;
	check(!ShouldRegroup(incremental,0,48),"baseline skill expense is not charged again to an incremental plan");
	std::cout << "Paid burst wallet, control, stage and retry contracts passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot arena; arena.houseX=-1000;
	Plant shooter; shooter.id=1; shooter.x=300; shooter.row=1; shooter.column=1; shooter.dps=10; shooter.health=300;
	Plant source; source.id=2; source.x=400; source.row=1; source.column=2; source.health=300;
	Unit victim; victim.body.row=1; victim.body.x=1000; victim.body.speed=0; victim.body.health=10000; victim.body.purchaseCost=10000;
	arena.plants={shooter,source}; arena.current={victim};
	const auto normal=Evaluate(arena,{});
	AttackAura aura; aura.plantID=2; aura.duration=12; aura.recharge=9; aura.bonus=1; aura.iceCost=30; aura.active=12;
	arena.attackAuras={aura}; ConstructionStats stats;
	const auto boosted=Evaluate(arena,{},&stats);
	check(std::abs(normal[3]-boosted[3]-120)<.1f && stats.auraActivations==0,"existing aura expires after twelve seconds and is not charged again");
	check(Evaluate(arena,{},nullptr,0,0,0,false,false,-1,true)==boosted,
		"preserving manual abilities cannot cancel an already paid active aura");
	arena.playerIce=30; arena.attackAuras[0].active=0;
	const auto renewed=Evaluate(arena,{},&stats);
	check(stats.auraActivations==1 && stats.iceSpent==30 && std::abs(renewed[3]-boosted[3])<.1f,"manual aura uses shared wallet only when a covered shooter has a target");
	arena.attackAuras.push_back(arena.attackAuras[0]); arena.attackAuras[1].plantID=3;
	source.id=3; arena.plants.push_back(source); arena.playerIce=30;
	Evaluate(arena,{},&stats);
	check(stats.auraActivations==1,"two auras cannot double spend the same resource");
	arena.playerIce=0; for(auto& a:arena.attackAuras) a.active=12;
	const auto stacked=Evaluate(arena,{});
	check(std::abs(normal[3]-stacked[3]-240)<.1f,"two active sources add attack bonuses instead of multiplying them");
	arena.attackAuras.resize(1); arena.plants.resize(2); arena.plants[1].health=0;
	check(Evaluate(arena,{})==normal,"destroyed aura source cannot keep buffing the field");
	arena.plants[1].health=300; arena.attackAuras[0].active=0; arena.attackAuras[0].blockedUntil=60; arena.playerIce=90;
	Evaluate(arena,{},&stats); check(stats.auraActivations==0,"shutdown source cannot activate before recovery");
	arena.attackAuras[0].blockedUntil=0; arena.current.clear();
	Evaluate(arena,{},&stats); check(stats.auraActivations==0,"manual mode does not waste ice with no target");
	arena.attackAuras[0].automatic=true;
	Evaluate(arena,{},&stats); check(stats.auraActivations==3 && stats.iceSpent==90,"automatic mode follows real unconditional activation and recharge");
	arena.playerIce=0; arena.playerSun=100; arena.anticipateEconomy=true;
	arena.shop={{100,30,2}};
	Evaluate(arena,{},&stats);
	check(stats.orders==1 && stats.auraActivations==1 && stats.iceSpent==30,"active ability demand can purchase ice without inventing free skill activations");
	arena.attackAuras[0].automatic=false; arena.current={victim};
	Evaluate(arena,{},&stats,0,0,0,false,false,-1,true);
	check(stats.orders==0 && stats.auraActivations==0,
		"holding a manual aura also removes its uncommitted ice-order demand");
	std::cout << "Temporary attack aura duration, wallet, stacking and source lifetime passed\n";

	// 大额投入不能因库存高或兵种偏好而跳过回本检查；已有工人收入仍从基线扣除。
	Result capital;
	capital.actions = {{0,0}}; capital.features[5] = 320;
	capital.features[4] = capital.baselineFeatures[4] = 600;
	capital.features[6] = 300; capital.preferenceScore = 500;
	check(ShouldConserveCapital(capital,400,48),"large doomed purchase cannot consume a healthy treasury");
	capital.features[4] += 320;
	check(!ShouldConserveCapital(capital,400,48),"a genuinely cash-profitable investment may still commit a large budget");
	capital.features[4] = 600; capital.features[2] = 1;
	check(!ShouldConserveCapital(capital,400,48),"a surviving breakthrough is not blocked by capital conservation");
	capital.features[2] = 0; capital.features[5] = 24; capital.features[6] = 24;
	check(!ShouldConserveCapital(capital,400,48),"small counter bait does not require immediate full payback");
	// 连续小额亏损不能每次按当前钱包重新获得试错额度；账本盈利与有效在场资产可补回额度。
	check(std::abs(RemainingCapitalRisk(1000,1000)-350)<.01f,"fresh treasury has bounded experimental loss capacity");
	check(std::abs(RemainingCapitalRisk(1000,700)-50)<.01f,"realized loss consumes the cumulative allowance");
	check(RemainingCapitalRisk(1000,600)==0,"successive small losses can exhaust the allowance before the wallet is empty");
	Result precisionOnly;
	precisionOnly.precisionTargetID=1;
	precisionOnly.features[5]=180; precisionOnly.features[0]=74;
	check(ShouldConserveCapital(precisionOnly,400,48,0),
		"a skill-only purchase consumes the same exhausted capital allowance as troops");
	check(ShouldRegroup(precisionOnly,200,240),
		"an empty troop cart does not bypass low-stock recovery for a paid precision strike");
	precisionOnly.baselineOpponentAssets=200; precisionOnly.opponentScore=200;
	check(!ShouldConserveCapital(precisionOnly,400,48,0),
		"a skill-only attack may still trade cash for independently forecast opponent attrition");
	precisionOnly.opponentScore=0; precisionOnly.features[0]=180;
	check(!ShouldConserveCapital(precisionOnly,400,48,0),
		"a fully repaid skill-only attack remains legal without remaining loss allowance");
	precisionOnly.features[0]=0; precisionOnly.features[2]=1;
	check(!ShouldConserveCapital(precisionOnly,400,48,0),
		"a real skill-only house breach remains ahead of capital conservation");
	Result freeWait;
	check(!ShouldConserveCapital(freeWait,400,48,0),"a zero-cost empty wait remains exempt");
	Result attrition; attrition.actions={{0,0}}; attrition.features[5]=84; attrition.features[0]=49;
	attrition.opponentScore=172; attrition.baselineOpponentAssets=9907; attrition.opponentAssets=9735;
	check(!ShouldConserveCapital(attrition,353,48,0),
		"depleted historical risk does not veto a small trade that costs the opponent more than the incremental loss");
	attrition.opponentAssets=attrition.baselineOpponentAssets;
	check(ShouldConserveCapital(attrition,353,48,0),"no extra opponent loss cannot justify repeated cash-losing probes");
	attrition.opponentAssets=9735; attrition.features[5]=220;
	check(!ShouldConserveCapital(attrition,353,48,0),"a large cash-losing attack may trade for greater actual opponent losses");
	attrition.opponentAssets=9780;
	check(ShouldConserveCapital(attrition,353,48,0),"a large losing attack without sufficient opponent losses remains blocked");
	attrition.opponentAssets=9735;
	attrition.features[5]=84; attrition.opponentScore=0;
	check(ShouldConserveCapital(attrition,353,48,0),"disabled opponent valuation cannot invent attrition credit");
	check(RemainingCapitalRisk(1000,1200)>RemainingCapitalRisk(1000,1000),"earned cash and surviving paid assets replenish risk capacity");
	check(ShouldConserveCapital(capital,400,48,0),"spent loss capacity blocks another individually small doomed purchase");
	capital.features[3]=24;
	check(!ShouldConserveCapital(capital,400,48,0),"a surviving frontline remains useful even without immediate production");
	capital.features[3]=0; capital.features[4]+=24;
	check(!ShouldConserveCapital(capital,400,48,0),"cash-profitable rebuilding remains possible with no remaining risk allowance");
	capital.features[4]=600; capital.features[2]=1;
	check(!ShouldConserveCapital(capital,400,48,0),"verified house breach is ahead of cumulative capital recovery");
	capital.features[2]=0;
	capital.features[5] = 180; capital.features[6] = 0;
	check(ShouldConserveCapital(capital,200,48),"depleting the treasury needs incremental return even without ash");
	capital.features[0] = 180;
	check(!ShouldConserveCapital(capital,200,48),"paid plant kills can finance an otherwise large attack");
	// 真人第13波的历史预测：交换回报可放行现金亏损，只有削血而没有资产损耗仍拦截。
	Result humanBurst; humanBurst.actions.assign(40,{0,0});
	humanBurst.features={222,227.3515625f,0,0,18,480,430.414795f,0};
	humanBurst.baselineFeatures={0,0,0,0,18,0,179.664764f,0};
	humanBurst.opponentScore=668.683838f; humanBurst.baselineOpponentAssets=3881.985107f; humanBurst.opponentAssets=3213.30127f;
	check(!ShouldConserveCapital(humanBurst,762,48),"large assault may trade its cash deficit for greater opponent asset losses");
	humanBurst.opponentAssets=humanBurst.baselineOpponentAssets;
	check(ShouldConserveCapital(humanBurst,762,48),"a wealthy wallet and partial damage alone cannot justify losing the whole large assault");
	humanBurst.opponentAssets=3213.30127f;
	humanBurst.features[4]+=480;
	check(!ShouldConserveCapital(humanBurst,762,48),"the same high-risk investment is allowed when incremental cash actually covers it");
	humanBurst.features[4]=18; humanBurst.features[2]=1;
	check(!ShouldConserveCapital(humanBurst,762,48),"a real breakthrough remains exempt from the cash-risk gate");
	// 普通火力同样结算本金；对方真实资产损耗与单纯削血应区别处理。
	Result helmetBurst; helmetBurst.actions.assign(34,{0,0});
	helmetBurst.features={159,161.1876068f,0,0,0,492,100.5600357f,0};
	helmetBurst.baselineFeatures={12,13.015625f,0,0,0,0,45.7583313f,0};
	helmetBurst.opponentScore=1015.5070801f;
	helmetBurst.baselineOpponentAssets=4274.7001953f; helmetBurst.opponentAssets=3259.1931152f;
	check(!ShouldConserveCapital(helmetBurst,789,48),"ordinary-fire losses may be justified by greater actual opponent attrition");
	helmetBurst.opponentAssets=helmetBurst.baselineOpponentAssets;
	check(ShouldConserveCapital(helmetBurst,789,48),"ordinary-fire capital loss without sufficient opponent loss remains blocked");
	helmetBurst.opponentAssets=3259.1931152f;
	helmetBurst.features[3]=200;
	check(!ShouldConserveCapital(helmetBurst,789,48),"surviving frontline can justify the same investment without forcing immediate cash payback");
	helmetBurst.features[3]=0; helmetBurst.features[4]=492;
	check(!ShouldConserveCapital(helmetBurst,789,48),"production that actually repays the investment remains allowed");
	helmetBurst.features[4]=0; helmetBurst.features[2]=1;
	check(!ShouldConserveCapital(helmetBurst,789,48),"winning ordinary-fire attack remains ahead of capital recovery");
	Snapshot cashSearch; cashSearch.searchVersion = 2; cashSearch.netEconomy = true;
	cashSearch.budget = 400; cashSearch.capacity = 64; cashSearch.recoveryReserve = 48;
	Option speculative; speculative.cost = 24; speculative.preference[0] = 500;
	speculative.unit.body.x = 1000; speculative.unit.body.health = 100; speculative.unit.body.purchaseCost = 24;
	Plant lethalFire; lethalFire.health = 1000; lethalFire.x = 300; lethalFire.dps = 10000; lethalFire.edible = false;
	cashSearch.options = {speculative}; cashSearch.plants = {lethalFire};
	const auto conserved = Search(cashSearch,InitialWeights,123);
	check(conserved.capitalRejected > 0 && conserved.features[5] <= 200,
		"portfolio search actually filters excessive speculative spending before picking an alternative");

	// 持续成长用真实开火时间推进；菠萝对成长的促进远大于冻结初始 DPS 后简单翻倍。
	Snapshot growing; growing.houseX = -1000;
	Plant novice; novice.id = 1; novice.x = 300; novice.health = 500; novice.dps = 8/1.5f;
	novice.growth = {0,.9f,100,1.5f,.2f,.85f,3,5,8,1,28};
	Unit trainingTarget; trainingTarget.body.x = 1000; trainingTarget.body.health = 50000;
	trainingTarget.body.purchaseCost = 50000;
	growing.plants = {novice}; growing.current = {trainingTarget};
	const float grownLoss = 50000-Evaluate(growing,{})[3];
	growing.plants[0].growth.perShot = 0;
	const float fixedLoss = 50000-Evaluate(growing,{})[3];
	check(grownLoss > fixedLoss*4,"a novice shooter becomes a serious threat within the forecast");
	growing.plants[0] = novice;
	Plant growthSource; growthSource.id = 2; growthSource.x = 400; growthSource.column = 1; growthSource.health = 500;
	growing.plants.push_back(growthSource);
	AttackAura growthAura; growthAura.plantID = 2; growthAura.active = 10; growthAura.bonus = 1;
	growthAura.iceCost = 40; growthAura.recharge = 12; growthAura.duration = 10;
	growing.attackAuras = {growthAura};
	const float acceleratedLoss = 50000-Evaluate(growing,{})[3];
	check(acceleratedLoss > grownLoss+fixedLoss/6,"temporary aura accelerates later growth as well as immediate fire");
	growing.current[0].body.x = 1200;
	check(Evaluate(growing,{})[3] == 50000,"growth forecast cannot attack an unseen offscreen target");
	check(growing.plants[0].growth.progress == 0,"forecast growth never changes the input snapshot");
	std::cout << "Capital conservation and growing attack aura counterfactuals passed\n";

	// 预存清场不一定会被首只工人骗掉：长期蓄爆必须独立比较完整、可支付的一条时间线。
	Snapshot storedDoom; storedDoom.houseX = -1000;
	Plant storedSource; storedSource.id = 10; storedSource.x = 800; storedSource.health = 300;
	storedDoom.plants = {storedSource}; storedDoom.playerSun = 100; storedDoom.playerIce = 5;
	Counter savedBlast; savedBlast.plantID = 10; savedBlast.stored = true;
	savedBlast.sunCost = 75; savedBlast.iceCost = 5; savedBlast.windup = 3.4f;
	savedBlast.recharge = 10000; savedBlast.blast.x = 800; savedBlast.blast.damage = 1800;
	savedBlast.blast.reach.fill(-1); savedBlast.blast.reach[0] = 275;
	storedDoom.counters = {savedBlast};
	Unit firstWorker; firstWorker.body.x = 900; firstWorker.body.health = 500;
	firstWorker.body.purchaseCost = 24; firstWorker.body.economic = true;
	storedDoom.current = {firstWorker,firstWorker,firstWorker};
	storedDoom.current[1].body.spawnAt = 24; storedDoom.current[2].body.spawnAt = 30;
	const auto earlyDoom = Evaluate(storedDoom,{});
	const auto heldDoom = Evaluate(storedDoom,{},nullptr,8,32);
	check(earlyDoom[6] == 24 && heldDoom[6] == 72 && heldDoom[4] < earlyDoom[4],
		"a stored doom can wait for late workers instead of disappearing after early bait");
	const auto storedResult = Search(storedDoom,InitialWeights,123);
	check(storedResult.counterHoldSeconds == 32 && storedResult.features == heldDoom,
		"search and its no-purchase baseline include the long stored-counter response");
	storedDoom.current[2].body.spawnAt = 55;
	const auto lateStoredResult = Search(storedDoom,InitialWeights,123);
	check(lateStoredResult.counterHoldSeconds == 55 && lateStoredResult.features[6] == 72,
		"late known arrivals extend stored patience instead of automatically escaping a fixed 32-second window");
	storedDoom.current[2].body.spawnAt = 30;
	storedDoom.plants[0].health = 0;
	check(Evaluate(storedDoom,{})[6] == 0,"destroyed stored doom cannot be awakened by a future coffee");
	storedDoom.anticipateEconomy = true; storedDoom.shop = {{100,100,0}}; storedDoom.playerIce = 0;
	ConstructionStats sourceStats;
	Evaluate(storedDoom,{},&sourceStats);
	check(sourceStats.orders == 0,"destroyed stored doom cannot invent a future ice order for coffee");
	storedDoom.anticipateEconomy = false; storedDoom.shop.clear();
	storedDoom.plants[0].health = 300; storedDoom.playerIce = 4;
	check(Evaluate(storedDoom,{},nullptr,8,32)[6] == 0,"holding a stored doom does not invent affordable coffee");
	storedDoom.playerIce = 5; storedDoom.counters[0].blast.committed = true;
	storedDoom.counters[0].blast.ready = 2; storedDoom.counters[0].sunCost = 1000;
	check(Evaluate(storedDoom,{}) == Evaluate(storedDoom,{},nullptr,8,32),
		"already committed wake/explosion ignores patience, new resource fees and card cooldown");

	Snapshot sharedCoffee; sharedCoffee.houseX = -1000; sharedCoffee.playerSun = 150; sharedCoffee.playerIce = 10;
	sharedCoffee.plants = {storedSource,storedSource}; sharedCoffee.plants[1].id = 11; sharedCoffee.plants[1].row = 4;
	sharedCoffee.current = {firstWorker,firstWorker}; sharedCoffee.current[1].body.row = 4;
	savedBlast.windup = 0; savedBlast.sharedSource = 2; savedBlast.sharedRecharge = 7.5f;
	sharedCoffee.counters = {savedBlast,savedBlast};
	sharedCoffee.counters[1].source = 1; sharedCoffee.counters[1].plantID = 11;
	sharedCoffee.counters[1].blast.reach[0] = -1; sharedCoffee.counters[1].blast.reach[4] = 275;
	check(Evaluate(sharedCoffee,{})[4] == 10,"two stored dooms must share the one real coffee-card cooldown");
	for (auto& counter : sharedCoffee.counters) counter.sharedSource = -1;
	check(Evaluate(sharedCoffee,{})[4] == 0,"independent trigger fixture differs from shared coffee");
	std::cout << "Stored doom patience, source lifetime, committed explosion and shared coffee passed\n";
	}


	{
	using namespace ColdStorageSearch;
	Snapshot state; state.houseX=-1000; state.budget=8;
	Unit guard; guard.body.x=900; guard.body.health=1800; guard.body.purchaseCost=2800;
	guard.repair={1000,2000,2800,266,5,5,300,4};
	Plant fire; fire.id=1; fire.x=300; fire.health=300; fire.dps=20;
	state.current={guard}; state.plants={fire}; ConstructionStats stats;
	const auto funded=Evaluate(state,{},&stats);
	check(stats.armorRepairs==2 && stats.armorRepairIce==8 && funded[5]==8,"repair spends shared wallet exactly once per successful cycle");
	state.budget=0; const auto broke=Evaluate(state,{},&stats);
	check(stats.armorRepairs==0 && funded[3]>broke[3],"repair cannot borrow nonexistent ice or restore body health");
	state.current[0].body.health=2800; state.current[0].repair.health=2000; state.plants[0].melon=true;
	const auto melon=Evaluate(state,{});
	check(std::abs(melon[3]-1600)<.01f,"melon consumes first-class shield before the body without double layer damage");
	state.plants.clear(); state.current[0]=guard; state.current[0].body.health=2800; state.current[0].repair.health=2000;
	Counter ash; ash.blast.committed=true; ash.blast.damage=1800; ash.blast.x=900; ash.blast.reach.fill(-1); ash.blast.reach[0]=100;
	state.counters={ash}; state.budget=4;
	const auto ashSurvivor=Evaluate(state,{},&stats);
	check(stats.armorRepairs==1 && std::abs(ashSurvivor[3]-1300)<.01f,"nonlethal ash leaves a repairable first-class shield");
	state.counters[0].blast.damage=2100;
	const auto broken=Evaluate(state,{},&stats);
	check(stats.armorRepairs==0 && std::abs(broken[3]-700)<.01f,"broken shield is permanent and cannot heal wounded body");
	state.counters.clear(); state.current={guard}; state.current[0].body.stopped=60;
	Evaluate(state,{},&stats); check(stats.armorRepairs==0,"hard control pauses guard repair timer");
	state.current[0].body.stopped=0; state.current[0].body.slow=60;
	Evaluate(state,{},&stats); check(stats.armorRepairs==1,"ordinary slow does not slow guard repair cycles");
	state.current={guard,guard}; state.budget=4;
	Evaluate(state,{},&stats); check(stats.armorRepairs==1,"two guards share one wallet rather than duplicating its balance");
	state.current={guard}; state.current[0].body.health=1000+266;
	Evaluate(state,{},&stats); check(stats.armorRepairs==0,"headless guard cannot repair even with surviving armor");
	state.current={guard}; state.current[0].repair.health=2000; state.current[0].body.health=2800;
	Evaluate(state,{},&stats); check(stats.armorRepairs==0,"full armor never spends repair money");
	state.current={guard}; state.budget=0; state.supplyRemaining=10; state.supplyInterval=30; state.supplyIce=4;
	Evaluate(state,{},&stats); check(stats.armorRepairs==2,"later supply can fund later cycles without replaying failed cycles");
	Snapshot escort; escort.houseX=-1000; escort.budget=80; escort.capacity=1;
	Unit producer; producer.body.x=990; producer.body.row=1; producer.body.health=500; producer.body.economic=true;
	producer.body.purchaseCost=12; escort.current={producer};
	fire.row=0; fire.dps=400; escort.plants={fire}; fire.row=1; fire.dps=80; escort.plants.push_back(fire);
	Option defense; defense.type=58; defense.cost=12; defense.unit=guard; defense.unit.body.health=2800;
	defense.unit.body.purchaseCost=12; defense.unit.repair.health=2000;
	for(int row=0; row<2; ++row) { defense.row=row; defense.unit.body.row=row; escort.options.push_back(defense); }
	const float alone=Evaluate(escort,{})[4];
	const auto covered=Evaluate(escort,{{1,0}},&stats);
	check(covered[4]>alone && stats.armorRepairs>0,"guard can sustain a producer against affordable incoming fire");
	const auto choice=Search(escort,InitialWeights,713);
	check(!choice.actions.empty() && escort.options[choice.actions[0].option].row==1,
		"search chooses the useful escort lane instead of spending on the lethal empty lane");
	std::cout << "Cold-chain first-class armor, paid repair, control and terminal break passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot state; state.houseX=-1000;
	Plant nut; nut.id=1; nut.x=800; nut.health=8000; nut.reward=20; nut.assetValue=20;
	nut.repairMaximum=IceStorageNutRules::kHealth; nut.repairAmount=IceStorageNutRules::kRepairHealth;
	nut.repairCost=IceStorageNutRules::kRepairIce; nut.repairRecharge=IceStorageNutRules::kRepairCooldown; nut.repairAutomatic=true;
	nut.crushDamage=IceStorageNutRules::kCrushDamage; nut.hasBurstProtection=true;
	nut.vehicleRetreat=IceStorageNutRules::kVehicleRetreatCells*80;
	Unit giant; giant.body.x=850; giant.body.health=3000; giant.body.purchaseCost=16; giant.body.speed=20; giant.body.smashSeconds=5;
	state.current={giant}; state.plants={nut}; ConstructionStats stats;
	const auto resistant=Evaluate(state,{});
	state.plants[0].crushDamage=0; const auto ordinary=Evaluate(state,{});
	check(resistant[7]<ordinary[7],"resistant nut delays giant instead of vanishing after one smash");
	state.plants={nut}; state.playerIce=100;
	const auto healed=Evaluate(state,{},&stats);
	check(healed[0]==0 && stats.plantRepairs>0 && stats.plantRepairIce==stats.plantRepairs*nut.repairCost,"affordable repairs keep resistant nut alive through repeated smashes");
	check(healed[1]>=0 && healed[1]<=nut.reward,"healed damage cannot be farmed for unlimited damage score");
	state.playerIce=0; state.current[0].body.spawnAt=55; state.current[0].body.smashSeconds=4;
	const auto one=Evaluate(state,{}); state.current.push_back(state.current[0]);
	check(Evaluate(state,{})[1]==one[1],"first giant triggers protection before simultaneous second smash");
	state.plants[0].hasBurstProtection=false;
	check(Evaluate(state,{})[1]>one[1],"without burst protection simultaneous giants each damage the nut");
	state.plants={nut};
	state.current={giant}; state.current[0].body.smashSeconds=0; state.current[0].vehicleCrush=true;
	const auto vehicle=Evaluate(state,{});
	check(vehicle[7]<ordinary[7],"vehicle must cover retreat distance instead of crossing the nut");
	state.current.clear(); state.plants={nut,nut}; state.plants[1].id=2;
	for(auto& p:state.plants) p.health=7000;
	state.playerIce=nut.repairCost; Evaluate(state,{},&stats);
	check(stats.plantRepairs==1 && stats.plantRepairIce==nut.repairCost,"two nuts compete for the actual player ice wallet");
	state.plants.resize(1); state.plants[0].health=0;
	Evaluate(state,{},&stats); check(stats.plantRepairs==0,"repair cannot revive a dead nut");
	state.plants={nut}; state.plants[0].health=7500;
	Evaluate(state,{},&stats); check(stats.plantRepairs==0,"automatic repair waits for a full recovery amount");
	state.plants[0].health=7000; state.plants[0].repairBlockedUntil=60;
	Evaluate(state,{},&stats); check(stats.plantRepairs==0,"shutdown blocks new repair while cooldown follows game time");
	state.plants={nut}; state.plants[0].health=7000; state.plants[0].repairAutomatic=false;
	Unit target; target.body.x=900; target.body.health=1700; target.body.purchaseCost=16;
	state.current={target,target}; state.playerIce=20;
	Counter emergency; emergency.iceCost=20; emergency.blast.damage=1800; emergency.blast.x=900;
	emergency.blast.reach.fill(-1); emergency.blast.reach[0]=100;
	state.counters={emergency}; const auto saved=Evaluate(state,{},&stats);
	check(stats.plantRepairs==0 && saved[6]==32,"manual healing cannot spend the last ice before a viable emergency ash response");
	std::cout << "Ice-storage nut crush, retreat, repair, shared cost and net damage credit passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-1000; s.budget=80; s.capacity=0; s.precisionReady=true; s.netEconomy=true;
	Plant flower; flower.id=1; flower.health=300; flower.reward=5; flower.x=400; flower.assetValue=35; flower.sunPerSecond=2;
	s.plants={flower};
	check(Search(s,InitialWeights,91).precisionTargetID==0,"precision retains 60 ice rather than deleting a low-value sunflower");
	Unit worker; worker.id=9; worker.body.health=500; worker.body.x=950; worker.body.economic=true; worker.body.purchaseCost=12;
	s.current={worker}; s.plants[0].dps=100;
	const auto targeted=Search(s,InitialWeights,91);
	check(targeted.precisionTargetID==1 && targeted.precisionGain>0,"precision removes lethal fire when protected production repays its opportunity cost");
	// 没有在场工人时，等待优案不能代表清除后的机会；必须比较技能和同路新工人的联合投入。
	auto joint=s; joint.current.clear(); joint.capacity=1; joint.budget=84;
	Option future; future.type=1; future.cost=12; future.unit=worker; future.unit.id=0;
	joint.options={future};
	const auto followed=Search(joint,InitialWeights,91);
	check(followed.largestPlan>=static_cast<int>(followed.actions.size()) && followed.routeEvaluated>0,
		"a winning precision plan retains the preceding formation coverage diagnostics");
	check(followed.precisionTargetID==1 && followed.actions.size()==1 && followed.features[4]>0,
		"precision jointly evaluates a new same-row worker even when the no-strike optimum waits");
	s.precisionTargetID=1; ConstructionStats stats;
	const auto fired=Evaluate(s,{},&stats);
	check(fired[5]==60 && stats.precisionHits==1 && stats.abilityIceSpent==60,"precision charges exactly once and resolves after its aim");
	s.budget=59; s.precisionTargetID=0;
	check(Search(s,InitialWeights,91).precisionTargetID==0,"precision cannot spend unaffordable ice");
	s.current.clear(); s.precisionReady=false; s.budget=0; s.pendingPrecisionID=1; s.pendingPrecisionRemaining=2;
	s.plants[0].immuneRemaining=60; s.plants[0].repairMaximum=300;
	Plant shell=flower; shell.id=2; shell.layer=2; shell.health=4000; shell.reward=30;
	s.plants.push_back(shell);
	const auto pending=Evaluate(s,{},&stats);
	check(pending[5]==0 && pending[0]==5 && stats.precisionHits==1,"committed precision bypasses immunity but preserves another layer in the cell");
	s.pendingPrecisionID=999; Evaluate(s,{},&stats);
	check(stats.precisionHits==0,"missing precision target never retargets a replacement at the same cell");
	s.pendingPrecisionID=0; s.current={worker}; s.current[0].body.economic=false; s.current[0].body.x=450; s.current[0].biteDps=1000;
	s.plants={flower}; s.pendingPrecisionID=1; s.pendingPrecisionRemaining=2;
	check(Evaluate(s,{},&stats)[0]==5 && stats.precisionHits==0,"target killed during aim earns only its original death reward");
	std::cout << "Precision value, affordability, delayed identity and layer accounting passed\n";
	}
	{
	using namespace ColdStorageSearch;
	// 清除主火力仍留下持续后备火力：少量跟兵不是完整进攻案，须比较同钱包可支付的大队。
	Snapshot s; s.rows=1; s.houseX=0; s.budget=220; s.capacity=16;
	s.searchVersion=2; s.precisionReady=true; s.netEconomy=true; s.opponentWeight=1;
	Plant key; key.id=401; key.x=400; key.health=100000; key.dps=1000; key.assetValue=1000;
	Plant reserve; reserve.id=402; reserve.x=500; reserve.health=400; reserve.dps=100;
	reserve.reward=10; reserve.assetValue=100;
	s.plants={key,reserve};
	Option troop; troop.type=731; troop.cost=10;
	troop.unit.body.x=900; troop.unit.body.health=200; troop.unit.body.speed=40;
	troop.unit.body.purchaseCost=10; troop.unit.biteDps=50;
	s.options={troop};
	std::vector<Action> full(16,{0,3});
	check(Evaluate(s,full)[2]==0,"an affordable full formation cannot cross both continuous firing sources without precision");
	auto cleared=s; cleared.precisionTargetID=key.id;
	const auto small=Evaluate(cleared,{{0,3},{0,3}});
	check(small[2]==0 && small[0]==0 && small[3]==0,
		"clearing the key source with two arbitrary troops still loses to the surviving backup fire");
	const auto funded=Evaluate(cleared,full);
	check(funded[2]>0 && funded[5]==s.budget,
		"precision and the whole affordable formation can breach while paying both transactions from one wallet");
	// 走实时调度分支，但截止放得足够远，仅由有限候选次数结束；不依赖机器快慢判断性能。
	s.timeLimitedSearch=true;
	for(unsigned seed : {17u,29u,91u}) {
		s.searchDeadline=std::chrono::steady_clock::now()+std::chrono::hours(1);
		const auto joint=Search(s,InitialWeights,seed);
		check(joint.precisionTargetID==key.id && joint.features[2]>0 && joint.actions.size()>2,
			"realtime precision exploration retains a winning complete followup instead of stopping at pure skill or a doomed pair");
		check(60+static_cast<int>(joint.actions.size())*troop.cost<=s.budget && joint.actions.size()<=static_cast<size_t>(s.capacity),
			"complete precision followup keeps the original wallet and real deployment capacity");
	}
	s.budget=79; s.searchDeadline=std::chrono::steady_clock::now()+std::chrono::hours(1);
	check(Search(s,InitialWeights,91).features[2]==0,
		"the same firing defense cannot be labelled a joint victory when the wallet cannot fund enough followup");
	check(s.plants[0].health==key.health && s.plants[1].health==reserve.health && s.options[0].unit.body.health==200,
		"precision and followup counterfactuals do not mutate the sampled defenders or troop option");
	std::cout << "Precision complete-followup, backup fire and common-wallet counterfactuals passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.gridLeft=0; s.cellWidth=100; s.columns=12;
	Unit drum; drum.id=10; drum.body.x=800; drum.body.health=1600; drum.drum.enabled=true;
	drum.drum.remaining=.5f; drum.drum.stopHealth=533;
	Unit ally; ally.id=11; ally.body.x=800; ally.body.health=3000; ally.body.speed=1; ally.body.purchaseCost=10;
	s.current={drum,ally}; ConstructionStats stats;
	const auto boosted=Evaluate(s,{},&stats);
	check(stats.drumRecipients>0 && stats.drumBeats>0,"live drum refreshes reachable allies");
	s.current={ally}; const auto alone=Evaluate(s,{});
	check(boosted[7]>alone[7],"drum increases actual allied movement rather than only a role preference");
	s.current={drum}; Evaluate(s,{},&stats); check(stats.drumRecipients==0,"drummer cannot inspire itself");
	s.current={drum,ally}; s.current[1].body.row=4;
	Evaluate(s,{},&stats); check(stats.drumRecipients==0,"drum uses Manhattan cell range including row distance");
	s.current[1].body.row=0; s.current[0].body.x=1300;
	Evaluate(s,{},&stats); check(stats.drumRecipients==0,"off-board drummer cannot deliver a pulse");
	s.current={drum,ally}; s.current[0].body.stopped=60;
	Evaluate(s,{},&stats); check(stats.drumBeats==0,"hard control pauses uncommitted drum actions");
	s.current={drum,ally}; s.current[0].body.slow=60;
	Evaluate(s,{},&stats); const int slowBeats=stats.drumBeats;
	s.current[0].body.slow=0; Evaluate(s,{},&stats);
	check(stats.drumBeats>slowBeats,"ordinary ice slow delays drum cadence");
	s.current={ally}; s.current[0].inspiration={{99,6}};
	const auto orphan=Evaluate(s,{});
	check(orphan[7]>alone[7] && orphan[7]<alone[7]*1.1f,"committed inspiration survives missing source but expires rather than lasting forever");
	s.current[0].body.economic=true; s.current[0].body.speed=0;
	const auto producer=Evaluate(s,{}); s.current[0].inspiration.clear();
	check(producer[4]==Evaluate(s,{})[4],"drum never accelerates ice production");
	std::cout << "Drum cadence, range, independent duration and economic neutrality passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.playerIce=5; s.discountRemaining=1;
	Plant p; p.id=1; p.health=100; p.repairMaximum=200; p.repairAmount=100; p.repairCost=9; p.repairRecharge=1; p.repairAutomatic=true;
	s.plants={p}; ConstructionStats stats;
	Evaluate(s,{},&stats); check(stats.plantRepairs==1 && stats.plantRepairIce==5,"active voucher rounds odd repair fees upward");
	s.plants[0].repairRemaining=2;
	Evaluate(s,{},&stats); check(stats.plantRepairs==0,"expired voucher cannot fund later repairs at its old discount");
	std::cout << "Voucher duration and rounded shared-wallet payment passed\n";
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Unit adaptive; adaptive.id=1; adaptive.body.x=900; adaptive.body.health=900; adaptive.body.purchaseCost=900;
	adaptive.adaptiveHelmet=AdaptiveHelmetRules::HelmetHealth; s.current={adaptive};
	Plant melon; melon.id=1; melon.health=1000; melon.x=400; melon.dps=20;
	melon.damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_MELONPULT);
	s.plants={melon}; auto winter=melon; winter.damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_WINTERMELON); s.plants.push_back(winter);
	check(Evaluate(s,{})[3]==800,"adaptive helmet cancels overflow then blocks both base and upgrade lineage");
	s.plants[1].damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_PEASHOOTER);
	check(Evaluate(s,{})[3]==0,"mixed lineages defeat adaptation instead of granting blanket immunity");
	s.plants.clear(); Counter ash; ash.blast.committed=true; ash.blast.damage=1800; ash.blast.x=900;
	ash.blast.reach.fill(-1); ash.blast.reach[0]=100;
	s.counters={ash,ash}; s.counters[1].blast.ready=5;
	check(Evaluate(s,{})[3]==800,"first ash adapts intact helmet and later ash remains blocked");
	s.current[0].adaptiveHelmet=0; s.current[0].body.health=800; s.current[0].adaptedOrigin=melon.damageOrigin;
	check(Evaluate(s,{})[3]==0,"lineage adaptation does not stop ash");
	s.counters.clear(); s.current={adaptive}; s.current[0].adaptedOrigin=PlantDamageOrigin::Ash(); s.current[0].adaptiveHelmet=0;
	s.mowers.push_back({0,890,60,230,false,true});
	check(Evaluate(s,{})[3]==0,"adaptation does not block the mower execution path");
	std::cout << "Adaptive lineage, first-hit overflow, ash and mower contracts passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.gridLeft=0; s.cellWidth=100; s.columns=10;
	for(int row=0;row<5;++row) for(int col=4;col<=6;++col) s.riftCells.push_back({row,col});
	for(auto& child:s.ritualSummons) { child.body.health=1000; child.body.speed=0; child.body.value=8; }
	Unit priest; priest.id=5; priest.body.x=900; priest.body.health=2000; priest.body.purchaseCost=24;
	priest.ritual.present=true; priest.ritual.enabled=true; priest.ritual.armor=800; priest.ritual.stopHealth=400;
	priest.ritual.remaining=6; s.current={priest}; ConstructionStats stats;
	const auto summon=Evaluate(s,{},&stats);
	check(stats.ritualReleases==3 && stats.riftSummons==9,"priest obeys three-release lifetime and three summons per normal release");
	check(summon[5]==0 && summon[3]==24,"free rifts neither spend ice nor manufacture purchase assets");
	Counter ash; ash.blast.committed=true; ash.blast.damage=1800; ash.blast.x=900; ash.blast.reach.fill(-1); ash.blast.reach[0]=100;
	s.counters={ash};
	check(Evaluate(s,{},&stats)[3]==0 && stats.ritualReleases==0,"priest device cannot prevent the real body-based ash execution");
	s.counters.clear();
	s.current[0].ritual.releases=2; Evaluate(s,{},&stats);
	check(stats.ritualReleases==1 && stats.riftSummons==3,"live release count is not reset in forecasts");
	s.current={priest}; s.current[0].ritual.armor=0; Evaluate(s,{},&stats);
	check(stats.ritualReleases==0,"broken device cancels uncommitted rituals");
	s.current={priest}; s.current[0].body.x=1200; Evaluate(s,{},&stats);
	check(stats.ritualReleases==0,"off-board priest can prepare but cannot cast");
	s.current={priest}; s.current[0].body.stopped=60; Evaluate(s,{},&stats);
	check(stats.ritualReleases==0,"hard control pauses ritual preparation");
	s.current={priest}; s.current[0].ritual.remaining=40; s.current[0].body.slow=60; Evaluate(s,{},&stats);
	check(stats.ritualReleases==0,"ice slow delays ritual while drum cannot shorten it");
	s.current.clear(); Rift pending; pending.unit=s.ritualSummons[0]; pending.unit.body.row=0; pending.unit.body.x=450;
	pending.column=4; pending.remaining=.8f; s.rifts={pending};
	Plant boundary; boundary.id=2; boundary.health=450; boundary.row=0; boundary.column=4;
	boundary.boundaryShards=1; boundary.boundaryRecharge=15; s.plants={boundary};
	const auto retained=Evaluate(s,{},&stats);
	check(stats.riftRedirects==1 && retained[5]==0,"committed rift survives missing source and consumes a real boundary shard");
	s.plants[0].health=0; Evaluate(s,{},&stats);
	check(stats.riftRedirects==0,"destroyed boundary flower cannot reject an arrival");
	std::cout << "Priest cycles, equipment, entry, free summons and committed boundary counter passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.playerSun=10000; s.playerIce=10000;
	Unit sniper; sniper.id=10; sniper.body.x=900; sniper.body.health=1200;
	sniper.sniper.enabled=true; sniper.sniper.stopHealth=400; s.current={sniper};
	Construction card; card.source=0; card.sunCost=100; card.recharge=4; card.remainingUses=2;
	card.plant.row=0; card.plant.column=1; card.plant.x=300;
	card.plant.health=card.plant.maximumHealth=100; card.plant.dps=1; card.plant.reward=6;
	s.construction={card}; ConstructionStats stats;
	const auto hit=Evaluate(s,{},&stats);
	check(stats.planted==2 && stats.deploymentShots==2 && stats.deploymentHits==2 && hit[0]==12,
		"deployment sniper punishes replacement while consuming finite card uses");
	check(s.current[0].sniper.remaining==0 && s.construction[0].remainingUses==2,
		"forecast does not mutate live reload or planting quota");
	s.construction[0].remainingUses=0; Evaluate(s,{},&stats);
	check(stats.planted==0 && stats.deploymentShots==0,"exhausted quota cannot create another target");
	auto depleted=s; depleted.anticipateEconomy=true; depleted.playerIce=0;
	depleted.shop={{100,40,1}}; depleted.construction[0].iceCost=30;
	Evaluate(depleted,{},&stats);
	check(stats.planted==0 && stats.orders==0,"exhausted replacement quota cannot invent future ice purchases or opponent cash losses");
	s.construction.clear(); s.plants={card.plant}; Evaluate(s,{},&stats);
	check(stats.deploymentShots==0,"ready sniper never attacks an old deployment without a planting event");
	s.plants.clear(); s.construction={card}; s.construction[0].remainingUses=1;
	s.current[0].body.row=1; Evaluate(s,{},&stats);
	check(stats.deploymentShots==0,"deployment reaction is restricted to the same row");
	s.current={sniper}; s.current[0].body.health=400; Evaluate(s,{},&stats);
	check(stats.deploymentShots==0,"head loss disables future deployment shots");
	s.current={sniper}; s.current[0].body.stopped=60; s.current[0].sniper.remaining=1.5f;
	Evaluate(s,{},&stats); check(stats.deploymentShots==0,"hard control pauses reload rather than granting free readiness");
	s.current={sniper};
	Plant shell; shell.id=1; shell.row=0; shell.column=6; shell.x=700; shell.layer=2; shell.health=500;
	s.plants={shell}; const auto blocked=Evaluate(s,{},&stats);
	check(stats.deploymentHits==1 && blocked[0]==0,"thermal pulse is intercepted by the nearest pumpkin and cannot delete the rear replacement");
	s.current[0].sniper.muzzleOffset=-700;
	check(Evaluate(s,{},&stats)[0]==6 && stats.deploymentHits==1,
		"projected muzzle offset determines flight direction and ignores walls outside the ray");
	s.current={sniper};
	s.plants[0].hostileMirrors=1; Evaluate(s,{},&stats);
	check(stats.deploymentShots==1 && stats.deploymentHits==0,"a formed hostile mirror consumes the pulse before plant damage");
	s.plants.clear(); s.current.clear();
	s.construction[0].quotaGroup=0;
	auto copy=s.construction[0]; copy.source=1; copy.plant.row=1; s.construction.push_back(copy);
	Evaluate(s,{},&stats); check(stats.planted==1,"imitater and original card share a cumulative quota across rows and cooldowns");
	s.construction.clear(); s.current={sniper};
	s.current[0].sniper.aiming=true; s.current[0].sniper.remaining=.35f;
	s.current[0].sniper.targetID=123; s.current[0].sniper.targetX=300; s.current[0].sniper.damage=100;
	auto target=card.plant; target.id=123; target.dps=3000; s.plants={target};
	Evaluate(s,{},&stats);
	check(stats.deploymentShots==1 && stats.deploymentHits==1,"a pulse already fired survives death of its source during flight");
		std::cout << "Deployment suppression, interception, independent flight and shared finite replacements passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Unit sniper; sniper.body.x=900; sniper.body.health=1200; sniper.body.purchaseCost=16;
	sniper.sniper.enabled=true; sniper.sniper.stopHealth=400;
	Unit worker; worker.body.x=920; worker.body.health=500; worker.body.purchaseCost=24; worker.body.economic=true;
	s.current={sniper,worker,worker};
	Counter ash; ash.blast.x=700; ash.blast.damage=1800; ash.blast.reach.fill(-1); ash.blast.reach[0]=10000;
	ash.cellRow=0; ash.cellColumn=6; ash.deploymentHealth=300; ash.deploymentReward=20;
	s.counters={ash}; ConstructionStats stats;
	const auto intercepted=Evaluate(s,{},&stats);
	check(intercepted[4]>0 && intercepted[6]==0 && intercepted[0]==20 && stats.deploymentShots==1 && stats.deploymentHits==1,
		"a ready deployment sniper kills newly planted ash before detonation and preserves the following workers");
	check(s.current[0].sniper.remaining==0 && s.plants.empty() && s.counters[0].blast.ready==0,
		"ash deployment forecast owns its source and cannot mutate live reload, plants or cooldowns");
    // 自主采购应识别狙击的增量掩护价值；没有灰烬威胁时，同一兵种不能因专项而变成必买。
    Snapshot choice=s; choice.current={worker,worker}; choice.budget=32; choice.capacity=2; choice.netEconomy=true;
    choice.counters[0].blast.ready=2;
    Option guard; guard.type=701; guard.cost=16; guard.unit=sniper; guard.unit.sniper.remaining=1.5f;
    Option other=guard; other.type=702; other.cost=4; other.unit.body.purchaseCost=4; other.unit.sniper.enabled=false;
    choice.options={guard,other}; Weights income{}; income[4]=1;
    const auto protectedPlan=Search(choice,income,731);
    check(std::any_of(protectedPlan.actions.begin(),protectedPlan.actions.end(),[](const Action& action){return action.option==0;})
        && protectedPlan.construction.deploymentHits>0,
        "free search buys deployment suppression when it protects existing workers from available ash");
    choice.counters.clear();
    check(Search(choice,income,731).actions.empty(),"the same sniper is optional when no profitable suppression target exists");
	s.current[0].sniper.remaining=10;
	check(Evaluate(s,{},&stats)[4]==0 && stats.deploymentShots==0,"reloading sniper cannot intercept a later old bomb without a new deployment");
	s.current[0]=sniper; s.current[0].body.row=1;
	check(Evaluate(s,{},&stats)[4]==0 && stats.deploymentShots==0,"a sniper in another lane cannot suppress this ash placement");
	s.current[0]=sniper;
	Plant wall; wall.id=1; wall.row=0; wall.column=7; wall.x=800; wall.health=8000;
	s.plants={wall};
	check(Evaluate(s,{},&stats)[4]==0 && stats.deploymentHits==1,"an actual front wall intercepts the pulse and protects the rear ash");
	s.plants.clear(); s.counters[0].blast.x=0; s.counters[0].windup=.5f;
	check(Evaluate(s,{},&stats)[4]==0 && stats.deploymentHits==0,"a pulse arriving after the ash deadline cannot cancel an earlier explosion");
	s.counters[0]=ash; s.counters[0].blast.committed=true; s.counters[0].blast.ready=.2f; s.counters[0].plantID=99;
	Plant bomb; bomb.id=99; bomb.x=700; bomb.health=bomb.maximumHealth=300; bomb.edible=false; bomb.deploymentInterceptionOnly=true;
	s.plants={bomb}; s.current[0].sniper.aiming=true; s.current[0].sniper.remaining=.35f;
	s.current[0].sniper.targetID=99; s.current[0].sniper.targetX=700; s.current[0].sniper.damage=300;
	check(Evaluate(s,{},&stats)[4]==0 && stats.deploymentHits==0,"an already committed near-expiry bomb wins the timing race against an unfinished aim");
	std::cout << "Ash planting reactions, wall interception and detonation deadline passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.playerSun=125; s.playerIce=20;
	Plant filler; filler.id=81; filler.row=0; filler.column=6; filler.x=700;
	filler.health=filler.initialHealth=filler.maximumHealth=300; filler.assetValue=10; filler.reward=99;
	Plant shell=filler; shell.id=82; shell.layer=2; shell.health=shell.initialHealth=shell.maximumHealth=4000;
	shell.assetValue=80; shell.reward=100;
	s.plants={filler,shell};
	Unit worker; worker.body.x=950; worker.body.health=500; worker.body.purchaseCost=24; worker.body.economic=true;
	s.current.assign(20,worker);
	Counter ash; ash.cellRow=0; ash.cellColumn=6; ash.shovelAllowed=true;
	ash.sunCost=125; ash.iceCost=20; ash.blast.x=700; ash.blast.reach.fill(-1); ash.blast.reach[0]=10000;
	ash.blast.damage=1800; ash.deploymentHealth=300; s.counters={ash};
	ConstructionStats before,after;
	const auto safe=Evaluate(s,{},&before);
	const auto burned=Evaluate(s,{},&after,0,0,0,false,false,-1,false,true);
	check(safe[4]>0 && burned[4]==0 && after.counterShovels==1 && after.counterShovelAssets==10,
		"a legal low-value sacrifice opens a paid ash response against a concentrated economy");
	check(burned[0]==0 && burned[1]==0 && after.opponentAssets==80,
		"shoveling awards no zombie kill ice or damage score and preserves the pumpkin asset");
	check(s.plants[0].health==300 && s.playerSun==125 && s.playerIce==20,
		"shovel forecast never mutates the board snapshot or wallet");
	s.playerSun=124; Evaluate(s,{},&after,0,0,0,false,false,-1,false,true);
	check(after.counterShovels==0 && after.sunSpent==0,"cannot sacrifice a plant for unaffordable ash");
	s.playerSun=125; s.counters[0].blast.ready=1000;
	Evaluate(s,{},&after,0,0,0,false,false,-1,false,true);
	check(after.counterShovels==0,"unavailable cooldown does not clear a planting cell early");
	s.counters[0].blast.ready=0; s.plants[0].assetValue=1000;
	Evaluate(s,{},&after,0,0,0,false,false,-1,false,true);
	check(after.counterShovels==0,"ordinary clear value does not justify sacrificing a more valuable plant");
	s.plants[0]=filler; s.counters[0].shovelAllowed=false;
	check(Evaluate(s,{},&after,0,0,0,false,false,-1,false,true)[4]>0 && after.counterShovels==0,
		"a Board-protected occupied cell cannot be shoveled by the opponent model");
	s.pendingPrecisionID=81; s.pendingPrecisionRemaining=2;
	check(Evaluate(s,{},&after)[4]<safe[4] && after.counterShovels==0 && after.precisionHits==1,
		"precision clearing opens the existing counter candidate after the target dies without player shoveling");
	s.pendingPrecisionID=0; s.counters[0].shovelAllowed=true;
	s.budget=0; Weights economy{}; economy[4]=1;
	check(Search(s,economy,71).construction.counterShovels==1,
		"action and waiting evaluation select the complete legal shovel response when it harms the commander");
	s.current.clear(); s.budget=960; s.capacity=40; s.searchVersion=2; s.netEconomy=true;
	Option purchase; purchase.type=56; purchase.cost=24; purchase.unit=worker; s.options={purchase};
	// 验证同步集中案的风险，不禁止搜索另选少量诱饵或跨冷却错峰。
	const std::vector<Action> concentrated(40,Action{0,0});
	check(Evaluate(s,concentrated,&after,0,0,0,false,false,-1,false,true)[4]==0 && after.counterShovels==1,
		"the synchronized forty-worker purchase loses its imaginary safe income against legal shovel-and-ash");
	std::cout << "Dynamic counter vacancies, optional sacrifice, shared payment and snapshot isolation passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Unit tank; tank.id=1; tank.body.x=900; tank.body.health=500; tank.body.purchaseCost=100;
	Unit clock; clock.id=10; clock.body.row=1; clock.body.x=920;
	clock.body.health=PolarClockRules::BodyHealth+PolarClockRules::ArmorHealth; clock.helmHealth=PolarClockRules::ArmorHealth;
	clock.clock={true,true,true,0,PolarClockRules::BodyHealth/3.0f};
	s.current={tank,clock};
	Counter ash; ash.blast.committed=true; ash.blast.ready=1; ash.blast.x=900; ash.blast.damage=10000;
	ash.blast.reach.fill(-1); ash.blast.reach[0]=10000; s.counters={ash}; ConstructionStats stats;
	const auto rewind=Evaluate(s,{},&stats);
	check(rewind[3]==100 && rewind[6]==0 && stats.clockRevivals==1 && stats.clockRewinds>0,
		"a committed neighboring time anchor revives an ash-killed investment and reverses its recoverable blast loss");
	s.counters[0].blast.reach[1]=10000;
	check(Evaluate(s,{},&stats)[3]==100 && stats.clockRevivals>=2,"a committed anchor survives death of its source and can restore recorded units");
	s.current[1].clock.remaining=10; s.current[1].clock.winding=false;
	check(Evaluate(s,{},&stats)[3]==0 && stats.clockAnchors==0,"destroying the clock before commitment cannot create a resurrection");
	s.current={tank,clock}; s.current[0].body.row=3;
	Evaluate(s,{},&stats); check(stats.clockRevivals==1,"clock recording is limited to its own and adjacent lanes");
	s.current.assign(15,tank); for (int i=0;i<15;++i) { s.current[i].id=i+1; s.current[i].body.health=3000; }
	clock.id=100; s.current.push_back(clock); s.counters[0].blast.reach[0]=-1;
	Evaluate(s,{},&stats);
	check(stats.clockAnchors==1 && stats.clockTargets==PolarClockRules::TargetLimit && stats.clockRewinds==PolarClockRules::TargetLimit,
		"one anchor records only the twelve highest threats and remains valid after its unrecorded source dies");
	Snapshot paid; paid.houseX=-10000; tank.body.purchaseCost=24; tank.playerRefund=18; paid.current={tank};
	TemporalAnchor anchor; anchor.ownerID=999; anchor.at=2; anchor.targets.push_back({0,tank}); paid.temporalAnchors={anchor};
	ash.blast.ready=1; ash.blast.reach.fill(-1); ash.blast.reach[0]=10000;
	paid.counters={ash,ash}; paid.counters[1].blast.ready=7;
	Evaluate(paid,{},&stats); check(stats.clockRevivals==1 && stats.opponentAssets==18,"death, revival and second death refund the original paid unit only once");
	paid.counters.clear(); paid.temporalAnchors[0].at=1; paid.current[0].body.x=200;
	paid.mowers.push_back({0,180,60,230,false,true});
	check(Evaluate(paid,{},&stats)[3]==0 && stats.clockRevivals==0,"irreversible mower execution cannot be undone by a submitted time anchor");
	paid.mowers.clear(); paid.current[0].body.health=0; paid.current[0].body.speed=0;
	paid.gridLeft=0; paid.cellWidth=100; paid.temporalAnchors[0].targets[0].saved.body.x=250;
	Plant boundary; boundary.row=0; boundary.column=2; boundary.health=450; boundary.boundaryShards=1; paid.plants={boundary};
	Evaluate(paid,{},&stats); check(stats.clockRevivals==1 && stats.clockRedirects==1,
		"boundary interception redirects a restored corpse without deleting the legitimate health rewind");
	Snapshot mature; mature.houseX=-10000; auto producer=tank; producer.body.economic=true; producer.nextYield=18;
	mature.current={producer}; anchor.targets={{0,producer}}; mature.temporalAnchors={anchor};
	mature.temporalAnchors[0].targets[0].restoreAbility=false;
	auto noRewind=mature; noRewind.temporalAnchors.clear();
	check(Evaluate(mature,{})==Evaluate(noRewind,{}),"surviving mature worker keeps actual production progress through core-only rewind");
	mature.temporalAnchors[0].targets[0].restoreAbility=true;
	mature.temporalAnchors[0].targets[0].saved.nextYield=IceProduction::InitialYield;
	check(Evaluate(mature,{})[4]<Evaluate(noRewind,{})[4],"worker maturity and production countdown rewind while already earned income remains");
	mature.current[0].body.health=0;
	mature.temporalAnchors[0].targets[0].saved.nextYield=IceProduction::MaximumYield;
	const auto matureRevival=Evaluate(mature,{});
	mature.temporalAnchors[0].targets[0].restoreAbility=false;
	check(matureRevival[4]>Evaluate(mature,{})[4],"dead worker resumes recorded mature production instead of a newborn yield");
	// 相同生命/位置下仅切换是否恢复能力，验证已付费阶段和修复倒计时的独立收益。
	Snapshot abilities; abilities.houseX=-10000;
	Unit boiler; boiler.body.health=1000; boiler.body.x=500; boiler.biteDps=50;
	boiler.burst.range=320; boiler.burst.stage=PaidBurst::Stage::SPENT;
	boiler.burst.cost=5; boiler.burst.biteMultiplier=10; boiler.burst.remaining=0;
	Plant wall; wall.row=0; wall.x=500; wall.health=100000; wall.reward=100;
	abilities.current={boiler}; abilities.plants={wall};
	anchor.at=0; anchor.targets={{0,boiler}};
	anchor.targets[0].saved.burst.stage=PaidBurst::Stage::ACTIVE;
	anchor.targets[0].saved.burst.remaining=8;
	abilities.temporalAnchors={anchor};
	const auto activeRewind=Evaluate(abilities,{},&stats);
	check(stats.abilityIceSpent==0 && stats.burstActivations==0,"restoring paid overdrive does not charge its fee a second time");
	abilities.temporalAnchors[0].targets[0].restoreAbility=false;
	check(activeRewind[1]>Evaluate(abilities,{})[1],"restored overdrive contributes actual extra attack damage");
	Unit guard; guard.body.health=600; guard.body.x=900; guard.helmHealth=100;
	guard.repair={100,1300,1800,166,2.5f,2.5f,400,1};
	abilities.current={guard}; abilities.plants.clear(); abilities.budget=1;
	anchor.targets={{0,guard}}; anchor.targets[0].saved.repair.remaining=0;
	abilities.temporalAnchors={anchor}; abilities.counters={ash}; abilities.counters[0].blast.ready=1;
	Evaluate(abilities,{},&stats);
	check(stats.armorRepairs==1 && stats.abilityIceSpent==1,"restored shield timer repairs once before the impending lethal hit and pays once");
	abilities.temporalAnchors[0].targets[0].restoreAbility=false;
	Evaluate(abilities,{},&stats);
	check(stats.armorRepairs==0,"core-only rewind cannot borrow an earlier shield repair countdown");
	// 两轮灰烬之间，比较一只与多只钟匠的实际存活收益；不指定搜索结果的固定出生时刻。
	Snapshot chain; chain.houseX=-10000; chain.budget=36; chain.capacity=2;
	tank.body.purchaseCost=100; tank.body.x=900; tank.playerRefund=0; chain.current.assign(4,tank);
	for (int i=0;i<4;++i) chain.current[i].id=i+1;
	clock.id=0; clock.body.purchaseCost=18; clock.clock.winding=false; clock.clock.remaining=PolarClockRules::Preparation;
	Option option; option.type=7; option.row=1; option.cost=18; option.unit=clock; chain.options={option};
	ash.blast.ready=7; chain.counters={ash,ash}; chain.counters[1].blast.ready=13;
	Weights value{}; value[3]=1; value[5]=-1;
	const auto multiple=Search(chain,value,71);
	check(multiple.actions.size()==2 && multiple.features[3]>=400 && multiple.construction.clockRevivals>=4,
		"free search can choose more than one clock to sustain assault through successive ash windows");
	check(chain.current[0].body.health==500 && chain.options[0].unit.clock.remaining==PolarClockRules::Preparation,
		"temporal forecasting cannot rewind or consume any actual entity state");
	std::cout << "Temporal coverage, multiple clocks, source loss and irreversible economy passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=102; s.capacity=2;
	Option heavy; heavy.type=1; heavy.cost=100; heavy.unit.body.x=800; heavy.unit.body.health=3000;
	heavy.unit.body.speed=1; heavy.unit.body.purchaseCost=100; heavy.unit.throwHealth=1500;
	Option drum; drum.type=2; drum.cost=2; drum.unit.body.x=800; drum.unit.body.health=1600;
	drum.unit.body.purchaseCost=2; drum.unit.drum.enabled=true; drum.unit.drum.remaining=.5f; drum.unit.drum.stopHealth=533;
	s.options={heavy,drum}; Weights weights{}; weights[7]=30; weights[5]=-1;
	const auto boosted=Search(s,weights,401);
	check(boosted.actions.size()==2 && boosted.construction.drumRecipients>0,
		"search buys drummer with a heavy advance when the buff repays its marginal cost");
	s.options[1].unit.drum.enabled=false;
	const auto plain=Search(s,weights,401);
	check(plain.actions.size()==1 && boosted.score>plain.score,"removing drum ability removes the reason to buy the extra unit");
	s.budget=32; s.options.clear(); weights={0,0,0,.5f,2,-1,0,0};
	Option tank; tank.type=3; tank.cost=20; tank.unit.body.x=800; tank.unit.body.health=900;
	tank.unit.body.speed=4; tank.unit.body.purchaseCost=20; tank.unit.adaptiveHelmet=100;
	Option worker; worker.type=4; worker.cost=12; worker.unit.body.x=820; worker.unit.body.health=500;
	worker.unit.body.speed=4; worker.unit.body.purchaseCost=12; worker.unit.body.economic=true;
	Plant fire; fire.id=1; fire.x=300; fire.health=10000; fire.dps=100;
	fire.damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_ELITE_SCAREDYSHROOM);
	s.plants={fire}; s.options={tank,worker};
	const auto protectedWorker=Search(s,weights,402);
	check(protectedWorker.actions.size()==2 && protectedWorker.features[4]>0 && protectedWorker.combinationEvaluated>0,
		"single-lineage fire permits an adaptive frontline and profitable rear worker");
	auto mixed=fire; mixed.damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_PEASHOOTER); s.plants.push_back(mixed);
	const auto mixedResult=Search(s,weights,402);
	check(mixedResult.features[4]<protectedWorker.features[4],"mixed fire removes adaptive protection instead of blindly preserving the same formation");
	std::cout << "Search chooses optional drummer/heavy and adaptive/worker synergy by counterfactual return\n";
	}

	{
	using namespace ColdStorageSearch;
	// 极端经营权重与负胜利权重也不能覆盖真实进屋结果；不依赖特定兵种 ID。
	Snapshot s; s.budget=24; s.capacity=1; s.netEconomy=true; s.recoveryReserve=100;
	Option runner; runner.type=171; runner.cost=24; runner.unit.body.x=850;
	runner.unit.body.health=100; runner.unit.body.speed=20; runner.unit.body.purchaseCost=24;
	Option worker=runner; worker.type=819; worker.unit.body.speed=0; worker.unit.body.economic=true;
	worker.unit.body.health=500; // 高于工人失去生产资格的真实掉头阈值。
	s.options={runner,worker}; Weights profit{}; profit[2]=-500; profit[4]=500;
	check(Evaluate(s,{{0,0}})[2]>0 && Evaluate(s,{{1,0}})[4]>0,"goal fixture has both a winning attack and profitable nonwinning production");
	for (unsigned seed=1; seed<=16; ++seed) {
		const auto win=Search(s,profit,seed);
		check(win.features[2]>0 && !win.actions.empty(),"victory precedes intermediate income and survives capital gates");
	}
	s.mowers={{0,160,60,230,false,true}};
	const auto cleared=Search(s,profit,19);
	check(cleared.features[2]==0 && !cleared.actions.empty() && s.options[cleared.actions.front().option].unit.body.economic,
		"a mower-cleared attack is not labelled victory; nonwinning candidates still compare net returns");
	s.mowers.clear(); s.options.clear(); s.capacity=0;
	auto late=runner.unit; late.body.spawnAt=120;
	s.current={late,worker.unit}; CommittedUnit paid; paid.unit=0; paid.legalRows[0]=true; s.committed={paid};
	const auto revision=ReplanCommitted(s,profit,19);
	check(!revision.beforeBreach && revision.afterBreach && revision.changed==1 && revision.afterScore<revision.beforeScore,
		"paid queue prioritizes a newly possible victory even when earlier ending reduces income score");
	// 清除落地前的薄血部队会被射死；联合胜利需要自行找出延迟进场，而不是立即跟进模板。
	s.current.clear(); s.committed.clear(); s.precisionReady=true; s.budget=100; s.capacity=1;
	runner.cost=40; runner.unit.body.purchaseCost=40;
	worker.row=worker.unit.body.row=1; worker.unit.body.x=1150;
	s.options={runner,worker}; Plant lethal; lethal.id=8; lethal.x=300; lethal.health=10000; lethal.dps=1000; lethal.reward=10;
	s.plants={lethal};
	const auto strike=Search(s,profit,19);
	check(strike.precisionTargetID==8 && strike.features[2]>0 && strike.precisionGain<0
		&& !strike.actions.empty() && strike.actions.front().delay>0,
		"precision and delayed arbitrary followup prioritize victory over larger intermediate production score");
	std::cout << "Victory-first purchasing, mower truth and paid-queue outcome ordering passed\n";
	}
	{
	using namespace ColdStorageSearch;
	// 矮血量普通伙伴不符合原鼓手的“重兵/高生命”筛选，但加速收益仍应参与搜索。
	Snapshot s; s.houseX=-10000; s.budget=102; s.capacity=2;
	Option fragile; fragile.type=919; fragile.cost=100; fragile.unit.body.x=800;
	fragile.unit.body.health=100; fragile.unit.body.speed=4; fragile.unit.body.purchaseCost=100;
	Option drum; drum.type=173; drum.cost=2; drum.unit.body.x=800;
	drum.unit.body.health=1600; drum.unit.body.purchaseCost=2;
	drum.unit.drum.enabled=true; drum.unit.drum.remaining=.5f; drum.unit.drum.stopHealth=533;
	s.options={fragile,drum}; Weights advance{}; advance[7]=30; advance[5]=-1;
	for (unsigned seed=1; seed<=16; ++seed) {
		const auto combined=Search(s,advance,seed);
		check(combined.actions.size()==2 && combined.construction.drumRecipients>0,
			"any affordable type pair can win comparison without role or health screening");
	}
	s.options[1].unit.drum.enabled=false;
	const auto unhelpful=Search(s,advance,19);
	check(unhelpful.actions.size()==1,"removing the companion skill removes its purchase benefit");
	s.budget=101; s.options[1].unit.drum.enabled=true;
	check(Search(s,advance,19).actions.size()==1,"free combinations still obey the joint purchase budget");
	std::cout << "Role-independent combinations, skill counterfactual and joint affordability passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=80; s.capacity=2; s.netEconomy=true;
	Unit existing; existing.body.row=0; existing.body.x=800; existing.body.health=20000; s.current={existing};
	Plant melon; melon.row=1; melon.x=300; melon.health=10000; melon.dps=100; melon.melon=true; melon.edible=false; s.plants={melon};
	Option decoy; decoy.row=decoy.unit.body.row=1; decoy.cost=24; decoy.unit.body.x=900;
	decoy.unit.body.health=3000; decoy.unit.body.purchaseCost=24; decoy.preference[0]=60;
	for (int type=0; type<9; ++type) { decoy.type=type; s.options.push_back(decoy); }
	Option worker; worker.type=99; worker.cost=24; worker.unit.body.x=920; worker.unit.body.health=500;
	worker.unit.body.purchaseCost=24; worker.unit.body.economic=true; worker.unit.productionRemaining=40; s.options.push_back(worker);
	Weights income{}; income[4]=1; income[5]=-1;
	check(Evaluate(s,{{9,0}})[4]>24 && Evaluate(s,{{0,0},{9,0}})[4]==0,
		"a standalone worker is profitable while appending it to a new adjacent-lane decoy exposes fatal splash");
	for (unsigned seed=1; seed<=16; ++seed) {
		const auto result=Search(s,income,seed);
		check(!result.actions.empty() && std::all_of(result.actions.begin(),result.actions.end(),[](const auto& a){return a.option==9;}),
			"route refinement can reinforce existing units independently of the current new attack plan");
		const auto workerStats=std::find_if(result.candidates.begin(),result.candidates.end(),[](const auto& item){return item.type==99;});
		check(workerStats!=result.candidates.end() && workerStats->standalone>0 && workerStats->allowed>0,
			"candidate diagnostics include the independent worker comparison");
		for (const auto& item : result.candidates) check(item.evaluated==item.allowed+item.regroupRejected+item.capitalRejected,
			"candidate outcome counters reconcile without duplicate same-type same-row counting");
	}
	std::cout << "Standalone reinforcement, splash exposure and candidate rejection accounting passed\n";
	}
	{
	using namespace ColdStorageSearch;
	// 能力由选项自身提供：没有经济/护卫类型名单，大池中的后续支援仍须评估实际生存收益。
	Snapshot s; s.houseX=-10000; s.budget=96; s.capacity=5; s.netEconomy=true;
	Plant fire; fire.x=300; fire.health=100000; fire.dps=160; fire.edible=false; s.plants={fire};
	Option front; front.type=921; front.cost=12; front.unit.body.x=800;
	front.unit.body.health=3200; front.unit.body.purchaseCost=12;
	front.preference[0]=20; s.options={front};
	for (int type=0; type<12; ++type) {
		auto fragile=front; fragile.type=1000+type; fragile.unit.body.health=50; fragile.preference={};
		s.options.push_back(fragile);
	}
	Option producer; producer.type=1917; producer.cost=24; producer.unit.body.x=920;
	producer.unit.body.health=500; producer.unit.body.purchaseCost=24; producer.unit.body.economic=true;
	s.options.push_back(producer); Weights income{}; income[4]=1;
	check(Evaluate(s,{{13,0}})[4]==0 && Evaluate(s,{{0,0},{13,6}})[4]>producer.cost,
		"unprotected support loses its investment while delayed support can produce behind the selected frontline");
	for (unsigned seed=1; seed<=16; ++seed) {
		const auto result=Search(s,income,seed);
		check(result.features[4]>producer.cost && std::any_of(result.actions.begin(),result.actions.end(),[](const auto& action){return action.option==13;}),
			"generic reinforcement finds profitable support in an attack plan without a predefined pairing");
		check(result.combinationEvaluated<=80 && result.features[5]<=s.budget,
			"reinforcement shares the existing combination budget and respects the actual wallet");
	}
	s.plants[0].dps=100000;
	check(Search(s,income,19).features[4]==0,"unprotectable reinforcement does not invent production to force an economic purchase");
	std::cout << "Generic followup support, ordinary-fire capital risk and fixed search budget passed\n";
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=200; s.capacity=8;
	Plant fire; fire.x=300; fire.health=10000; fire.dps=280; fire.edible=false;
	fire.damageOrigin=PlantDamageOrigin::FromPlant(PlantType::PLANT_ELITE_SCAREDYSHROOM); s.plants={fire};
	Option front; front.cost=12; front.unit.body.x=1140; front.unit.body.speed=20;
	front.unit.body.health=900; front.unit.body.purchaseCost=12; front.unit.adaptiveHelmet=100;
	Option producer; producer.cost=24; producer.unit.body.x=1140; producer.unit.body.speed=20;
	producer.unit.body.health=500; producer.unit.body.purchaseCost=24; producer.unit.body.economic=true;
	s.options={front,producer};
	const auto sameSpeed=Evaluate(s,{{0,0},{1,6}});
	s.options[0].unit.birthMovementKnown=s.options[1].unit.birthMovementKnown=true;
		s.options[0].unit.minimumMoveSpeed=s.options[1].unit.minimumMoveSpeed=8;
	s.options[0].unit.maximumMoveSpeed=s.options[1].unit.maximumMoveSpeed=16;
	ConstructionStats stats;
	const auto overtaken=Evaluate(s,{{0,0},{1,6}},&stats);
	std::cout << "Movement fixture income before/after=" << sameSpeed[4] << "/" << overtaken[4]
		<< " bounded=" << stats.movementBoundsApplied << '\n';
	check(sameSpeed[4]>100 && overtaken[4]<producer.cost && stats.movementBoundsApplied==2,
		"a faster producer overtakes its immune escort instead of earning imaginary same-speed income");
	check(Evaluate(s,{{0,0},{1,30}})[4]>producer.cost,
		"a sufficiently delayed producer can still pay back behind a slow escort without a compulsory pairing");
	check(s.options[0].unit.body.speed==20 && s.options[1].unit.body.speed==20,
		"movement-bound evaluation does not mutate sampled units or options");
	s.current={s.options[0].unit}; s.current[0].minimumMoveSpeed=s.current[0].maximumMoveSpeed=0;
	s.current[0].birthMovementKnown=false;
		s.current[0].body.x=800; s.current[0].body.speed=8;
	Evaluate(s,{{1,0}},&stats);
	check(stats.movementBoundsApplied==1,"known live escort keeps its sampled movement rather than being rerolled");
	std::cout << "Birth movement uncertainty, escort overtaking, delayed investment and live-speed preservation passed\n";
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=160; s.rightEdge=1000; s.capacity=1;
	Unit vehicle; vehicle.body.x=500; vehicle.body.speed=10; vehicle.body.health=1000;
	vehicle.body.stopX=-10000; vehicle.movementCurveReference=500;
	s.current={vehicle}; const auto constant=Evaluate(s,{});
	s.current[0].movementCurve={200,500,300,.2f,.3f};
	const auto slowing=Evaluate(s,{});
	check(constant[2]>slowing[2],"position-dependent vehicle slowdown changes the breach timeline instead of keeping birth speed forever");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=24; s.capacity=1;
	Plant fire; fire.x=300; fire.health=10000; fire.dps=280; fire.edible=false; s.plants={fire};
	Unit convoy; convoy.body.x=1040; convoy.body.speed=15; convoy.body.health=30000; s.current={convoy};
	Option worker; worker.type=1917; worker.cost=24; worker.unit.body.x=1140; worker.unit.body.health=500;
	worker.unit.body.economic=true; worker.unit.body.purchaseCost=24; worker.unit.birthMovementKnown=true;
	worker.unit.minimumMoveSpeed=8; worker.unit.maximumMoveSpeed=20; s.options={worker};
	const auto extreme=Evaluate(s,{{0,0}});
	s.options[0].unit.lowerForecastMoveSpeed=11; s.options[0].unit.upperForecastMoveSpeed=16;
	const auto representative=Evaluate(s,{{0,0}});
	check(representative[4]>extreme[4] && representative[4]>worker.cost,
		"a modestly fast producer can profit behind an existing convoy without assuming every newborn draws maximum speed");
	Weights income{}; income[4]=1;
	check(!Search(s,income,29).actions.empty(),"a generic income objective can find convoy support without a mandatory worker recipe");
	s.current[0].body.health=500;
	check(Evaluate(s,{{0,0}})[4]<worker.cost,"representative birth uncertainty cannot turn a dying escort into reliable protection");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Plant wall; wall.x=400; wall.health=100000; wall.reward=100; s.plants={wall};
	Unit car; car.body.x=500; car.body.speed=15; car.body.health=10000; car.biteDps=50; car.vehicleCrush=true; s.current={car};
	const auto chewing=Evaluate(s,{}); s.current[0].instantVehicleCrush=true;
	check(chewing[0]==0 && Evaluate(s,{})[0]==wall.reward,"an ice vehicle clears an ordinary plant by crushing rather than chewing through its health");
	s.plants[0].vehicleCrushable=false;
	check(Evaluate(s,{})[0]==0,"awake instant plants excluded by the ice vehicle's target rules are not crushed");
	s.plants[0].vehicleCrushable=true; s.plants[0].crushDamage=100; s.plants[0].vehicleRetreat=40;
	check(Evaluate(s,{})[0]==0,"anti-crush storage nuts retain limited impact damage and vehicle retreat");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Unit car; car.body.x=1000; car.body.speed=2; car.body.health=1000; car.body.purchaseCost=48;
	car.goldenDrive={true,0,1100,50}; s.current={car};
	ConstructionStats freeStats, fireStats;
	const auto freeDrive=Evaluate(s,{},&freeStats);
	Plant fire; fire.x=0; fire.dps=1; fire.health=100000; fire.edible=false; s.plants={fire};
	const auto underFire=Evaluate(s,{},&fireStats);
	check(freeStats.goldenAccelerationSteps>0 && fireStats.goldenAccelerationSteps==0 && freeDrive[7]>underFire[7],
		"unhurt ice vehicle accelerates over time; repeated real body damage resets upgrades rather than keeping sampled acceleration forever");
	s.plants.clear(); s.current.push_back(car); ConstructionStats overlap;
	Evaluate(s,{},&overlap);
	check(overlap.goldenMaxStacks==2,"nearby live golden vehicles count independent sources with vehicle-body padding");
	s.current[1].body.spawnAt=61; ConstructionStats future;
	Evaluate(s,{},&future);
	check(future.goldenMaxStacks==1,"a not-yet-born golden vehicle cannot amplify existing allies");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000;
	Unit ally; ally.body.row=1; ally.body.x=1080; ally.body.speed=1; ally.body.health=1000; ally.body.purchaseCost=8;
	ally.inspiration={{10,12}}; s.current={ally};
	const auto bare=Evaluate(s,{});
	s.goldenTrails[1]={0,35}; ConstructionStats residual;
	const auto helped=Evaluate(s,{},&residual);
	check(helped[7]>bare[7] && residual.goldenDrumSteps>0 && residual.goldenResidualSteps>0 && residual.goldenMaxStacks==1,
		"persistent golden trail still doubles drum movement after all live sources are gone");
	s.current[0].inspiration.clear();
	const auto neutral=Evaluate(s,{}); s.goldenTrails[1].remaining=0;
	check(Evaluate(s,{})[7]==neutral[7],"golden trail does not accelerate neutral unbuffed movement");
	s.current[0].body.slow=60; s.goldenTrails[1]={0,35};
	const auto coldGolden=Evaluate(s,{}); s.goldenTrails[1].remaining=0;
	check(Evaluate(s,{})[7]>coldGolden[7],"persistent golden trail also amplifies movement slowdown, rather than only helping speed buffs");
	s.goldenTrails[1]={0,35}; s.goldenAllowedRows[1]=false; ConstructionStats water;
	Evaluate(s,{},&water);
	check(water.goldenMaxStacks==0,"water lanes are excluded from residual golden speed fields");
	s.goldenAllowedRows[1]=true; s.current[0].body.slow=0; s.current[0].inspiration={{10,120}};
	s.goldenTrails[1]={0,1}; const auto brief=Evaluate(s,{});
	s.goldenTrails[1].remaining=35; const auto lasting=Evaluate(s,{});
	s.goldenTrails[1].remaining=120;
	check(brief[7]<lasting[7] && lasting[7]<Evaluate(s,{})[7],"residual speed field expires instead of persisting throughout the forecast");
	Unit source; source.body.x=950; source.body.health=1000; source.goldenDrive={true,0,1100,50};
	s.goldenTrails={}; s.current={source,ally}; s.current[1].body.x=1040;
	ConstructionStats adjacent, distant; Evaluate(s,{},&adjacent);
	s.current[1].body.row=2; Evaluate(s,{},&distant);
	check(adjacent.goldenDrumSteps>0 && distant.goldenDrumSteps==0,"live golden source helps the neighboring lane, not lanes outside its three-row range");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=24; s.capacity=1; s.netEconomy=true;
	s.context[0][3]=s.context[0][4]=s.context[0][5]=3;
	Plant fire; fire.x=300; fire.health=10000; fire.dps=280; fire.edible=false; s.plants={fire};
	Unit convoy; convoy.body.x=1040; convoy.body.speed=15; convoy.body.health=4500; s.current={convoy};
	Option worker; worker.type=1917; worker.cost=24; worker.unit.body.x=1140; worker.unit.body.health=500;
	worker.unit.body.speed=16; worker.unit.body.economic=true; worker.unit.body.purchaseCost=24;
	worker.preference={-2.27f,-1.88f,0,-5.43f,-13.04f,-12,0,10.56f}; s.options={worker};
	Weights income{}; income[4]=1;
	const auto selected=Search(s,income,29);
	std::cout << "Protected profitable worker: income=" << Evaluate(s,{{0,0}})[4]
		<< " selected=" << selected.actions.size() << " preference=" << selected.preferenceScore << '\n';
	check(!selected.actions.empty(),"learned danger priors cannot veto an economically profitable reinforcement behind a living escort");
	check(selected.rawPreferenceScore<selected.preferenceScore && selected.preferenceScore>=-6,
		"raw danger priors remain observable while the net-economic contribution stays within the paid price bound");
	s.current[0].body.health=500;
	check(Search(s,income,29).actions.empty(),"bounded danger priors do not force an unprofitable worker behind a dying escort");
	s.options[0].preference[0]=500; s.options[0].unit.body.economic=false;
	check(Search(s,income,29).actions.empty(),"a bounded positive prior cannot make a useless purchase beat waiting");
	s.netEconomy=false;
	check(!Search(s,income,29).actions.empty(),"legacy non-economic policies retain their original preference semantics");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.budget=192; s.capacity=8; s.netEconomy=true;
	Plant fire; fire.x=300; fire.health=10000; fire.dps=100; fire.edible=false; s.plants={fire};
	Option worker; worker.type=1917; worker.cost=24; worker.unit.body.x=1000; worker.unit.body.health=500;
	worker.unit.body.economic=true; worker.unit.body.purchaseCost=24; s.options={worker};
	check(Evaluate(s,{{0,0},{0,0}})[4]<48,"one or two exposed producers cannot repay this sustained-fire investment");
	Weights income{}; income[4]=1;
	const auto batch=Search(s,income,61);
	check(batch.actions.size()>2 && batch.features[4]>batch.features[5] && batch.cohortEvaluated>0 && batch.combinationEvaluated<=80,
		"free batch exploration discovers profitable collective production after early casualties without increasing the combination budget");
	for (auto& p:s.plants) p.dps=10000;
	check(Search(s,income,61).actions.empty(),"batch opportunities do not require mass purchasing when the whole cohort dies before producing");
	}

	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.playerIce=119; s.interferenceAvailable=true;
	Unit dead; dead.id=1; dead.body.health=0; dead.body.x=900; s.current={dead};
	TemporalAnchor anchor; anchor.at=2; Unit saved=dead; saved.body.health=2200;
	anchor.targets.push_back({0,saved}); s.temporalAnchors={anchor};
	ConstructionStats stats;
	Evaluate(s,{},&stats);
	check(stats.clockRevivals==1 && stats.interferences==0,"unaffordable interference cannot erase a committed revival");
	s.playerIce=120; Evaluate(s,{},&stats);
	check(stats.clockRevivals==0 && stats.interferences==1 && stats.iceSpent==120,
		"paid interference cancels dead-target records before rewind and spends the shared player wallet once");
	check(s.playerIce==120 && s.temporalAnchors.size()==1,"interference forecast cannot change live wallet or anchors");
	s.interferenceReady=3; Evaluate(s,{},&stats);
	check(stats.clockRevivals==1 && stats.interferences==0,"a cooling interference cannot cancel an earlier rewind");
	s.interferenceAvailable=false; s.interferenceRemaining=6; Evaluate(s,{},&stats);
	check(stats.clockRevivals==0 && stats.interferences==0 && stats.iceSpent==0,"already paid interference is not charged again");
	// 灰烬前取消锚与灰烬费用共用钱包，预算不足或仍在冷却时不能凭空获得连招。
	Snapshot combo; combo.houseX=-10000; combo.playerSun=500; combo.playerIce=140;
	combo.interferenceAvailable=true;
	Unit worker; worker.body.x=900; worker.body.health=500; worker.body.purchaseCost=24; worker.body.economic=true;
	combo.current={worker,worker,worker}; TemporalAnchor future; future.at=20;
	for(int i=0;i<3;++i) future.targets.push_back({i,worker});
	combo.temporalAnchors={future}; Counter ash; ash.blast.x=900; ash.blast.damage=1800;
	ash.blast.reach.fill(10000); ash.iceCost=20; combo.counters={ash};
	Evaluate(combo,{},&stats,0,0,0,false,false,-1,false,false,false,true);
	std::cout << "pre-ash interference=" << stats.interferences << " revivals=" << stats.clockRevivals
		<< " ice=" << stats.iceSpent << " casts=" << stats.paidCounterCasts << '\n';
	check(stats.interferences==1 && stats.clockRevivals==0 && stats.iceSpent==120 && stats.paidCounterCasts==1,
		"pre-ash interference reserves and spends both real costs once before cancelling the anchor");
	check(combo.playerIce==140 && combo.temporalAnchors.size()==1 && combo.current[0].body.health==500,
		"pre-ash combination cannot mutate the sampled wallet, live health or anchor");
	combo.playerIce=139;
	Evaluate(combo,{},&stats,0,0,0,false,false,-1,false,false,false,true);
	check(stats.interferences==0 && stats.clockRevivals==3,"insufficient combined funds cannot prepay interference and ash");
	combo.playerIce=140; combo.interferenceReady=30;
	Evaluate(combo,{},&stats,0,0,0,false,false,-1,false,false,false,true);
	check(stats.interferences==0 && stats.clockRevivals==3,"cooling time interference does not stop an earlier ash revival");
	}
	{
	using namespace ColdStorageSearch;
	Snapshot s; s.houseX=-10000; s.playerSun=1000; s.playerIce=1000;
	Plant alive; alive.id=1; alive.eliteQuota=true; alive.health=500; alive.x=200; s.plants={alive};
	Construction card; card.source=0; card.remainingUses=4; card.simultaneousLimit=1;
	card.sunCost=200; card.iceCost=30; card.recharge=10; card.plant=alive;
	card.plant.column=1; card.plant.x=280; card.plant.dps=100; s.construction={card};
	ConstructionStats stats; Evaluate(s,{},&stats);
	check(stats.planted==0,"replacement allowances cannot exceed the concurrent elite limit");
	s.plants[0].health=0; Evaluate(s,{},&stats);
	check(stats.planted==1,"losing an elite permits a paid replacement without raising the concurrent cap");
	s.construction[0].remainingUses=0; Evaluate(s,{},&stats);
	check(stats.planted==0,"an empty board does not replenish the cumulative replacement allowance");
	}

    {
    using namespace WeatherStationRules;
    Control device; device.pending=3; device.warning=WarningSeconds;
    check(!Advance(device,7) && device.value==0,"station prewarning does not apply environment early");
    check(Advance(device,2) && device.value==3 && device.protection==29,"station crossing keeps only remaining time in protection");
    check(!CanChange(device,RAIN,0),"both factions must respect the active protection");
    Advance(device,29);
    check(CanChange(device,RAIN,0) && !CanChange(device,RAIN,3),"expired device unlocks but cannot renew its current setting");
    }
    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.capacity=4; s.budget=100;
    s.stationCharge=74; s.station.controls[0].value=3;
    Unit caster; caster.id=1; caster.hijacker=true; caster.body.health=1000; caster.temporalStopHealth=333;
    caster.body.x=1000; caster.body.value=caster.body.purchaseCost=24;
    Unit worker; worker.id=2; worker.body.health=500; worker.body.x=1050; worker.body.economic=true;
    worker.body.value=worker.body.purchaseCost=24;
    s.current={caster,worker};
    Plant target; target.health=1000; target.x=300; target.reward=30; target.assetValue=100;
    target.executionGroup=0; target.countsExecution=target.diesExecution=true; s.plants={target};
    ConstructionStats offStats,onStats;
    const auto off=Evaluate(s,{},&offStats);
    s.station.controls[2].value=1;
    const auto on=Evaluate(s,{},&onStats);
    check(offStats.stationDischarges==0 && onStats.stationDischarges>0,"disabled station freezes charge; enabled rainy station reaches discharge");
    check(on[0]>off[0] && on[1]>off[1] && on[3]<off[3] && on[4]<off[4],"hijacker includes rewarded plant kills, friendly casualties and lost production");
    check(s.current[0].body.health==1000 && s.current[1].body.health==500 && s.stationCharge==74,"station forecast never mutates the source battlefield");

    Plant shell=target; shell.layer=2; shell.health=3000; shell.reward=40; shell.assetValue=50;
    s.plants.push_back(shell);
    check(Evaluate(s,{})[1]==0,"pumpkin and host health jointly exceed the hijacker execution line");
    s.plants[1].health=500;
    check(Evaluate(s,{})[1]==70,"a damaged pumpkin cannot protect a group below the execution line");
    s.plants.resize(1); s.playerSun=1000;
    Construction pumpkin; pumpkin.plant=shell; pumpkin.plant.executionGroup=-1;
    pumpkin.plant.countsExecution=pumpkin.plant.diesExecution=false;
    pumpkin.sunCost=125; pumpkin.recharge=1000; s.construction={pumpkin};
    check(Evaluate(s,{},&onStats)[1]==0 && onStats.planted==1,
        "a future pumpkin protects its host by joining the same execution health group");
    s.construction.clear(); s.plants[0].crushDamage=10;
    s.plants[0].immuneRemaining=100; s.plants[0].deploymentInterceptionOnly=true;
    check(Evaluate(s,{})[1]==30,
        "hijacker execution bypasses ordinary crush limits and temporary plant invulnerability");
    }
    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=800; s.budget=100; s.capacity=1;
    s.stationCharge=74; s.station.controls[0].value=3; s.station.controls[2].value=1;
    Unit front; front.body.row=1; front.body.x=900; front.body.speed=5;
    front.body.health=1000; front.body.purchaseCost=100;
    Unit reserve=front; reserve.body.x=1100; reserve.body.speed=10; reserve.body.spawnAt=20;
    reserve.body.health=3000;
    Unit worker=reserve; worker.body.spawnAt=0; worker.body.speed=0; worker.body.economic=true;
    worker.nextYield=18; s.current={front,reserve,worker};
    Plant income; income.row=4; income.column=0; income.x=300; income.health=300;
    income.reward=5; income.executionGroup=36; income.countsExecution=income.diesExecution=true;
    s.plants={income};
    Option hijack; hijack.type=1; hijack.cost=24; hijack.row=hijack.unit.body.row=4;
    hijack.unit.hijacker=true; hijack.unit.body.x=1050; hijack.unit.body.health=1000;
    hijack.unit.body.purchaseCost=24; hijack.unit.temporalStopHealth=333; s.options={hijack};
    ConstructionStats keep,ruin;
    const auto baseline=Evaluate(s,{},&keep), sacrifice=Evaluate(s,{{0,0}},&ruin);
    check(baseline[2]==1 && sacrifice[2]==1 && ruin.breachSeconds>keep.breachSeconds
        && sacrifice[1]>baseline[1] && sacrifice[4]>baseline[4],
        "hijacker can destroy the front to harvest a small kill and more income before a later reserve wins");
    Weights incomeOnly{}; incomeOnly[4]=1;
    check(Search(s,incomeOnly,17).actions.empty(),
        "the commander rejects hijacker friendly fire that delays a winning front for side income");
    Option reinforcement=hijack; reinforcement.type=2; reinforcement.unit.hijacker=false;
    reinforcement.unit.body.row=reinforcement.row=1; reinforcement.unit.body.x=850;
    reinforcement.unit.body.speed=10; reinforcement.unit.body.health=3000;
    s.options.push_back(reinforcement);
    const auto supported=Search(s,incomeOnly,17);
    check(!supported.actions.empty() && supported.actions.front().option==1
        && supported.construction.breachSeconds<keep.breachSeconds,
        "a winning position still buys reinforcement that secures an earlier breach");
    s.current.clear(); s.options.resize(1); s.houseX=-10000; s.plants[0].reward=100;
    Weights kills{}; kills[1]=1; kills[5]=-1;
    check(!Search(s,kills,17).actions.empty(),
        "hijacker remains a legal autonomous choice when the exchange is profitable without friendly losses");
    }
    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.columns=9; s.rows=5;
    Unit u; u.body.health=10000; u.body.x=900; u.body.speed=10; u.body.purchaseCost=20; u.goldenStacks=3;
    s.current={u};
    const auto clear=Evaluate(s,{});
    s.station.controls[1].value=4;
    const auto fog=Evaluate(s,{});
    check(fog[7]<clear[7],"fog reduces movement independently of golden ice");
    Plant light; light.health=1000; light.plantern=true; light.lightFuel=100; light.lightGear=PlanternGear::LOW; light.illumination.fill(1); s.plants={light};
    const auto lit=Evaluate(s,{});
    check(std::abs(lit[7]-clear[7])<.001f,"illumination removes fog movement loss");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.playerIce=40;
    s.station.controls[2].value=1; s.stationCharge=80;
    Unit observer; observer.body.health=10000; observer.body.x=900; s.current={observer};
    Plant target; target.health=1000; target.assetValue=100; target.x=200; s.plants={target};
    ConstructionStats visible,hidden;
    Evaluate(s,{},&visible); s.stationJammed=100; Evaluate(s,{},&hidden);
    check(visible.stationCounters>0 && hidden.stationCounters==0,
        "a player forecast starting inside blackout cannot read hidden live charge");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.playerSun=1000; s.playerIce=200;
    Unit worker; worker.body.x=600; worker.body.health=500; worker.body.purchaseCost=24;
    worker.body.economic=true; worker.productionRemaining=2; s.current={worker};
    Option purchase; purchase.cost=24; purchase.unit=worker; s.options={purchase};
    Counter bomb; bomb.blast.x=600; bomb.blast.damage=1800; bomb.blast.reach.fill(-1); bomb.blast.reach[0]=100;
    bomb.sunCost=125; bomb.iceCost=30; bomb.recharge=100; s.counters={bomb};
    const auto clear=Evaluate(s,{});
    s.station.controls[1].value=4; s.stationFogAlpha.fill(255);
    const auto hidden=Evaluate(s,{});
    check(hidden[4]>clear[4] && hidden[6]<clear[6],"fog prevents a manual bomb from precisely locating one unseen worker");
    s.current.assign(20,worker);
    const auto blind=Evaluate(s,{});
    check(blind[6]>0,"sufficient public pressure can still trigger a blind bomb and kill hidden units");
    for(auto& u:s.current) u.body.row=4;
    check(Evaluate(s,{})[6]==0,"blind bombing does not relocate to the hidden units' exact row");
    s.current={worker};
    Plant lamp; lamp.plantern=true; lamp.health=1000; lamp.lightFuel=100; lamp.lightGear=PlanternGear::LOW;
    lamp.illumination.fill(.72f); s.plants={lamp};
    check(Evaluate(s,{})[6]==24,"III-thin illumination permits precise bomb targeting after fog clears");
    s.plants.clear(); s.counters[0].blast.committed=true;
    check(Evaluate(s,{})[6]==24,"committed explosions hit fogged units normally");
    }
    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.budget=48; s.capacity=2;
    Option worker; worker.cost=24; worker.type=17; worker.unit.body.x=900; worker.unit.body.health=500;
    worker.unit.body.economic=true; worker.unit.body.purchaseCost=24; s.options={worker};
    Weights weights{}; weights[4]=1;
    s.timeLimitedSearch=true; s.searchDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
    const auto stopped=Search(s,weights,42);
    check(stopped.timeLimited && stopped.actions.empty() && stopped.features==stopped.baselineFeatures,
        "expired realtime budget returns a complete baseline rather than a partial forecast");
    s.timeLimitedSearch=false;
    check(!Search(s,weights,42).actions.empty(),"synchronous search ignores the realtime deadline and retains full exploration");
    }

    {
    using namespace ColdStorageSearch;
    check(PlanternRules::BurnRate(PlanternGear::HIGH,PlanternRules::Scarcity(true,30,1))==2.1f
        && PlanternRules::BurnRate(PlanternGear::HIGH,PlanternRules::Scarcity(true,31,0))==4.0f,
        "station III fuel cost changes only after wave 30");
    check(PlanternRules::Illumination(PlanternGear::HIGH,0,5)>.6f
        && PlanternRules::Illumination(PlanternGear::HIGH,0,6)==0,
        "III edge is thin visible fog while cells outside the actual shape remain dark");
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.station.controls[1].value=4;
    Unit u; u.body.health=10000; u.body.x=700; u.body.speed=10; u.body.purchaseCost=20; s.current={u};
    Plant lamp; lamp.plantern=true; lamp.health=1000; lamp.x=0; lamp.lightGear=PlanternGear::HIGH;
    lamp.lightFuel=30; lamp.illumination.fill(1); s.plants={lamp}; s.stationWave=30;
    const auto early=Evaluate(s,{});
    s.stationWave=31; const auto late=Evaluate(s,{});
    check(early[7]>late[7],"higher late III burn extinguishes light sooner and restores fog slowing");
    s.plants[0].lightFuel=0; const auto empty=Evaluate(s,{});
    check(empty[7]<late[7],"empty lamp provides no permanent illumination");
    s.plants[0].lightDeliveries={{3,30}}; const auto refueled=Evaluate(s,{});
    check(refueled[7]>empty[7],"already reserved fuel restores light after arrival");
    check(s.plants[0].lightFuel==0 && s.plants[0].lightDeliveries.size()==1,"fuel rollout never mutates real snapshot");
    }

    {
    using namespace ColdStorageSearch;
    using namespace ColdStorageDeploymentRules;
    check(Capacity(2000)==64 && Capacity(4000)==106 && Capacity(8000)==192 && Capacity(100000)==192,
        "deployment room grows continuously with capital and stays bounded");
    check(Capacity(8000-768+768)==192,"transferring cash to paid troops preserves capacity");
    Snapshot s; s.budget=8000; s.capacity=192; s.houseX=100;
    Option unit; unit.cost=4; unit.unit.body.purchaseCost=4;
    unit.unit.body.health=100; unit.unit.body.x=900; unit.unit.body.speed=40; s.options={unit};
    Plant fire; fire.health=1000; fire.x=50; fire.dps=500; fire.edible=false; s.plants={fire};
    Weights weights{}; weights[2]=100; weights[5]=-1;
    const auto large=Search(s,weights,731);
    check(large.expandedForecast && large.largestPlan>64 && !large.actions.empty() && large.features[2]>0,
        "funded commander compares and chooses a larger breakthrough without forcing purchases");
    check(large.actions.size()<=192 && large.features[5]<=s.budget,"large plans obey both live room and real money");
    s.plants[0].dps=100000; s.plants[0].multiTarget=true;
    check(Search(s,weights,731).actions.empty(),"wealth does not require buying a doomed army");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.playerSun=640; s.playerIce=1000;
    Unit worker; worker.body.row=2; worker.body.x=1000; worker.body.health=500;
    worker.body.economic=true; worker.body.purchaseCost=24; worker.productionRemaining=IceProduction::Interval;
    s.current.assign(20,worker);
    Counter chili; chili.blast.x=760; chili.blast.damage=1800; chili.blast.ready=1;
    chili.blast.reach.fill(-1); chili.blast.reach[2]=10000;
    chili.cellRow=2; chili.cellColumn=7; chili.sunCost=125; chili.iceCost=20; chili.recharge=50; chili.windup=1;
    s.counters={chili};
    Construction wall; wall.plant.health=8000; wall.plant.x=760; wall.plant.row=2; wall.plant.column=7;
    wall.sunCost=150; wall.recharge=100; s.construction={wall};
    ConstructionStats built,held;
    const auto building=Evaluate(s,{},&built);
    const auto reserving=Evaluate(s,{},&held,0,0,0,true);
    std::cout<<"Counter space: building income="<<building[4]<<" reserved="<<reserving[4]<<" plantings="<<built.planted<<"/"<<held.planted<<"\n";
    check(built.planted==1 && held.planted==0 && building[4]>reserving[4] && reserving[4]==0,
        "reserving a legal counter cell avoids invented safety from automatically filling it");
    Weights weights{}; weights[4]=1;
    const auto robust=Search(s,weights,42);
    check(robust.construction.counterSpaceReserved && robust.features[4]==0,
        "complete plan comparison includes the legal counter-first player response");
    s.playerSun=0;
    const auto poor=Evaluate(s,{},nullptr,0,0,0,true);
    check(poor[4]>0,"a player without sun cannot invent a bomb and denies no genuine development window");
    Plant producer; producer.health=1000; producer.x=0; producer.sunPerSecond=50; s.plants={producer};
    check(Evaluate(s,{},nullptr,0,0,0,true)[4]==0,"real sun income reopens affordable counterplay during the rollout");
    s.plants.clear(); s.playerSun=640; s.counters[0].blast.ready=70;
    check(Evaluate(s,{},nullptr,0,0,0,true)[4]>0,"reserving a cell does not bypass an unavailable counter cooldown");
    s.counters[0].blast.ready=5; s.counters.clear();
    check(Evaluate(s,{},nullptr,0,0,0,true)[4]>0,"without the selected counter card the army keeps its genuine income");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.playerIce=40;
    s.station.controls[WeatherStationRules::FOG].value=4; s.stationFogAlpha.fill(255);
    Unit worker; worker.body.x=700; worker.body.health=500; worker.body.purchaseCost=24;
    worker.body.economic=true; worker.productionRemaining=3.6f; s.current={worker};
    Plant shooter; shooter.x=0; shooter.health=10000; shooter.dps=100; shooter.edible=false; s.plants={shooter};
    ConstructionStats keptStats,clearStats;
    const auto kept=Evaluate(s,{},&keptStats);
    const auto cleared=Evaluate(s,{},&clearStats,0,0,0,false,true);
    std::cout<<"Fog counter: kept="<<kept[4]<<" cleared="<<cleared[4]<<" cost="<<clearStats.iceSpent<<"\n";
    check(kept[4]>cleared[4] && cleared[4]>0 && clearStats.stationFogCounters==1 && clearStats.iceSpent==40,
        "paid fog clearing denies prolonged worker shelter only after the real warning window");
    check(s.playerIce==40 && s.station.controls[1].value==4 && s.station.controls[1].pending==-1,
        "fog counter rollout cannot mutate the live wallet or device");
    Weights income{}; income[4]=1;
    const auto robust=Search(s,income,42);
    check(robust.construction.stationFogCounters==1 && robust.features[4]==cleared[4],
        "free search compares paid clearing with keeping fog in a complete player world");
    s.playerIce=39;
    check(Evaluate(s,{},&clearStats,0,0,0,false,true)[4]==kept[4] && clearStats.stationFogCounters==0,
        "unaffordable fog clearing preserves the genuine worker development window");
    s.playerIce=40; s.station.controls[1].protection=100;
    check(Evaluate(s,{},&clearStats,0,0,0,false,true)[4]==kept[4] && clearStats.stationFogCounters==0,
        "fog protection cannot be bypassed by the player forecast");
    s.station.controls[1].protection=10;
    check(Evaluate(s,{},nullptr,0,0,0,false,true)[4]>cleared[4],
        "remaining protection delays clearing and extends actual productive time");
    s.station.controls[1].protection=0; s.station.controls[1].pending=0; s.station.controls[1].warning=8;
    Evaluate(s,{},&clearStats,0,0,0,false,true);
    check(clearStats.stationFogCounters==0 && clearStats.iceSpent==0,"already paid fog clearing is never charged twice");
    s.station.controls[1].pending=-1; s.stationJammed=100;
    check(Evaluate(s,{},&clearStats,0,0,0,false,true)[4]==cleared[4] && clearStats.stationFogCounters==1,
        "blackout hides forecasts but does not hide present fog or forbid a legal control");
    s.stationJammed=0; s.station.controls[2].value=1; s.stationCharge=80; s.plants[0].assetValue=100;
    Evaluate(s,{},&clearStats,0,0,0,false,true);
    check(clearStats.stationCounters==0 && clearStats.iceSpent==40,"fog and charge counters cannot spend the same forty ice twice");
    s.station.controls[2].value=0; s.stationCharge=0;
    s.plants.clear(); s.current[0].body.economic=false; s.current[0].body.speed=10;
    Weights advance{}; advance[7]=1;
    check(Search(s,advance,42).construction.stationFogCounters==0,
        "player keeps slowing fog when removing it would only help the attacker");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.cellWidth=80;
    s.station.controls[1].value=4; s.station.controls[1].protection=100; s.stationFogAlpha.fill(255);
    Unit worker; worker.body.row=0; worker.body.x=700; worker.body.health=500;
    worker.body.purchaseCost=24; worker.body.economic=true; worker.productionRemaining=3.6f; s.current={worker};
    Plant shooter; shooter.row=0; shooter.x=0; shooter.health=10000; shooter.dps=100; shooter.edible=false;
    Plant lamp; lamp.plantern=true; lamp.row=2; lamp.column=4; lamp.x=360; lamp.health=300;
    lamp.lightFuel=52; lamp.lightGear=PlanternGear::OFF; s.plants={shooter,lamp};
    const auto off=Evaluate(s,{});
    const auto low=Evaluate(s,{},nullptr,0,0,0,false,false,1);
    const auto medium=Evaluate(s,{},nullptr,0,0,0,false,false,2);
    const auto high=Evaluate(s,{},nullptr,0,0,0,false,false,3);
    check(off[4]==low[4] && off[4]==medium[4] && high[4]<off[4],
        "switching to III reveals its real thin edge; smaller gears cannot invent coverage");
    Weights income{}; income[4]=1;
    const auto response=Search(s,income,42);
    check(response.construction.planternResponseGear==3 && response.features[4]==high[4],
        "commander compares opening the fueled but currently switched-off lamp");
    check(s.plants[1].lightGear==PlanternGear::OFF && s.plants[1].lightFuel==52 && s.plants[1].illumination[8]==0,
        "projected gear, fuel and footprint never mutate the source lamp");
    s.plants[1].row=4; s.plants[1].column=0;
    check(Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]==off[4],"III cannot illuminate outside its actual position and shape");
    s.plants[1]=lamp; s.plants[1].lightFuel=0;
    check(Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]==off[4],"opening an empty lamp does not conjure fuel");
    s.plants[1].lightDeliveries={{10,30}};
    check(Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]>high[4]
        && Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]<off[4],"known fuel deliveries light the lamp only after arrival");
    s.plants[1]=lamp; s.plants[1].health=0;
    check(Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]==off[4],"a dead lamp cannot respond to fog");
    s.plants[1]=lamp; s.plants[1].lightFuel=10; s.plants[0].dps=0;
    s.current[0].body.health=10000; s.current[0].body.speed=10;
    s.stationWave=30; const auto early=Evaluate(s,{},nullptr,0,0,0,false,false,3);
    s.stationWave=31; const auto late=Evaluate(s,{},nullptr,0,0,0,false,false,3);
    check(early[7]>late[7],"active III response still loses useful light sooner after wave thirty");
    s.current[0]=worker;
    s.stationWave=30; s.plants[0].dps=100; s.plants[1]=lamp; s.plants[1].lightFuel=30;
    s.plants[1].lightGear=PlanternGear::HIGH;
    for(int r=0;r<s.rows;++r) for(int c=0;c<s.columns;++c)
        s.plants[1].illumination[r*s.columns+c]=PlanternRules::Illumination(PlanternGear::HIGH,r-2,c-4);
    s.station.controls[1].value=0; s.station.controls[1].pending=4; s.station.controls[1].warning=30;
    s.stationFogAlpha.fill(0); s.current[0].body.spawnAt=35;
    check(Evaluate(s,{},nullptr,0,0,0,false,false,3)[4]<Evaluate(s,{})[4],
        "player can conserve fuel while clear and relight when fog actually appears before a delayed wave");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.cellWidth=80;
    s.station.controls[1].value=4; s.station.controls[1].protection=100; s.stationFogAlpha.fill(255);
    s.playerSun=150; s.playerIce=40;
    Plant shooter; shooter.row=0; shooter.x=0; shooter.health=10000; shooter.dps=1; shooter.edible=false;
    s.plants={shooter};
    Unit worker; worker.body.row=0; worker.body.x=680; worker.body.health=500;
    worker.body.purchaseCost=24; worker.body.economic=true; worker.productionRemaining=3.6f; s.current={worker};
    Construction lamp; lamp.source=1; lamp.sunCost=25; lamp.iceCost=10; lamp.ready=5; lamp.recharge=100;
    lamp.plant.plantern=true; lamp.plant.row=2; lamp.plant.column=4; lamp.plant.x=360;
    lamp.plant.health=300; lamp.plant.lightFuel=PlanternRules::InitialFuel; lamp.plant.lightGear=PlanternGear::LOW;
    for(int r=0;r<s.rows;++r) for(int c=0;c<s.columns;++c)
        lamp.plant.illumination[r*s.columns+c]=PlanternRules::Illumination(PlanternGear::LOW,r-2,c-4);
    Construction wall; wall.sunCost=150; wall.plant.row=0; wall.plant.column=7; wall.plant.x=600; wall.plant.health=8000;
    s.construction={wall,lamp};
    Counter blast; blast.blast.x=600; blast.blast.damage=1800; blast.blast.reach.fill(-1); blast.blast.reach[0]=130;
    blast.cellRow=0; blast.cellColumn=7; blast.sunCost=125; blast.iceCost=30; blast.windup=1; blast.recharge=100;
    s.counters={blast};
    ConstructionStats dark,lit;
    const auto noBuilding=Evaluate(s,{},&dark,0,0,0,true);
    const auto lighting=Evaluate(s,{},&lit,0,0,0,true,false,3);
    check(dark.planted==0 && lit.planted==1 && lit.sunSpent==25 && lit.iceSpent==10 && lighting[4]<noBuilding[4],
        "counter-first player can rebuild only the lamp and keep the blast cell and shared resources available");
    check(lighting[4]>0,"lamp cooldown preserves real income before visibility returns");
    Weights income{}; income[4]=1;
    const auto chosen=Search(s,income,17);
    std::cout<<"Counter light: dark/lit/chosen="<<noBuilding[4]<<"/"<<lighting[4]<<"/"<<chosen.features[4]
        <<" plantings="<<chosen.construction.planted<<" reserved="<<chosen.construction.counterSpaceReserved<<"\n";
    check(chosen.construction.planted==1 && chosen.features[4]<=lighting[4],
        "free search includes rebuilding counter visibility instead of treating every reserved world as permanently dark");
    s.playerIce=9;
    check(Evaluate(s,{},&lit,0,0,0,true,false,3)[4]==noBuilding[4] && lit.planted==0,
        "unaffordable lamp does not create free counter visibility");
    s.playerIce=40; s.construction[1].ready=100;
    check(Evaluate(s,{},&lit,0,0,0,true,false,3)[4]==noBuilding[4] && lit.planted==0,
        "lamp rebuilding cannot bypass the selected card cooldown");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=-10000; s.gridLeft=0; s.cellWidth=80;
    s.station.controls[1].value=4; s.station.controls[1].protection=100; s.stationFogAlpha.fill(255);
    s.incomingIce=50; s.incomingIceAt=8; s.anticipateEconomy=true;
    Plant lamp; lamp.plantern=true; lamp.row=2; lamp.column=4; lamp.x=360; lamp.health=300; lamp.lightFuel=9;
    Plant shooter; shooter.row=4; shooter.x=0; shooter.health=10000; shooter.dps=100; shooter.edible=false;
    s.plants={lamp,shooter};
    Unit carrier; carrier.body.row=1; carrier.body.x=680; carrier.body.health=100;
    carrier.body.purchaseCost=24; carrier.mistFuelReward=40;
    Unit worker; worker.body.row=4; worker.body.x=680; worker.body.health=200; worker.body.spawnAt=12;
    worker.body.purchaseCost=24; worker.body.economic=true; worker.productionRemaining=3.6f;
    s.current={carrier,worker};
    Counter blast; blast.source=0; blast.blast.x=680; blast.blast.reach.fill(-1); blast.blast.reach[1]=100;
    blast.blast.damage=1800; blast.iceCost=50; blast.windup=1; blast.recharge=1000; s.counters={blast};
    const auto adaptive=Evaluate(s,{},nullptr,0,0,0,false,false,FuelAwarePlanternResponse);
    for(int gear=0;gear<=3;++gear) check(adaptive[4]<Evaluate(s,{},nullptr,0,0,0,false,false,gear)[4],
        "saving light until a paid blast refuels it then opening III exposes a worker missed by every fixed gear");
    Weights income{}; income[4]=1;
    check(Search(s,income,17).construction.planternResponseGear==FuelAwarePlanternResponse,
        "commander considers fuel-aware relighting instead of treating medium-gear shelter as permanent");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.playerSun=1000; s.playerIce=1000;
    Option troop; troop.cost=24; troop.row=troop.unit.body.row=2;
    troop.unit.body.x=900; troop.unit.body.health=1000; troop.unit.body.purchaseCost=24;
    s.options={troop};
    Counter doom; doom.source=0; doom.blast.x=850; doom.blast.reach.fill(-1); doom.blast.reach[2]=100;
    doom.blast.damage=1800; doom.windup=1; doom.recharge=5; doom.cellRow=2; doom.cellColumn=8;
    doom.deploymentHealth=300; doom.clearsCell=true; doom.craterSeconds=180;
    s.counters={doom}; ConstructionStats stats;
    const auto held=Evaluate(s,{{0,0},{0,12}},&stats);
    check(held[3]==24 && stats.cratersCreated==1,
        "a doom crater prevents casting again in the same cell against a later wave");
    check(stats.workerTrace.empty() && stats.counterTrace.empty(),"ordinary forecasts allocate no diagnostic trajectories");
    s.traceEconomy=true;
    check(Evaluate(s,{{0,0},{0,12}},&stats)==held && stats.counterTrace.size()==1,
        "counter trace records the actual explosion without changing forecast outcomes");
    s.traceEconomy=false;
    s.counters[0].craterSeconds=5;
    check(Evaluate(s,{{0,0},{0,12}})[3]==0,"an expired crater releases its planting cell");
    s.counters={doom,doom}; s.counters[1].cellColumn=7;
    check(Evaluate(s,{{0,0},{0,12}})[3]==0,"another legal blast cell remains available during a crater");
    s.counters={doom}; s.anticipateEconomy=true;
    Construction seed; seed.source=0; seed.ready=3; seed.recharge=1000;
    seed.plant.row=2; seed.plant.column=8; seed.plant.x=850; seed.plant.health=300; seed.plant.sunPerSecond=1;
    s.construction={seed}; SunExchange exchange; exchange.ready=3; exchange.recharge=1000;
    exchange.sunGain=100; exchange.iceCost=10; exchange.cells={{2,8}}; s.exchanges={exchange};
    Evaluate(s,{{0,0}},&stats);
    check(stats.planted==0 && stats.exchanges==0,
        "a new crater blocks both ordinary construction and economy-card cycling");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.weatherStation=true; s.houseX=800; s.budget=100; s.capacity=1; s.netEconomy=true;
    Unit runner; runner.body.x=1000; runner.body.speed=10; runner.body.health=300; runner.body.purchaseCost=4;
    Unit worker; worker.body.x=1050; worker.body.health=500; worker.body.economic=true;
    worker.body.purchaseCost=24; worker.nextYield=18;
    s.current={runner,worker};
    Option fog; fog.device=1; fog.setting=2; fog.cost=20; s.options={fog};
    Weights income{}; income[4]=1;
    check(Search(s,income,17).actions.empty(),
        "delaying an existing house breach to collect extra production is not a better victory");
    ConstructionStats traced;
    const auto plain=Evaluate(s,{}); s.traceEconomy=true;
    check(Evaluate(s,{},&traced)==plain,"worker tracing must not alter combat or economic outcomes");
    float recorded=0; for(const auto& sample:traced.workerTrace) recorded+=sample.income;
    check(recorded==plain[4] && !traced.workerTrace.empty(),"diagnostic production events match credited income before victory");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-1000; s.budget=100; s.capacity=1; s.netEconomy=true;
    s.opponentWeight=1; s.playerIce=200;
    Plant shooter; shooter.id=1; shooter.x=300; shooter.row=1; shooter.column=1;
    shooter.dps=100; shooter.health=300;
    Plant source=shooter; source.id=2; source.column=2; source.x=400; source.dps=0;
    s.plants={shooter,source};
    AttackAura aura; aura.plantID=2; aura.duration=12; aura.recharge=9; aura.bonus=1; aura.iceCost=30;
    s.attackAuras={aura};
    Option bait; bait.type=1; bait.row=bait.unit.body.row=1; bait.cost=12;
    bait.unit.body.x=900; bait.unit.body.health=100; bait.unit.body.purchaseCost=12;
    s.options={bait}; Weights income{}; income[4]=1;
    check(Search(s,income,42).actions.empty(),
        "a harmless bait cannot earn pressure by assuming a manual aura is always activated");
    s.attackAuras[0].automatic=true;
    const auto automatic=Search(s,income,42);
    check(automatic.construction.auraActivations>0,
        "the conservative response cannot disable an explicitly automatic aura");
    s.attackAuras[0].automatic=false; s.allowWait=false;
    s.options[0].unit.body.health=1400; s.options[0].unit.body.speed=50;
    s.options[0].unit.biteDps=1000; s.plants[0].reward=s.plants[1].reward=100;
    s.plants[0].assetValue=s.plants[1].assetValue=100;
    income[0]=3; income[1]=1;
    const auto dangerous=Search(s,income,42);
    check(!dangerous.actions.empty() && dangerous.construction.auraActivations>0,
        "the player still activates a manual aura when it prevents a damaging attack");
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.budget=1400; s.capacity=64;
    Option worker; worker.type=700; worker.cost=24; worker.unit.body.x=900;
    worker.unit.body.health=500; worker.unit.body.economic=true; worker.unit.body.purchaseCost=24;
    s.options={worker}; Weights income{}; income[4]=1;
    const auto opening=Search(s,income,42);
    check(!opening.expandedForecast && opening.largestPlan<=8,
        "a fresh productive opening retains the existing small-party search");
    Unit paid; paid.body.health=500; paid.body.x=900; paid.body.purchaseCost=600;
    paid.body.spawnAt=10; s.current={paid};
    const auto invested=Search(s,income,42);
    check(invested.expandedForecast && invested.largestPlan>8,
        "paid queued assets preserve full search after cash falls below the capital threshold");
    check(invested.reinforcementEvaluated>0 && invested.reinforcementEvaluated<=invested.combinationEvaluated,
        "generic reinforcement comparisons share the existing combination evaluation budget");
    float investedSpend=0;
    for(const auto& action:invested.actions) investedSpend+=s.options[action.option].cost;
    check(investedSpend<=s.budget,"committed capital expands exploration but cannot finance new purchases");
    s.current[0].body.spawnAt=0;
    check(Search(s,income,42).expandedForecast,"arrival preserves the same paid search capital");
    s.current[0].body.purchaseCost=0;
    check(!Search(s,income,42).expandedForecast,"free summons do not inflate search capital");
    s.current[0].body.purchaseCost=600; s.current[0].body.health=0;
    check(!Search(s,income,42).expandedForecast,"lost troops no longer preserve full search capital");
    s.current.clear();
    s.resumePortfolio=true;
    const auto continued=Search(s,income,42);
    check(continued.expandedForecast && continued.largestPlan>8,
        "a waiting commander can resume full formations below the rich-capital threshold");
    s.budget=24;
    check(!Search(s,income,42).expandedForecast,"a continuation hint cannot invent funds for a larger formation");
    s.budget=1400; s.searchVersion=2; s.weatherStation=true; s.netEconomy=true;
    s.options[0].unit.body.economic=false; s.options[0].cost=4;
    Option weather; weather.type=-1; weather.cost=40; weather.device=1; weather.setting=4;
    s.options.insert(s.options.begin(),weather);
    for(unsigned seed=1;seed<=8;++seed) {
        const auto trial=Search(s,income,seed);
        check(trial.largestPlan>=64 && !trial.candidates.empty() && trial.candidates.front().type==worker.type,
            "full search starts with an affordable troop formation rather than a deduplicated weather-only plan");
        check(trial.actions.empty(),"evaluating a large army does not force a pointless purchase");
        check(trial.reinforcementEvaluated>0,
            "rejected nonempty exploration anchors can receive followup comparisons without forcing a purchase");
    }
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.budget=192; s.capacity=8; s.netEconomy=true;
    Plant fire; fire.x=300; fire.health=10000; fire.dps=100; fire.range=1000; fire.edible=false; s.plants={fire};
    Option ordinary; ordinary.type=0; ordinary.cost=4; ordinary.unit.body.health=300;
    ordinary.unit.body.x=900; ordinary.unit.body.purchaseCost=4;
    Option worker; worker.type=56; worker.cost=24; worker.unit.body.health=500;
    worker.unit.body.x=1000; worker.unit.body.purchaseCost=24; worker.unit.body.economic=true;
    s.options={ordinary,worker}; Weights income{}; income[4]=1;
    check(Evaluate(s,{{1,0}})[4]<worker.cost,"an exposed individual worker cannot repay this ordinary-fire fixture");
    for(unsigned seed=1;seed<=8;++seed) {
        const auto economy=Search(s,income,seed);
        check(economy.features[4]>economy.features[5] && economy.actions.size()>1,
            "ordinary weak bodies and workers can freely form a profitable economy without a strong-unit prerequisite");
    }
    s.plants[0].multiTarget=true; s.plants[0].dps=10000;
    check(Search(s,income,42).actions.empty(),"weak-unit economy exploration still rejects a fully lethal field");
    }

    {
    using namespace ColdStorageSearch;
    // 两两搭配都亏损，加入第三类后才能生产；单一产冰排名不能提前丢掉这些探索中间态。
    Snapshot deep; deep.houseX=-10000; deep.searchVersion=2; deep.netEconomy=true;
    deep.budget=180; deep.capacity=8; deep.playerSun=5000; deep.playerIce=500;
    Plant fire; fire.x=300; fire.health=100000; fire.dps=400; fire.edible=false; deep.plants={fire};
    Option worker; worker.type=21001; worker.cost=24; worker.unit.body.x=1000;
    worker.unit.body.health=500; worker.unit.body.purchaseCost=24; worker.unit.body.economic=true;
    Option engineer; engineer.type=21002; engineer.cost=35; engineer.unit.body.x=1020;
    engineer.unit.body.health=1000; engineer.unit.body.purchaseCost=35; engineer.unit.engineer=true;
    Option front; front.type=21003; front.cost=32; front.unit.body.x=850;
    front.unit.body.health=12000; front.unit.body.purchaseCost=32;
    deep.options={worker,engineer,front};
    for(int i=0;i<13;++i) { auto fragile=engineer; fragile.type=23000+i; fragile.cost=12;
        fragile.unit.engineer=false; fragile.unit.body.health=270; fragile.unit.body.purchaseCost=12;
        deep.options.push_back(fragile); }
    Counter ash; ash.blast.x=900; ash.blast.reach.fill(10000); ash.blast.damage=1800; ash.sunCost=125; ash.recharge=1000;
    deep.counters={ash}; const Weights profit{0,0,0,0,1,-1,0,0};
    check(Evaluate(deep,{{0,0},{1,0}})[4]==0 && Evaluate(deep,{{0,0},{2,0}})[4]==0,
        "ash protection without frontline and frontline without ash protection both fail before income");
    const auto known=Evaluate(deep,{{0,0},{0,0},{0,0},{1,0},{2,0}});
    check(known[4]>known[5],"the three-type full plan repays its purchase under the exact same lethal threats");
    ConstructionStats progress;
    const auto unfinished=Evaluate(deep,{{0,0},{0,0},{0,0},{1,0}},&progress);
    check(progress.workerProtectionProgress>0 && unfinished[4]<=unfinished[5],
        "real protection progress preserves an unfinished branch without inventing ice income or profit");
    int misses=0;
    for(unsigned seed=1;seed<=16;++seed) {
        const auto found=Search(deep,profit,seed);
        if(found.features[4]<=found.features[5]) { ++misses; std::cout << "deep miss=" << seed << '\n'; }
    }
    std::cout << "three-type factory misses=" << misses << "/16\n";
    check(misses==0,"diverse unprofitable intermediates must be able to reach profitable three-type cooperation");
    auto renamed=deep;
    for(size_t i=0;i<renamed.options.size();++i) renamed.options[i].type=31000-static_cast<int>(i)*47;
    const auto before=Search(deep,profit,3), after=Search(renamed,profit,3);
    check(before.features==after.features && before.actions.size()==after.actions.size(),
        "multi-stage economy exploration retains the same outcome after all unit ids change");
    // 同一出生位置与移速，前排必须先出生才可挡住直射；不能在夹具里预先把肉盾放到前面。
    auto staged=deep;
    for(auto& option:staged.options) { option.unit.body.x=1100; option.unit.body.speed=10; }
    const std::vector<Action> together{{0,0},{0,0},{0,0},{1,0},{2,0}};
    const std::vector<Action> frontFirst{{2,0},{0,6},{0,6},{0,6},{1,6}};
    check(Evaluate(staged,together)[4]<=Evaluate(staged,together)[5],
        "normal co-located births do not grant a pre-positioned frontline");
    const auto stagedProfit=Evaluate(staged,frontFirst);
    check(stagedProfit[4]>stagedProfit[5],"actual lead time creates a profitable guarded factory from normal births");
    int stagedMisses=0;
    for(unsigned seed=1;seed<=8;++seed) {
        const auto found=Search(staged,profit,seed);
        if(found.features[4]<=found.features[5]) {
            ++stagedMisses;
            std::cout << "staged miss=" << seed << " income=" << found.features[4] << " cost=" << found.features[5]
                << " refinements=" << found.refinementEvaluated << " incomeEval=" << found.incomeEvaluated << '\n';
            for(const auto& c:found.candidates) if(c.type==front.type)
                std::cout << "front candidates=" << c.evaluated << " production=" << c.bestProduction << " cost=" << c.bestCost << '\n';
        }
    }
    std::cout << "normal-birth staged factory misses=" << stagedMisses << "/8\n";
    check(stagedMisses==0,"wide free search covers lead-time protection without pre-positioned tanks");
    // 四类共同起效的纯数值夹具；前排/费用为测试画像，不作为正式单位平衡或实战强度证据。
    auto fourth=deep; fourth.budget=240; fourth.capacity=10;
    fourth.options[2].unit.body.health=7000; fourth.options[2].cost=80; fourth.options[2].unit.body.purchaseCost=80;
    fourth.counters[0].blast.x=1000; fourth.counters[0].blast.reach.fill(-1); fourth.counters[0].blast.reach[0]=130;
    Option clock; clock.type=27008; clock.cost=18; clock.unit.body.x=1140; clock.unit.body.health=2200;
    clock.unit.body.purchaseCost=18; clock.unit.helmHealth=1200; clock.unit.temporalStopHealth=333;
    clock.unit.clock.present=clock.unit.clock.enabled=true; clock.unit.clock.remaining=2; clock.unit.clock.stopBodyHealth=333;
    fourth.options.push_back(clock); const int c=static_cast<int>(fourth.options.size()-1);
    const std::vector<Action> complete{{0,0},{0,0},{0,0},{1,0},{2,0},{c,0}};
    const auto whole=EvaluateCandidate(fourth,profit,complete);
    check(whole.score>0,"a legal four-type factory repays under the same common player response");
    for(int omit:{1,2,c}) {
        auto parts=complete;
        parts.erase(std::remove_if(parts.begin(),parts.end(),[&](const auto& a){return a.option==omit;}),parts.end());
        check(EvaluateCandidate(fourth,profit,parts).score<=0,
            "the same four-type purchase cannot claim profitable income with a required partner absent");
    }
    int fourMisses=0;
    for(unsigned seed=1;seed<=8;++seed) {
        const auto searched=Search(fourth,profit,seed);
        if(searched.features[4]<=searched.features[5]) {
            ++fourMisses; std::cout<<"four miss seed="<<seed<<" anchor=";
            for(const auto& a:searched.investmentPruningActions) std::cout<<a.option<<":"<<a.delay<<",";
            for(const auto& candidate:searched.candidates) if(candidate.type==clock.type)
                std::cout<<" clock="<<candidate.evaluated<<":"<<candidate.bestProduction<<":"<<candidate.bestCost;
            std::cout<<'\n';
        }
    }
    std::cout<<"four-type factory misses="<<fourMisses<<"/8\n";
    check(fourMisses==0,"free exploration must retain a profitable four-type cooperation in a wide roster");
    // 检查实时调度形状，但不给墙钟制造硬件依赖；一小时截止只让有限候选自行跑完。
    fourth.timeLimitedSearch=true;
    for(unsigned seed:{1u,3u,7u,11u}) {
        auto continuing=fourth;
        Result realtime;
        // 更深构成共享有限名额；验证未付款起点能跨少量决策继续深化，而非要求一轮随机搜索必胜。
        for(unsigned round=0;round<3;++round) {
            continuing.searchDeadline=std::chrono::steady_clock::now()+std::chrono::hours(1);
            realtime=Search(continuing,profit,seed+round*31);
            if(realtime.features[4]>realtime.features[5]) break;
            continuing.proposals=realtime.proposals;
        }
        std::cout<<"realtime four seed="<<seed<<" production="<<realtime.features[4]<<" cost="<<realtime.features[5]<<'\n';
        check(realtime.features[4]>realtime.features[5],
            "bounded realtime continuation must deepen incomplete multi-type income plans before repayment, without deadline luck");
    }
    }

    {
    using namespace ColdStorageSearch;
    // 同卡的各路落点共用冷却，比较完整协作分路；生命/价格只用于隔离搜索覆盖，非正式平衡。
    Snapshot field; field.houseX=-10000; field.searchVersion=2; field.netEconomy=true;
    field.budget=695; field.capacity=64; field.playerSun=5000; field.rows=5;
    Option worker; worker.type=41001; worker.cost=24; worker.unit.body.x=1000;
    worker.unit.body.health=500; worker.unit.body.purchaseCost=24; worker.unit.body.economic=true;
    Option engineer; engineer.type=41002; engineer.cost=35; engineer.unit.body.x=1020;
    engineer.unit.body.health=1000; engineer.unit.body.purchaseCost=35; engineer.unit.engineer=true;
    Option front; front.type=41003; front.cost=32; front.unit.body.x=850;
    front.unit.body.health=7000; front.unit.body.purchaseCost=32;
    for(int row=0;row<5;++row) {
        Plant fire; fire.row=row; fire.x=300; fire.health=100000; fire.dps=120; fire.edible=false;
        field.plants.push_back(fire);
        for(auto option:{worker,engineer,front}) { option.row=option.unit.body.row=row; field.options.push_back(option); }
        Counter ash; ash.blast.x=900; ash.blast.reach.fill(-1); ash.blast.reach[row]=10000;
        ash.blast.damage=10000; ash.sunCost=125; ash.recharge=8; field.counters.push_back(ash);
    }
    const Weights profit{0,0,0,0,1,-1,0,0};
    std::vector<Action> full;
    for(int rows=1;rows<=5;++rows) {
        std::vector<Action> plan;
        for(int row=0;row<rows;++row) { for(int i=0;i<3;++i) plan.push_back({row*3,0});
            plan.push_back({row*3+1,0}); plan.push_back({row*3+2,0}); }
        const auto result=EvaluateCandidate(field,profit,plan);
        std::cout<<"spread rows="<<rows<<" income="<<result.features[4]<<" cost="<<result.features[5]
            <<" protection="<<result.construction.workerProtectionProgress<<'\n';
        if(rows==1) check(result.score<=0,"one complete lane cannot repay against the same ready reusable ash");
        if(rows==5) {
            check(result.score>0,"multiple complete lanes can repay against a genuinely shared ash cooldown");
            full=std::move(plan);
        }
    }
    auto independent=field;
    for(size_t row=0;row<independent.counters.size();++row) independent.counters[row].source=static_cast<int>(row);
    check(EvaluateCandidate(independent,profit,full).score<=0,
        "the same multi-lane purchase loses when the defenses really have independent ready cooldowns");
    for(unsigned seed=1;seed<=8;++seed) {
        const auto result=Search(field,profit,seed);
        std::cout<<"spread seed="<<seed<<" income="<<result.features[4]<<" cost="<<result.features[5]
            <<" expanded="<<result.spreadCohortEvaluated<<'\n';
        check(result.features[4]>result.features[5] && result.spreadCohortEvaluated>0,
            "free search can resize and spread its own partial cooperation under the existing candidate budget");
        check(result.features[5]<=field.budget && result.actions.size()<=static_cast<size_t>(field.capacity)
            && result.combinationEvaluated<=80,"replicated cohorts must share the same real cash, slots and combination quota");
    }
    auto renamed=field;
    for(auto& option:renamed.options) option.type=51000-option.type;
    const auto before=Search(field,profit,3), after=Search(renamed,profit,3);
    check(before.features==after.features && before.actions.size()==after.actions.size(),
        "spreading a discovered cooperation does not depend on fixed zombie identities");
    auto small=field; small.budget=70;
    check(Search(small,profit,6).actions.empty(),
        "a wallet too small to retain each partner across routes must reject the proposal safely");
    }

    {
    using namespace ColdStorageSearch;
    // 可见直射阵地与就绪灰烬共同存在；保护赚的钱必须来自实际生产，不能假定先骗光灰烬。
    Snapshot field; field.houseX=-10000; field.searchVersion=2; field.netEconomy=true;
    field.budget=300; field.capacity=16; field.playerSun=2000; field.playerIce=500;
    Plant fire; fire.x=300; fire.health=100000; fire.dps=80; fire.edible=false; field.plants={fire};
    Option worker; worker.type=3001; worker.cost=24; worker.unit.body.x=1000;
    worker.unit.body.health=500; worker.unit.body.purchaseCost=24; worker.unit.body.economic=true;
    Option engineer; engineer.type=4007; engineer.cost=35; engineer.unit.body.x=1020;
    engineer.unit.body.health=1000; engineer.unit.body.purchaseCost=35; engineer.unit.engineer=true;
    Option tank; tank.type=5009; tank.cost=32; tank.unit.body.x=850;
    tank.unit.body.health=3000; tank.unit.body.purchaseCost=32;
    Option clock; clock.type=6113; clock.cost=18; clock.unit.body.x=1050;
    clock.unit.body.health=PolarClockRules::BodyHealth+PolarClockRules::ArmorHealth;
    clock.unit.helmHealth=PolarClockRules::ArmorHealth; clock.unit.body.purchaseCost=18;
    clock.unit.clock={true,true,false,PolarClockRules::Preparation,PolarClockRules::BodyHealth/3.0f};
    field.options={worker,engineer,tank,clock};
    Counter ash; ash.blast.x=900; ash.blast.reach.fill(-1); ash.blast.reach[0]=10000;
    ash.blast.damage=1800; ash.sunCost=125; ash.recharge=1000; field.counters={ash};
    const Weights profit{0,0,0,0,1,-1,0,0};
    const auto recipes=BuildExperiencedFormations(field,field.capacity,field.budget);
    bool profitableRecipe=false, guardedFollowup=false, protectedRecipe=false;
    for(const auto& recipe:recipes) {
        int bill=0; bool hasWorker=false, hasTank=false, hasEngineer=false;
        for(const auto& action:recipe) {
            bill+=field.options[action.option].cost;
            hasWorker|=action.option==0; hasTank|=action.option==2; hasEngineer|=action.option==1;
            if(action.option==0 && action.delay>=4) guardedFollowup=true;
        }
        check(bill<=field.budget && recipe.size()<=static_cast<size_t>(field.capacity),
            "experienced recipes preserve the complete wallet and deployment capacity");
        protectedRecipe|=hasWorker && hasTank && hasEngineer;
        const auto result=EvaluateCandidate(field,profit,recipe);
        profitableRecipe|=result.features[4]>result.features[5] && result.construction.paidCounterCasts>0;
    }
    check(guardedFollowup && protectedRecipe && profitableRecipe,
        "history-based complete recipes offer profitable protected workers while player ash remains ready");
    auto limited=field; limited.budget=50;
    for(const auto& recipe:BuildExperiencedFormations(limited,2,limited.budget)) {
        int bill=0; for(const auto& action:recipe) bill+=limited.options[action.option].cost;
        check(bill<=50 && recipe.size()<=2,"small wallets cannot truncate a protected recipe into an invalid purchase");
    }
    auto renamedRecipes=field;
    for(size_t i=0;i<renamedRecipes.options.size();++i) renamedRecipes.options[i].type=99000-static_cast<int>(i)*31;
    const auto renamedPlans=BuildExperiencedFormations(renamedRecipes,field.capacity,field.budget);
    check(recipes.size()==renamedPlans.size(),"experienced roles do not depend on hard-coded zombie identifiers");
    for(size_t i=0;i<recipes.size();++i) for(size_t j=0;j<recipes[i].size();++j)
        check(recipes[i][j].option==renamedPlans[i][j].option && recipes[i][j].delay==renamedPlans[i][j].delay,
            "renaming types retains experienced recipe quantities, routes and timing");
    auto freeOnly=field; freeOnly.experiencedFormations=false;
    check(BuildExperiencedFormations(freeOnly,field.capacity,field.budget).empty(),
        "disabling experience only removes its candidate source");
    auto rejecting=field; rejecting.plants[0].multiTarget=true; rejecting.plants[0].dps=100000;
    const auto rejectAll=Search(rejecting,profit,19);
    check(rejectAll.actions.empty() && rejectAll.experiencedEvaluated>0,
        "experienced recipes must still lose to waiting when their entire investment fails");
    const auto hybrid=Search(field,profit,17), originalFree=Search(freeOnly,profit,17);
    check(hybrid.experiencedEvaluated>0 && hybrid.combinationEvaluated>0 && hybrid.widestComposition>=3,
        "hybrid search retains free cooperation and mixed composition alongside evaluated recipes");
    check(originalFree.experiencedEvaluated==0 && originalFree.features[4]>originalFree.features[5],
        "free combinations remain a functional independent candidate source");
    check(field.options[1].unit.canisterFull && field.playerSun==2000 && field.playerIce==500,
        "experience and free comparisons cannot consume real protection or opponent resources");
    std::cout<<"Hybrid experienced recipes, ready-ash income, complete admission and free exploration passed\n";
    auto attackRecipes=field; attackRecipes.options[0].unit.body.economic=false;
    attackRecipes.options[2].unit.body.speed=20; attackRecipes.options[2].unit.body.smashSeconds=3;
    auto runner=tank; runner.type=72001; runner.cost=12; runner.unit.body.health=2000;
    runner.unit.body.speed=80; runner.unit.body.x=1100; runner.unit.body.purchaseCost=12;
    attackRecipes.options.push_back(runner); const int runnerIndex=static_cast<int>(attackRecipes.options.size()-1);
    bool rush=false, breachRush=false;
    for(const auto& recipe:BuildExperiencedFormations(attackRecipes,field.capacity,field.budget)) {
        bool opener=false, fast=false, worker=false;
        for(const auto& action:recipe) {
            opener|=action.option==2; fast|=action.option==runnerIndex;
            worker|=attackRecipes.options[action.option].unit.body.economic;
            check(action.delay>=0 && action.delay<=60,"legacy assault timing remains in the complete forecast domain");
        }
        rush|=fast && !opener && !worker;
        breachRush|=fast && opener && !worker;
    }
    check(rush && breachRush,"experience also offers pure fast assault and breach-plus-rush without requiring workers");
    ConstructionStats knownStats;
    std::vector<Action> defended{{2,0},{2,0},{0,0},{0,0},{0,0},{1,0}};
    const auto known=Evaluate(field,defended,&knownStats);
    check(known[4]>known[5] && knownStats.engineerBlocks==3 && knownStats.paidCounterCasts==1,
        "frontline plus engineer protects profitable production despite an affordable ready row ash");
    auto layered=defended; layered.push_back({3,6}); layered.push_back({3,12});
    const auto supported=Evaluate(field,layered,&knownStats);
    check(supported[4]>known[4] && knownStats.clockAnchors>=2 && knownStats.clockRewinds>0,
        "multiple delayed clocks can sustain an engineer-protected factory under continuous straight fire");
    check(Evaluate(field,{{0,0},{0,0},{0,0}})[4]==0,
        "the same ready ash wipes exposed workers before the first production tick");
    {
        auto wide=field;
        for(int i=0;i<12;++i) { auto weak=tank; weak.type=7000+i; weak.unit.body.health=270; wide.options.push_back(weak); }
        Planner realtime;
        check(realtime.Start(wide,profit,7,600),"ready-ash economic search uses the ordinary two-worker budget");
        std::unique_ptr<Planner::Work> completed;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!completed && std::chrono::steady_clock::now()<until) {
            completed=realtime.TakeReady();
            if(!completed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(completed && !completed->failed && completed->workerThreads==2 && completed->budgetMilliseconds==600,
            "economic branches neither add threads nor expand the real-time deadline");
        std::cout << "live ready-ash income=" << completed->result.features[4] << " cost=" << completed->result.features[5]
            << " branches=" << completed->result.incomeEvaluated << " pruning=" << completed->result.pruningEvaluated << '\n';
        check(completed->result.features[4]>completed->result.features[5]
            && completed->result.incomeEvaluated>0 && completed->result.pruningEvaluated>0,
            "a wide live roster can find profitable guarded production while ash is ready");
    }
    for(unsigned seed=1;seed<=4;++seed) {
        const auto chosen=Search(field,profit,seed);
        std::cout << "ready-ash investment seed=" << seed << " income=" << chosen.features[4]
            << " cost=" << chosen.features[5] << " branches=" << chosen.incomeEvaluated
            << " pruning=" << chosen.pruningEvaluated << '\n';
        check(chosen.features[4]>chosen.features[5] && chosen.incomeEvaluated>0 && chosen.pruningEvaluated>0,
            "free search finds a payable factory with ready ash and compares final member removals");
        float upfront=0;
        for(const auto& action:chosen.actions) upfront+=field.options[action.option].cost;
        check(upfront<=field.budget && chosen.actions.size()<=static_cast<size_t>(field.capacity),
            "protected factory cannot borrow future production to buy its initial team; later reloads use only already credited income");
    }
    auto renamed=field;
    for(size_t i=0;i<renamed.options.size();++i) renamed.options[i].type=9000-static_cast<int>(i)*31;
    const auto original=Search(field,profit,17), alternate=Search(renamed,profit,17);
    check(original.features==alternate.features && original.actions.size()==alternate.actions.size(),
        "protected economy exploration does not depend on named zombie ids or a fixed roster");
    for(size_t i=0;i<original.actions.size();++i)
        check(original.actions[i].option==alternate.actions[i].option && original.actions[i].delay==alternate.actions[i].delay,
            "renaming a type does not change freely searched counts, routes or timing");
    field.plants[0].multiTarget=true; field.plants[0].dps=100000;
    check(Search(field,profit,19).actions.empty(),
        "economic branches remain exploration only when all protection fails");
    check(field.options[1].unit.canisterFull && field.options[3].unit.clock.remaining==PolarClockRules::Preparation,
        "economic exploration and pruning do not consume live canisters or clock timers");
    }


    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.gridLeft=0; s.cellWidth=100; s.rows=5; s.columns=9;
    Unit car; car.body.x=740; car.body.health=850; car.body.purchaseCost=12;
    car.catapult.present=true; car.catapult.ammunition=2; car.catapult.release=.5f; car.catapult.duration=1;
    car.catapult.damage=75; s.current={car};
    Plant rear; rear.x=100; rear.column=0; rear.health=150; rear.reward=10;
    auto pumpkin=rear; pumpkin.layer=2; pumpkin.health=1000;
    auto wall=rear; wall.x=650; wall.column=6; wall.health=1000;
    s.plants={rear,pumpkin,wall}; ConstructionStats stats;
    const auto lobbed=Evaluate(s,{},&stats);
    check(lobbed[0]==10 && stats.catapultShots==2 && stats.catapultHits==2,
        "finite basketball inventory shoots the rear host through its pumpkin without eating the front wall");
    check(s.current[0].catapult.ammunition==2 && s.plants[0].health==150,
        "catapult forecast cannot consume real ammunition or mutate a live plant");
    auto close=s; close.plants[2].x=730;
    check(Evaluate(close,{},&stats)[0]==20 && stats.catapultShots==2,
        "a vehicle already overlapping a front plant may crush it while the same ranged cycle continues");
    auto defended=s; auto umbrella=wall; umbrella.row=1; umbrella.column=1; umbrella.x=200;
    umbrella.airborneDefenseRadius=1; defended.plants.push_back(umbrella);
    check(Evaluate(defended,{},&stats)[0]==0 && stats.catapultBlocks==2 && stats.catapultHits==0,
        "an adjacent living umbrella blocks lobbed attacks instead of letting the search invent rear kills");
    auto unarmed=s; unarmed.current[0].catapult.ammunition=0;
    check(Evaluate(unarmed,{},&stats)[1]==0 && stats.catapultShots==0,"empty ammunition cannot fire again");
    Counter lethal; lethal.blast.committed=true; lethal.blast.x=740;
    lethal.blast.reach.fill(-1); lethal.blast.reach[0]=10000; lethal.blast.damage=1800; lethal.blast.ready=.1f;
    auto blocked=unarmed; blocked.plants={wall}; blocked.plants[0].x=740;
    blocked.plants[0].initialHealth=blocked.plants[0].health;
    blocked.plants[0].crushDamage=100; blocked.plants[0].vehicleRetreat=50;
    blocked.counters={lethal}; // 第一逻辑步后死亡，仅检查同一步不能叠加旧通用啃食/碾压。
    check(Evaluate(blocked,{})[1]==1,"empty catapult resolves one vehicle impact without a second generic bite or crush");
    blocked.plants[0].catapultCrushable=false;
    check(Evaluate(blocked,{})[1]==0,"an uncrushable target does not become edible after the last basketball");
    auto before=s;
    before.counters={lethal};
    check(Evaluate(before,{},&stats)[1]==0 && stats.catapultShots==0,
        "source death before release cancels an unfinished shot");
    lethal.blast.ready=1.1f; before.counters={lethal};
    const auto afterRelease=Evaluate(before,{},&stats);
    check(afterRelease[1]==5 && stats.catapultShots==1 && stats.catapultHits==1,
        "an already released basketball still hits after ash destroys its source");
    auto paid=unarmed; paid.current[0].body.health=0; paid.basketballs={{0,0,1,75}};
    check(Evaluate(paid,{},&stats)[1]==5 && stats.catapultHits==1 && stats.catapultShots==0,
        "captured committed basketball survives independently without recreating a source or another shot");
    auto fired=s; fired.current[0].catapult.phase=CatapultAttack::Phase::SHOOTING;
    fired.current[0].catapult.ammunition=1; fired.current[0].catapult.launched=true;
    fired.current[0].catapult.remaining=.25f; fired.current[0].catapult.targetColumn=0; fired.basketballs={{0,0,1,75}};
    check(Evaluate(fired,{},&stats)[1]==5 && stats.catapultShots==0 && stats.catapultHits==1,
        "a live post-release frame consumes its current round once and cannot duplicate the captured basketball");
    auto reloading=s; reloading.current[0].catapult.phase=CatapultAttack::Phase::RELOADING;
    reloading.current[0].catapult.remaining=3; lethal.blast.ready=5; reloading.counters={lethal};
    const auto ordinary=Evaluate(reloading,{},&stats);
    check(stats.catapultShots>0 && ordinary[1]>0,"completed reload may release a shot before a later lethal event");
    reloading.current[0].body.slow=100;
    check(Evaluate(reloading,{},&stats)[1]==0 && stats.catapultShots==0,
        "slow internal action time delays reload instead of treating the vehicle as ready immediately");
    auto clock=s; clock.plants[0].health=1000; clock.current[0].catapult.ammunition=1;
    TemporalAnchor rewind; rewind.at=10; TemporalTarget target; target.unit=0; target.saved=car;
    target.saved.catapult.ammunition=CatapultRules::kInitialBasketballs; rewind.targets={target}; clock.temporalAnchors={rewind};
    Evaluate(clock,{},&stats);
    check(stats.catapultShots==1,"a living clock rewind cannot refill irreversible basketball inventory");
    clock.current[0].body.health=0; clock.current[0].catapult.ammunition=0; clock.temporalAnchors[0].at=1;
    Evaluate(clock,{},&stats);
    check(stats.clockRevivals==1 && stats.catapultShots==CatapultRules::kInitialBasketballs,
        "a newly created resurrected catapult uses the formal birth inventory rather than an empty dead instance");
    auto attack=s; attack.current.clear(); attack.budget=12; attack.capacity=1; attack.netEconomy=true;
    attack.plants[0].reward=30; Option choice; choice.type=51007; choice.cost=12; choice.unit=car; attack.options={choice};
    const Weights gain{1,0,0,0,1,-1,0,0};
    check(!Search(attack,gain,42).actions.empty(),"free search can discover a payable rear attack through the real ranged ability");
    attack.options[0].unit.catapult.present=false;
    check(Search(attack,gain,42).actions.empty(),"the same immobile body without the ranged ability has no invented attack value");
    std::cout<<"Catapult shots, host layers, umbrella, finite ammo, commitment and clock contracts passed\n";
    }

    {
    using namespace ColdStorageSearch;
    Snapshot closed; closed.rows=1; closed.houseX=-10000; closed.gridLeft=0; closed.cellWidth=100;
    closed.searchVersion=2; closed.netEconomy=true; closed.budget=1000; closed.capacity=16;
    Plant echo; echo.x=300; echo.health=100000; echo.dps=100000;
    echo.echo=echo.multiTarget=true; echo.hitDamage=100; echo.range=600; echo.edible=false; closed.plants={echo};
    Option weak; weak.type=710001; weak.cost=4; weak.unit.body.x=850;
    weak.unit.body.health=270; weak.unit.body.speed=20; weak.unit.body.purchaseCost=4;
    auto strong=weak; strong.type=710002; strong.cost=8; strong.unit.body.health=1370; strong.unit.body.purchaseCost=8;
    closed.options={weak,strong};
    const auto wait=Search(closed,InitialWeights,7);
    check(wait.actions.empty() && wait.fallbackMode==0,"ordinary evaluation retains a valid losing-field wait");
    auto trial=closed; trial.fallbackProbeBudget=20;
    const auto probed=Search(trial,InitialWeights,7);
    int bill=0; for(const auto& a:probed.actions) bill+=trial.options[a.option].cost;
    check(probed.fallbackMode==1 && probed.actions.size()>=3 && bill>0 && bill<=20
        && probed.score<probed.baselineFeatures[0],
        "authorized finite trial pays a real bounded cohort without relabeling a negative forecast as profitable");
    auto lastChance=closed; lastChance.fallbackAllIn=true;
    const auto full=Search(lastChance,InitialWeights,7);
    bill=0; for(const auto& a:full.actions) bill+=lastChance.options[a.option].cost;
    check(full.fallbackMode==2 && bill>=96 && bill<=1000 && full.actions.size()<=16,
        "last chance uses most deployable combat capital rather than forcing one cheapest zombie");
    check(std::all_of(full.actions.begin(),full.actions.end(),[](const Action& a){return a.option==1;}),
        "equally ineffective forecasts still compare real combat vitality for the final attack");
    lastChance.budget=7;
    const auto tail=Search(lastChance,InitialWeights,7);
    check(tail.fallbackMode==2 && tail.actions.size()==1 && lastChance.options[tail.actions[0].option].cost<=7,
        "last chance can spend the legally affordable tail instead of surrendering with purchasable troops");
    auto income=closed; income.plants.clear(); income.budget=96; income.fallbackAllIn=true;
    auto producer=weak; producer.type=710003; producer.cost=24; producer.unit.body.health=500;
    producer.unit.body.speed=0; producer.unit.body.economic=true; producer.unit.body.purchaseCost=24;
    income.options.push_back(producer);
    const auto profitable=Search(income,InitialWeights,7);
    check(profitable.fallbackMode==0 && profitable.features[4]>profitable.features[5],
        "real profitable economy remains preferred even when last-chance fallback is armed");
    trial.timeLimitedSearch=true; trial.searchDeadline=std::chrono::steady_clock::now();
    check(Search(trial,InitialWeights,7).actions.empty(),"an expired search cannot invent an uncomputed fallback purchase");
    check(closed.options[0].unit.body.health==270 && closed.plants[0].health==100000,
        "fallback comparisons preserve caller entities and actual wallet");
    std::cout<<"Finite trials, last-chance combat cohorts, tail spending and positive-plan priority passed\n";
    }

    {
    using namespace ColdStorageSearch;
    Snapshot s; s.houseX=-10000; s.gridLeft=100; s.cellWidth=80; s.columns=9;
    Unit gun; gun.id=1; gun.pressure=true; gun.body.x=820; gun.body.row=2;
    gun.body.health=1000; gun.body.speed=0; gun.biteDps=0; s.current={gun};
    Plant wall; wall.id=1; wall.row=2; wall.column=5; wall.x=540;
    wall.health=wall.maximumHealth=wall.initialHealth=4000; wall.reward=40; s.plants={wall};
    const auto firing=Evaluate(s,{});
    s.current[0].pressure=false; const auto silent=Evaluate(s,{});
    check(firing[1]>silent[1],"pressure gun damages existing plants without deployment trigger");
    s.current[0]=gun;
    Plant cotton; cotton.id=2; cotton.cotton=true; cotton.row=1; cotton.column=5; cotton.x=540;
    cotton.health=cotton.maximumHealth=300; s.plants.push_back(cotton);
    const auto healed=Evaluate(s,{});
    check(healed[1]<firing[1],"cotton counters sustained damage and withdraws healed damage credit");
    s.plants[1].row=0;
    check(Evaluate(s,{})[1]==firing[1],"cotton cannot heal a target outside its eight adjacent cells");
    s.plants={wall};s.current[0].body.stopped=60;
    check(Evaluate(s,{})[1]==0,"hard control pauses pressure burst and recharge");
    s.current[0]=gun;s.current[0].body.health=300;
    check(Evaluate(s,{})[1]==0,"head loss suppresses future pressure fire");
    s.current.clear(); s.pressureRays={{2,700,0,25}};
    check(Evaluate(s,{})[1]>0,"pressure projectiles survive the firing unit");
    s.plants[0].hostileMirrors=1;
    check(Evaluate(s,{})[1]==0,"one ice mirror intercepts one independent pressure projectile");
    check(s.plants[0].health==4000 && s.plants[0].hostileMirrors==1,
        "forecast never mutates actual plant health or mirror inventory");
    s.pressureRays.clear(); s.plants={wall}; s.current={gun};
    s.playerSun=150; s.playerIce=10;
    Construction cottonCard; cottonCard.source=0; cottonCard.sunCost=150; cottonCard.iceCost=10;
    cottonCard.recharge=20; cottonCard.plant=cotton; s.construction={cottonCard};
    ConstructionStats construction;
    const auto built=Evaluate(s,{},&construction);
    check(construction.planted==1 && built[1]<firing[1],"future cotton is valued for adjacent sustained-fire recovery with real shared cost");
    s.playerIce=0; Evaluate(s,{},&construction);
    check(construction.planted==0,"forecast cannot plant cotton without the required ice");
    std::cout<<"Pressure burst and cotton healing forecast passed\n";
    }

    {
    using namespace ColdStorageSearch;
    Snapshot field; field.rows=1; field.columns=9; field.cellWidth=80;
    field.houseX=0; field.rightEdge=1100; field.searchVersion=2; field.netEconomy=true;
    field.budget=100; field.capacity=8;
    Plant wall; wall.row=0; wall.column=5; wall.x=600; wall.health=wall.maximumHealth=10000;
    field.plants={wall};
    const auto option=[](int type,int cost,float health,float speed) {
        Option o; o.type=type; o.row=0; o.cost=cost; o.unit.body.row=0;
        o.unit.body.x=1100; o.unit.body.health=health; o.unit.body.speed=speed;
        o.unit.body.purchaseCost=cost; return o;
    };
    auto guard=option(820001,8,2000,12);
    auto runner=option(820002,16,1500,60);
    auto gun=option(820003,20,1000,20); gun.unit.pressure=true;
    auto lobber=option(820004,12,850,20); lobber.unit.catapult.present=true;
    field.options={guard,runner,gun,lobber};
    const auto recipes=BuildExperiencedFormations(field,field.capacity,field.budget);
    bool screened=false,fastScreen=false,legacyLobber=false;
    for(const auto& plan:recipes) {
        int bill=0,guards=0,runners=0,guns=0,lobs=0; float gunDelay=0;
        for(const auto& action:plan) {
            bill+=field.options[action.option].cost;
            guards+=action.option==0; runners+=action.option==1; guns+=action.option==2; lobs+=action.option==3;
            if(action.option==2) gunDelay=action.delay;
        }
        check(bill<=field.budget && plan.size()<=static_cast<size_t>(field.capacity),
            "pressure formations keep complete shared cash and slot admission");
        if(guards && guns && !runners && !lobs) {
            screened=true;
            check(gunDelay>0,"a slower screen departs before its faster shooter rather than being overtaken");
        }
        if(runners && guns && !guards && !lobs) {
            fastScreen=true;
            check(gunDelay==0,"a naturally faster escort can depart with the shooter without a fixed delay");
        }
        legacyLobber|=guards>0 && lobs>0;
    }
    check(screened && fastScreen && legacyLobber,
        "direct pressure shooters receive guard and fast-screen candidates without displacing lobbers");
    auto poor=field; poor.budget=47;
    for(const auto& plan:BuildExperiencedFormations(poor,2,poor.budget))
        check(std::none_of(plan.begin(),plan.end(),[](const Action& a){return a.option==2;}),
            "insufficient budget or capacity cannot silently truncate the pressure partnership");
    auto solo=field; solo.options={gun};
    check(BuildExperiencedFormations(solo,solo.capacity,solo.budget).empty(),
        "a pressure gun is not classified as its own guard or fast frontliner");
    auto escort=guard.unit; escort.body.x=650; escort.temporalStopHealth=300;
    solo.current={escort};
    const auto reinforcement=BuildExperiencedFormations(solo,solo.capacity,solo.budget);
    check(!reinforcement.empty() && std::all_of(reinforcement.begin(),reinforcement.end(),[](const auto& plan){
        return std::all_of(plan.begin(),plan.end(),[](const Action& a){return a.option==0 && a.delay==0;});
    }),"a living front line can receive shooter-only reinforcements");
    solo.current[0].body.spawnAt=3;
    check(BuildExperiencedFormations(solo,solo.capacity,solo.budget).empty(),"paid but unarrived escorts are not treated as established cover");
    solo.current[0].body.spawnAt=0;solo.current[0].body.health=1200;solo.current[0].temporalStopHealth=1300;
    check(BuildExperiencedFormations(solo,solo.capacity,solo.budget).empty(),"headless remnants cannot qualify as an established screen");
    auto renamed=field;
    for(size_t i=0;i<renamed.options.size();++i) renamed.options[i].type=910000+static_cast<int>(i)*19;
    const auto names=BuildExperiencedFormations(renamed,renamed.capacity,renamed.budget);
    check(names.size()==recipes.size(),"pressure recipes classify capabilities rather than fixed unit IDs");
    for(size_t i=0;i<recipes.size();++i) {
        check(names[i].size()==recipes[i].size(),"renamed pressure recipe retains all partners");
        for(size_t j=0;j<recipes[i].size();++j)
            check(names[i][j].option==recipes[i][j].option && names[i][j].delay==recipes[i][j].delay,
                "renamed pressure recipe keeps placement timing");
    }
    auto losing=field; losing.plants[0].dps=100000; losing.plants[0].multiTarget=true;
    const auto rejected=Search(losing,Weights{0,0,0,0,1,-1,0,0},7);
    check(rejected.actions.empty() && rejected.experiencedEvaluated>0,
        "unprofitable ranged recipes still lose to waiting rather than forcing a preset army");
    auto mortar=option(820005,45,1800,20);mortar.unit.floodMortar=true;
    auto healer=option(820006,25,1200,20);healer.unit.healer.present=true;
    auto artillery=field;artillery.options.push_back(mortar);artillery.options.push_back(healer);artillery.budget=250;
    bool mortarScreen=false,supportedMortar=false,pressureKept=false,lobberKept=false;
    for(const auto& plan:BuildExperiencedFormations(artillery,8,250)) {
        int bill=0;bool hasGuard=false,hasMortar=false,hasHealer=false;
        for(const auto& action:plan) {
            bill+=artillery.options[action.option].cost;hasGuard|=action.option==0 || action.option==1;
            hasMortar|=action.option==4;hasHealer|=action.option==5;
            pressureKept|=action.option==2;lobberKept|=action.option==3;
            if(action.option==4) check(action.delay<15,"mortar cover uses firing range instead of waiting for melee contact");
        }
        check(bill<=250 && plan.size()<=8,"artillery recipe obeys shared budget and capacity");
        mortarScreen|=hasGuard && hasMortar && !hasHealer;
        supportedMortar|=hasGuard && hasMortar && hasHealer;
    }
    check(mortarScreen && supportedMortar && pressureKept && lobberKept,"mortar has independent light and supported recipes without displacing other ranged roles");
    bool mixed=false,staggered=false,rush=false;
    for(const auto& plan:BuildExperiencedFormations(artillery,8,250)) {
        std::vector<float> shots;int direct=0,fast=0;
        for(const auto& action:plan) {
            if(action.option==4)shots.push_back(action.delay);
            direct+=action.option==2;fast+=action.option==1;
        }
        mixed|=!shots.empty() && direct>0;
        staggered|=shots.size()==2 && std::abs(shots[0]-shots[1])>1;
        rush|=!shots.empty() && fast>0;
    }
    check(mixed && staggered && rush,"mortar seeds include mixed guns, staggered fire and fast follow-through");
    auto lanes=artillery;lanes.rows=3;
    auto distantShield=guard;distantShield.row=2;distantShield.unit.body.row=2;
    auto distantMortar=mortar;distantMortar.row=2;distantMortar.unit.body.row=2;
    lanes.options.push_back(distantShield);lanes.options.push_back(distantMortar);
    bool crossfire=false;
    for(const auto& plan:BuildExperiencedFormations(lanes,8,250)) {
        bool low=false,high=false;
        for(const auto& action:plan) {low|=action.option==4;high|=action.option==7;}
        crossfire|=low && high;
    }
    check(crossfire,"two-lane artillery uses only legal row-specific options");
    auto alone=artillery;alone.options={mortar};alone.current.clear();
    check(BuildExperiencedFormations(alone,8,250).empty(),"mortar cannot classify itself as its own shield");
    alone.current={escort};alone.current[0].body.row=0;
    check(!BuildExperiencedFormations(alone,8,250).empty(),"existing live shields can receive mortar reinforcements");
    alone.current[0].body.spawnAt=3;
    check(BuildExperiencedFormations(alone,8,250).empty(),"pending shields cannot unlock mortar-only reinforcement");
    auto tight=artillery;tight.budget=52;
    for(const auto& plan:BuildExperiencedFormations(tight,8,52))
        check(std::none_of(plan.begin(),plan.end(),[](const Action& a){return a.option==4;}),"unaffordable guard plus mortar cannot be truncated into a naked mortar");
    auto renamedMortar=artillery;for(auto& o:renamedMortar.options)o.type+=170000;
    check(BuildExperiencedFormations(renamedMortar,8,250).size()==BuildExperiencedFormations(artillery,8,250).size(),"mortar role is capability-based rather than tied to enum IDs");
    std::cout<<"Mortar screen, support, reinforcement and independent role recipes passed\n";
    std::cout<<"Pressure shooter screen, rush, reinforcement and affordability recipes passed\n";
    }
}
