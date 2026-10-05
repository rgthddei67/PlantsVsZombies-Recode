#include "Game/AI/ColdStorageSearch.h"
#include "Game/Zombie/BalloonRules.h"
#include "Game/Zombie/JackBoxRules.h"
#include "Game/Plant/ThunderFlowerRules.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
/** 固定合法行高并隔离建筑、资金反制及进屋收益；各用例显式打开所需事务。 */
ColdStorageSearch::Snapshot Arena()
{
    ColdStorageSearch::Snapshot snapshot;
    snapshot.houseX=-10000;
    snapshot.rowY={100,200,300,400,500,600};
    return snapshot;
}

/** 静止且不啃食的单体夹具，只保留本次测试所需的独立能力。 */
ColdStorageSearch::Unit Body(int id,float health,float x,float cost)
{
    ColdStorageSearch::Unit unit;
    unit.id=id;
    unit.body.health=health;
    unit.maximumBody=health;
    unit.body.x=x;
    unit.body.speed=0;
    unit.body.purchaseCost=cost;
    unit.body.boundsOffset=-25;
    unit.body.boundsWidth=50;
    unit.boundsY=-50;
    unit.boundsHeight=100;
    unit.biteDps=0;
    return unit;
}

/** 普通开盒或精英投盒来源，倒计时由用例给定而非依赖正式随机源。 */
ColdStorageSearch::Unit Jack(bool elite=false,float x=700)
{
    auto unit=Body(1,elite ? 900 : 500,x,elite ? 24 : 12);
    unit.jack.present=true;
    unit.jack.elite=elite;
    unit.jack.remaining=1;
    unit.jack.release=.5f;
    unit.jack.stopHealth=static_cast<int>(unit.maximumBody)/3;
    unit.magneticLayer=elite ? 0 : 3;
    return unit;
}

/** 飞行夹具显式保留20点额外层；基础漂移可置零以隔离吹飞或击落事务。 */
ColdStorageSearch::Unit Balloon(float flightSpeed=0,float x=900)
{
    auto unit=Body(1,270+BalloonRules::Health,x,4);
    unit.maximumBody=270;
    unit.balloon.present=true;
    unit.balloon.health=BalloonRules::Health;
    unit.balloon.popDuration=.5f;
    unit.balloon.flightSpeed=flightSpeed;
    unit.balloon.walkSpeed=0;
    unit.groundHazard=false;
    return unit;
}

/** 使用实际格中心附近的矩形；reward可等于生命以把伤害特征投影成直观生命点。 */
ColdStorageSearch::Plant FixturePlant(int id,float x,float health,float reward,int row=0,int column=0)
{
    ColdStorageSearch::Plant plant;
    plant.id=id;
    plant.x=x;
    plant.y=100+row*100;
    plant.row=row;
    plant.column=column;
    plant.health=health;
    plant.maximumHealth=health;
    plant.reward=reward;
    plant.edible=false;
    plant.targetValue=100;
    return plant;
}

/** 已提交同行灰烬；不借玩家钱包或虚构可用卡槽。 */
ColdStorageSearch::Counter Ash(float x,float time,float damage)
{
    ColdStorageSearch::Counter counter;
    counter.blast.committed=true;
    counter.blast.x=x;
    counter.blast.ready=time;
    counter.blast.damage=damage;
    counter.blast.reach.fill(-1);
    counter.blast.reach[0]=0;
    return counter;
}
}

/** 只验证能力反事实与已提交事务，不对自由搜索的购买编队提出必选断言。 */
void RunColdStorageJackBalloonForecastTests()
{
    using namespace ColdStorageSearch;
    const auto check=[](bool condition,const char* message) {
        if(!condition) { std::cerr<<"FAILED jack/balloon forecast: "<<message<<'\n'; std::exit(1); }
    };
    const auto near=[](float left,float right) { return std::abs(left-right)<.001f; };
    int cases=0;
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Jack()};
        snapshot.plants={FixturePlant(10,700,100,5),FixturePlant(11,700,100,7,1),FixturePlant(12,950,100,11)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackExplosions==1 && result[0]==12 && result[3]==0,
            "ordinary timed explosion clears its circular plant range and consumes its own body once");
        check(snapshot.current[0].body.health==500 && snapshot.current[0].jack.remaining==1
            && snapshot.plants[0].health==100,"forecast cannot mutate captured live health or the actual pop timer");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack();
        source.jack.phase=JackBoxRules::Forecast::Phase::POPPING; source.jack.releaseRemaining=1;
        snapshot.current={source};
        auto gun=FixturePlant(10,700,100,5); gun.dps=100000;
        snapshot.plants={gun}; snapshot.counters={Ash(700,0,499)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackExplosions==1 && result[0]==5,
            "opening animation ignores ordinary damage and sublethal ash without aborting its explosion");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack();
        source.jack.phase=JackBoxRules::Forecast::Phase::POPPING; source.jack.releaseRemaining=1;
        snapshot.current={source}; snapshot.plants={FixturePlant(10,700,100,5)};
        snapshot.counters={Ash(700,0,500)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackExplosions==0 && result[0]==0 && result[3]==0,
            "ash reaching actual body health kills an opening jack before the uncommitted explosion");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack();
        source.jack.phase=JackBoxRules::Forecast::Phase::DISARMED; source.jack.remaining=0;
        source.magneticLayer=0;
        snapshot.current={source}; snapshot.plants={FixturePlant(10,700,100,5)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackExplosions==0 && result[0]==0 && result[3]==12,
            "a lost box cannot start another explosion or erase its living disarmed owner");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack(false,650); source.jack.remaining=2;
        auto bucket=Body(2,1370,550,8); bucket.maximumBody=270;
        bucket.helmHealth=bucket.maximumHelm=1100; bucket.magneticLayer=1;
        snapshot.current={source,bucket};
        auto magnet=FixturePlant(10,500,300,0);
        magnet.magnetRadius=magnet.magnetEatingRadius=300;
        magnet.magnetRows=1; magnet.magnetRecharge=1000;
        snapshot.plants={magnet,FixturePlant(11,650,100,5)};
        ConstructionStats slow;
        Evaluate(snapshot,{},&slow);
        check(slow.magneticExtractions==1 && slow.jackExplosions==1,
            "the nearest helmet consumes the magnet charge before a more distant jack's tool");
        snapshot.plants[0].magnetRecharge=.5f;
        ConstructionStats fast;
        Evaluate(snapshot,{},&fast);
        check(fast.magneticExtractions==2 && fast.jackExplosions==0,
            "a later real magnet recharge may remove the box while its countdown is still running");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack(true,900); source.jack.remaining=0;
        snapshot.current={source}; snapshot.plants={FixturePlant(10,200,10000,100)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackThrows>1 && stats.jackBoxHits>1 && stats.jackExplosions==0
            && result[3]==24,"elite jack submits repeated independent boxes without sacrificing its own body");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack(true,900); source.jack.remaining=0;
        snapshot.current={source}; snapshot.plants={FixturePlant(10,200,1000,100)};
        snapshot.counters={Ash(900,.5f,1800)};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackThrows==1 && stats.jackBoxHits==0 && result[1]==0 && result[3]==0,
            "immediate ash removal of its carrier cancels the elite box exactly as the formal member-owned transaction");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack(true,900); source.jack.remaining=1000;
        source.jack.stopHealth=900; // 隔离旧在途盒：复活后的新来源不再主动投掷。
        snapshot.current={source}; snapshot.plants={FixturePlant(10,200,1000,100)};
        snapshot.jackBoxes={{200,100,2,false,source.id}};
        snapshot.counters={Ash(900,.5f,1800)};
        TemporalAnchor anchor; anchor.at=1; anchor.targets={{0,source}};
        snapshot.temporalAnchors={anchor};
        ConstructionStats stats;
        Evaluate(snapshot,{},&stats);
        check(stats.clockRevivals==1 && stats.jackBoxHits==0,"reviving the ash-removed carrier cannot revive its cancelled box transaction");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto dead=Jack(true); dead.body.health=0;
        snapshot.current={dead}; snapshot.plants={FixturePlant(10,200,1000,100)};
        snapshot.jackBoxes={{200,100,1,false,dead.id}};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackThrows==0 && stats.jackBoxHits==1 && near(result[1],5),
            "a captured in-flight box continues during the carrier dying animation without starting another throw");
    }
    {
        ++cases;
        auto snapshot=Arena(); auto source=Jack(true,900); source.jack.remaining=1000;
        snapshot.current={source};
        snapshot.jackBoxes={{200,100,.75f,false,source.id}};
        auto first=FixturePlant(10,200,1000,100); first.targetValue=1000;
        auto second=FixturePlant(11,680,1000,100,0,6); second.targetValue=1;
        snapshot.plants={first,second}; snapshot.pendingPrecisionID=10; snapshot.pendingPrecisionRemaining=.5f;
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackThrows==0 && stats.jackBoxHits==0 && near(result[1],first.reward),
            "removing the chosen plant cannot redirect an already thrown box toward a surviving distant cell");
    }
    {
        ++cases;
        auto snapshot=Arena();
        auto under=FixturePlant(10,440,500,500,0,3); under.layer=0;
        auto host=FixturePlant(11,440,500,500,0,3);
        auto shell=FixturePlant(12,440,500,500,0,3); shell.layer=2; shell.pumpkin=true;
        auto adjacent=FixturePlant(13,360,500,500,0,2);
        snapshot.plants={under,host,shell,adjacent}; snapshot.jackBoxes={{440,100,0,false,99}};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackBoxHits==1 && near(result[1],250) && result[0]==0,
            "several layers and a neighboring cell share one pumpkin recipient for the same box event");
    }
    {
        ++cases;
        auto snapshot=Arena();
        auto under=FixturePlant(10,440,500,500,0,3); under.layer=0;
        auto host=FixturePlant(11,440,500,500,0,3);
        auto shell=FixturePlant(12,440,100,100,0,3); shell.layer=2; shell.pumpkin=true;
        snapshot.plants={under,host,shell}; snapshot.jackBoxes={{440,100,0,false,99}};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(stats.jackBoxHits==1 && result[0]==100 && result[1]==100,
            "a pumpkin breaking during one box event does not spill damage into its protected layers");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.houseX=100; snapshot.current={Balloon(40)};
        auto gun=FixturePlant(10,200,10000,0); gun.dps=100000;
        auto thunder=FixturePlant(11,280,10000,0); thunder.thunder=true;
        thunder.dps=ThunderFlowerRules::Damage/ThunderFlowerRules::Interval;
        auto wall=FixturePlant(12,700,1000000,0); wall.edible=true;
        snapshot.plants={gun,thunder,wall}; snapshot.thunderRays={{850,0,{}}};
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(result[2]==1 && stats.thunderStuns==0 && result[0]==0,
            "a flying balloon ignores ground fire and thunder rays and crosses a surviving front wall");
        auto grounded=snapshot; grounded.current[0].balloon.present=false; grounded.current[0].body.health=270;
        grounded.current[0].body.speed=40;
        check(Evaluate(grounded,{})[2]==0,"the same corridor stops an ordinary grounded body with the existing gun");
    }
    {
        ++cases;
        BalloonRules::Forecast balloon; balloon.present=true; balloon.popDuration=1;
        const auto first=balloon.AbsorbDamage(10);
        check(first.absorbed==10 && first.remainder==0 && balloon.health==10
            && balloon.phase==BalloonRules::Phase::FLYING,"partial anti-air hits only spend the surviving extra balloon layer");
        const auto second=balloon.AbsorbDamage(15);
        check(second.absorbed==10 && second.remainder==5 && balloon.phase==BalloonRules::Phase::POPPING,
            "the final anti-air hit removes exactly the remaining balloon health and forwards only overflow");
        check(!balloon.CanTargetProjectile(false) && !balloon.AdvanceLanding(.5f),
            "popping animation is not prematurely treated as a grounded target");
        check(balloon.AdvanceLanding(.5f) && balloon.CanTargetProjectile(false)
            && !balloon.CanTargetProjectile(true),"only completed landing switches projectile target eligibility");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Balloon()};
        auto cactus=FixturePlant(10,200,300,0); cactus.targetsAir=true; cactus.dps=50;
        snapshot.plants={cactus}; snapshot.pendingPrecisionID=10; snapshot.pendingPrecisionRemaining=.5f;
        ConstructionStats stats;
        const auto result=Evaluate(snapshot,{},&stats);
        check(near(result[3],4.0f*265/290) && result[2]==0,
            "one 25-damage cactus step spends the 20-point balloon before five points reach its body");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Balloon()};
        auto cactus=FixturePlant(10,200,300,0); cactus.targetsAir=true; cactus.dps=40;
        auto gun=FixturePlant(11,300,10000,0); gun.dps=100000;
        snapshot.plants={cactus,gun}; snapshot.pendingPrecisionID=10; snapshot.pendingPrecisionRemaining=.5f;
        check(Evaluate(snapshot,{})[3]==0,
            "after the cactus pops the balloon and landing completes ordinary ground fire can kill its body");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Balloon(40)}; snapshot.houseX=100;
        snapshot.counters={Ash(900,0,290)};
        const auto result=Evaluate(snapshot,{});
        check(result[2]==0 && result[3]==0,"sufficient committed ash removes both flying extra health and actual body health");
    }
    {
        ++cases;
        BalloonRules::Forecast balloon; balloon.present=true;
        balloon.BeginBlow(true); balloon.BeginBlow(true);
        check(balloon.blowRemaining==800,"two house-directed winds accumulate two independent 400-pixel displacements");
        float x=900;
        check(!balloon.AdvanceBlow(.5f,x,1100) && x==600 && balloon.blowRemaining==500,
            "house-directed wind spends only the available movement while retaining its committed remainder");
        balloon.BeginBlow(false);
        check(balloon.blowRemaining==0 && !balloon.towardHouse,"front-directed wind replaces the pending house-directed displacement");
        check(balloon.AdvanceBlow(1,x,1100) && x==1180,
            "front-directed wind reports irreversible removal at the explicit right-edge padding");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Balloon()};
        WindCounter committed; committed.plantID=-1; committed.recharge=10000;
        snapshot.windCounters={committed};
        const auto front=Evaluate(snapshot,{});
        check(front[2]==0 && front[3]==0,"an already committed front wind clears the flying unit without awarding a breach");
        snapshot.windCounters[0].house=true;
        const auto house=Evaluate(snapshot,{});
        check(house[3]==4 && near(house[7],2),"one committed house wind moves a live balloon exactly 400 pixels without inventing damage");
        snapshot.windCounters.push_back(snapshot.windCounters[0]); snapshot.houseX=150;
        check(Evaluate(snapshot,{})[2]==1,"two house winds can move a living balloon through the real house boundary");
    }
    {
        ++cases;
        auto snapshot=Arena(); snapshot.current={Balloon()};
        snapshot.plants={FixturePlant(10,200,300,0)};
        WindCounter pending; pending.plantID=10; pending.ready=1; pending.recharge=10000;
        snapshot.windCounters={pending}; snapshot.pendingPrecisionID=10; snapshot.pendingPrecisionRemaining=.5f;
        check(Evaluate(snapshot,{})[3]==4,
            "a still-winding blover needs its living source instead of acting like an independently released elite box");
        snapshot.plants[0].shutdownUntil=2; snapshot.pendingPrecisionRemaining=2.5f;
        check(Evaluate(snapshot,{})[3]==4,
            "shutdown preserves the blover windup so a source removed before its resumed release cannot blow");
    }
    std::cout<<"Jack/balloon "<<cases<<" ability counterfactual cases passed\n";
}
