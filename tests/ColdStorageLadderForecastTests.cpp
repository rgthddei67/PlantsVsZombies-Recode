#include "Game/AI/ColdStorageSearch.h"
#include "Game/AI/ColdStorageLadderForecast.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
/** 合成同格南瓜/坚果高墙，不靠杀墙打开后排；只接入真实共享梯事务。 */
ColdStorageSearch::Snapshot Arena()
{
    using namespace ColdStorageSearch;
    Snapshot snapshot;
    snapshot.rows=1; snapshot.houseX=-10000;
    Plant nut;
    nut.id=10; nut.x=500; nut.row=0; nut.column=4;
    nut.health=8000; nut.ladderTarget=true;
    auto pumpkin=nut;
    pumpkin.id=11; pumpkin.layer=2; pumpkin.health=4000; pumpkin.pumpkin=true;
    Plant rear;
    rear.id=12; rear.x=300; rear.column=1; rear.health=100; rear.reward=20;
    snapshot.plants={nut,pumpkin,rear};
    return snapshot;
}

/** 明确出生携梯、护盾和两种稳态步速；纯测试不用资源或正式随机数。 */
ColdStorageSearch::Unit Builder()
{
    using namespace ColdStorageSearch;
    Unit unit;
    unit.id=1; unit.body.x=555; unit.body.speed=40; unit.body.health=1000;
    unit.maximumBody=500; unit.maximumShield=unit.shieldHealth=500;
    unit.body.purchaseCost=20; unit.temporalStopHealth=unit.ladder.stopHealth=166;
    unit.magneticLayer=2;
    unit.ladder.present=true; unit.ladder.placementSeconds=2; unit.ladder.normalSpeed=14;
    unit.ladder.carryingSpeed=40;
    unit.ladderClimb.eligible=true; unit.ladderClimb.horizontalBoost=LadderRules::HorizontalBoost;
    return unit;
}

/** 普通支持攀梯的后续步兵，没有携梯技能或磁性装备。 */
ColdStorageSearch::Unit Follower(float delay=0)
{
    auto unit=Builder();
    unit.id=2; unit.body.x=610; unit.body.speed=20; unit.body.spawnAt=delay;
    unit.body.health=unit.maximumBody=500; unit.maximumShield=unit.shieldHealth=0;
    unit.ladder.present=false; unit.magneticLayer=0;
    return unit;
}

/** 失败即打印断言名称，不把最终分数当作已提交事务的替代证据。 */
void Check(bool condition,const char* text)
{
    if(!condition) {std::cerr<<"FAILED ladder forecast: "<<text<<'\n';std::exit(1);}
}
}

/** 放梯/共享攀爬、动作取消、世界独立清理及自由搜索的反事实契约。 */
void RunColdStorageLadderForecastTests()
{
    using namespace ColdStorageSearch;
    using BuildPhase=LadderRules::Builder::Phase;
    using ClimbPhase=LadderRules::Climb::Phase;
    {
        auto snapshot=Arena(); snapshot.current={Builder(),Follower(3)};
        ConstructionStats stats;
        const auto shared=Evaluate(snapshot,{},&stats);
        Check(shared[0]==20 && stats.ladderPlaced==1 && stats.ladderClimbs==2,
            "one completed ladder is shared across both pumpkin and nut layers by builder and follower");
        Check(shared[6]==0,"consuming a carried ladder is no invented ash injury or currency reward");
        auto blocked=snapshot; blocked.current[0].ladder.present=false;
        Check(Evaluate(blocked,{},&stats)[0]==0 && stats.ladderPlaced==0 && stats.ladderClimbs==0,
            "the same ordinary formation cannot bypass a high wall without an actual shared ladder");
        snapshot.current[1].ladderClimb.eligible=false;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderClimbs==1,"a non-supporting follower cannot borrow another zombie's ladder path");
        Check(snapshot.current[0].shieldHealth==500 && snapshot.current[0].ladder.phase==BuildPhase::CARRYING,
            "forecasting cannot consume captured live shields or advance actual ladder state");
    }
    {
        auto snapshot=Arena(); auto unit=Builder();
        unit.ladderClimb.phase=ClimbPhase::CLIMBING; unit.ladderClimb.usedColumn=4;
        std::vector<LadderRules::Cell> ladders{{0,4}};
        ConstructionStats stats; int contact=0;
        const float start=unit.body.x;
        AdvanceLadderAction(snapshot,0,unit,contact,snapshot.plants,ladders,1,1,1,1,false,55,stats);
        Check(unit.ladderClimb.phase==ClimbPhase::CLIMBING && unit.ladderClimb.altitude==80,
            "climbing spends actual vertical action time rather than teleporting across all walls");
        AdvanceLadderAction(snapshot,1,unit,contact,snapshot.plants,ladders,.125f,.125f,1,1,false,55,stats);
        Check(unit.ladderClimb.phase==ClimbPhase::FALLING && unit.ladderClimb.altitude==90,
            "the ladder top transitions into a separately timed landing");
        AdvanceLadderAction(snapshot,1.125f,unit,contact,snapshot.plants,ladders,.9f,.9f,1,1,false,55,stats);
        Check(unit.ladderClimb.phase==ClimbPhase::NONE && unit.ladderClimb.altitude==0
            && start-unit.body.x<150,"landing completes after its real duration with bounded horizontal movement");
    }
    {
        auto snapshot=Arena(); auto unit=Builder();
        unit.ladder.phase=BuildPhase::PLACING; unit.ladder.row=0; unit.ladder.column=4; unit.ladder.remaining=1;
        snapshot.current={unit}; snapshot.plants[1].health=0;
        ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==1,"a lost original pumpkin does not cancel placement while its nut still supports the same cell");
        snapshot.plants[0].health=0;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==0,"placement cannot create a ladder after all current supporting layers disappear");
        snapshot=Arena(); snapshot.current={unit}; snapshot.current[0].body.stopped=1000;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==0,"hard control cannot finish an otherwise valid pending placement");
    }
    {
        auto snapshot=Arena(); snapshot.current={Builder()};
        snapshot.current[0].body.health=500;
        snapshot.current[0].shieldHealth=0;
        ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==0,"a shield broken before commitment cancels the carrying ability");
        snapshot.current={Builder()}; snapshot.current[0].body.health=600;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==0,"a headless builder cannot submit a new ladder through its surviving shield");
        Counter fatal; fatal.blast.committed=true; fatal.blast.ready=1;
        fatal.blast.x=555; fatal.blast.damage=1800; fatal.blast.reach.fill(1000);
        snapshot.current={Builder()}; snapshot.counters={fatal};
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==0,"source death during the placement windup cancels the uncommitted world object");
        snapshot.current={Builder(),Follower(8)}; snapshot.counters[0].blast.ready=4;
        const auto late=Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==1 && stats.ladderClimbs>=2 && late[0]==20,
            "a completed ladder persists after its builder dies and remains usable by later paid arrivals");
    }
    {
        auto snapshot=Arena(); snapshot.current={Builder()};
        snapshot.current[0].ladder.phase=BuildPhase::PLACING;
        snapshot.current[0].ladder.row=0; snapshot.current[0].ladder.column=4; snapshot.current[0].ladder.remaining=1;
        Plant magnet; magnet.id=20; magnet.x=600; magnet.column=5; magnet.health=10000; magnet.edible=false;
        magnet.magnetRadius=magnet.magnetEatingRadius=1000; magnet.magnetRecharge=1000;
        snapshot.plants.push_back(magnet);
        ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        Check(stats.magneticExtractions==1 && stats.ladderPlaced==0,
            "magnetic carried-shield extraction cancels pending placement instead of creating a free ladder");
        snapshot.current={Follower()}; snapshot.ladders={{0,4}};
        Check(Evaluate(snapshot,{},&stats)[0]==0 && stats.ladderRemoved==1,
            "without nearby metal equipment a charged magnet removes the actual shared scene ladder");
        auto metal=Follower(1000); metal.body.spawnAt=0; metal.body.x=900;
        metal.helmHealth=100; metal.body.health+=100; metal.magneticLayer=1;
        snapshot.current.push_back(metal);
        Check(Evaluate(snapshot,{},&stats)[0]==20 && stats.ladderRemoved==0 && stats.magneticExtractions==1,
            "an eligible zombie item consumes the same magnet cycle before fallback scene-ladder extraction");
    }
    {
        auto snapshot=Arena(); auto follower=Follower(); follower.body.x=800;
        snapshot.current={follower}; snapshot.ladders={{0,4}};
        Counter ash; ash.blast.committed=true; ash.blast.ready=1;
        ash.blast.x=520; ash.blast.damage=1800; ash.blast.reach.fill(-1); ash.blast.reach[0]=0;
        ash.ladderClearRow=0; ash.ladderClearRadius=1;
        snapshot.counters={ash}; ConstructionStats stats;
        Check(Evaluate(snapshot,{},&stats)[0]==0 && stats.ladderRemoved==1,
            "a formal grid blast clears ladders even when it misses every distant zombie");
        std::vector<LadderRules::Cell> ladders{{0,4},{0,1}};
        std::vector<unsigned char> living(snapshot.plants.size(),1);
        snapshot.plants[1].health=0;
        stats={}; PruneDeadLadderHosts(snapshot.plants,living,ladders,stats);
        Check(ladders.size()==1 && ladders[0].column==1 && stats.ladderRemoved==1,
            "an actual dying layer removes its own cell ladder without deleting unrelated world ladders");
    }
    {
        auto snapshot=Arena(); snapshot.plants.resize(1);
        auto second=snapshot.plants[0]; second.id=13; second.column=1; second.x=280;
        snapshot.plants.push_back(second);
        snapshot.current={Builder()}; ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==1,"a normal builder consumes its ladder after the first completed placement");
        snapshot.current[0].ladder.retain=true;
        Evaluate(snapshot,{},&stats);
        Check(stats.ladderPlaced==2,"an already acquired elite retention ability can place another real ladder while its shield survives");
    }
    {
        auto snapshot=Arena(); auto unit=Builder();
        unit.ladder.phase=BuildPhase::NORMAL; unit.body.speed=14;
        unit.body.health=500; unit.shieldHealth=0; unit.magneticLayer=0;
        snapshot.current={unit};
        TemporalAnchor rewind; rewind.at=1;
        TemporalTarget target; target.unit=0; target.saved=Builder(); rewind.targets={target};
        snapshot.temporalAnchors={rewind}; ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        Check(stats.clockRewinds==1 && stats.ladderPlaced==0,
            "living core shield restoration cannot reverse an already consumed placement ability phase");
        snapshot.current[0].body.health=0;
        Evaluate(snapshot,{},&stats);
        Check(stats.clockRevivals==1 && stats.ladderPlaced==1,
            "a new revived ladder entity begins carrying again when the saved core restores its real shield");
        snapshot.temporalAnchors[0].targets[0].restoreShield=false;
        Evaluate(snapshot,{},&stats);
        Check(stats.clockRevivals==1 && stats.ladderPlaced==0,
            "magnetically extracted shields cannot be restored to revive a free placement ability");
    }
    {
        auto snapshot=Arena(); Option option;
        option.type=95001; option.cost=20; option.unit=Builder(); option.unit.id=0;
        snapshot.options={option}; snapshot.budget=20; snapshot.capacity=1; snapshot.netEconomy=true;
        const Weights benefit{3,0,120,0,1,-1,0,0};
        Check(ValidWeights(benefit),"shared ladder discovery uses legal bounded tactical weights");
        const auto found=Search(snapshot,benefit,19);
        Check(found.features[0]==20 && found.construction.ladderPlaced==1,
            "free search can discover actual rear destruction through a numeric ladder ability without a fixed unit recipe");
        snapshot.options[0].type=97011;
        Check(Search(snapshot,benefit,19).features[0]==20,"renaming the legal type preserves the discovered ladder route");
        snapshot.options[0].unit.ladder.present=false;
        Check(Search(snapshot,benefit,19).actions.empty(),"without placement the same payable body cannot invent a bypass purchase");
    }
    std::cout<<"Ladder placement, shared climb and independent world counterfactuals passed\n";
}
