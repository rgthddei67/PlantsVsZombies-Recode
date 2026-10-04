#include "TestDriver.h"
#include "DeltaTime.h"
#include "Game/GameScene.h"
#include "Game/SceneManager.h"
#include "Game/Plant/ColdPineappleRules.h"
#include "Game/Plant/IceStorageNutRules.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

namespace {
using Json = nlohmann::json;
constexpr float kStoredWakeStake=4200; // 蓄爆陪练愿意唤醒毁灭的可见威胁总值，生命及猎工优先值
constexpr float kStoredWakeReach=200; // 蓄爆择时的保守水平覆盖，像素；实际爆炸仍由正式实体结算
/** 固定的植物方陪练，只从可见状态选择动作，不加钱、不重置冷却、不替指挥官出兵。 */
std::vector<Json> PlayerActions(const Json& state, const std::string& opponent, bool shovelCounters) {
	std::vector<Json> actions;
	const auto& ice = state.at("coldStorage");
	const bool temporalBunker=opponent=="ice_bunker_temporal";
	const bool holdPineapple = opponent == "ice_pine_hold" || opponent == "ice_bunker_hold";
	const bool bunker = opponent == "ice_bunker" || opponent == "ice_bunker_hold" || temporalBunker;
	const bool storageDefense = opponent == "ice_fortifier" || opponent == "ice_pine" || holdPineapple || bunker;
	const bool pineElite = opponent == "pine_elite" || opponent == "ice_pine" || holdPineapple || bunker;
	const bool planner = opponent == "planner" || pineElite || storageDefense;
	const std::string wall = storageDefense ? "PLANT_ICESTORAGENUT" : "PLANT_WALLNUT";
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
    const bool station=state.value("background",std::string())=="WEATHER_STATION";
    const auto& lamp=state.at("plantern");
    // 气象站陪练按公开雾势开灯，无雾关灯；不从雾中实体的隐藏坐标选择挡位。
    if(station && lamp.value("active",false)) {
        const int fog=state.at("weatherStation").at("controls").at(1).at("value");
        const int gear=fog>0 ? (lamp.value("fuelTenths",0)>=200 ? 3 : 2) : 0;
        if(lamp.value("gearValue",0)!=gear) actions.push_back({{"op","player_set_plantern_gear"},{"gear",gear}});
    }
	std::vector<Json> zombies;
	for (const auto& z : state.at("zombies")) if (z.value("bodyHealth", 0) > 0 && (!station || !z.value("fogObscured",false))) zombies.push_back(z);
	std::stable_sort(zombies.begin(), zombies.end(), [](const auto& a, const auto& b) { return a.at("xInt") < b.at("xInt"); });
	int defenseReserve = 0, defenseIceReserve = 0;
	// 威胁已经接近时，为十秒内转好的灰烬预留真实阳光；空场及长期冷却时释放这笔预算发展经济。
	if (planner && !zombies.empty() && zombies.front().at("xInt").get<int>() < 950)
		for (const auto& card : state.at("cards")) {
			const auto kind = card.at("gameplayType").get<std::string>();
			if ((kind == "PLANT_CHERRYBOMB" || kind == "PLANT_JALAPENO" || kind == "PLANT_SQUASH" || (station && kind == "PLANT_DOOMSHROOM"))
				&& card.value("cooldownRemainingMs",0) <= 10000) {
				defenseReserve = std::max(defenseReserve,card.at("sunCost").get<int>());
				defenseIceReserve = std::max(defenseIceReserve,ice.at("plantCosts").at(kind).get<int>());
			}
		}
	bool releasedLotus = false;
	// 厚墙陪练只为完整缺血量付费修复，逐株预留真实冰费；不利用过量回执透支同一钱包。
	if (storageDefense) for (const auto& p : state.at("plants"))
		if (p.value("nutReady",false) && p.value("nutAffordable",false)
			&& p.at("maxHealth").get<int>()-p.at("health").get<int>()>=IceStorageNutRules::kRepairHealth) {
			const int cost=static_cast<int>(std::ceil(IceStorageNutRules::kRepairIce
				/ (ice.value("discountRemainingMs",0)>0 ? static_cast<float>(ColdStorageSkillRules::DiscountDivisor) : 1.0f)));
			if (stock<cost+defenseIceReserve) break;
			actions.push_back({{"op","player_activate_ice_storage_nut"},{"row",p.at("row")},{"col",p.at("col")}});
			stock-=cost;
		}
	// 菠萝只在九格内精英菇确有同行目标时付费增幅，保留近身反制的预算；不空场循环耗冰。
	// 保留独立省冰陪练，不覆盖旧对手，便于识别依赖玩家无谓开技能的假收益。
	if (pineElite && !holdPineapple && stock >= ColdPineappleRules::kIceCost+defenseIceReserve) {
		const Json* selected = nullptr; int bestTargets = 0;
		for (const auto& p : state.at("plants")) if (p.value("pineappleReady",false) && p.value("pineappleAffordable",false)) {
			int targets = 0;
			for (const auto& ally : state.at("plants")) {
				if (ally.at("type") != "PLANT_ELITE_SCAREDYSHROOM" || ally.value("health",0) <= 0
					|| std::abs(ally.at("row").get<int>()-p.at("row").get<int>()) > 1
					|| std::abs(ally.at("col").get<int>()-p.at("col").get<int>()) > 1) continue;
				if (std::any_of(zombies.begin(),zombies.end(),[&](const Json& z) {
					return z.at("row") == ally.at("row") && z.at("xInt").get<int>() <= SCENE_WIDTH
						&& z.at("xInt").get<int>() > state.at("cells").at(ally.at("row").get<int>())
							.at(ally.at("col").get<int>()).at("centerXInt").get<int>();
				})) ++targets;
			}
			if (targets > bestTargets) { selected = &p; bestTargets = targets; }
		}
		if (selected) {
			actions.push_back({{"op","player_activate_cold_pineapple"},{"row",selected->at("row")},{"col",selected->at("col")}});
			stock -= ColdPineappleRules::kIceCost;
		}
	}
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
				const int targetRow=r,targetColumn=c;
				const Json cell = Json::array({r,c});
				if (std::find(card.at("legalCells").begin(), card.at("legalCells").end(), cell) == card.at("legalCells").end()) continue;
				// 独立陪练在可见工人群有钟匠掩护时先付时间干扰，再按原门槛交灰烬。
				// 仅使用公开单位及真实钱包/冷却，不改变原 ice_bunker 基线，也不替僵尸出兵。
				if(temporalBunker && (kind=="PLANT_JALAPENO" || kind=="PLANT_CHERRYBOMB" || kind=="PLANT_DOOMSHROOM")
					&& ice.value("interferenceReady",false)
					&& stock>=ColdStorageSkillRules::InterferenceIceCost+ice.at("plantCosts").at(kind).get<int>()
					&& std::any_of(zombies.begin(),zombies.end(),[](const Json& z){return z.at("type")=="ZOMBIE_POLAR_CLOCKMAKER";})
					&& std::count_if(zombies.begin(),zombies.end(),[&](const Json& z){
						if(z.at("type")!="ZOMBIE_ICE_WORKER") return false;
						const int dy=std::abs(z.at("row").get<int>()-targetRow);
						const int dx=std::abs(z.at("xInt").get<int>()-state.at("cells").at(targetRow).at(targetColumn).at("centerXInt").get<int>());
						return kind=="PLANT_JALAPENO" ? dy==0 : dy<=(kind=="PLANT_DOOMSHROOM" ? 2 : 1)
							&& dx<=(kind=="PLANT_DOOMSHROOM" ? kStoredWakeReach : 130);
					})>=3) {
					actions.push_back({{"op","temporal_interference"}}); stock-=ColdStorageSkillRules::InterferenceIceCost;
				}
				actions.push_back({{"op","player_plant"},{"slot",card.at("slot")},{"row",r},{"col",c}});
				planted = true; return;
			}
			// 独立陪练选项：没有空位才考虑牺牲一株。只读取本方合法格及可见威胁，
			// 本秒只铲，下一秒重新观察后正常付款放灰烬；不假定铲除当帧已经释放占位。
			if (shovelCounters && (kind == "PLANT_JALAPENO" || kind == "PLANT_CHERRYBOMB"
				|| (station && kind == "PLANT_DOOMSHROOM"))) {
				std::pair<int,int> selected{-1,-1}; int lowestCost = 100000;
				for (const auto& [r,c] : cells) {
					const Json cell = Json::array({r,c});
					const auto& vacant = card.at("counterVacantCells");
					if (std::find(vacant.begin(),vacant.end(),cell) == vacant.end()) continue;
					const auto found = plants.find({r,c});
					if (found == plants.end()) continue;
					const auto type = found->second.at("type").get<std::string>();
					// 不铲正在结算的灰烬、蓄爆、路灯或累计限额输出，避免制造另一种假对手。
					if (type == "PLANT_CHERRYBOMB" || type == "PLANT_JALAPENO" || type == "PLANT_DOOMSHROOM"
						|| type == "PLANT_SQUASH" || type == "PLANT_PLANTERN" || type == "PLANT_ELITE_SCAREDYSHROOM"
						|| type == "PLANT_MARIGOLD") continue;
					int cost = 300;
					for (const auto& ownedCard : state.at("cards")) if (ownedCard.at("gameplayType") == type)
						cost = ownedCard.at("sunCost").get<int>();
					if (cost < lowestCost) { lowestCost = cost; selected = {r,c}; }
				}
				if (selected.first >= 0) {
					actions.push_back({{"op","player_shovel"},{"row",selected.first},{"col",selected.second}});
					planted = true; return;
				}
			}
		}
	};
    if(station && !lamp.value("active",false)) attempt("PLANT_PLANTERN",{{2,4},{2,3},{1,4},{3,4}});
	// 反制陪练优先堵住将要接触防线的路线，让快僵尸实际经历坚果前聚团。
	if (counterplay && !adaptive) {
		std::vector<std::pair<int,int>> walls;
		for (const auto& z : zombies) if (z.at("xInt").get<int>() < 1050 && !plants.count({z.at("row"),6}))
			walls.emplace_back(z.at("row"),6);
		attempt(wall, walls);
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
					&& std::abs(z.at("xInt").get<int>()-x)<=130)
				|| (bunker && type=="PLANT_DOOMSHROOM" && (!p.value("sleeping",true) || p.value("wakeUpTimeMs",0)>0)
					&& std::abs(z.at("row").get<int>()-cell.first)<=2 && std::abs(z.at("xInt").get<int>()-x)<=kStoredWakeReach)) health -= 1800;
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
    // 夜晚毁灭菇落地就会起爆，不能沿用白天的空场蓄爆。只用可见目标选合法落点。
    if(bunker && station) {
        std::vector<std::pair<float,std::pair<int,int>>> doomCells;
        for(int r=0;r<state.at("rows").get<int>();++r) for(int c=0;c<state.at("columns").get<int>();++c) {
            const float x=state.at("cells").at(r).at(c).at("centerXInt");
            float value=0;
            for(const auto& z:zombies) if(std::abs(z.at("row").get<int>()-r)<=2
                && std::abs(z.at("xInt").get<float>()-x)<=kStoredWakeReach) value+=blastValue(z);
            if(value>=kStoredWakeStake) doomCells.push_back({value,{r,c}});
        }
        std::stable_sort(doomCells.begin(),doomCells.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        std::vector<std::pair<int,int>> targets;
        for(const auto& entry:doomCells) targets.push_back(entry.second);
        attempt("PLANT_DOOMSHROOM",targets);
        if(planted) return actions;
    }
	// 蓄爆陪练用可见聚团择时唤醒预存毁灭；正在唤醒的同一株不会再次提交咖啡。
	if (bunker) {
		std::vector<std::pair<float,std::pair<int,int>>> wakeCells;
		for (const auto& [cell,p]:plants) if (p.at("type")=="PLANT_DOOMSHROOM" && p.value("sleeping",false)
			&& p.value("wakeUpTimeMs",0)==0) {
			const float x=state.at("cells").at(cell.first).at(cell.second).at("centerXInt");
			float value=0;
			for (const auto& z:zombies) if (std::abs(z.at("row").get<int>()-cell.first)<=2
				&& std::abs(z.at("xInt").get<float>()-x)<=kStoredWakeReach) value+=blastValue(z);
			if (value>=kStoredWakeStake) wakeCells.push_back({value,cell});
		}
		std::stable_sort(wakeCells.begin(),wakeCells.end(),[](const auto& a,const auto& b){return a.first>b.first;});
		std::vector<std::pair<int,int>> wake;
		for (const auto& entry:wakeCells) wake.push_back(entry.second);
		attempt("PLANT_INSTANT_COFFEE",wake);
		if (planted) return actions; // 待下一次快照观察爆炸结果，再决定是否补交灰烬。
	}
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
		attempt(wall,cells);
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
	if (!zombies.empty() && zombies.front().at("xInt").get<int>() < 850) attempt(wall, cells);
	int producers = 0;
	for (const auto& [cell,p] : plants) if (p.at("type") == "PLANT_SUNFLOWER" || p.at("type") == "PLANT_SUNSHROOM") ++producers;
	// 防守型陪练有基本经济后先补各路输出与后排保护，避免必须铺完经济才开始防守。
	// 只改变这个对手的合法动作顺序，旧陪练继续保留，不能把训练变成针对单一固定阵型。
	if (fortifier && producers >= 4) {
		cells.clear(); for (int r : rows) cells.emplace_back(r,0);
		attempt(eliteDefense ? "PLANT_ELITE_SCAREDYSHROOM" : "PLANT_MELONPULT",cells);
		const auto attackCells = cells;
		if (pineElite) {
			// 尽量让菠萝邻接已建输出，后续补菇保留另一侧格，避免经济株先占满领域核心。
			cells.clear();
			for (int r : rows) for (int c : {1,2}) if (std::any_of(plants.begin(),plants.end(),[&](const auto& entry) {
				return entry.second.at("type") == "PLANT_ELITE_SCAREDYSHROOM"
					&& std::abs(entry.first.first-r) <= 1 && std::abs(entry.first.second-c) <= 1;
			})) cells.emplace_back(r,c);
			attempt("PLANT_COLDPINEAPPLE",cells);
		}
		// 高优先级的合法输出已转好但暂时缺阳光时，允许攒钱；不能每秒花掉零钱后永远买不起。
		// 前面的救险与金盏花周转仍可执行；这是陪练的建设计划，不干预僵尸购买或免费补资源。
		// 先有八株基本经济再冻结其他建设，避免过早攒大件反而长期缺少收入。
		if (planner && producers >= 8 && !planted) for (const auto& card : state.at("cards")) {
			if (card.at("gameplayType") != (eliteDefense ? "PLANT_ELITE_SCAREDYSHROOM" : "PLANT_MELONPULT")
				|| !card.at("ready").get<bool>() || card.at("sunCost").get<int>() <= sun) continue;
			for (const auto& [r,c] : attackCells) {
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
				if (!pineElite) cells.emplace_back(r,2);
			}
		}
		attempt(station ? "PLANT_SUNSHROOM" : "PLANT_SUNFLOWER", cells);
	}
	// 陌生阵型沿用正式累计配额和冷却；不能在精英菇死亡后免费重建四株。
	if (eliteDefense) {
		cells.clear(); for (int r : rows) { cells.emplace_back(r,0); cells.emplace_back(r,pineElite ? 2 : 1); }
		attempt("PLANT_ELITE_SCAREDYSHROOM", cells);
		cells.clear(); for (int r : rows) { cells.emplace_back(r,2); cells.emplace_back(r,1); }
		attempt("PLANT_REPEATER", cells);
	}
	cells.clear(); for (int r : rows) cells.emplace_back(r,0);
	attempt("PLANT_THUNDERFLOWER", cells);
	attempt("PLANT_MELONPULT", cells);
	if (opponent == "growth") { cells.clear(); for (int r : rows) cells.emplace_back(r,1); attempt("PLANT_MELONPULT", cells); }
	attempt("PLANT_WINTERMELON", {{1,0},{3,0},{0,0},{4,0},{2,0}});
	cells.clear(); for (int r : rows) { cells.emplace_back(r,1); cells.emplace_back(r,2); }
	attempt("PLANT_MELONPULT", cells);
	if (adaptive) attempt("PLANT_PUMPKINSHELL", protectionCells);
	if (bunker && !station) {
		cells.clear();
		const bool stored=std::any_of(plants.begin(),plants.end(),[](const auto& entry){return entry.second.at("type")=="PLANT_DOOMSHROOM";});
		if (!stored) for (int r:rows) cells.emplace_back(r,7);
		attempt("PLANT_DOOMSHROOM",cells);
	}
	cells.clear(); for (int r : rows) cells.emplace_back(r,6);
	attempt(wall, cells);
	return actions;
}
}

void TestDriver::RecordEngineerAshProtection(float elapsed,int row,int engineerID,const std::vector<int>& workerIDs) {
	if(!mActive || mEpisodeTicks<0 || !mEpisodeTraceProtections || workerIDs.empty()) return;
	// Board 的冻结名单和跳过伤害在同一主线程事务中完成；记录数值/稳定 ID，来源随后死亡也不丢证据。
	mEpisodeEngineerProtections.push_back({{"elapsed",elapsed},{"row",row},{"engineerID",engineerID},{"workerIDs",workerIDs}});
}

/** 用正式玩家入口推进脚本对战，可选逐秒实体轨迹用于核对掩护及战损。 */
bool TestDriver::ExecuteCommanderEpisode(const nlohmann::json& command) {
	const int ticks = static_cast<int>(std::lround(command.value("seconds", 120.0f) * 60));
	const int timeScale = command.value("timeScale",1);
	// 同步训练仍固定 1 倍；显式实时验收可用正式 2 倍物理步和对应后台预算，结果必须标记。
	if ((timeScale != 1 && timeScale != 2)
		|| (timeScale != 1 && (!BackgroundCommander() || BatchStepsPerFrame() != 0))) {
		Fail("commander_episode: 2x requires realtime background validation"); return false;
	}
	const auto opponent = command.value("opponent", std::string("bomb"));
	const int refillBelow = command.value("sunRefillBelow",-1);
	const int refillTo = command.value("sunRefillTo",MAX_SUN);
	if (refillBelow < -1 || (refillBelow >= 0 && (refillTo <= refillBelow || refillTo > MAX_SUN))) {
		Fail("commander_episode: invalid external sun refill"); return false;
	}
	if (ticks < 60 || ticks > 72000 || (opponent != "bomb" && opponent != "growth" && opponent != "deny" && opponent != "counter" && opponent != "ash" && opponent != "adaptive" && opponent != "hunter" && opponent != "builder" && opponent != "lotus" && opponent != "fortifier" && opponent != "planner" && opponent != "pine_elite" && opponent != "ice_fortifier" && opponent != "ice_pine" && opponent != "ice_bunker" && opponent != "ice_pine_hold" && opponent != "ice_bunker_hold" && opponent != "ice_bunker_temporal")) {
		Fail("commander_episode: invalid duration or opponent"); return false;
	}
	auto* scene = dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
	auto* board = scene ? scene->GetBoard() : nullptr;
	if (!board || !board->IsColdStorage()) { Fail("commander_episode requires cold storage"); return false; }
	const bool terminal = board->mBoardState == BoardState::LOSE_GAME || board->mTrophySpawned;
	// 正式判负会暂停时钟；先接收胜负，再检查比赛中是否被改速或手动暂停。
	if (!terminal && (DeltaTime::GetTimeScale() != timeScale || DeltaTime::IsPaused())) {
		Fail("commander_episode: speed differs from declared timeScale"); return false;
	}
	if (mEpisodeTicks < 0) {
		mEpisodeTicks = 0; mEpisodeInitial = BuildInteractiveState(); mEpisodeTrace = Json::array();
		mEpisodePlantings = Json::object(); mEpisodeDecisions = Json::array(); mEpisodeSunRefills = Json::array();
		mEpisodeTraceProtections=command.value("traceUnits",false); mEpisodeEngineerProtections=Json::array();
		if (mEpisodeInitial.at("cards").empty()) { Fail("commander_episode: player has no cards"); return false; }
		Log("commander episode started: " + opponent);
	}
	// 普通观测每秒一次，正式胜负在每个逻辑步立即收尾。
	Json full;
	if (terminal || mEpisodeTicks % 60 == 0 || mEpisodeTicks >= ticks) full = BuildInteractiveState();
	else { mEpisodeTicks += timeScale; return false; }
	if (!full.contains("coldStorage")) { Fail("commander_episode requires cold storage"); return false; }
	const auto& ice = full.at("coldStorage");
	// 每次决策留一份紧凑证据，包括观望；避免十秒采样漏掉中间的高额采购。
	if (ice.value("searchSerial",0) > 0 && (mEpisodeDecisions.empty()
		|| mEpisodeDecisions.back().at("serial") != ice.at("searchSerial"))) {
		mEpisodeDecisions.push_back({{"seconds",mEpisodeTicks/60.0},{"serial",ice.at("searchSerial")},
			{"features",ice.at("searchFeatures")},{"baseline",ice.at("searchBaselineFeatures")},
			{"elapsed",ice.at("searchElapsed")},{"wave",ice.at("decisions")},
			{"rowStrikes",ice.at("searchRowStrikeCount")},
			{"specialAbilities",{{"precisionTarget",ice.at("searchPrecisionTargetID")},{"precisionGain",ice.at("searchPrecisionGain")},
				{"precisionEvaluated",ice.at("searchPrecisionEvaluated")},
				{"armorRepairs",ice.at("searchArmorRepairs")},{"repairIce",ice.at("searchArmorRepairIce")},
				{"drumBeats",ice.at("searchDrumBeats")},{"drumRecipients",ice.at("searchDrumRecipients")},
				{"ritualReleases",ice.at("searchRitualReleases")},{"riftSummons",ice.at("searchRiftSummons")},
				{"riftRedirects",ice.at("searchRiftRedirects")},
				{"clockAnchors",ice.at("searchClockAnchors")},{"clockRewinds",ice.at("searchClockRewinds")},
				{"clockRevivals",ice.at("searchClockRevivals")}}},
			{"formation",ice.at("searchFormation")},{"combination",ice.at("searchCombination")},
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
	const bool traceUnits=command.value("traceUnits",false);
	if (mEpisodeTicks % (traceUnits ? 60 : 600) == 0 || ended || mEpisodeTicks >= ticks) {
		Json plantTypes = Json::object();
		for (const auto& plant : full.at("plants")) if (plant.value("health",0) > 0 && !plant.value("squished",false)) {
			const auto type = plant.at("type").get<std::string>();
			plantTypes[type] = plantTypes.value(type,0) + 1;
		}
		mEpisodeTrace.push_back({{"seconds",mEpisodeTicks / 60.0},{"ice",ice},{"sun",full.at("sun")},
			{"plants",full.at("plantCount")},{"zombies",full.at("zombieCount")},
			{"plantTypes",plantTypes},{"mowers",full.at("mowerCount")}});
		// 专项按稳定实体ID跟踪真实前后位置及掉血，不能用“同一局出过两种兵”替代掩护证据。
		if(traceUnits) mEpisodeTrace.back()["units"]=full.at("zombies");
	}
	if (ended || mEpisodeTicks >= ticks) {
		const auto name = command.value("name", std::string("episode"));
		if (name.empty() || name.find_first_of("/\\:") != std::string::npos) { Fail("invalid episode filename"); return false; }
		Json result{{"schema",1},{"opponent",opponent},{"seconds",mEpisodeTicks / 60.0},{"timeScale",timeScale},
			{"outcome",full.at("boardState") == "LOSE_GAME" ? "commander_win" : ice.value("trophySpawned",false) ? "player_win" : "timeout"},
			{"playerActions",command.value("playerActions",true)},
			{"shovelCounters",command.value("shovelCounters",false)},
			{"externalSun",{{"enabled",refillBelow >= 0},{"below",refillBelow},{"target",refillTo},{"events",mEpisodeSunRefills}}},
			{"engineerProtectionEvents",mEpisodeEngineerProtections},
			{"initial",mEpisodeInitial},{"final",full},{"trace",mEpisodeTrace},{"playerPlantings",mEpisodePlantings},{"decisions",mEpisodeDecisions}};
		std::ofstream output(std::filesystem::path(mOutDir) / (name + ".json"));
		output << result.dump(2); output.flush();
		if (!output) { Fail("cannot write episode result"); return false; }
		Log("commander episode finished: " + result.at("outcome").get<std::string>());
		mEpisodeTicks = -1; mEpisodeTraceProtections=false; return true;
	}
	// 模拟主人用 CE 在低阳光时再次补满；仅测试命令显式启用，不把未来补款泄露给 AI。
	// 记录每一笔注入，普通训练/真人观察缺省不会进入此分支，灰烬冷却和冰块仍走正式规则。
	if (refillBelow >= 0 && board->mSun <= refillBelow) {
		mEpisodeSunRefills.push_back({{"seconds",mEpisodeTicks/60.0},{"before",board->mSun},{"after",refillTo},{"added",refillTo-board->mSun}});
		board->mSun = refillTo; full["sun"] = refillTo;
	}
	// 静态诊断保留正式战斗，只关闭陪练输入，不能把结果混入实战胜率。
	if (command.value("playerActions",true)) for (const auto& action : PlayerActions(full, opponent, command.value("shovelCounters",false))) {
		ExecuteInteractive(action);
		if (action.at("op") == "player_plant" && !mInteractiveResults.empty() && mInteractiveResults.back().value("ok",false))
			for (const auto& card : full.at("cards")) if (card.at("slot") == action.at("slot")) {
				const auto type = card.at("gameplayType").get<std::string>();
				mEpisodePlantings[type] = mEpisodePlantings.value(type,0) + 1;
			}
	}
	mInteractiveResults.clear(); // 训练只保留周期状态，避免把整场收阳光回执积累在内存中。
	mEpisodeTicks += timeScale;
	return false;
}
