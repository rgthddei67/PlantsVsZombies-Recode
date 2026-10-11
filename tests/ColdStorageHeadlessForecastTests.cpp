#include "Game/AI/ColdStorageSearch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
using namespace ColdStorageSearch;

void CheckHeadless(bool condition,const char* message)
{
    if(!condition) { std::cerr<<"FAILED headless forecast: "<<message<<'\n'; std::exit(1); }
}

bool Near(float left,float right) { return std::abs(left-right)<.001f; }

/** 保留真实本体与掉头阈值；零移速隔离啃食、流血、经济和反制价值。 */
Unit Body(float health,float maximum=270)
{
    Unit unit;
    unit.id=71; unit.body.row=0; unit.body.x=500; unit.body.health=health;
    unit.body.speed=0; unit.body.purchaseCost=12;
    unit.maximumBody=maximum; unit.temporalStopHealth=static_cast<int>(maximum)/3;
    unit.productionStopHealth=unit.temporalStopHealth;
    return unit;
}

/** 本体270加门盾1030，不以总生命1300充当本体流血上限。 */
Unit Door()
{
    auto unit=Body(1300);
    unit.shieldHealth=unit.maximumShield=1030;
    return unit;
}

Snapshot World()
{
    Snapshot state;
    state.rows=1; state.houseX=-10000; state.traceEconomy=true;
    Plant wall;
    wall.id=81; wall.row=0; wall.x=500; wall.health=10000; wall.reward=100; wall.assetValue=100;
    state.plants={wall};
    return state;
}

/** 已离膛的反制不需来源、资源或新选择资格。 */
Counter Committed(float damage,float at=0)
{
    Counter hit;
    hit.blast.committed=true; hit.blast.ready=at; hit.blast.damage=damage;
    hit.blast.x=500; hit.blast.reach.fill(-1); hit.blast.reach[0]=100;
    return hit;
}

/** 免费炮与付费牌共享命中几何，只有真实费用、来源寿命和门槛不同。 */
Counter Reusable()
{
    auto hit=Committed(1800);
    hit.blast.committed=false; hit.source=1; hit.plantID=81;
    hit.consumesPlant=false; hit.windup=.5f; hit.flightSeconds=.25f; hit.recharge=30;
    return hit;
}

float TracedHealth(const ConstructionStats& stats,float at)
{
    const auto found=std::find_if(stats.workerTrace.begin(),stats.workerTrace.end(),[&](const auto& entry) {
        return Near(entry.at,at) && entry.income==0;
    });
    CheckHeadless(found!=stats.workerTrace.end(),"the requested deterministic game-time trace exists");
    return found->health;
}
}

/** 反事实只跑固定游戏时步；验证死亡语义、低价值补刀及恢复，不依赖搜索墙钟或随机胜率。 */
void RunColdStorageHeadlessForecastTests()
{
    using namespace ColdStorageSearch;
    {
        auto state=World();
        auto door=Door(); door.shieldedAshCap=320; state.current={door};
        state.counters.assign(4,Committed(1800));
        ConstructionStats stats;
        const auto result=Evaluate(state,{},&stats);
        CheckHeadless(result[0]==0 && result[1]==0 && result[3]==0,
            "four capped hits leaving a 20-health door neither keep a surviving asset nor allow endless chewing");
        CheckHeadless(Near(result[6],12.0f*1280/1300) && stats.counterTrace.size()==4,
            "natural headless death does not invent a fifth shot or credit remaining blood as blast damage");
        CheckHeadless(state.current[0].body.health==1300 && state.current[0].shieldHealth==1030
            && !state.current[0].headless && state.plants[0].health==10000,
            "forecast damage and death leave the captured real world unchanged");
    }
    {
        auto state=World();
        auto unit=Body(80); unit.body.economic=true; unit.productionRemaining=0;
        state.current={unit};
        ConstructionStats stats;
        const auto result=Evaluate(state,{},&stats);
        CheckHeadless(TracedHealth(stats,0)==67 && TracedHealth(stats,2)==0,
            "an 80-health headless body survives its first 13-point bleed and later reaches the formal death threshold");
        CheckHeadless(result[1]==0 && result[4]==0 && result[3]==0,
            "a headless worker cannot chew or produce while bleeding and cannot remain as a final asset");
        auto frozen=state; frozen.current[0].body.stopped=1000; frozen.current[0].body.slow=1000;
        ConstructionStats controlled;
        Evaluate(frozen,{},&controlled);
        CheckHeadless(TracedHealth(controlled,0)==67 && TracedHealth(controlled,2)==0,
            "hard control and chill do not slow the unscaled game-time bleeding clock");
        auto scaled=state; scaled.current[0]=Body(300,1000); scaled.current[0].body.economic=true;
        Evaluate(scaled,{},&stats);
        CheckHeadless(TracedHealth(stats,0)==250 && TracedHealth(stats,2)==50 && TracedHealth(stats,4)==0,
            "scaled bodies bleed at ten percent of their own maximum without being instantly killed at head loss");
    }
    {
        auto state=World(); auto unit=Body(80); unit.body.economic=true;
        unit.shieldHealth=1000; unit.helmHealth=200; unit.body.health+=1200;
        state.current={unit};
        ConstructionStats stats;
        const auto result=Evaluate(state,{},&stats);
        CheckHeadless(TracedHealth(stats,0)==1267 && TracedHealth(stats,2)==0 && result[1]==0 && result[3]==0,
            "remaining helmet and shield neither hide a decapitated body nor absorb its bleeding");
        auto headed=World(); headed.current={Body(91)};
        const auto intact=Evaluate(headed,{});
        CheckHeadless(intact[1]>0 && intact[3]>0,
            "a body above the integer one-third threshold keeps its head and chewing ability");
        auto noDrop=World(); noDrop.current={Body(20)}; noDrop.current[0].temporalStopHealth=0;
        const auto exempt=Evaluate(noDrop,{});
        CheckHeadless(exempt[1]>0 && exempt[3]>0,
            "zero drop-head threshold preserves the existing dedicated lifetime of non-decapitating units");
    }
    {
        auto state=World(); auto wounded=Body(80); wounded.body.economic=true;
        wounded.productionRemaining=0; state.current={wounded};
        auto saved=wounded; saved.body.health=270;
        TemporalAnchor anchor; anchor.at=1; anchor.targets.push_back({0,saved,true,true,false});
        state.temporalAnchors={anchor};
        ConstructionStats stats;
        auto result=Evaluate(state,{},&stats);
        CheckHeadless(stats.clockRewinds==1 && stats.clockRevivals==0 && result[4]>0 && result[1]>0 && result[3]>0,
            "living core rewind restores the saved head and resumes production and chewing without an ability rewind");
        state.current[0].body.health=20; state.current[0].playerRefund=9; state.temporalAnchors[0].at=5;
        result=Evaluate(state,{},&stats);
        CheckHeadless(stats.clockRewinds==1 && stats.clockRevivals==1 && result[4]>0 && result[3]>0,
            "an independent anchor revives a body that naturally bled out and restores its headed core");
        auto delayed=World(); auto pending=Body(80); pending.body.economic=true; pending.body.spawnAt=59.5f;
        delayed.current={pending};
        const auto future=Evaluate(delayed,{});
        CheckHeadless(Near(future[3],12.0f*67/80),
            "a future queued body receives only its single active half-second of bleeding");
    }
    {
        auto state=World(); state.current={Door()}; state.current[0].biteDps=0;
        state.counters={Committed(1200),Reusable()};
        ConstructionStats stats;
        const auto free=Evaluate(state,{},&stats);
        CheckHeadless(stats.counterTrace.size()==2 && stats.paidCounterCasts==0 && Near(free[6],12) && free[3]==0,
            "a free reusable cannon finishes a still-headed 100-health survivor worth less than one ice");
        auto paid=state; paid.playerSun=500; paid.counters[1].consumesPlant=true;
        paid.counters[1].plantID=0; paid.counters[1].sunCost=100;
        const auto area=Evaluate(paid,{},&stats);
        CheckHeadless(stats.counterTrace.size()==1 && stats.paidCounterCasts==0 && area[3]>0,
            "paid area ash retains its original minimum-value threshold");
        paid.counters[1].targeted=true;
        const auto targeted=Evaluate(paid,{},&stats);
        CheckHeadless(stats.counterTrace.size()==1 && stats.paidCounterCasts==0 && targeted[3]>0,
            "paid targeted ash retains its original lower but nonzero minimum-value threshold");
        auto covered=state; covered.counters.insert(covered.counters.begin()+1,Committed(100,1));
        Evaluate(covered,{},&stats);
        CheckHeadless(stats.counterTrace.size()==2,
            "an already committed lethal shell still prevents a duplicate free cannon shot");
        auto unfunded=paid; unfunded.playerSun=0; unfunded.current[0].body.purchaseCost=1000;
        Evaluate(unfunded,{},&stats);
        CheckHeadless(stats.counterTrace.size()==1 && stats.paidCounterCasts==0,
            "a valuable target never bypasses a paid counter's actual wallet requirement");
    }
    std::cout<<"Headless lifetime, restored core and sub-one-ice reusable counter contracts passed\n";
}
