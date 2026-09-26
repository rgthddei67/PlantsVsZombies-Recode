#include "TestDriver.h"
#include "DeltaTime.h"
#include "Game/GameScene.h"
#include "Game/SceneManager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

namespace {
using Json = nlohmann::json;
/** 固定的植物方陪练，只从可见状态选择动作，不加钱、不重置冷却、不替指挥官出兵。 */
std::vector<Json> PlayerActions(const Json& state, const std::string& opponent) {
	std::vector<Json> actions;
	const auto& ice = state.at("coldStorage");
	const bool planner = opponent == "planner";
	const bool fortifier = opponent == "fortifier" || planner;
	const bool lotusPlayer = opponent == "lotus" || fortifier;
	const bool builder = opponent == "builder" || lotusPlayer;
	const bool hunter = opponent == "hunter" || builder;
	const bool adaptive = opponent == "adaptive" || hunter;
	const bool counterplay = adaptive || opponent == "counter" || opponent == "ash";
	const bool eliteDefense = std::any_of(state.at("cards").begin(), state.at("cards").end(),
		[](const auto& card) { return card.at("gameplayType") == "PLANT_ELITE_SCAREDYSHROOM"; });
	int sun = state.at("sun"), stock = ice.at("playerIce");
	std::map<std::pair<int,int>, Json> plants;
	std::set<std::pair<int,int>> shells;
	for (const auto& p : state.at("plants")) if (!p.value("squished", false) && p.value("health", 0) > 0) {
		const std::pair<int,int> cell{p.at("row"), p.at("col")};
		if (p.at("type") == "PLANT_PUMPKINSHELL") shells.insert(cell);
		else plants[cell] = p;
	}
	std::vector<Json> zombies;
	for (const auto& z : state.at("zombies")) if (z.value("bodyHealth", 0) > 0) zombies.push_back(z);
	std::stable_sort(zombies.begin(), zombies.end(), [](const auto& a, const auto& b) { return a.at("xInt") < b.at("xInt"); });
	int defenseReserve = 0, defenseIceReserve = 0;
	// 威胁已经接近时，为十秒内转好的灰烬预留真实阳光；空场及长期冷却时释放这笔预算发展经济。
	if (planner && !zombies.empty() && zombies.front().at("xInt").get<int>() < 950)
		for (const auto& card : state.at("cards")) {
			const auto kind = card.at("gameplayType").get<std::string>();
			if ((kind == "PLANT_CHERRYBOMB" || kind == "PLANT_JALAPENO" || kind == "PLANT_SQUASH")
				&& card.value("cooldownRemainingMs",0) <= 10000) {
				defenseReserve = std::max(defenseReserve,card.at("sunCost").get<int>());
				defenseIceReserve = std::max(defenseIceReserve,ice.at("plantCosts").at(kind).get<int>());
			}
		}
	bool releasedLotus = false;
	// 使用已经充满的实际植物，通过玩家输入门禁释放；不直接改能量或调用伤害结算。
	if (lotusPlayer && !zombies.empty()) for (const auto& p : state.at("plants"))
		if (p.value("dawnCanActivate",false)) {
			actions.push_back({{"op","player_activate_dawn_lotus"},{"row",p.at("row")},{"col",p.at("col")}});
			releasedLotus = true;
		}
	for (const auto& coin : state.at("suns")) if (!coin.value("collected", false)) actions.push_back({{"op","collect_sun"},{"id",coin.at("id")}});
	for (const auto& entry : plants) {
		const auto& cell = entry.first; const auto& p = entry.second;
		const bool farm = p.at("type") == "PLANT_MARIGOLD";
		const bool deny = opponent == "deny" && p.value("health", 1000) < 130
			&& p.at("type") != "PLANT_CHERRYBOMB" && p.at("type") != "PLANT_JALAPENO"
			&& std::any_of(zombies.begin(), zombies.end(), [&](const auto& z) {
				return z.at("row") == cell.first && std::abs(z.at("xInt").template get<int>() - state.at("cells").at(cell.first).at(cell.second).at("centerXInt").template get<int>()) < 120;
			});
		if (farm || deny) actions.push_back({{"op","player_shovel"},{"row",cell.first},{"col",cell.second}});
	}
	// 建设型陪练只在种植储备不足时补冰，避免尚有可用冰时反复花掉筹建输出的阳光。
	// 缺少反制所需冰时仍须先订货，否则预留阳光也无法救险。
	const int purchaseReserve = stock >= defenseIceReserve ? defenseReserve : 0;
	if (ice.at("orderIce") == 0 && stock < (planner ? 40 : 100) && sun >= 225+purchaseReserve) {
		actions.push_back({{"op","buy_ice"},{"large",true}}); sun -= 225;
	} else if (ice.at("orderIce") == 0 && stock < 30 && sun >= 100+purchaseReserve) {
		actions.push_back({{"op","buy_ice"},{"large",false}}); sun -= 100;
	}
	// 打击即时结算，下一次观察后再选灰烬，避免按释放前快照重复炸已经死亡的目标。
	if (releasedLotus) return actions;
	bool planted = false;
	auto attempt = [&](const std::string& kind, const std::vector<std::pair<int,int>>& cells) {
		if (planted) return;
		for (const auto& card : state.at("cards")) {
			if (card.at("gameplayType") != kind || !card.at("ready").get<bool>()
				|| card.at("sunCost").get<int>() > sun || ice.at("plantCosts").at(kind).get<int>() > stock) continue;
			for (const auto& [r,c] : cells) {
				const Json cell = Json::array({r,c});
				if (std::find(card.at("legalCells").begin(), card.at("legalCells").end(), cell) == card.at("legalCells").end()) continue;
				actions.push_back({{"op","player_plant"},{"slot",card.at("slot")},{"row",r},{"col",c}});
				planted = true; return;
			}
		}
	};
	// 反制陪练优先堵住将要接触防线的路线，让快僵尸实际经历坚果前聚团。
	if (counterplay && !adaptive) {
		std::vector<std::pair<int,int>> walls;
		for (const auto& z : zombies) if (z.at("xInt").get<int>() < 1050 && !plants.count({z.at("row"),6}))
			walls.emplace_back(z.at("row"),6);
		attempt("PLANT_WALLNUT", walls);
	}
	// 全兵种训练配备实际对空卡，避免把“陪练根本不能打气球”误学成通用最优策略。
	if (std::any_of(zombies.begin(), zombies.end(), [](const auto& z) { return z.at("type") == "ZOMBIE_BALLOON"; })) {
		std::vector<std::pair<int,int>> antiAir;
		for (int r = 0; r < 5; ++r) for (int c = 7; c >= 2; --c) antiAir.emplace_back(r,c);
		attempt("PLANT_BLOVER", antiAir);
		attempt("PLANT_CACTUS", antiAir);
	}
	// 陪练之间改变炸弹使用门槛与补阵次序，防止只学会针对一个固定脚本。
	if (!zombies.empty() && zombies.front().at("xInt").get<int>() < 550) {
		std::vector<std::pair<int,int>> cells;
		for (int c = 7; c >= 0; --c) cells.emplace_back(zombies.front().at("row"), c);
		attempt("PLANT_JALAPENO", cells);
	}
	// 自适应陪练保留已提交爆炸的覆盖，避免把第二张灰烬交给即将死亡的同一批目标。
	auto remainingHealth = [&](const Json& z) {
		int health = z.value("countableExecutionHealth", z.value("bodyHealth",0));
		if (adaptive) for (const auto& [cell,p] : plants) {
			const auto type = p.at("type").get<std::string>();
			const int x = state.at("cells").at(cell.first).at(cell.second).at("centerXInt");
			if ((type == "PLANT_JALAPENO" && z.at("row") == cell.first)
				|| (type == "PLANT_CHERRYBOMB" && std::abs(z.at("row").get<int>()-cell.first)<=1
					&& std::abs(z.at("xInt").get<int>()-x)<=130)) health -= 1800;
		}
		return std::max(0,health);
	};
	// 猎工陪练把可见且尚未被已提交灰烬覆盖的经济单位视为高价值目标。
	auto blastValue = [&](const Json& z) {
		const int health = remainingHealth(z);
		return std::min(1800,health) + (hunter && health > 0 && z.at("type") == "ZOMBIE_ICE_WORKER" ? 1500 : 0);
	};
	std::vector<std::pair<float,std::pair<int,int>>> blastCells;
	for (int r = 0; r < state.at("rows").get<int>(); ++r) for (int c = 0; c < state.at("columns").get<int>(); ++c) {
		const auto& cell = state.at("cells").at(r).at(c);
		// 正式 Board 导出的格中心，避免从截图分辨率推算战斗坐标。
		const float x = cell.at("centerXInt").get<float>();
		float damage = 0;
		for (const auto& z : zombies) if (std::abs(z.at("row").get<int>() - r) <= 1 && std::abs(z.at("xInt").get<float>() - x) <= 130)
			damage += blastValue(z);
		if (damage >= (opponent == "bomb" || counterplay ? 2500 : 4200)) blastCells.push_back({damage,{r,c}});
	}
	std::stable_sort(blastCells.begin(), blastCells.end(), [](auto a, auto b) { return a.first > b.first; });
	std::vector<std::pair<int,int>> cells;
	for (const auto& c : blastCells) cells.push_back(c.second);
	attempt("PLANT_CHERRYBOMB", cells);
	if (counterplay) {
		std::vector<std::pair<float,int>> lanes;
		for (int r = 0; r < state.at("rows").get<int>(); ++r) {
			float damage = 0;
			for (const auto& z : zombies) if (z.at("row") == r && z.at("xInt").get<int>() < 1100)
				damage += blastValue(z);
			if (damage >= 2500) lanes.push_back({damage,r});
		}
		std::stable_sort(lanes.begin(), lanes.end(), [](auto a, auto b) { return a.first > b.first; });
		cells.clear();
		for (const auto& lane : lanes) for (int c = 8; c >= 0; --c) cells.emplace_back(lane.second,c);
		attempt("PLANT_JALAPENO", cells);
		// 倭瓜只在实际触发距离附近落种，不能隔着半场凭空砸中目标。
		cells.clear();
		for (const auto& z : zombies) if (z.at("xInt").get<int>() < 1050
			&& remainingHealth(z) > 0 && (remainingHealth(z) >= 900 || z.at("xInt").get<int>() < 550
				|| (hunter && z.at("type") == "ZOMBIE_ICE_WORKER"))) {
			const int r = z.at("row");
			for (int c = 8; c >= 0; --c)
				if (std::abs(state.at("cells").at(r).at(c).at("centerXInt").get<int>() - z.at("xInt").get<int>()) <= 100)
					cells.emplace_back(r,c);
		}
		attempt("PLANT_SQUASH", cells);
	}
	// 先救险再补墙；敌人已经越过的前排格不能再阻挡它，按当前位置向后补一层。
	if (adaptive) {
		cells.clear();
		for (const auto& z : zombies) for (int c = 6; c >= 2; --c) {
			const int r = z.at("row"), x = state.at("cells").at(r).at(c).at("centerXInt");
			if (x < z.at("xInt").get<int>()-20 && !plants.count({r,c})) { cells.emplace_back(r,c); break; }
		}
		attempt("PLANT_WALLNUT",cells);
	}
	attempt("PLANT_MARIGOLD", {{0,4},{4,4}});
	// 上面的救险正常使用全部现金；仅后续可延后的建设受预留预算约束，不改玩家真实余额。
	sun = std::max(0,sun-defenseReserve);
	if (lotusPlayer && std::none_of(plants.begin(),plants.end(),[](const auto& entry) {
		return entry.second.at("type") == "PLANT_DAWNLOTUS";
	})) attempt("PLANT_DAWNLOTUS",{{2,2},{1,2},{3,2}});
	std::vector<int> rows{2,0,4,1,3};
	if (!zombies.empty()) std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
		auto nearest = [&](int r) { for (const auto& z : zombies) if (z.at("row") == r) return z.at("xInt").get<int>(); return 2000; };
		return nearest(a) < nearest(b);
	});
	// 后排保护是基础建设；使用正常冷却/资金，并按眼前威胁优先保护已有输出。
	cells.clear();
	for (int r : rows) for (int c = 0; c <= 2; ++c) {
		const auto it = plants.find({r,c});
		if (it == plants.end() || shells.count({r,c})) continue;
		const auto kind = it->second.at("type").get<std::string>();
		if (kind == "PLANT_MELONPULT" || kind == "PLANT_WINTERMELON" || kind == "PLANT_CACTUS"
			|| kind == "PLANT_ELITE_SCAREDYSHROOM" || kind == "PLANT_REPEATER") cells.emplace_back(r,c);
	}
	const auto protectionCells = cells;
	if (!adaptive) attempt("PLANT_PUMPKINSHELL", cells);
	cells.clear(); for (int r : rows) if (!plants.count({r,6})) cells.emplace_back(r,6);
	if (!zombies.empty() && zombies.front().at("xInt").get<int>() < 850) attempt("PLANT_WALLNUT", cells);
	int producers = 0;
	for (const auto& [cell,p] : plants) if (p.at("type") == "PLANT_SUNFLOWER") ++producers;
	// 防守型陪练有基本经济后先补各路输出与后排保护，避免必须铺完经济才开始防守。
	// 只改变这个对手的合法动作顺序，旧陪练继续保留，不能把训练变成针对单一固定阵型。
	if (fortifier && producers >= 4) {
		cells.clear(); for (int r : rows) cells.emplace_back(r,0);
		attempt(eliteDefense ? "PLANT_ELITE_SCAREDYSHROOM" : "PLANT_MELONPULT",cells);
		// 高优先级的合法输出已转好但暂时缺阳光时，允许攒钱；不能每秒花掉零钱后永远买不起。
		// 前面的救险与金盏花周转仍可执行；这是陪练的建设计划，不干预僵尸购买或免费补资源。
		// 先有八株基本经济再冻结其他建设，避免过早攒大件反而长期缺少收入。
		if (planner && producers >= 8 && !planted) for (const auto& card : state.at("cards")) {
			if (card.at("gameplayType") != (eliteDefense ? "PLANT_ELITE_SCAREDYSHROOM" : "PLANT_MELONPULT")
				|| !card.at("ready").get<bool>() || card.at("sunCost").get<int>() <= sun) continue;
			for (const auto& [r,c] : cells) {
				const Json cell = Json::array({r,c});
				if (std::find(card.at("legalCells").begin(),card.at("legalCells").end(),cell) != card.at("legalCells").end())
					return actions;
			}
		}
		attempt("PLANT_PUMPKINSHELL",protectionCells);
		attempt("PLANT_WINTERMELON",{{1,0},{3,0},{0,0},{4,0},{2,0}});
	}
	// 扩建陪练保留两格金盏花周转，其余中排逐步发展；只维持七株会低估真人的后期反制资源。
	if (producers < (builder ? 18 : 7)) {
		cells.clear(); for (int r : rows) {
			cells.emplace_back(r,3); cells.emplace_back(r,5);
			if (builder) {
				if (r != 0 && r != 4) cells.emplace_back(r,4);
				cells.emplace_back(r,2);
			}
		}
		attempt("PLANT_SUNFLOWER", cells);
	}
	// 陌生阵型沿用正式累计配额和冷却；不能在精英菇死亡后免费重建四株。
	if (eliteDefense) {
		cells.clear(); for (int r : rows) { cells.emplace_back(r,0); cells.emplace_back(r,1); }
		attempt("PLANT_ELITE_SCAREDYSHROOM", cells);
		cells.clear(); for (int r : rows) { cells.emplace_back(r,2); cells.emplace_back(r,1); }
		attempt("PLANT_REPEATER", cells);
	}
	cells.clear(); for (int r : rows) cells.emplace_back(r,0);
	attempt("PLANT_MELONPULT", cells);
	if (opponent == "growth") { cells.clear(); for (int r : rows) cells.emplace_back(r,1); attempt("PLANT_MELONPULT", cells); }
	attempt("PLANT_WINTERMELON", {{1,0},{3,0},{0,0},{4,0},{2,0}});
	cells.clear(); for (int r : rows) { cells.emplace_back(r,1); cells.emplace_back(r,2); }
	attempt("PLANT_MELONPULT", cells);
	if (adaptive) attempt("PLANT_PUMPKINSHELL", protectionCells);
	cells.clear(); for (int r : rows) cells.emplace_back(r,6);
	attempt("PLANT_WALLNUT", cells);
	return actions;
}
}

bool TestDriver::ExecuteCommanderEpisode(const nlohmann::json& command) {
	const int ticks = static_cast<int>(std::lround(command.value("seconds", 120.0f) * 60));
	const auto opponent = command.value("opponent", std::string("bomb"));
	if (ticks < 60 || ticks > 72000 || (opponent != "bomb" && opponent != "growth" && opponent != "deny" && opponent != "counter" && opponent != "ash" && opponent != "adaptive" && opponent != "hunter" && opponent != "builder" && opponent != "lotus" && opponent != "fortifier" && opponent != "planner")) {
		Fail("commander_episode: invalid duration or opponent"); return false;
	}
	auto* scene = dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
	auto* board = scene ? scene->GetBoard() : nullptr;
	if (!board || !board->IsColdStorage()) { Fail("commander_episode requires cold storage"); return false; }
	const bool terminal = board->mBoardState == BoardState::LOSE_GAME || board->mTrophySpawned;
	// 正式判负会暂停时钟；先接收胜负，再检查比赛中是否被改速或手动暂停。
	if (!terminal && (DeltaTime::GetTimeScale() != 1 || DeltaTime::IsPaused())) {
		Fail("commander_episode requires 1x fixed steps"); return false;
	}
	if (mEpisodeTicks < 0) {
		mEpisodeTicks = 0; mEpisodeInitial = BuildInteractiveState(); mEpisodeTrace = Json::array();
		mEpisodePlantings = Json::object(); mEpisodeDecisions = Json::array();
		if (mEpisodeInitial.at("cards").empty()) { Fail("commander_episode: player has no cards"); return false; }
		Log("commander episode started: " + opponent);
	}
	// 普通观测每秒一次，正式胜负在每个逻辑步立即收尾。
	Json full;
	if (terminal || mEpisodeTicks % 60 == 0 || mEpisodeTicks >= ticks) full = BuildInteractiveState();
	else { ++mEpisodeTicks; return false; }
	if (!full.contains("coldStorage")) { Fail("commander_episode requires cold storage"); return false; }
	const auto& ice = full.at("coldStorage");
	// 每次决策留一份紧凑证据，包括观望；避免十秒采样漏掉中间的高额采购。
	if (ice.value("searchSerial",0) > 0 && (mEpisodeDecisions.empty()
		|| mEpisodeDecisions.back().at("serial") != ice.at("searchSerial"))) {
		mEpisodeDecisions.push_back({{"seconds",mEpisodeTicks/60.0},{"serial",ice.at("searchSerial")},
			{"features",ice.at("searchFeatures")},{"baseline",ice.at("searchBaselineFeatures")},
			{"elapsed",ice.at("searchElapsed")},{"wave",ice.at("decisions")},
			{"rowStrikes",ice.at("searchRowStrikeCount")},
			{"formation",ice.at("searchFormation")},
			{"stateInputs",ice.at("searchStateInputs")},{"effectiveWeights",ice.at("searchEffectiveWeights")},
			{"adaptive",ice.at("searchAdaptive")},
			{"expandedForecast",ice.at("searchExpandedForecast")},
			{"counterHoldSeconds",ice.at("searchCounterHoldSeconds")},
			{"queueRevision",ice.at("searchQueue")},
			{"searchVersion",ice.at("searchVersion")},{"largestPlan",ice.at("searchLargestPlan")},
			{"netEconomy",ice.at("searchNetEconomy")},
			{"anticipateBuilding",ice.at("searchAnticipateBuilding")},
			{"playerEconomy",ice.at("searchPlayerEconomy")},
			{"predictedPlantings",ice.at("searchPredictedPlantings")},
			{"rawProduction",ice.at("searchRawProduction")},{"productionInputs",ice.at("searchProductionInputs")},
			{"preferenceScore",ice.at("searchPreferenceScore")},{"scoreOn100",ice.at("lastBestScoreOn100")},
			{"opponent",ice.at("searchOpponent")},
			{"spent",ice.at("spent")},{"workerIncome",ice.at("workerIncome")},{"killIncome",ice.at("killIncome")},
			{"enemyIce",ice.at("enemyIce")},{"pending",ice.at("pending")}});
	}
	const bool ended = full.at("boardState") != "GAME" || ice.value("trophySpawned", false);
	if (mEpisodeTicks % 600 == 0 || ended || mEpisodeTicks >= ticks) {
		Json plantTypes = Json::object();
		for (const auto& plant : full.at("plants")) if (plant.value("health",0) > 0 && !plant.value("squished",false)) {
			const auto type = plant.at("type").get<std::string>();
			plantTypes[type] = plantTypes.value(type,0) + 1;
		}
		mEpisodeTrace.push_back({{"seconds",mEpisodeTicks / 60.0},{"ice",ice},{"sun",full.at("sun")},
			{"plants",full.at("plantCount")},{"zombies",full.at("zombieCount")},
			{"plantTypes",plantTypes},{"mowers",full.at("mowerCount")}});
	}
	if (ended || mEpisodeTicks >= ticks) {
		const auto name = command.value("name", std::string("episode"));
		if (name.empty() || name.find_first_of("/\\:") != std::string::npos) { Fail("invalid episode filename"); return false; }
		Json result{{"schema",1},{"opponent",opponent},{"seconds",mEpisodeTicks / 60.0},
			{"outcome",full.at("boardState") == "LOSE_GAME" ? "commander_win" : ice.value("trophySpawned",false) ? "player_win" : "timeout"},
			{"playerActions",command.value("playerActions",true)},
			{"initial",mEpisodeInitial},{"final",full},{"trace",mEpisodeTrace},{"playerPlantings",mEpisodePlantings},{"decisions",mEpisodeDecisions}};
		std::ofstream output(std::filesystem::path(mOutDir) / (name + ".json"));
		output << result.dump(2); output.flush();
		if (!output) { Fail("cannot write episode result"); return false; }
		Log("commander episode finished: " + result.at("outcome").get<std::string>());
		mEpisodeTicks = -1; return true;
	}
	// 静态诊断保留正式战斗，只关闭陪练输入，不能把结果混入实战胜率。
	if (command.value("playerActions",true)) for (const auto& action : PlayerActions(full, opponent)) {
		ExecuteInteractive(action);
		if (action.at("op") == "player_plant" && !mInteractiveResults.empty() && mInteractiveResults.back().value("ok",false))
			for (const auto& card : full.at("cards")) if (card.at("slot") == action.at("slot")) {
				const auto type = card.at("gameplayType").get<std::string>();
				mEpisodePlantings[type] = mEpisodePlantings.value(type,0) + 1;
			}
	}
	mInteractiveResults.clear(); // 训练只保留周期状态，避免把整场收阳光回执积累在内存中。
	++mEpisodeTicks;
	return false;
}
