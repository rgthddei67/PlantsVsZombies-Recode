#include "Game/AI/ColdStorageFogFerryForecast.h"
#include "Game/AI/ColdStorageFormationSeeds.h"
#include "Game/AI/ColdStorageSearch.h"
#include "Game/Zombie/FogFerryRules.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace {
using namespace ColdStorageSearch;
void Check(bool ok,const char* message) {if(!ok) {std::cerr << message << std::endl;throw std::runtime_error(message);}}
bool Near(float a,float b) {return std::abs(a-b)<.01f;}
/** 静止数值夹具隔离传送事务；移动/收益由下方完整 Evaluate 反事实另验。 */
Unit Birth(int id,float x,bool ferry=false) {
    Unit u;u.id=id;u.body.x=x;u.body.row=0;u.body.health=u.maximumBody=ferry ? 2200.0f : 3000.0f;
    u.body.purchaseCost=u.body.value=ferry ? 40.0f : 16.0f;u.body.speed=0;
    if(ferry) u.ferry={true,true,false,5,733};
    return u;
}
}

/** 覆盖队列出生、照明/天气、乘客边界及共享付款候选，不把固定配方当作必选策略。 */
void RunColdStorageFogFerryForecastTests() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=1;s.gridLeft=242;s.columns=9;s.cellWidth=80;s.fogLeftColumn=2;
    std::array<float,54> light{};ConstructionStats stats;
    std::vector<Unit> units{Birth(10,920,true),Birth(20,920)};
    AdvanceFogFerries(s,0,4.75f,2,light,units,stats);
    Check(!units[0].ferry.winding && Near(units[1].body.x,920),"ferry must wait five seconds before starting");
    AdvanceFogFerries(s,4.75f,.25f,2,light,units,stats);
    Check(units[0].ferry.winding && units[0].ferry.blocksMovement && stats.ferryTransfers==0,"full windup must follow first wait");
    light[8]=1;
    AdvanceFogFerries(s,5,2,2,light,units,stats);
    Check(Near(units[0].ferry.remaining,1) && stats.ferryTransfers==0,"lamp must halve channel progress without disabling ferry");
    AdvanceFogFerries(s,7,2,2,light,units,stats);
    Check(stats.ferryPassengers==1 && Near(units[1].body.x,462) && Near(units[0].body.x,920)
        && Near(units[0].ferry.remaining,10),"fog edge ignores light holes and carrier stays in place");
    units.push_back(Birth(21,920));
    AdvanceFogFerries(s,9,10,9,light,units,stats);
    AdvanceFogFerries(s,19,4,9,light,units,stats);
    Check(stats.ferryTransfers==2 && Near(units[2].body.x,702) && Near(units[0].ferry.remaining,15),"repeat carries later arrivals; clear weather cooldown is fifteen");

    units={Birth(10,520,true),Birth(20,520)};units[0].ferry={true,true,true,.25f,733};
    light.fill(0);stats={};
    AdvanceFogFerries(s,0,.25f,9,light,units,stats);
    Check(Near(units[1].body.x,462),"clear-weather landing cannot pass the third column");
    units={Birth(10,1140,true),Birth(20,1140)};
    AdvanceFogFerries(s,0,8,2,light,units,stats);
    Check(!units[0].ferry.winding && !units[0].ferry.blocksMovement,"off-board ready ferry must keep walking");

    units={Birth(10,920,true),Birth(20,920)};units[0].body.spawnAt=8;
    AdvanceFogFerries(s,7,1,2,light,units,stats);
    Check(Near(units[0].ferry.remaining,5),"paid unborn ferry cannot consume first wait");
    units[0].body.spawnAt=0;units[0].body.stopped=3;
    AdvanceFogFerries(s,8,1,2,light,units,stats);
    Check(Near(units[0].ferry.remaining,5),"hard control pauses ferry timer");
    units[0].body.stopped=0;units[0].headless=true;
    AdvanceFogFerries(s,9,20,2,light,units,stats);
    Check(!units[0].ferry.enabled && Near(units[1].body.x,920),"headless ferry cannot release");

    units={Birth(100,920,true)};units[0].ferry.remaining=0;
    for(int i=0;i<17;++i) units.push_back(Birth(17-i,900));
    units[1].vehicleCrush=true;units[2].body.smashSeconds=2;units[3].body.blastAnchorOffset=30;
    units[3].body.eating=true;units[3].body.stopped=2;units[3].clock.winding=true;
    units[4].jammer=units[4].jammerChanneling=true;units[4].jammerRemaining=.5f;
    units.push_back(Birth(101,920,true));units.back().ferry.enabled=false;
    units.push_back(Birth(102,920));units.back().balloon.present=true;
    units.push_back(Birth(103,920));units.back().digger.present=true;
    units.push_back(Birth(104,920));units.back().body.row=1;
    units.push_back(Birth(105,920));units.back().body.spawnAt=100;
    units.push_back(Birth(106,920));units.back().ferryPassengerEligible=false;
    stats={};AdvanceFogFerries(s,0,.25f,2,light,units,stats);
    AdvanceFogFerries(s,.25f,2,2,light,units,stats);
    Check(stats.ferryPassengers==17,"transport has no twelve-passenger gameplay cap; vehicles and giants qualify");
    for(int i=1;i<=17;++i) Check(Near(units[i].body.x+units[i].body.blastAnchorOffset,462+(units[i].id-1)%5),"stable identity order and logical origins determine landing");
    for(size_t i=18;i<units.size();++i) Check(Near(units[i].body.x,920),"other ferries, air, underground, other rows and unborn passengers are excluded");
    Check(!units[3].body.eating && !units[3].clock.winding && Near(units[3].body.stopped,2)
        && Near(units[3].body.health,3000),"transport cancels contact/action without clearing control or healing");

    Check(!units[4].jammerChanneling && Near(units[4].jammerReboot,5) && Near(units[4].jammerRemaining,4),"transport restarts uncommitted jammer without granting an early release");

    // 完整推演必须在活体和延迟出生路径真正使用能力，而非只有直接单步测试有效。
    s.current={Birth(1,920,true),Birth(2,920)};s.current[1].body.speed=8;
    ConstructionStats enabled,disabled;const auto with=Evaluate(s,{},&enabled);
    s.current[0].ferry.enabled=false;const auto without=Evaluate(s,{},&disabled);
    Check(enabled.ferryTransfers>0 && disabled.ferryTransfers==0 && with!=without,"ferry must change full combat forecast, not merely diagnostics");
    s.current[0].ferry.enabled=true;s.current[0].body.spawnAt=12;s.current[1].body.spawnAt=12;
    Evaluate(s,{},&enabled);Check(enabled.ferryPassengers>0,"pending paid births must reach forecast transport");

    // 搜索可以因已有乘客的突破收益自主采购，而不是只在手填购物车里看见能力。
    Snapshot reinforcement;reinforcement.rows=1;reinforcement.gridLeft=242;reinforcement.fogLeftColumn=2;
    reinforcement.budget=40;reinforcement.capacity=1;reinforcement.netEconomy=true;
    reinforcement.current={Birth(1,960)};reinforcement.current[0].body.speed=8;
    Option ferryOption;ferryOption.type=62;ferryOption.row=0;ferryOption.cost=40;
    ferryOption.unit=Birth(0,1140,true);ferryOption.unit.body.speed=13.56f;
    reinforcement.options.push_back(ferryOption);
    const Weights breakthrough{4,1.5f,148,.3f,3.4f,-3.4f,-.27f,1.7f};
    const auto selected=Search(reinforcement,breakthrough,42);
    if(selected.actions.empty() || selected.construction.ferryPassengers==0) {
        ConstructionStats debugStats;
        const auto debug=Evaluate(reinforcement,{{0,0}},&debugStats);
        std::cerr << "ferry search: actions=" << selected.actions.size() << " transported=" << selected.construction.ferryPassengers
            << " candidate transports=" << debugStats.ferryPassengers << " raw=";
        for(float f:debug) std::cerr << f << ',';
        std::cerr << " score=" << selected.score << " evaluated=" << selected.evaluated << std::endl;
    }
    Check(selected.actions.size()==1 && selected.actions[0].option==0 && selected.construction.ferryPassengers>0,
        "ordinary search must be able to select a funded ferry reinforcement from its transport payoff");

    // 伴舞/裂隙/小鬼使用原队列之外的预留槽，运输不能用其索引访问仅属原队的投掷计时数组。
    s.current={Birth(1,920,true),Birth(2,840)};
    s.current[1].dance.leader=true;s.current[1].dance.remaining=.25f;s.current[1].dance.snapSeconds=.25f;
    s.dancerBackup=Birth(0,0);s.dancerBackup.dance.backup=true;s.dancerBackup.dance.phase=DancerRules::Forecast::Phase::HOLD;
    Evaluate(s,{},&enabled);
    Check(enabled.dancerSummons>=2 && enabled.ferryPassengers>=2,"summoned backup slots must be safe transport passengers");

    s.current.clear();s.netEconomy=true;s.budget=500;s.capacity=24;
    for(int role=0;role<4;++role) {
        Option o;o.type=62000+role;o.row=0;o.cost=role==0 ? 40 : 16;o.unit=Birth(role+1,1140,role==0);
        o.unit.body.speed=role==0 ? 12 : role==1 ? 8 : 25;
        if(role==1) {o.unit.body.smashSeconds=2;o.unit.body.health=6000;}
        if(role==2) o.unit.vehicleCrush=true;
        if(role==3) o.unit.pressure=true;
        s.options.push_back(o);
    }
    bool heavy=false,vehicle=false,shooter=false,repeated=false,delayed=false;
    const auto plans=BuildExperiencedFormations(s,24,500);
    for(const auto& plan:plans) {
        int price=0,carriers=0,cargo=0;float last=0,first=1000;
        for(const auto& a:plan) {price+=s.options[a.option].cost;carriers+=a.option==0;}
        Check(price<=500 && plan.size()<=24,"experienced ferry recipes share ordinary wallet/capacity");
        if(!carriers) continue;
        for(const auto& a:plan) {
            heavy|=a.option==1;vehicle|=a.option==2;shooter|=a.option==3;
            delayed|=a.option==0 && a.delay>0;
            if(a.option!=0) {++cargo;first=std::min(first,a.delay);last=std::max(last,a.delay);}
        }
        repeated|=cargo>=4 && last>first;
    }
    Check(heavy && vehicle && shooter && repeated && delayed,"seed portfolio must retain heavy, vehicle, shooter and staggered repeat ferry proposals");
    s.options.erase(s.options.begin());
    for(const auto& plan:BuildExperiencedFormations(s,24,500)) for(const auto& a:plan)
        Check(a.option>=0 && a.option<static_cast<int>(s.options.size()),"locked ferry cannot create an illegal recipe");
}
