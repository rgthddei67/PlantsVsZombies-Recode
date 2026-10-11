#include "Game/AI/ColdStorageSearch.h"
#include "Game/Plant/PlantType.h"
#include "Game/Zombie/ZombieBirthVitalsRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace ColdStorageSearch;
constexpr int kCannonID=901; // 夹具中已有炮体的稳定实体身份
constexpr int kJackCost=18; // Board::GetZombieIceCost 的精英小丑成交冰价
constexpr int kNormalCost=4; // Board::GetZombieIceCost 的普通僵尸成交冰价
constexpr float kFixtureSpeed=20; // 隔离入屏时序的合成位移，px/秒；不是实体移速的替代配置
constexpr float kFollowupAt=4.5f; // 真实付款队列在旧即时炮落地后出生，游戏秒

/** 只检查完整候选的因果差异，不给自由搜索指定必买阵容或必胜结论。 */
void Check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED visible counter response: "<<message<<'\n';std::exit(1);}
}
bool Near(float left,float right) {return std::abs(left-right)<.001f;}
bool Named(const CombatForecastTrace& event,const char* name) {return std::strcmp(event.event,name)==0;}

/** 保留权威出生生命层与单口伤害，只把几何、位移和死亡返款从本次反例中隔离。 */
Unit Body(int id,ZombieType type,int cost,float x) {
    const auto vitals=ZombieBirthVitalsRules::Get(type);
    Unit unit;unit.id=id;unit.body.row=0;unit.body.x=x;unit.body.speed=kFixtureSpeed;
    unit.body.health=static_cast<float>(vitals.Total());unit.maximumBody=static_cast<float>(vitals.body);
    unit.helmHealth=unit.maximumHelm=static_cast<float>(vitals.helm);
    unit.shieldHealth=unit.maximumShield=static_cast<float>(vitals.shield);
    unit.temporalStopHealth=static_cast<float>(vitals.body/3);
    unit.body.purchaseCost=static_cast<float>(cost);unit.biteDps=static_cast<float>(vitals.bite);
    unit.body.boundsOffset=-25;unit.body.boundsWidth=50;unit.boundsY=-50;unit.boundsHeight=100;
    unit.playerRefund=0; // 本夹具不投影死亡返款，两个反事实共用同一画像与钱包。
    return unit;
}

/** 新生精英小丑的计时来自 Board 画像使用的共用 Rules，绝不把活体短余时冒充出生余时。 */
Unit BirthJack() {
    auto unit=Body(0,ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX,kJackCost,1140);
    unit.jack.present=unit.jack.elite=true;
    unit.jack.remaining=(JackBoxRules::ThrowMin+JackBoxRules::ThrowMax)*.5f;
    unit.jack.stopHealth=unit.temporalStopHealth;
    return unit;
}

/** 炮体/炮击沿当前 gamedata 与 CobCannon 的数值契约；唯一植物隔离精英盒择点。 */
Snapshot WindowArena(float visibleAt=2) {
    Snapshot state;state.rows=1;state.searchVersion=2;state.houseX=-10000;
    state.traceCombat=true;state.rowY={100,200,300,400,500,600};
    state.budget=6*kJackCost;state.capacity=6;
    Plant cannon;cannon.id=kCannonID;cannon.actualType=static_cast<int>(PlantType::PLANT_COBCANNON);
    cannon.row=0;cannon.column=0;cannon.footprintColumns=2;cannon.x=200;cannon.y=100;
    cannon.health=cannon.maximumHealth=300;cannon.boundsX=-30;cannon.boundsY=-35;
    cannon.boundsWidth=140;cannon.boundsHeight=80;cannon.targetValue=500;
    cannon.reward=40*(4+2)/4.0f; // Board::PlantKillIce，夹具显式采用难度4的返冰。
    cannon.assetValue=40+500*4.0f/9;
    cannon.edible=false; // 本例只比较盒与炮的远程事务，不把抵达后啃咬混入时序证据。
    state.plants={cannon};
    Counter shot;shot.source=0;shot.plantID=kCannonID;shot.consumesPlant=false;
    shot.windup=4;shot.flightSeconds=2;shot.recharge=34.83f;
    shot.blast.x=1100;shot.blast.damage=1800;shot.blast.usesObjectX=true;
    shot.blast.reach.fill(-1);shot.blast.reach[0]=115;shot.ladderClearRow=0;
    state.counters={shot};
    state.current={Body(21,ZombieType::ZOMBIE_NORMAL,kNormalCost,1100+kFixtureSpeed*visibleAt)};
    Option jack;jack.type=static_cast<int>(ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX);
    jack.row=0;jack.cost=kJackCost;jack.unit=BirthJack();state.options={jack};
    return state;
}

std::vector<Action> Followup() {return std::vector<Action>(6,{0,kFollowupAt});}
struct Forecast {Weights features;ConstructionStats stats;};

/** 独立重放一种完整响应，只改变见证或留火规则，保留同一输入和购物车。 */
Forecast Replay(const Snapshot& original,const std::vector<Action>& plan,bool visible,float hold=0,float stored=0) {
    auto state=original;state.diagnosticVisibleCounterWitness=visible;
    Forecast result;result.features=Evaluate(state,plan,&result.stats,hold,stored,std::max(hold,stored));
    return result;
}

/** 找到来源实际提交的第一炮；未开炮时返回负值，避免用最终分数冒充时序证据。 */
float FirstLock(const ConstructionStats& stats,int source=kCannonID) {
    for(const auto& event:stats.combatTrace) if(Named(event,"counter_lock") && event.sourceID==source) return event.at;
    return -1;
}

/** 返回第一发实际爆炸的受害者数；未出生的队列成员必须不在其中。 */
std::size_t FirstImpactTargets(const ConstructionStats& stats) {
    for(const auto& event:stats.combatTrace)
        if(Named(event,"counter_impact") && event.sourceID==kCannonID) return event.targets.size();
    return 0;
}

/** 将已付款部队移入快照而不改能力；可选保留小丑能力，隔离 current 与 options 的菜单资格。 */
void AddCommittedFollowup(Snapshot& state,bool eliteJack) {
    for(int i=0;i<6;++i) {
        auto unit=state.options.front().unit;unit.id=100+i;unit.body.spawnAt=kFollowupAt;
        if(!eliteJack) unit.jack={};
        state.current.push_back(unit);
    }
}

/** 两路互不重叠的一次性付费灰烬；候选拥有独立冷却，仍须竞争同一份阳光和冰。 */
void AddPaidAshCompetition(Snapshot& state) {
    state.rows=5;state.sunIceValue=1;state.playerSun=150;state.playerIce=20;
    for(int lane=0;lane<2;++lane) {
        const int row=2+lane*2;
        for(int i=0;i<6;++i) {
            auto unit=Body(200+lane*10+i,ZombieType::ZOMBIE_NORMAL,kNormalCost,700);
            unit.body.row=row;unit.body.speed=0;state.current.push_back(unit);
        }
        Counter ash;ash.source=1+lane;ash.sunCost=150;ash.iceCost=20;ash.recharge=50;
        ash.windup=13/(30*.45f); // CherryBomb::GetMinimumChargeDuration 的正式最短引爆等待。
        ash.blast.x=700;ash.blast.damage=1800;ash.blast.usesObjectX=true;
        ash.blast.reach.fill(-1);ash.blast.reach[row]=115;ash.ladderClearRow=row;
        state.counters.push_back(ash);
    }
}

/** 不依赖内存布局或 padding，检查此反例涉及的输入事务在多次完整推演后保持原样。 */
void CheckSourceUnchanged(const Snapshot& state,float visibleAt) {
    Check(state.visibleCounterResponse && !state.diagnosticVisibleCounterWitness
        && state.current.front().body.x==1100+kFixtureSpeed*visibleAt
        && state.current.front().body.health==ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_NORMAL).body
        && state.options.front().unit.id==0 && state.options.front().unit.body.health==900
        && Near(state.options.front().unit.jack.remaining,(JackBoxRules::ThrowMin+JackBoxRules::ThrowMax)*.5f)
        && state.plants.front().health==300 && state.counters.front().blast.ready==0
        && !state.counters.front().blast.committed && state.budget==6*kJackCost && state.capacity==6,
        "response evaluation cannot mutate source positions, vitals, jack timers, cannon cooldown, wallet or capacity");
}
}

/** 新世界须真的改变固定候选的结果，同时保留旧世界、同能力等待和实际付款边界。 */
void RunColdStorageVisibleCounterTests() {
    {
        // 旧0秒炮只打到前兵；8秒炮等到首盒破炮才落地；32秒炮的来源先被破坏。
        // 1..3秒入屏后开炮则在后队首盒前清场，且从未依赖“必须进屋/获胜”的断言。
        for(float visibleAt:{1.0f,2.0f,3.0f}) {
            const auto state=WindowArena(visibleAt);const auto plan=Followup();
            const auto immediate=Replay(state,plan,false);
            const auto patient=Replay(state,plan,false,8);
            const auto longHold=Replay(state,plan,false,8,32);
            const auto visible=Replay(state,plan,true);
            Check(Near(FirstLock(immediate.stats),0) && FirstImpactTargets(immediate.stats)==1,
                "legacy immediate fire spends a real shot on the first body before the paid followup is born");
            Check(Near(FirstLock(visible.stats),visibleAt) && FirstImpactTargets(visible.stats)==7,
                "the visible response waits for the 1-3 second screen entry and catches the complete later cohort");
            Check(immediate.stats.jackBoxHits>0 && patient.stats.jackBoxHits>0 && longHold.stats.jackBoxHits>0
                && visible.stats.jackThrows==0 && visible.stats.jackBoxHits==0,
                "a short visibility window is behaviorally different from both immediate and fixed 8/32 second holds");
            auto legacyState=state;legacyState.visibleCounterResponse=false;
            const auto legacy=EvaluateCandidate(legacyState,InitialWeights,plan);
            const auto selected=EvaluateCandidate(state,InitialWeights,plan);
            Check(selected.construction.visibleCounterResponse && !legacy.construction.visibleCounterResponse
                && selected.features==visible.features && selected.score<legacy.score
                && selected.features[0]<legacy.features[0] && selected.features[1]<legacy.features[1],
                "EvaluateCandidate can select the complete visible world and remove the old false jack damage/kill return");
            Check(selected.features[2]==0 && legacy.features[2]==0 && selected.features[5]==6*kJackCost
                && selected.actions.size()==plan.size() && selected.counterHoldSeconds==0,
                "the improvement neither manufactures a breach nor changes purchased units, price or spawn schedule");
            for(std::size_t i=0;i<plan.size();++i)
                Check(selected.actions[i].option==plan[i].option && selected.actions[i].delay==plan[i].delay,
                    "response selection preserves each original paid deployment time");
            CheckSourceUnchanged(state,visibleAt);
        }
    }
    {
        auto state=WindowArena();AddCommittedFollowup(state,false);
        // 已付队列这里没有投盒能力；仅可购选项有精英小丑，也必须给空案同一玩家菜单。
        Check(std::none_of(state.current.begin(),state.current.end(),[](const Unit& u){return u.jack.elite;}),
            "the waiting-menu counterexample has no elite jack in its current army");
        auto old=state;old.visibleCounterResponse=false;
        const auto wait=EvaluateCandidate(state,InitialWeights,{});
        const auto oldWait=EvaluateCandidate(old,InitialWeights,{});
        const auto purchase=EvaluateCandidate(state,InitialWeights,Followup());
        Check(wait.actions.empty() && wait.construction.visibleCounterResponse && wait.features[3]==0
            && oldWait.features[3]>0 && wait.score<oldWait.score,
            "an unpurchased elite-jack option gives waiting the same stronger legal visible response");
        Check(purchase.baselineFeatures==wait.features && Near(purchase.baselineOpponentAssets,wait.opponentAssets),
            "candidate marginal accounting uses the exact independently evaluated same-menu waiting world");
        auto currentOnly=WindowArena();AddCommittedFollowup(currentOnly,true);currentOnly.options.clear();
        Check(EvaluateCandidate(currentOnly,InitialWeights,{}).construction.visibleCounterResponse,
            "existing elite jacks also enable the response when no unit can currently be bought");
    }
    {
        auto state=WindowArena();AddPaidAshCompetition(state);const auto plan=Followup();
        const auto one=EvaluateCandidate(state,InitialWeights,plan);
        auto rich=state;rich.playerSun=300;rich.playerIce=40;
        const auto two=EvaluateCandidate(rich,InitialWeights,plan);
        auto shortSun=state;--shortSun.playerSun;
        const auto unfunded=EvaluateCandidate(shortSun,InitialWeights,plan);
        Check(one.construction.visibleCounterResponse && two.construction.visibleCounterResponse
            && !one.construction.paidDefensesRetained && one.construction.paidCounterCasts==1
            && two.construction.paidCounterCasts==2 && unfunded.construction.paidCounterCasts==0,
            "the selected visible world may pay for ash, but two ready sources cannot duplicate one shared wallet");
        Check(Near(one.opponentAssets,state.plants.front().assetValue)
            && Near(two.opponentAssets,rich.plants.front().assetValue)
            && Near(unfunded.opponentAssets,shortSun.plants.front().assetValue+shortSun.playerSun+shortSun.playerIce)
            && Near(one.features[3]-two.features[3],6*kNormalCost)
            && Near(unfunded.features[3]-one.features[3],6*kNormalCost),
            "actual casts debit their full sun/ice cost and only the funded lane is cleared");
        Check(state.playerSun==150 && state.playerIce==20 && state.counters[1].blast.ready==0
            && state.counters[2].blast.ready==0 && one.features[5]==6*kJackCost,
            "player response costs do not change either captured wallet, cooldown or commander purchase cost");
        CheckSourceUnchanged(state,2);
    }
    {
        // 消融的是能力，不是名字/价格：没有精英盒，或只有一次性灰烬，都不得扩大菜单。
        for(int absent=0;absent<3;++absent) {
            auto state=WindowArena();
            if(absent==0) state.options.front().unit.jack={};
            if(absent==1) state.counters.clear();
            if(absent==2) state.counters.front().consumesPlant=true;
            auto disabled=state;disabled.visibleCounterResponse=false;
            const auto enabled=EvaluateCandidate(state,InitialWeights,Followup());
            const auto legacy=EvaluateCandidate(disabled,InitialWeights,Followup());
            Check(!enabled.construction.visibleCounterResponse && enabled.features==legacy.features
                && enabled.baselineFeatures==legacy.baselineFeatures && Near(enabled.score,legacy.score)
                && Near(enabled.opponentAssets,legacy.opponentAssets)
                && enabled.construction.jackBoxHits==legacy.construction.jackBoxHits,
                "missing elite-box or reusable-cannon capability leaves the full candidate and wait results unchanged");
        }
        auto explicitWitness=WindowArena();explicitWitness.diagnosticVisibleCounterWitness=true;
        auto noAutomatic=explicitWitness;noAutomatic.visibleCounterResponse=false;
        const auto explicitResult=EvaluateCandidate(explicitWitness,InitialWeights,Followup());
        const auto explicitAblated=EvaluateCandidate(noAutomatic,InitialWeights,Followup());
        Check(explicitResult.features==explicitAblated.features
            && explicitResult.baselineFeatures==explicitAblated.baselineFeatures
            && Near(explicitResult.score,explicitAblated.score)
            && explicitResult.construction.combatTrace.size()==explicitAblated.construction.combatTrace.size(),
            "an explicit witness diagnostic retains its existing worlds without adding an observable extra response");
    }
    std::cout<<"Visible counter response preserves complete worlds, short entry windows, fair waiting and shared paid wallets\n";
}
