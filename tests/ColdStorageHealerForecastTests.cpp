#include "Game/AI/ColdStorageHealerForecast.h"
#include "Game/AI/ColdStorageSearch.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
/** 设置明确的三层夹具；总生命与分层字段同步，单位不随测试脚本移动。 */
ColdStorageSearch::Unit Target(int id, float body, float maximum, float x=500)
{
    ColdStorageSearch::Unit unit;
    unit.id=id;
    unit.body.health=body;
    unit.maximumBody=maximum;
    unit.temporalStopHealth=static_cast<int>(maximum)/3;
    unit.body.x=x;
    unit.body.boundsOffset=-25;
    unit.body.boundsWidth=50;
    unit.boundsY=-50;
    unit.boundsHeight=100;
    return unit;
}

/** 就绪的高难度急救员夹具，真实掉头阈值仍阻止低血量施法。 */
ColdStorageSearch::Unit Healer(int id=1, float x=500)
{
    auto unit=Target(id,800,800,x);
    unit.healer.present=true;
    unit.healer.cooldown=0;
    unit.healer.disableBodyHealth=800/3;
    return unit;
}
}

/** 验证有界治疗的提交、层资格、控场计时与独立来源；由统一策略测试入口调用。 */
void RunColdStorageHealerForecastTests()
{
    using namespace ColdStorageSearch;
    using Phase=HealerRules::Forecast::Phase;
    const auto check=[](bool condition,const char* message) {
        if(!condition) { std::cerr<<"FAILED healer forecast: "<<message<<'\n'; std::exit(1); }
    };
    const auto near=[](float left,float right) { return std::abs(left-right)<.001f; };
    Snapshot snapshot;
    snapshot.budget=123;
    snapshot.playerIce=71;
    snapshot.playerSun=250;
    snapshot.rowY={100,200,300,400,500,600};
    {
        auto source=Healer();
        auto target=Target(2,500,1000);
        target.helmHealth=100; target.maximumHelm=1000;
        target.shieldHealth=200; target.maximumShield=1000;
        target.body.health+=300;
        target.body.purchaseCost=20; target.blastCredit=10;
        std::vector<Unit> units{source,target};
        const std::vector<float> initial{800,3000};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(stats.healerCasts==0 && units[0].healer.phase==Phase::FOCUSED,
            "first half of a cast reserves a target without granting premature healing");
        check(units[0].healer.movementActivity==0,"the cast stops both walking and biting");
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(near(units[1].body.health,2000) && near(units[1].helmHealth,500)
            && near(units[1].shieldHealth,600),"focused healing repairs each living layer by 400 independently");
        check(stats.healerCasts==1 && stats.healerRecipients==1 && near(stats.healerAmount,1200),
            "one committed focused treatment records its actual restoration");
        check(near(stats.healerRecoveryCredit,8) && near(units[1].blastCredit,2),
            "restored health withdraws only previously credited blast losses with the same initial denominator");
        check(snapshot.budget==123 && snapshot.playerIce==71 && snapshot.playerSun==250,
            "forecast healing never changes captured wallets or invents resource income");
        units[0].body.health=0;
        AdvanceHealers(snapshot,1,units,initial,stats);
        check(near(units[1].body.health,2000) && stats.healerCasts==1,
            "a healer dying after commitment cannot roll back another unit's restored health");
    }
    {
        auto source=Healer(); source.healer.cooldown=HealerRules::Cooldown;
        std::vector<Unit> units{source,Target(2,600,1000)};
        const std::vector<float> initial{800,1000};
        ConstructionStats stats;
        for(int step=0;step<10;++step) AdvanceHealers(snapshot,step*HealerForecastStep,units,initial,stats);
        check(stats.healerCasts==0 && units[0].healer.phase==Phase::FOCUSED
            && near(units[0].healer.remaining,1),"birth cooldown is retained and a cast starts at its expiry boundary");
        AdvanceHealers(snapshot,5,units,initial,stats);
        check(stats.healerCasts==0,"the one-second cast cannot resolve after only half a second");
        AdvanceHealers(snapshot,5.5f,units,initial,stats);
        check(stats.healerCasts==1 && units[1].body.health==1000,"the actual cooldown plus windup is required before benefits");
    }
    {
        auto source=Healer(); source.healer.phase=Phase::FOCUSED;
        source.healer.remaining=.5f; source.healer.focusedTargetID=2;
        auto worker=Target(2,250,500); worker.body.economic=true;
        worker.body.purchaseCost=10; worker.productionStopHealth=500/3;
        std::vector<Unit> units{source,worker};
        const std::vector<float> initial{800,500};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[1].body.health==500 && near(stats.workerProtectionProgress,5),
            "actual worker healing retains promising cooperation for exploration without creating resource income");
        check(snapshot.budget==123 && snapshot.playerIce==71 && snapshot.playerSun==250,
            "worker recovery progress is never paid into either wallet");
    }
    {
        auto source=Healer();
        source.healer.phase=Phase::FOCUSED; source.healer.remaining=.25f; source.healer.focusedTargetID=2;
        std::vector<Unit> units{source,Target(2,600,1000)};
        const std::vector<float> initial{800,1000};
        ConstructionStats stats;
        units[0].body.stopped=.5f;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(stats.healerCasts==0 && units[0].healer.remaining==.25f,"hard control pauses the preserved treatment timer");
        units[0].body.stopped=0;
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(stats.healerCasts==1 && near(units[0].healer.movementActivity,.5f),
            "a half-step remaining cast resumes movement for exactly the remaining half of the active step");
    }
    {
        auto source=Healer(); source.body.slow=10;
        std::vector<Unit> units{source,Target(2,600,1000)};
        const std::vector<float> initial{800,1000};
        ConstructionStats stats;
        for(int step=0;step<3;++step) AdvanceHealers(snapshot,step*.5f,units,initial,stats);
        check(stats.healerCasts==0,"chill slows the internal cast instead of only reducing movement");
        AdvanceHealers(snapshot,1.5f,units,initial,stats);
        check(stats.healerCasts==1,"a chilled one-second cast resolves after two wall-clock seconds");
    }
    {
        auto source=Healer(); source.healer.phase=Phase::FOCUSED; source.healer.remaining=.5f; source.healer.focusedTargetID=2;
        std::vector<Unit> units{source,Target(2,600,1000)};
        const std::vector<float> initial{800,1000};
        ConstructionStats stats;
        units[0].body.health=260;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(stats.healerCasts==0 && !units[0].healer.enabled && units[1].body.health==600,
            "dropping the source's head cancels an uncommitted treatment");
        units[0]=source; units[0].healer.disableBodyHealth=800*2/3; units[0].body.health=500;
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(!units[0].healer.enabled && stats.healerCasts==0,"the low-difficulty arm-loss gate permanently disables healing earlier");
    }
    {
        auto source=Healer();
        source.healer.phase=Phase::FOCUSED; source.healer.remaining=.5f; source.healer.focusedTargetID=2;
        auto target=Target(2,600,1000); target.maximumHelm=1000; target.maximumShield=1000;
        std::vector<Unit> units{source,target};
        const std::vector<float> initial{800,1000};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[1].body.health==1000 && units[1].helmHealth==0 && units[1].shieldHealth==0,
            "healing cannot recreate helmet or shield layers that have already dropped off");
        units[0]=source; units[1].body.health=300;
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(stats.healerCasts==1 && units[1].body.health==300,"a target that already lost its head cannot be healed back into eligibility");
    }
    {
        std::vector<Unit> units{Healer(),Healer(4),Target(2,600,1000),Target(3,800,1000)};
        const std::vector<float> initial{800,800,1000,1000};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[0].healer.focusedTargetID==2 && units[1].healer.focusedTargetID==3,
            "several healers reserve distinct focused targets rather than stacking the same benefit");
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(stats.healerCasts==2 && units[2].body.health==1000 && units[3].body.health==1000,
            "distinct sources keep independent cooldowns and commitment");
    }
    {
        std::vector<Unit> units{Healer()};
        for(int id=2;id<9;++id) units.push_back(Target(id,400,500));
        const std::vector<float> initial(units.size(),500);
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[0].healer.phase==Phase::AREA,"three or more wounded units select formal deterministic area healing");
        AdvanceHealers(snapshot,.5f,units,initial,stats);
        check(stats.healerRecipients==7 && stats.healerAmount==700,"area healing has no invented target-count cap");
    }
    {
        auto source=Healer(); source.healer.phase=Phase::FOCUSED; source.healer.remaining=.5f; source.healer.focusedTargetID=2;
        auto target=Target(2,200,270); target.temporalStopHealth=0;
        target.balloon.present=true; target.balloon.health=10; target.body.health+=10;
        std::vector<Unit> units{source,target};
        const std::vector<float> initial{800,290};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[1].body.health==280 && units[1].balloon.health==10 && stats.healerAmount==70,
            "balloon health is an unrepairable extra layer and cannot disguise or inflate body healing");
    }
    {
        auto source=Healer(); source.healer.phase=Phase::AREA; source.healer.remaining=.5f;
        auto adjacent=Target(2,400,500,620); adjacent.body.row=1;
        auto boundary=Target(3,400,500,640);
        auto future=Target(4,400,500); future.body.spawnAt=10;
        std::vector<Unit> units{source,adjacent,boundary,future};
        const std::vector<float> initial{800,500,500,500};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(units[1].body.health==400 && units[2].body.health==500 && units[3].body.health==400,
            "circular collider-center geometry excludes a diagonal target and unpaid future presence while retaining the exact radius boundary");
    }
    {
        auto sloped=snapshot;
        for(int column=0;column<sloped.columns;++column) sloped.cellY[column]=100+column*40;
        auto source=Healer(); source.healer.phase=Phase::AREA; source.healer.remaining=.5f;
        std::vector<Unit> units{source,Target(2,400,500,640)};
        const std::vector<float> initial{800,500};
        ConstructionStats stats;
        AdvanceHealers(sloped,0,units,initial,stats);
        check(stats.healerCasts==0 && units[1].body.health==400,
            "roof slope interpolation contributes vertical distance even for targets on the same lane");
    }
    {
        auto source=Healer(); source.healer.phase=Phase::FOCUSED; source.healer.remaining=.5f; source.healer.focusedTargetID=2;
        std::vector<Unit> units{source,Target(2,600,1000,900),Target(3,600,1000)};
        const std::vector<float> initial{800,1000,1000};
        ConstructionStats stats;
        AdvanceHealers(snapshot,0,units,initial,stats);
        check(stats.healerCasts==0 && units[1].body.health==600 && units[2].body.health==600,
            "a focused target leaving range cancels instead of silently retargeting another nearby unit");
    }
    {
        auto battle=snapshot; battle.houseX=-10000;
        auto worker=Target(2,250,500); worker.body.economic=true;
        worker.body.purchaseCost=24; worker.body.speed=worker.biteDps=0;
        worker.productionStopHealth=500/3;
        battle.current={worker};
        Plant attacker; attacker.x=400; attacker.health=attacker.initialHealth=100000;
        attacker.dps=10; attacker.range=1000; attacker.edible=false;
        battle.plants={attacker};
        auto source=Healer(3,650); source.healer.cooldown=HealerRules::Cooldown;
        source.body.speed=source.biteDps=0; source.body.purchaseCost=16;
        Option option; option.unit=source; option.cost=16;
        battle.options={option};
        ConstructionStats stats;
        const auto alone=Evaluate(battle,{});
        const auto supported=Evaluate(battle,{{0,0}},&stats);
        check(stats.healerCasts>0 && stats.healerAmount>0 && supported[4]>alone[4],
            "a purchased healer's real birth cooldown and healing prolong an injured worker's production under ordinary fire");
        check(supported[5]==16 && alone[5]==0,
            "worker/healer cooperation still pays the actual support purchase cost");
    }
    std::cout<<"Healer cooldowns, cast commitment, repair layers, reservations, range and worker production contracts passed\n";
}
