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
	check(ColdStorageSearch::Search(portfolio,breakthrough,731).actions.empty(),"small formations cannot cross this fire lane");
	portfolio.searchVersion = 2;
	const auto large = ColdStorageSearch::Search(portfolio,breakthrough,731);
	check(large.largestPlan == 64 && large.actions.size() > 8 && large.features[2] > 0,
		"portfolio search finds a paid large-team breakthrough without a state layer or spending reward");
	check(large.features[5] <= portfolio.budget && large.evaluated <= 198,"larger search obeys money and bounded evaluation limits");
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
	ColdStorageSearch::Snapshot mowerTest;
	mowerTest.searchVersion = 2; mowerTest.houseX = 100;
	portfolioUnit.unit.body.x = 300; portfolioUnit.unit.body.speed = 20;
	mowerTest.options = {portfolioUnit};
	mowerTest.mowers.push_back({0,200,60,230});
	check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{0,0}})[2] == 0,
		"a ready mower clears the first cohort instead of awarding two false breakthroughs");
	check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{0,20}})[2] == 1,
		"a spent mower cannot clear reinforcements which have not spawned during its sweep");
	mowerTest.options[0].unit.mowerImmune = true;
	check(ColdStorageSearch::Evaluate(mowerTest,{{0,0}})[2] == 1,"mower immunity comes from the unit capability");
	mowerTest.options[0].unit.mowerImmune = false;
	mowerTest.options[0].unit.consumesOtherMowers = true;
	portfolioUnit.row = portfolioUnit.unit.body.row = 1;
	mowerTest.options.push_back(portfolioUnit); mowerTest.mowers.push_back({1,200,60,230});
	check(ColdStorageSearch::Evaluate(mowerTest,{{0,0},{1,20}})[2] == 1,
		"a mower-consuming unit removes other lanes' mowers without granting itself immunity");
	std::cout << "Mower clearing, delayed reinforcements and mower abilities passed\n";
	ColdStorageSearch::Snapshot wonLane;
	wonLane.searchVersion = 2; wonLane.houseX = 100;
	portfolioUnit.row = portfolioUnit.unit.body.row = 0;
	wonLane.options = {portfolioUnit};
	const auto oneVictory = ColdStorageSearch::Evaluate(wonLane,{{0,0}});
	const auto repeatedVictory = ColdStorageSearch::Evaluate(wonLane,{{0,0},{0,0},{0,10}});
	check(oneVictory[2] == 1 && repeatedVictory[2] == 1,
		"additional intruders cannot multiply the value of one already won game");
	check(repeatedVictory[5] == 3*oneVictory[5],"redundant invasion still pays every purchase");
	wonLane.searchVersion = 1;
	check(ColdStorageSearch::Evaluate(wonLane,{{0,0},{0,0}})[2] == 2,
		"legacy policy retains its original breach feature until explicitly migrated");
	wonLane.searchVersion = 2;
	wonLane.options[0].unit.body.x = 101;
	ColdStorageSearch::Unit lateIncome;
	lateIncome.body.health = 500; lateIncome.body.x = 900; lateIncome.body.economic = true;
	wonLane.current = {lateIncome};
	const auto endedGame = ColdStorageSearch::Evaluate(wonLane,{{0,0}});
	check(endedGame[2] == 1 && endedGame[4] == 0,"the game cannot keep producing ice after an actual projected victory");
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
	check(pressured.actions.size() == 6 && pressured.features[0] == 0 && pressured.opponentScore > 60,
		"optional terminal value recognizes paid counter depletion without forcing a formation");
	check(std::abs(pressured.opponentAssets-18) < .001f,
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
}
