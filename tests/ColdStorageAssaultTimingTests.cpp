#include "Game/AI/ColdStorageAssaultTiming.h"
#include "Game/AI/ColdStorageSearch.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

namespace {
/** 失败时保留独立时序契约名称，不把结果变成固定购兵要求。 */
void CheckTiming(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED assault timing: "<<message<<'\n';std::exit(1);}
}

/** 只有所有付费士兵都在指定时刻入场才算完整同步案；设备不算士兵。 */
const std::vector<ColdStorageSearch::Action>* FullAt(const ColdStorageSearch::Snapshot& state,
    const std::vector<std::vector<ColdStorageSearch::Action>>& schedules,float after) {
    const auto found=std::find_if(schedules.begin(),schedules.end(),[&](const auto& plan) {
        return std::all_of(plan.begin(),plan.end(),[&](const auto& action) {
            return action.delay==(state.options[action.option].device>=0 ? 0 : after);
        });
    });
    return found==schedules.end() ? nullptr : &*found;
}

/** 无移动、无生产的低价部队使炮的真实触发与延迟收费可以单独核对。 */
ColdStorageSearch::Snapshot TimingArena() {
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1; state.searchVersion=2; state.netEconomy=true; state.houseX=-10000;
    state.budget=32; state.capacity=4; state.traceEconomy=true;
    Plant cannon;
    cannon.id=51001; cannon.x=300; cannon.health=300; cannon.assetValue=600;
    state.plants={cannon};
    Counter shot;
    shot.plantID=cannon.id; shot.consumesPlant=false; shot.windup=4; shot.flightSeconds=2;
    shot.recharge=1000; shot.blast.x=900; shot.blast.damage=1800;
    shot.blast.reach.fill(-1); shot.blast.reach[0]=115;
    state.counters={shot};
    Option troop;
    troop.type=51002; troop.cost=8; troop.unit.body.x=900; troop.unit.body.health=100;
    troop.unit.body.purchaseCost=8; troop.unit.body.speed=0; troop.unit.biteDps=0;
    state.options={troop};
    return state;
}
}

/** 时序只生成完整可重评动作；费用、在途炮弹和不受骗的反制仍走原预测与资本门禁。 */
void RunColdStorageAssaultTimingTests() {
    using namespace ColdStorageSearch;
    {
        auto state=TimingArena();
        state.budget=122; state.capacity=6;
        state.precisionTargetID=state.plants[0].id;
        state.counters[0].blast.ready=3;
        auto flight=state.counters[0]; flight.blast.committed=true; flight.blast.ready=10;
        flight.plantID=0; state.counters.push_back(flight);
        auto partner=state.options[0]; partner.type=51003; partner.cost=11;
        state.options.push_back(partner);
        Option device; device.type=-51004; device.device=0; device.cost=5;
        state.options.push_back(device);
        const std::vector<Action> original{{0,0},{1,0},{0,1},{1,1},{0,2},{1,2},{2,0}};
        const auto schedules=BuildAssaultSchedules(state,original,60);
        CheckTiming(schedules.size()==4,"three distinct event families and one mixed split remain bounded to four schedules");
        CheckTiming(FullAt(state,schedules,ColdStorageSkillRules::StrikeAimDuration+.5f)
            && FullAt(state,schedules,10.5f) && FullAt(state,schedules,7.5f),
            "complete formations cover actual aim, committed impact and ready plus total windup without double flight time");
        bool mixedSplit=false;
        for(const auto& plan:schedules) {
            CheckTiming(plan.size()==original.size(),"staging neither clones nor drops a paid member");
            int cost=0; std::map<int,int> early,late;
            for(size_t i=0;i<plan.size();++i) {
                CheckTiming(plan[i].option==original[i].option && plan[i].delay>=0 && plan[i].delay<=60,
                    "member identity, route, original order and valid time bounds survive scheduling");
                cost+=state.options[plan[i].option].cost;
                if(state.options[plan[i].option].device>=0) CheckTiming(plan[i].delay==0,"devices never receive a fictitious delayed purchase");
                else ++(plan[i].delay==0 ? early : late)[plan[i].option];
            }
            CheckTiming(cost==62,"all timing variants retain precisely the original transaction cost");
            mixedSplit|=early[0]>0 && early[1]>0 && late[0]>0 && late[1]>0;
        }
        CheckTiming(mixedSplit,"repeated mixed types retain representatives in both echelons instead of peeling off a whole support type");
        CheckTiming(state.counters[0].blast.ready==3 && state.counters[1].blast.ready==10
            && state.plants[0].health==300 && state.budget==122 && original[2].delay==1,
            "candidate timing does not consume source cooldown, wallet, plants or the original formation");
        auto future=state; future.counters.clear(); future.precisionUnlockAfterPurchase=true;
        future.precisionUnlockAimStartSeconds=3;
        CheckTiming(FullAt(future,BuildAssaultSchedules(future,original,60),ColdStorageSkillRules::StrikeAimDuration+3.5f),
            "next-wave followup waits for actual later aim start as well as the ordinary aim duration");
        CheckTiming(BuildAssaultSchedules(future,original,ColdStorageSkillRules::StrikeAimDuration).empty(),
            "an event beyond the stage delay limit is not clamped into an allegedly post-impact early arrival");
        future.precisionTargetID=0;
        CheckTiming(BuildAssaultSchedules(future,original,60).empty(),"having no actual skill intent or cannon event adds no speculative cooldown benefit");
    }
    {
        auto state=TimingArena();
        state.counters[0].blast.ready=12;
        auto otherPlant=state.plants[0]; otherPlant.id=51005; state.plants.push_back(otherPlant);
        auto ready=state.counters[0]; ready.source=1; ready.plantID=otherPlant.id; ready.blast.ready=0;
        state.counters.push_back(ready);
        const std::vector<Action> original(4,{0,0});
        const auto schedules=BuildAssaultSchedules(state,original,60);
        CheckTiming(!schedules.empty() && FullAt(state,schedules,.5f)==&schedules.front()
            && FullAt(state,schedules,4.5f) && schedules.size()<=4,
            "a living reloading cannon opens an immediate complete-entry comparison before the next-impact schedule");
        CheckTiming(state.counters[0].blast.ready==12 && state.counters[1].blast.ready==0
            && state.plants[0].health==300 && state.plants[1].health==300,
            "using one cannon's current reload window neither consumes it nor invents a cooldown for another ready cannon");
        state.plants[0].health=0;
        CheckTiming(!FullAt(state,BuildAssaultSchedules(state,original,60),.5f),
            "a dead source cannot supply a fictitious current reload-window event");
    }
    {
        // 复用现有双火力完整跟进反事实；不改兵种、生命、输出或钱包来迎合新候选。
        Snapshot state;
        state.rows=1; state.houseX=0; state.budget=220; state.capacity=16;
        state.searchVersion=2; state.precisionReady=true; state.netEconomy=true; state.opponentWeight=1;
        Plant key; key.id=401; key.x=400; key.health=100000; key.dps=1000; key.assetValue=1000;
        Plant reserve; reserve.id=402; reserve.x=500; reserve.health=400; reserve.dps=100;
        reserve.reward=10; reserve.assetValue=100; state.plants={key,reserve};
        Option troop; troop.type=731; troop.cost=10;
        troop.unit.body.x=900; troop.unit.body.health=200; troop.unit.body.speed=40;
        troop.unit.body.purchaseCost=10; troop.unit.biteDps=50; state.options={troop};
        const std::vector<Action> full(16,{0,0});
        state.precisionTargetID=key.id;
        const auto schedules=BuildAssaultSchedules(state,full,60);
        const auto* delayed=FullAt(state,schedules,ColdStorageSkillRules::StrikeAimDuration+.5f);
        CheckTiming(delayed && delayed->size()==16,"post-clearance scheduling preserves the entire payable sixteen-member army");
        const auto result=EvaluateCandidate(state,InitialWeights,*delayed);
        CheckTiming(result.features[2]>0 && result.features[5]==220 && result.construction.precisionHits==1,
            "the existing complete followup earns its real breakthrough and pays the one shared wallet");
        CheckTiming(Evaluate(state,full)[2]==0,"entering before the same source removal still dies instead of borrowing future clearance");
        auto without=state; without.precisionTargetID=0;
        CheckTiming(Evaluate(without,*delayed)[2]==0,"timing alone cannot award the unpurchased source removal");
    }
    {
        auto state=TimingArena();
        state.pendingPrecisionID=state.plants[0].id; state.pendingPrecisionRemaining=8;
        state.counters[0].blast.committed=true; state.counters[0].blast.ready=10;
        state.counters[0].plantID=0;
        const std::vector<Action> original(4,{0,0});
        const auto schedules=BuildAssaultSchedules(state,original,60);
        const auto* before=FullAt(state,schedules,8.5f);
        const auto* after=FullAt(state,schedules,10.5f);
        CheckTiming(before && after,"already-paid aim and airborne impact both retain complete entry alternatives");
        ConstructionStats earlyStats,lateStats;
        const auto early=Evaluate(state,*before,&earlyStats),late=Evaluate(state,*after,&lateStats);
        CheckTiming(early[3]==0 && late[3]==32 && early[5]==32 && late[5]==32,
            "source clearance does not erase an airborne shell or charge pending precision again");
        CheckTiming(earlyStats.counterTrace.size()==1 && lateStats.counterTrace.size()==1
            && earlyStats.precisionHits==1 && lateStats.precisionHits==1,
            "the same committed impact resolves independently in both full worlds, including before later troops spawn");
    }
    {
        auto state=TimingArena();
        state.searchVersion=1; state.budget=220; state.capacity=16;
        const auto armed=EvaluateCandidate(state,InitialWeights,{});
        CheckTiming(armed.expandedForecast,
            "a payable larger army recognizes reusable cannon fire even when the living source has zero continuous dps");
        auto harmless=state; harmless.counters.clear();
        CheckTiming(!EvaluateCandidate(harmless,InitialWeights,{}).expandedForecast,
            "the same high-value harmless plant does not invent an armed-defense reason to extend the window");
        auto poor=state; poor.budget=63;
        CheckTiming(!EvaluateCandidate(poor,InitialWeights,{}).expandedForecast,
            "a reusable cannon cannot open unaffordable larger-army exploration below the existing cash requirement");
    }
    {
        auto state=TimingArena();
        // 备选落点保持同一source，不能把一门炮当成几门炮或多次装填。
        auto alternate=state.counters[0]; alternate.blast.x=905; state.counters.push_back(alternate);
        const std::vector<Action> original(4,{0,0});
        const auto schedules=BuildAssaultSchedules(state,original,60);
        const auto split=std::find_if(schedules.begin(),schedules.end(),[](const auto& plan) {
            return std::count_if(plan.begin(),plan.end(),[](const Action& a){return a.delay==0;})==1;
        });
        CheckTiming(split!=schedules.end(),"generic repeated members admit a small advance group and intact later main group");
        ConstructionStats stats;
        const auto earlyShot=Evaluate(state,*split,&stats);
        CheckTiming(stats.counterTrace.size()==1 && stats.counterTrace.front().at==4
            && earlyShot[3]==24 && earlyShot[5]==32,
            "a free reusable cannon can fire at one cheap member instead of granting it the paid-ash twenty-four-ice exemption");
        const auto result=EvaluateCandidate(state,InitialWeights,*split);
        CheckTiming(result.features[3]==0 && result.construction.counterTrace.size()==1
            && result.construction.counterTrace.front().at>=8.5f,
            "the same advance group cannot force that shot when a complete legal retained-cannon world defeats the later army");
        CheckTiming(ShouldConserveCapital(result,state.budget,0,0),
            "a staged army with no actual return still fails the existing capital gate rather than receiving a timing bonus");
        auto poor=state; poor.budget=31; poor.deploymentCapital=31;
        const auto repaired=EvaluateCandidate(poor,InitialWeights,*split);
        CheckTiming(repaired.actions.size()==3 && repaired.features[5]==24,
            "a later evaluation with a smaller wallet repairs the same proposal instead of borrowing future income");
        CheckTiming(state.counters[0].blast.ready==0 && state.counters[0].recharge==1000 && state.budget==32,
            "neither construction nor evaluation consumes the captured ready cannon or actual wallet");
        const std::vector<Action> tempting{{0,0},{0,0},{0,0},{0,34}};
        const auto immediate=Evaluate(state,tempting);
        const auto patient=EvaluateCandidate(state,InitialWeights,tempting);
        CheckTiming(immediate[3]==8 && patient.features[3]==0 && patient.counterHoldSeconds>=34
            && patient.construction.counterTrace.size()==1 && patient.construction.counterTrace.front().at>=38,
            "a ready reusable cannon may retain its whole shot for the later group rather than inevitably spending it within eight seconds");
        CheckTiming(patient.features[5]==32 && patient.construction.opponentAssets==600
            && patient.construction.paidCounterCasts==0,
            "retained cannon defense is one complete world with unchanged source asset, shared costs and no fictitious extra paid cast");
    }
    {
        // 留空位与即时开炮必须能联合：不能强迫玩家为了清一只低价兵，先把金盏周转格补满。
        auto state=TimingArena();
        state.playerSun=3500; state.playerIce=122; state.sunIceValue=100.0f/225;
        state.anticipateEconomy=true; state.opponentWeight=1;
        state.shop={{100,40,5},{225,100,10}};
        state.plants[0].x=282; state.plants[0].assetValue=40+500*state.sunIceValue;
        auto& shot=state.counters[0];
        shot.blast.x=1100; shot.recharge=34.83f;
        const auto reusable=shot;
        // 同一炮的合法备选爆点让留炮世界仍能追击；不能靠缺失远处爆点使它故意失效。
        for(int column=0;column<9;++column) {
            auto point=reusable; point.blast.x=282+80.0f*column;
            state.counters.push_back(point);
        }
        Plant producer;
        producer.id=51006; producer.column=7; producer.x=842; producer.health=300;
        producer.sunPerSecond=1.25f; producer.assetValue=10+50*state.sunIceValue;
        state.plants.push_back(producer);
        Construction flower;
        flower.plant=producer; flower.plant.id=0; flower.plant.column=6; flower.plant.x=762;
        flower.sunCost=50; flower.iceCost=10; flower.recharge=7.5f/2.2f; flower.firstSunDelay=5.55f;
        state.construction={flower};
        SunExchange exchange;
        exchange.sunGain=100; exchange.iceCost=10; exchange.recharge=25.0f/2.2f;
        exchange.cells={{{0,6}}}; state.exchanges={exchange};
        Counter cherry;
        cherry.source=1; cherry.cellRow=0; cherry.cellColumn=6;
        cherry.sunCost=150; cherry.iceCost=20; cherry.recharge=50.0f/2.2f;
        cherry.blast.x=762; cherry.blast.damage=1800; cherry.blast.reach.fill(-1); cherry.blast.reach[0]=115;
        state.counters.push_back(cherry);
        auto& unit=state.options[0];
        unit.cost=4; unit.unit.body.purchaseCost=4; unit.unit.playerRefund=3;
        unit.unit.body.x=1100; unit.unit.body.health=270; unit.unit.body.speed=20; unit.unit.biteDps=50;
        const std::vector<Action> probe{{0,0}};
        ConstructionStats rebuilt,retained;
        const auto immediate=Evaluate(state,probe,&rebuilt);
        const auto reserved=Evaluate(state,probe,&retained,0,0,0,true);
        CheckTiming(immediate[0]==0 && immediate[1]==0 && immediate[3]==0 && rebuilt.planted==1
            && reserved[0]==0 && reserved[1]==0 && reserved[3]==0 && retained.planted==0
            && retained.exchanges>rebuilt.exchanges && retained.opponentAssets>rebuilt.opponentAssets,
            "identical free kills can preserve the real exchange cell instead of forcing unnecessary construction");
        const auto idle=EvaluateCandidate(state,InitialWeights,{});
        const auto result=EvaluateCandidate(state,InitialWeights,probe);
        CheckTiming(result.construction.counterSpaceReserved && result.construction.planted==0
            && result.construction.paidCounterCasts==0 && result.construction.counterTrace.size()==1
            && result.construction.counterTrace.front().at==4 && result.features[0]==0 && result.features[1]==0
            && result.features[2]==0 && result.features[3]==0,
            "the complete opponent response combines reserved economy space with the already-ready free cannon");
        CheckTiming(result.construction.exchanges==idle.construction.exchanges
            && std::abs(result.opponentAssets-idle.opponentAssets-unit.unit.playerRefund)<.01f
            && result.opponentScore<=0 && result.score<idle.score,
            "a harmlessly cleared cheap probe gains no pressure from blocked farming and credits the actual death refund");
        CheckTiming(state.playerSun==3500 && state.playerIce==122 && state.plants[1].health==300
            && state.counters[0].blast.ready==0 && state.exchanges[0].ready==0,
            "joint response evaluation leaves the captured player wallet, producer and source cooldowns unchanged");
    }
    std::cout<<"Assault timing preserves complete members, real impacts, counter choice and shared-wallet gates\n";
}
