#include "Game/AI/ColdStorageObservedForecast.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

/** 独立行为世界只延后公开、当前合法的付费反制，且持续窗口不能被分段或重规划续期。 */
void RunColdStorageObservedForecastTests()
{
    using namespace ColdStorageSearch;
    using namespace ColdStorageObservedForecast;
    const auto check=[](bool okay,const char* message) {
        if(!okay) {std::cerr<<"FAILED observed forecast: "<<message<<'\n';std::exit(1);}
    };
    Snapshot s;s.rows=3;s.columns=9;s.gridLeft=160;s.cellWidth=80;s.cellHeight=100;
    s.rowY={150,250,350,0,0,0};
    for(int row=0;row<s.rows;++row) for(int col=0;col<s.columns;++col) s.cellY[row*s.columns+col]=s.rowY[row];
    Option option;option.cost=4;option.unit.responseNominalCapital=4;s.options.push_back(option);
    Unit ordinary;ordinary.id=1;ordinary.body.row=1;ordinary.body.x=900;ordinary.body.health=270;
    ordinary.responseNominalCapital=4;ordinary.responseObjectYKnown=true;
    ordinary.responseCobTargetKnown=ordinary.responseCobTargetable=true;
    std::vector<Unit> units{ordinary};
    std::vector<Plant> plants;
    Counter cherry;cherry.source=0;cherry.observedSourceKey=1;cherry.sunCost=150;cherry.iceCost=20;
    cherry.cellRow=1;cherry.cellColumn=8;cherry.blast.x=840;cherry.blast.damage=1800;
    cherry.ladderClearRow=1;cherry.ladderClearRadius=1;
    std::vector<Counter> counters{cherry};
    std::vector<float> ready{0};
    std::vector<Block> blocks;
    std::vector<ColdStorageStrategy::BlastThreat> pending;
    std::array<float,54> fog{};
    std::vector<unsigned char> certified;
    float time=0,sun=150,ice=20;
    const auto inspect=[&] {
        View view{s,units,plants,counters,ready,time,sun,ice,blocks,pending,&fog,certified};
        return Inspect(view);
    };
    const auto baseline=inspect();
    check(baseline.eligible && baseline.context.readyPaidAreaMask==1 && baseline.context.learnablePaidAreaMask==1,
        "ready paid card has an exact visible legal and learnable target");
    check(baseline.directPaidCounters.size()==1 && baseline.directPaidCounters[0],"source permission also identifies the exact legal candidate");
    check(baseline.context.exposedCapitalBand==0 && baseline.context.coveredCapitalBands[0]==0,
        "nominal prices enter public bands without creating paid assets");
    sun=149;check(inspect().context.readyPaidAreaMask==0,"no future sunshine can fund current opportunity");sun=150;
    ice=19;check(inspect().context.readyPaidAreaMask==0,"current ice must cover actual payment");ice=20;
    ready[0]=1;check(!inspect().eligible,"cooldown source is unavailable");ready[0]=0;
    s.discountRemaining=10;ice=10;check(inspect().eligible,"active real discount applies to current payment");
    time=10;check(!inspect().eligible,"expired discount cannot finance a future cast");time=0;ice=20;s.discountRemaining=0;
    blocks.push_back({1,8,2});check(!inspect().eligible,"active crater blocks direct placement");
    time=2;check(inspect().eligible,"expired block permits the originally legal cell");time=0;blocks.clear();
    Plant blocker;blocker.id=17;blocker.row=1;blocker.column=8;blocker.layer=1;blocker.x=840;blocker.health=300;
    plants.push_back(blocker);check(!inspect().eligible,"shovelAllowed cannot pretend occupied ground is empty");plants.clear();
    auto occupiedAim=cherry;occupiedAim.cellColumn=7;occupiedAim.blast.x=760;occupiedAim.shovelAllowed=true;
    counters.push_back(occupiedAim);auto occupied=blocker;occupied.column=7;occupied.x=760;plants.push_back(occupied);
    const auto mixedPlacement=inspect();
    check(mixedPlacement.directPaidCounters[0] && !mixedPlacement.directPaidCounters[1],
        "one direct opportunity cannot suppress another same-source shovel candidate");
    counters={cherry};plants.clear();
    s.plants.push_back(blocker);check(!inspect().eligible,"unknown newly vacated terrain is conservatively excluded");
    certified={1};check(inspect().eligible,"explicit current direct legality admits a safely vacated cell");certified.clear();s.plants.clear();
    auto support=blocker;support.layer=0;s.plants.push_back(support);
    check(!inspect().eligible,"lost required support cannot be ignored");plants.push_back(support);
    check(inspect().context.readyPaidAreaMask==1,"surviving original support preserves direct legality");plants.clear();s.plants.clear();

    units[0].body.x=1101;check(!inspect().visibleHostiles,"offscreen unit cannot teach public patience");units[0]=ordinary;
    units[0].responseObjectYOffset=400;check(!inspect().visibleHostiles,"known offscreen object Y is filtered");units[0]=ordinary;
    fog[1*s.columns+8]=97;check(!inspect().visibleHostiles,"dense fog is not public target position");fog.fill(0);
    units[0].body.spawnAt=1;check(!inspect().visibleHostiles,"unborn paid queue is not a current opportunity");units[0]=ordinary;
    units[0].body.eating=1;auto urgent=inspect();
    check(!urgent.eligible && urgent.context.readyPaidAreaMask==1,"urgent censoring preserves the true legal-source clock");units[0]=ordinary;
    units[0].healer.present=true;check(!inspect().eligible,"complex ability never borrows another unit's patience history");units[0]=ordinary;
    units[0].responseNominalCapital=0;check(!inspect().eligible && inspect().context.readyPaidAreaMask==1,
        "unknown nominal value disables optimism without renewing an available source");units[0]=ordinary;
    ColdStorageStrategy::BlastThreat incoming;incoming.x=900;incoming.reach.fill(-1);incoming.reach[1]=115;
    incoming.responseArea={ColdStorageResponse::AreaKind::Cob,1,900,250};
    pending.push_back(incoming);auto censored=inspect();
    check(censored.censored && !censored.eligible && censored.context.readyPaidAreaMask==1
        && !censored.context.learnablePaidAreaMask && !censored.directPaidCounters[0],
        "already committed covering shell censors evidence but preserves source age");pending.clear();

    // 一炮覆盖A行不应抹去远B行的机会；同来源仅覆盖A的另一个落点仍不得延期。
    auto a=ordinary;a.body.row=0;
    auto b=ordinary;b.id=2;b.body.row=2;
    units={a,b};
    auto aimA=cherry;aimA.cellRow=0;
    auto aimB=cherry;aimB.cellRow=2;
    counters={aimA,aimB};
    incoming.responseArea={ColdStorageResponse::AreaKind::Cob,0,900,150};pending={incoming};
    const auto separate=inspect();
    check(separate.eligible && separate.censored && separate.context.readyPaidAreaMask==1
        && separate.context.learnablePaidAreaMask==1 && !separate.directPaidCounters[0] && separate.directPaidCounters[1],
        "one committed shot on A preserves the uncovered B opportunity but not the covered-only aim");
    check(separate.context.exposedCapitalBand==1 && separate.context.uncoveredCapitalBand==0,
        "partial coverage records full and uncovered total capital separately");
    counters={cherry};const auto mixedBlast=inspect();
    check(mixedBlast.eligible && mixedBlast.directPaidCounters[0] && mixedBlast.context.coveredCapitalBands[0]==1
        && mixedBlast.context.uncoveredCoveredCapitalBands[0]==0,
        "one mixed explosion retains all victims in full capital while only residual victims teach tolerance");
    ColdStorageResponse::Profile partialProfile{mixedBlast.context,4,.333333f,1,1,false};
    units[0].responseNominalCapital=32;
    check(!ColdStorageResponse::Supports(partialProfile,inspect().context),
        "increasing covered capital cannot hide behind an unchanged residual target");
    units={a,b};
    pending.push_back(incoming);pending.back().responseArea={ColdStorageResponse::AreaKind::Cob,2,900,350};
    const auto bothCovered=inspect();
    check(!bothCovered.eligible && !bothCovered.context.learnablePaidAreaMask && bothCovered.context.readyPaidAreaMask==1,
        "separately committed shots covering every target leave no learning witness");
    pending={incoming};units[0].responseCobTargetKnown=false;
    check(inspect().unknownGeometry && !inspect().eligible,"unknown target susceptibility cannot invent partial coverage");
    units={a,b};pending[0].responseArea={};
    check(inspect().unknownGeometry && !inspect().eligible,"unknown pending shape cannot be replaced by its coarse reach envelope");
    pending.clear();units={ordinary};counters={cherry};

    Counter free=cherry;free.source=1;free.plantID=9;free.consumesPlant=false;free.sunCost=free.iceCost=0;
    free.cellRow=free.cellColumn=-1;
    auto cannon=blocker;cannon.id=9;cannon.row=0;cannon.column=0;cannon.x=200;cannon.health=5000;
    plants.push_back(cannon);counters.push_back(free);counters.push_back(free);ready.push_back(0);
    check(inspect().context.readyFreeAreaCount==1 && inspect().context.readyPaidAreaMask==1,
        "many aim points count one free cannon and never add a paid source");
    counters={free};check(inspect().context.readyPaidAreaMask==0,"free cannon cannot be deferred even with an accidental source key");
    plants.clear();counters={cherry};ready={0};
    counters[0].blast.committed=true;check(inspect().context.readyPaidAreaMask==0,"committed paid attack is never deferred");counters[0]=cherry;
    counters[0].observedSourceKey=65;check(!inspect().eligible,"unknown source key cannot borrow a card's evidence");counters[0]=cherry;

    auto distant=ordinary;distant.id=2;distant.body.x=580;units.push_back(distant);
    auto left=cherry;left.cellColumn=4;left.blast.x=520;counters.push_back(left);
    check(inspect().context.coveredCapitalBands[0]==0,"separate target clusters retain one-unit maximum coverage");
    units[1].body.x=910;check(inspect().context.coveredCapitalBands[0]==1,
        "all possible placements and all targets are searched for true maximum clustered coverage");
    units={ordinary};counters={cherry};
    counters[0].clearsLadderRow=true;units[0].body.x=1080;
    check(inspect().eligible,"jalapeno uses entire logical row rather than cherry radius");
    units[0].body.row=0;check(!inspect().eligible,"jalapeno does not cover adjacent logical row");units={ordinary};
    counters[0]=cherry;counters[0].clearsCell=true;counters[0].cellColumn=5;counters[0].blast.x=600;
    units[0].body.x=875;check(inspect().eligible,"Doom includes its exact circle-to-object-rectangle boundary");
    units[0].body.x=875.1f;check(!inspect().eligible,"Doom excludes position beyond actual radius");units[0].body.x=875;
    units[0].responseObjectYKnown=false;check(!inspect().eligible,"unknown objectY does not invent Doom geometry");units[0].responseObjectYKnown=true;
    counters[0].sharedSource=1;ready.push_back(1);check(!inspect().eligible,"daytime Doom requires ready coffee source");
    ready[1]=0;check(inspect().eligible,"ready shared coffee permits the validated combined opportunity");
    counters={cherry};ready={0};units={ordinary};

    ColdStorageResponse::Forecast history;history.sourceExposureSeconds.fill(-1);history.sourceExposureSeconds[0]=3;
    ColdStorageResponse::Profile profile{baseline.context,8,.5f,2,2,false};
    WorldClock clock(history);clock.Update(0,baseline);
    check(clock.SourceAge(1)==3 && clock.DeferredSources(profile,baseline)==1,"world starts with real observed source age");
    auto changed=baseline;changed.context.economicPresent=true;
    clock.Update(1,changed);check(clock.SourceAge(1)==4,"context change does not restart continuous opportunity");
    clock.Update(2,censored);check(clock.SourceAge(1)==5 && !clock.DeferredSources(profile,censored),
        "pending censoring cannot pause or renew the continuous source deadline");
    clock.Update(5,baseline);check(clock.SourceAge(1)==8 && !clock.DeferredSources(profile,baseline),
        "observed H expires exactly once per continuous opportunity");
    history.sourceExposureSeconds[0]=8;WorldClock replanned(history);replanned.Update(0,baseline);
    check(!replanned.DeferredSources(profile,baseline),"replanning cannot grant an already elapsed fresh H");
    auto absent=baseline;absent.context.readyPaidAreaMask=absent.context.learnablePaidAreaMask=0;absent.eligible=false;
    clock.Update(6,absent);clock.Update(7,baseline);
    check(clock.SourceAge(1)==0 && clock.DeferredSources(profile,baseline)==1,"truly lost then renewed opportunity starts at zero");
    auto packed=baseline;packed.context.coveredCapitalBands[0]=1;clock.Update(7,packed);
    check(!clock.DeferredSources(profile,packed),"more concentrated force cannot inherit dispersed experience");
    clock.Update(6,baseline);check(!clock.DeferredSources(profile,baseline),"clock rollback fails closed for the entire world");

    history.sourceExposureSeconds[0]=1;WorldClock partialClock(history);
    partialClock.Update(0,mixedBlast);
    check(partialClock.DeferredSources(partialProfile,mixedBlast)==1,"matching partial coverage can authorize the residual witness");
    partialClock.Update(1,bothCovered);partialClock.Update(2,mixedBlast);
    check(partialClock.SourceAge(1)==3 && partialClock.DeferredSources(partialProfile,mixedBlast)==1,
        "all-covered interval advances legal source age even though learning is censored");
    partialClock.Update(3,mixedBlast);
    check(!partialClock.DeferredSources(partialProfile,mixedBlast),"coverage cannot extend the learned four-second deadline");
}
