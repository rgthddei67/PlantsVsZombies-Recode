#include "Game/AI/ColdStorageSearch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace {
using namespace ColdStorageSearch;

/** 固定案消融的事务断言；不依赖墙钟预算、真实随机源或自由搜索必选阵容。 */
void Check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED jack ablation: "<<message<<'\n';std::exit(1);}
}
bool Named(const CombatForecastTrace& event,const char* name) {return std::strcmp(event.event,name)==0;}

/** 静止、无啃咬的明确能力画像，隔离择点与炮击资格。 */
Unit Body(int id,float x=1000) {
    Unit unit;unit.id=id;unit.body.row=0;unit.body.x=x;unit.body.speed=0;
    unit.body.health=unit.maximumBody=900;unit.body.purchaseCost=36;unit.biteDps=0;
    unit.body.boundsOffset=-25;unit.body.boundsWidth=50;unit.boundsY=-50;unit.boundsHeight=100;
    return unit;
}
Unit Jack(int id) {
    auto unit=Body(id);unit.jack.present=unit.jack.elite=true;
    unit.jack.remaining=0;unit.jack.stopHealth=300;return unit;
}

/** 不重叠的窄矩形与格中心相符，默认高耐久令并列集合在各次投掷间保持。 */
Plant Target(int id,int column,float health=100000,float value=100) {
    Plant plant;plant.id=id;plant.row=0;plant.column=column;plant.x=160+(column+.5f)*80;
    plant.y=100;plant.health=plant.maximumHealth=health;plant.targetValue=value;plant.edible=false;
    plant.boundsX=plant.boundsY=-10;plant.boundsWidth=plant.boundsHeight=20;return plant;
}
Snapshot Arena() {
    Snapshot state;state.rows=1;state.houseX=-10000;state.traceCombat=true;
    state.rowY={100,200,300,400,500,600};return state;
}
struct Result {Weights features;ConstructionStats stats;};
Result Run(const Snapshot& state,const std::vector<Action>& actions={}) {
    Result result;result.features=Evaluate(state,actions,&result.stats);return result;
}
std::vector<CombatForecastTrace> Events(const ConstructionStats& stats,const char* name) {
    std::vector<CombatForecastTrace> result;
    for(const auto& event:stats.combatTrace) if(Named(event,name)) result.push_back(event);
    return result;
}
std::map<int,std::vector<int>> Choices(const ConstructionStats& stats) {
    std::map<int,std::vector<int>> result;
    for(const auto& event:stats.combatTrace) if(Named(event,"jack_throw")) result[event.sourceID].push_back(event.targetColumn);
    return result;
}
std::set<int> FirstCells(const ConstructionStats& stats) {
    std::set<int> cells;for(const auto& [id,choices]:Choices(stats)) if(!choices.empty()) cells.insert(choices.front());return cells;
}

/** 只有一个合法免费炮来源，四秒到爆炸、后两秒独立飞行，长装填隔离单次交易。 */
Snapshot CannonArena() {
    auto state=Arena();state.plants={Target(901,1,300,0)};state.current={Body(21,1140)};
    Counter cannon;cannon.source=0;cannon.plantID=901;cannon.consumesPlant=false;
    cannon.windup=4;cannon.flightSeconds=2;cannon.recharge=1000;
    cannon.blast.x=1100;cannon.blast.damage=1800;cannon.blast.usesObjectX=true;
    cannon.blast.reach.fill(-1);cannon.blast.reach[0]=115;cannon.ladderClearRow=0;
    state.counters={cannon};return state;
}

/** 已承诺炮击可单独覆盖一个对象，不要求未来仍有屏内目标。 */
Counter Pending(float x,float at,float reach=0) {
    Counter shot;shot.source=1;shot.consumesPlant=false;shot.blast.committed=true;
    shot.blast.ready=at;shot.blast.x=x;shot.blast.damage=1800;shot.blast.usesObjectX=true;
    shot.blast.reach.fill(-1);shot.blast.reach[0]=reach;return shot;
}
}

/** 手动诊断模式只改变指定机制；默认预测、输入快照、在途独立性与场外连带伤害保持。 */
void RunColdStorageJackAblationTests() {
    {
        auto state=Arena();state.plants={Target(101,0),Target(102,3)};
        for(int id=1;id<=24;++id) state.current.push_back(Jack(id));
        Check(state.diagnosticJackTargetMode==0 && !state.diagnosticVisibleCounterWitness,"new diagnostic switches default to legacy behavior");
        const auto legacy=Run(state);
        auto explicitLegacy=state;explicitLegacy.diagnosticJackTargetMode=0;explicitLegacy.diagnosticVisibleCounterWitness=false;
        const auto repeated=Run(explicitLegacy);
        Check(legacy.features==repeated.features && Choices(legacy.stats)==Choices(repeated.stats)
            && FirstCells(legacy.stats)==std::set<int>{0},"mode zero keeps exact full-value scoring and smallest-cell ties");
        state.diagnosticJackTargetMode=1;
        const auto sampled=Run(state),again=Run(state);
        Check(sampled.features==again.features && Choices(sampled.stats)==Choices(again.stats),"independent ties repeat without consuming game random state");
        Check(FirstCells(sampled.stats)==std::set<int>({0,3}),"identical clones do not all share the same tied first target");
        auto reordered=state;std::reverse(reordered.plants.begin(),reordered.plants.end());
        Check(Choices(Run(reordered).stats)==Choices(sampled.stats),"plant input order cannot change a tied diagnostic sample");
        auto delayed=state;for(auto& unit:delayed.current) unit.body.spawnAt=.5f;
        const auto delayedChoices=Choices(Run(delayed).stats),originalChoices=Choices(sampled.stats);
        for(const auto& [id,choices]:delayedChoices) {
            const auto& original=originalChoices.at(id);
            Check(!choices.empty() && choices.size()<=original.size()
                && std::equal(choices.begin(),choices.end(),original.begin(),original.begin()+choices.size()),
                "timing shifts preserve the common per-owner throw samples");
        }
        Check(state.current[0].jack.remaining==0 && state.current[0].body.health==900
            && state.plants[0].health==100000 && state.plants[1].health==100000,
            "forecast ties and damage leave captured timers, health and target lists unchanged");
    }
    {
        auto state=Arena();state.diagnosticJackTargetMode=1;state.plants={Target(101,0),Target(102,3,100000,100.0004f)};
        for(int id=1;id<=24;++id) state.current.push_back(Jack(id));
        Check(FirstCells(Run(state).stats)==std::set<int>({0,3}),"the entity-sized 0.001 epsilon includes near-equal scores");
        state.plants[1].targetValue=101;
        Check(FirstCells(Run(state).stats)==std::set<int>{3},"tie sampling never replaces a genuinely higher full-value candidate");
        state.plants.erase(state.plants.begin());
        Check(FirstCells(Run(state).stats)==std::set<int>{3},"one legal cell always remains the selected target");
        state.plants.clear();Check(Run(state).stats.jackThrows==0,"an empty target set cannot create a throw");
    }
    {
        auto state=Arena();state.current={Jack(1)};state.plants={Target(101,0,6000,600),Target(102,3,300,100)};
        Check(Choices(Run(state).stats).at(1).front()==0,"legacy favors the larger full asset value");
        state.diagnosticJackTargetMode=2;
        Check(Choices(Run(state).stats).at(1).front()==3,"mode two uses direct damage fraction rather than pretending to run full MC");
        state.plants[1].health=1;
        Check(Choices(Run(state).stats).at(1).front()==0,"marginal proxy caps overkill and invents no last-hit bonus");
    }
    {
        auto state=Arena();state.diagnosticJackTargetMode=1;state.plants={Target(101,0),Target(102,3)};
        Option option;option.unit=Jack(0);option.cost=18;state.options={option};state.budget=1000;state.capacity=24;
        std::vector<Action> actions(24,{0,0});
        const auto result=Run(state,actions);
        Check(Choices(result.stats).size()==24 && FirstCells(result.stats)==std::set<int>({0,3}),
            "newly purchased clones receive distinct stable numerical identities for their tie samples");
        Check(state.options[0].unit.id==0 && state.budget==1000 && state.capacity==24,"diagnostic identities do not mutate the paid snapshot or budget");
    }
    {
        auto state=Arena();state.diagnosticJackTargetMode=1;state.current={Jack(1)};
        state.plants={Target(101,0),Target(102,3)};
        const auto normal=Choices(Run(state).stats).at(1);
        state.counters={Pending(1000,.5f)};
        TemporalAnchor anchor;anchor.at=2;anchor.targets={{0,state.current[0]}};state.temporalAnchors={anchor};
        const auto restored=Choices(Run(state).stats).at(1);
        Check(restored.size()>1 && restored.size()<=normal.size()
            && std::equal(restored.begin(),restored.end(),normal.begin(),normal.begin()+restored.size()),
            "ash removal and clock revival cannot roll back the successful per-owner throw sample ordinal");
    }
    {
        auto state=CannonArena();const auto legacy=Run(state);
        Check(Events(legacy.stats,"counter_lock").size()==1 && legacy.features[3]==0,"legacy can fire at an offscreen-only cluster");
        state.diagnosticVisibleCounterWitness=true;const auto witnessed=Run(state);
        Check(Events(witnessed.stats,"counter_lock").empty() && witnessed.features[3]==36,
            "offscreen-only actors cannot witness a new free repeated counter in the diagnostic response");
        Check(state.current[0].body.health==900 && state.plants[0].health==300 && state.counters[0].blast.ready==0,
            "the diagnostic response leaves source, target and cooldown input unchanged");
    }
    {
        auto state=CannonArena();state.diagnosticVisibleCounterWitness=true;
        state.current={Body(21,1080),Body(22,1140)};
        const auto result=Run(state);const auto impact=Events(result.stats,"counter_impact");
        Check(Events(result.stats,"counter_lock").size()==1 && impact.size()==1 && impact[0].targets.size()==2 && result.features[3]==0,
            "one visible witness still allows valuation and damage to every offscreen victim in the same blast");
        state.current={Body(21,1140)};state.current[0].body.blastAnchorOffset=-60;
        Check(Events(Run(state).stats,"counter_lock").size()==1,"witness visibility uses the object origin rather than an offscreen collider center");
        state.current[0].body.x=1080;state.current[0].body.blastAnchorOffset=60;
        Check(Events(Run(state).stats,"counter_lock").empty(),"an onscreen collider center cannot witness an offscreen object origin");
        state.current[0].body.x=1100.49f;state.current[0].body.blastAnchorOffset=0;
        Check(Events(Run(state).stats,"counter_lock").size()==1,"object positions use the script's rounded inclusive right boundary");
        state.current[0].body.x=1100.51f;
        Check(Events(Run(state).stats,"counter_lock").empty(),"rounding beyond the right boundary does not create a visible witness");
    }
    {
        auto state=CannonArena();state.diagnosticVisibleCounterWitness=true;
        auto visible=Body(22,1080);visible.body.spawnAt=100;state.current.push_back(visible);
        Check(Events(Run(state).stats,"counter_lock").empty(),"an unborn visible-position queue entry is not a witness");
        state.current[1].body.spawnAt=0;state.current[1].groundHazard=false;
        Check(Events(Run(state).stats,"counter_lock").empty(),"an ineligible visible actor cannot witness a cannon target");
        state.current[1].groundHazard=true;state.current[1].body.x=700;
        Check(Events(Run(state).stats,"counter_lock").empty(),"a visible actor outside this blast does not authorize blind firing elsewhere");
        state.current[1].body.x=1080;state.counters.push_back(Pending(1080,4));
        Check(Events(Run(state).stats,"counter_lock").empty(),"a visible actor already removed by pending ash cannot witness a redundant new shot");
        state.counters.resize(1);state.weatherStation=true;
        state.station.controls[WeatherStationRules::FOG].value=4;state.stationFogAlpha.fill(255);
        Check(Events(Run(state).stats,"counter_lock").empty(),"fog-obscured actors cannot witness the diagnostic visible response");
    }
    {
        auto state=CannonArena();state.diagnosticVisibleCounterWitness=true;state.counters={Pending(1100,1,115)};
        const auto result=Run(state);
        Check(Events(result.stats,"counter_impact").size()==1 && result.features[3]==0,
            "committed shells retain offscreen damage without any current visible witness");
        state.counters[0].plantID=901;state.counters[0].blast.ready=4;state.counters[0].flightSeconds=2;
        state.pendingPrecisionID=901;state.pendingPrecisionRemaining=3;
        Check(Events(Run(state).stats,"counter_impact").size()==1,"source death after release cannot cancel a committed independent shell");
        state.pendingPrecisionRemaining=1;
        Check(Events(Run(state).stats,"counter_impact").empty(),"source death before release still cancels its pending shot");
    }
    {
        auto state=CannonArena();state.diagnosticVisibleCounterWitness=true;state.playerSun=100;
        state.counters[0].consumesPlant=true;state.counters[0].sunCost=25;
        Check(Events(Run(state).stats,"counter_lock").size()==1,"the witness diagnostic does not remove an ordinary legal paid ash response");
    }
    std::cout<<"Diagnostic jack ties, marginal proxy and visible cannon witnesses passed\n";
}
