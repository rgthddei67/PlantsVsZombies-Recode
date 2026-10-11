#include "Game/AI/ColdStorageSearch.h"
#include "Game/Plant/PlantType.h"
#include "Game/PlantCardRechargeRules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
using namespace ColdStorageSearch;
constexpr float kAssetTolerance=.02f; // 浮点资产折冰核对误差，冰

/** 核对真实交易及生命周期结果，不规定正式指挥官必须选择某个阵容。 */
void CheckCobConstruction(bool okay,const char* message) {
	if(!okay) {std::cerr<<"FAILED cob construction: "<<message<<'\n';std::exit(1);}
}

/** 普通卡单次付款对应的一株玉米；同source的左右候选共用真实卡冷却。 */
Construction KernelCard(int column) {
	Construction card;
	card.source=0; card.sunCost=100; card.iceCost=10; card.recharge=7.5f;
	card.plant.actualType=static_cast<int>(PlantType::PLANT_KERNELPULT);
	card.plant.cobBaseEligible=true; card.plant.row=0; card.plant.column=column;
	card.plant.x=200+80.0f*column;
	card.plant.health=card.plant.maximumHealth=300; card.plant.reward=15;
	card.plant.assetValue=10+100*4.0f/9;
	card.plant.dps=8.33f;
	return card;
}

/** 两株活底材和已选升级卡；敌人不移动、不攻击，单独观察升级及后续炮击。 */
Snapshot CobConstructionArena() {
	Snapshot state;
	state.rows=1; state.houseX=-10000; state.playerSun=500; state.playerIce=40;
	state.sunIceValue=4.0f/9; state.traceEconomy=true;
	Unit target;
	target.id=21; target.body.row=0; target.body.x=900; target.body.health=30000;
	target.maximumBody=30000; target.body.speed=0; target.biteDps=0;
	target.body.purchaseCost=60; target.body.value=60;
	state.current={target};
	for(int column=0;column<2;++column) {
		auto plant=KernelCard(column).plant; plant.id=11+column;
		state.plants.push_back(plant);
	}
	Construction cannon;
	cannon.source=1; cannon.sunCost=500; cannon.iceCost=40; cannon.recharge=50;
	cannon.requiredBaseKind=static_cast<int>(PlantType::PLANT_KERNELPULT);
	cannon.plant.actualType=static_cast<int>(PlantType::PLANT_COBCANNON);
	cannon.plant.footprintColumns=2; cannon.plant.x=200;
	cannon.plant.health=cannon.plant.maximumHealth=300;
	cannon.plant.assetValue=40+500*state.sunIceValue; cannon.plant.reward=60;
	cannon.cob.firstReady=5+16.0f/12;
	cannon.cob.recharge=34.83f; cannon.cob.windup=4; cannon.cob.flightSeconds=2;
	cannon.cob.damage=1800; cannon.cob.radius=115; cannon.cob.rowRadius=1;
	state.construction={cannon};
	state.cobAimPoints={{0,850},{0,900},{0,950}};
	return state;
}

/** 可从两块空地开始建造；三个交易分别消费同一玉米卡与独立炮卡。 */
Snapshot EmptyCobConstructionArena() {
	auto state=CobConstructionArena();
	state.plants.clear(); state.playerSun=700; state.playerIce=60;
	state.construction.push_back(KernelCard(0)); state.construction.push_back(KernelCard(1));
	return state;
}
}

/** 双格重建必须服从费用/冷却/底材，并把新炮接入同一实体绑定的反制时间线。 */
void RunColdStorageCobConstructionTests() {
	using namespace ColdStorageSearch;
	{
		const auto state=CobConstructionArena();
		ConstructionStats stats;
		const auto result=Evaluate(state,{},&stats);
		const float assets=state.playerSun*state.sunIceValue+state.playerIce
			+state.plants[0].assetValue+state.plants[1].assetValue;
		CheckCobConstruction(stats.cobUpgrades==1 && stats.planted==1 && stats.sunSpent==500 && stats.iceSpent==40,
			"two living kernels consume exactly one real upgrade transaction");
		CheckCobConstruction(result[0]==0 && result[1]==0 && std::abs(stats.opponentAssets-assets)<kAssetTolerance,
			"consumed bases yield no zombie kill/damage income and cash plus surviving assets is conserved");
		CheckCobConstruction(std::abs(stats.cobTransferredAssets-state.plants[0].assetValue-state.plants[1].assetValue)<kAssetTolerance,
			"each surviving base asset transfers exactly once to the new cannon");
		CheckCobConstruction(stats.constructedCobShots==2 && stats.counterTrace.size()==2,
			"all aim points share one new cannon reload cycle");
		CheckCobConstruction(stats.counterTrace.front().at>=10.333f && stats.counterTrace.front().at<=11,
			"new cannon waits for initial arming, charge, launch and flight before its first blast");
		CheckCobConstruction(state.plants[0].health==300 && state.playerSun==500 && state.counters.empty(),
			"forecast owns its new sources and cannot mutate snapshot plants or wallets");
	}
	{
		auto state=CobConstructionArena(); state.construction[0].ready=4;
		state.basketballs.push_back({0,1,1,150});
		ConstructionStats wounded; const auto outcome=Evaluate(state,{},&wounded);
		const float remainingBases=state.plants[0].assetValue+state.plants[1].assetValue*.5f;
		CheckCobConstruction(wounded.cobUpgrades==1 && std::abs(wounded.cobTransferredAssets-remainingBases)<kAssetTolerance
			&& outcome[0]==0 && std::abs(outcome[1]-7.5f)<kAssetTolerance,
			"upgrade transfers only the damaged base's remaining asset and does not manufacture extra damage or kill credit");
		CheckCobConstruction(std::abs(wounded.opponentAssets-state.construction[0].plant.assetValue-remainingBases)<kAssetTolerance,
			"damaged material cannot regain its lost asset value merely by changing identity during upgrade");
	}
	{
		for(int failure=0;failure<9;++failure) {
			auto state=CobConstructionArena();
			if(failure==0) --state.playerSun;
			if(failure==1) --state.playerIce;
			if(failure==2) state.construction[0].ready=1000;
			if(failure==3) state.construction.clear();
			if(failure==4) state.plants.pop_back();
			if(failure==5) state.plants[0].cobBaseEligible=false;
			if(failure==6) state.plants[0].actualType=static_cast<int>(PlantType::PLANT_IMITATER);
			if(failure==7) state.construction[0].requiredSupportIDs={31,32};
			if(failure==8) state.plants[1].id=state.plants[0].id;
			ConstructionStats stats; Evaluate(state,{},&stats);
			CheckCobConstruction(stats.cobUpgrades==0 && stats.constructedCobShots==0 && stats.sunSpent==0 && stats.iceSpent==0,
				"one missing currency unit, cooldown, missing card/base, ineligible/morphing base or missing support forbids upgrade");
		}
		for(int layer:{2,3}) {
			auto state=CobConstructionArena();
			Plant blocker; blocker.id=31; blocker.row=0; blocker.column=1; blocker.x=280;
			blocker.layer=layer; blocker.health=blocker.maximumHealth=1000;
			state.plants.push_back(blocker);
			ConstructionStats stats; Evaluate(state,{},&stats);
			CheckCobConstruction(stats.cobUpgrades==0 && stats.sunSpent==0,"either pumpkin or overlay blocks the entire two-cell upgrade");
		}
	}
	{
		auto state=EmptyCobConstructionArena();
		ConstructionStats ordinary; Evaluate(state,{},&ordinary);
		CheckCobConstruction(ordinary.cobUpgrades==1 && ordinary.planted==3 && ordinary.sunSpent==700 && ordinary.iceSpent==60,
			"empty pair is rebuilt through two paid kernels followed by a paid upgrade");
		CheckCobConstruction(!ordinary.counterTrace.empty() && ordinary.counterTrace.front().at>=20.333f,
			"one shared kernel card prevents simultaneous base construction");
		for(auto& card:state.construction) {
			const float multiplier=static_cast<float>(PlantCardRechargeRules::GetMultiplier(
				static_cast<PlantType>(card.plant.actualType),true,1.0));
			card.recharge/=multiplier; card.ready/=multiplier;
		}
		CheckCobConstruction(state.construction[0].recharge==50
			&& std::abs(state.construction[1].recharge-7.5f/2.2f)<.001f,
			"opening support keeps the cannon card at 50 seconds while accelerating its kernel base card");
		ConstructionStats accelerated; Evaluate(state,{},&accelerated);
		CheckCobConstruction(accelerated.cobUpgrades==1 && !accelerated.counterTrace.empty()
			&& accelerated.counterTrace.front().at<ordinary.counterTrace.front().at
			&& accelerated.counterTrace.front().at>=16.333f,
			"sampled card acceleration changes build timing without accelerating the new cannon arming cycle");
		state=EmptyCobConstructionArena(); state.construction[1].ready=state.construction[2].ready=1000;
		ConstructionStats blocked; Evaluate(state,{},&blocked);
		CheckCobConstruction(blocked.planted==0 && blocked.cobUpgrades==0,"a selected kernel card cannot be used before its real cooldown");
	}
	{
		auto state=EmptyCobConstructionArena();
		Plant original=state.construction[0].plant; original.id=31;
		state.plants={original};
		ConstructionStats alive; Evaluate(state,{},&alive);
		CheckCobConstruction(alive.planted==0 && alive.cobUpgrades==0,"live original cannon is never dismantled to satisfy a hypothetical recipe");
		state.pendingPrecisionID=original.id; state.pendingPrecisionRemaining=2;
		ConstructionStats vacant; const auto outcome=Evaluate(state,{},&vacant);
		CheckCobConstruction(vacant.cobUpgrades==1 && vacant.planted==3 && outcome[0]==60,
			"whole-cannon death opens both cells and rewards only that actual kill, not the subsequent upgrade consumption");
		CheckCobConstruction(!vacant.counterTrace.empty() && vacant.counterTrace.front().at>=22.333f,
			"future vacancy cannot start either kernel transaction before the original cannon dies");
		state.current.clear();
		ConstructionStats prepared; Evaluate(state,{},&prepared);
		CheckCobConstruction(prepared.cobUpgrades==1 && prepared.planted==3 && prepared.constructedCobShots==0,
			"an empty battlefield can restore its known original cannon after precision removal without inventing a target");
		state.current=CobConstructionArena().current; state.current[0].body.spawnAt=35;
		ConstructionStats beforeArrival; Evaluate(state,{},&beforeArrival);
		CheckCobConstruction(beforeArrival.cobUpgrades==1 && !beforeArrival.counterTrace.empty()
			&& beforeArrival.counterTrace.front().at>=39 && beforeArrival.counterTrace.front().at<=39.5f,
			"known-defence preparation finishes before a later cohort arrives, but aim and fire wait for its real arrival");
		--state.playerSun; ConstructionStats poor; Evaluate(state,{},&poor);
		CheckCobConstruction(poor.cobUpgrades==0,"empty-board readiness never supplies the missing last sunlight unit");
		auto unknown=EmptyCobConstructionArena(); unknown.current.clear();
		ConstructionStats noHistory; Evaluate(unknown,{},&noHistory);
		CheckCobConstruction(noHistory.cobUpgrades==0,"restoration readiness does not invent a prior cannon on an unrelated empty board");
	}
	{
		auto state=CobConstructionArena();
		state.pendingPrecisionID=11; state.pendingPrecisionRemaining=2;
		ConstructionStats identities; const auto outcome=Evaluate(state,{},&identities);
		CheckCobConstruction(identities.cobUpgrades==1 && identities.constructedCobShots==2 && outcome[0]==0,
			"an old base-ID precision lock never transfers to the newly created cannon");
		for(float death:{8.0f,9.0f}) {
			state=CobConstructionArena();
			state.basketballs.push_back({0,1,death,300});
			ConstructionStats killed; const auto killedOutcome=Evaluate(state,{},&killed);
			CheckCobConstruction(killed.cobUpgrades==1 && killed.constructedCobShots==1 && killedOutcome[0]==60,
				"right-cell hit kills one new source and negative source IDs cannot fire after death");
			CheckCobConstruction(killed.counterTrace.size()==(death<8.5f ? 0u : 1u),
				"death before launch cancels the shot; death after launch preserves exactly its independent flight");
		}
	}
	{
		auto state=CobConstructionArena();
		Plant cannon=state.construction[0].plant; cannon.id=31;
		state.plants={cannon}; state.construction={KernelCard(1)};
		state.playerSun=1000; state.playerIce=1000; state.anticipateEconomy=true;
		SunExchange exchange; exchange.sunGain=100; exchange.iceCost=10; exchange.recharge=25; exchange.cells={{0,1}};
		state.exchanges={exchange};
		Counter ash; ash.source=0; ash.cellRow=0; ash.cellColumn=1; ash.shovelAllowed=true;
		ash.blast.x=900; ash.blast.damage=1800; ash.blast.reach.fill(-1); ash.blast.reach[0]=115;
		state.counters={ash};
		ConstructionStats blocked;
		Evaluate(state,{},&blocked,0,0,0,false,false,-1,false,true);
		CheckCobConstruction(blocked.planted==0 && blocked.exchanges==0 && blocked.counterShovels==0 && blocked.counterTrace.empty(),
			"right half of a wide cannon blocks construction, exchange, ash planting and partial shoveling");
		state.exchanges.clear(); state.counters.clear(); state.pendingPrecisionID=31; state.pendingPrecisionRemaining=2;
		ConstructionStats opened; Evaluate(state,{},&opened);
		CheckCobConstruction(opened.planted==1,"right-cell ordinary planting becomes legal only after the whole cannon dies");
		state.playerSun=150; state.playerIce=20; state.current[0].body.purchaseCost=2000;
		ash.blast.ready=5; ash.sunCost=25; ash.iceCost=5; state.counters={ash};
		ConstructionStats replanted; const auto result=Evaluate(state,{},&replanted,0,0,0,false,false,-1,false,true);
		CheckCobConstruction(replanted.planted==1 && replanted.counterShovels==1 && replanted.paidCounterCasts==1
			&& replanted.counterTrace.size()==1 && result[0]==60,
			"after wide-source death a newly planted single-cell kernel may be legally shoveled for paid ash without zombie kill income");
	}
	{
		auto state=EmptyCobConstructionArena(); state.anticipateEconomy=true;
		state.playerIce=0; state.playerSun=2000; state.shop={{225,100,10}};
		ConstructionStats reserve;
		Evaluate(state,{},&reserve,0,0,0,true);
		CheckCobConstruction(reserve.planted==0 && reserve.cobUpgrades==0 && reserve.orders==0 && reserve.constructedCobShots==0,
			"independent reserve-space response keeps cash and never orders ice for a forgone cannon recipe");
		auto building=state; ConstructionStats rebuilding; Evaluate(building,{},&rebuilding);
		CheckCobConstruction(rebuilding.orders>0 && rebuilding.cobUpgrades==1 && rebuilding.iceSpent==60,
			"construction response can buy and await real ice deliveries while paying each build step once");
	}
	{
		auto state=CobConstructionArena(); state.rows=2; state.weatherStation=true;
		state.station.controls[WeatherStationRules::FOG].value=4;
		state.station.controls[WeatherStationRules::FOG].protection=100;
		state.stationFogAlpha.fill(255);
		for(auto& p:state.plants) {p.column+=4;p.x+=320;p.dps=0;}
		state.construction[0].plant.column=4; state.construction[0].plant.x=520;
		state.current[0].body.row=1; state.current[0].body.x=200;
		state.cobAimPoints={{1,200}};
		auto hidden=state.current[0]; hidden.id=22; hidden.body.row=0; hidden.body.x=520;
		hidden.body.purchaseCost=0; hidden.body.value=0; hidden.biteDps=0;
		state.current.push_back(hidden);
		ConstructionStats near; Evaluate(state,{},&near);
		state.current[1].body.x=850;
		ConstructionStats far; Evaluate(state,{},&far);
		CheckCobConstruction(near.cobUpgrades==1 && far.cobUpgrades==1 && near.sunSpent==far.sunSpent
			&& near.iceSpent==far.iceSpent && near.constructedCobShots==far.constructedCobShots,
			"identical visible worlds cannot change rebuilding because a harmless hidden unit moved near the construction anchor");
	}
	std::cout<<"Cob construction preserves cards, both cells, payment, assets, identities and independent player responses\n";
}
