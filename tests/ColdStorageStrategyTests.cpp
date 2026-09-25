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
		check(focused.formationTested == 31 && focused.evaluated == 101,
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
}
