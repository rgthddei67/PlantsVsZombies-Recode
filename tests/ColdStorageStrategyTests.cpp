#include "Game/AI/ColdStorageSearch.h"
#include <limits>
#include "Game/AI/ColdStorageStrategy.h"
#include <cstdlib>
#include <iostream>

/** Deterministic counterfactuals: triggering splash, existing targets, spacing and paid arrivals. */
int main()
{
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
	auto check = [](bool condition, const char* name) {
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; std::exit(1); }
	};
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
	check(ColdStorageSearch::Search(contextual,contextScore,77).actions.empty(),"waiting remains legal before the deadline");
	contextual.allowWait = false;
	const auto resume = ColdStorageSearch::Search(contextual,contextScore,77);
	check(!resume.actions.empty() && resume.actions.front().delay == 0,"empty board must act when bounded observation expires");
	contextual.budget = 0;
	check(ColdStorageSearch::Search(contextual,contextScore,77).actions.empty(),"observation deadline cannot authorize unpaid troops");
}
