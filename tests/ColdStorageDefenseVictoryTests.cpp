#include "Game/AI/ColdStorageSearch.h"
#include <cmath>
#include <stdexcept>

namespace {
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}

/** 对照同一局面的撤军截止、零返冰部队击杀与指挥官狙击，禁止认输后继续虚构收益。 */
void RunColdStorageDefenseVictoryTests() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=1;s.searchVersion=2;s.houseX=-10000;s.troopNoKillSeconds=235;
    Unit worker;worker.id=1;worker.body.row=0;worker.body.x=900;
    worker.body.health=worker.maximumBody=5000;worker.body.economic=true;worker.body.purchaseCost=24;
    worker.biteDps=0;worker.productionRemaining=10;s.current={worker};
    ConstructionStats ended;const auto cutoff=Evaluate(s,{},&ended);
    Check(ended.defenseVictorySeconds>=5 && ended.defenseVictorySeconds<5.5f && cutoff[4]==0 && cutoff[3]==0,
        "defense victory must cut off future production and remove remaining troop assets without refunds");
    auto late=s;late.defenseWave=15;late.troopNoKillSeconds=175;
    ConstructionStats lateEnded;Evaluate(late,{},&lateEnded);
    Check(lateEnded.defenseVictorySeconds>=5 && lateEnded.defenseVictorySeconds<5.5f,
        "wave 15 must surrender after 120 idle seconds plus the unchanged 60-second final assault");
    auto boundary=s;boundary.defenseWave=14;boundary.troopNoKillSeconds=175;
    ConstructionStats waiting;Evaluate(boundary,{},&waiting);
    Check(waiting.defenseVictorySeconds<0 || waiting.defenseVictorySeconds>=65,
        "waiting on wave 14 must retain the early deadline");
    Option reinforcement;reinforcement.type=74002;reinforcement.unit=worker;boundary.options={reinforcement};
    ConstructionStats purchased;Evaluate(boundary,{{0,0}},&purchased);
    Check(purchased.defenseVictorySeconds>=5 && purchased.defenseVictorySeconds<5.5f,
        "buying wave 15 troops must use the shorter deadline without resetting elapsed idle time");
    boundary.budget=10;boundary.capacity=4;boundary.options[0].cost=10;
    FollowupPolicy followup;followup.actions={{0,0}};followup.earliestSeconds=2;followup.expirySeconds=3;
    ConstructionStats later;
    Evaluate(boundary,{},&later,0,0,0,false,false,-1,false,false,false,false,&followup);
    Check(later.followup.status==FollowupForecast::Status::Purchased
        && later.defenseVictorySeconds>=5 && later.defenseVictorySeconds<5.5f,
        "a later wave 15 purchase must shorten the same running clock only when actually paid");
    auto unrestricted=s;unrestricted.troopNoKillSeconds=-1;
    Check(Evaluate(unrestricted,{})[4]>0,"counterfactual worker must otherwise produce beyond the deadline");
    Plant p;p.id=7;p.row=0;p.x=880;p.health=p.initialHealth=1;p.edible=true;p.reward=0;s.plants={p};
    Unit biter=worker;biter.id=2;biter.body.economic=false;biter.biteDps=100;s.current.push_back(biter);
    ConstructionStats extended;const auto wonTime=Evaluate(s,{},&extended);
    Check(extended.defenseVictorySeconds<0 && wonTime[4]>0,"even zero-reward troop kills must reset the whole defense clock");
    s.current.pop_back();s.precisionTargetID=7;s.troopNoKillSeconds=230;
    ConstructionStats skill;Evaluate(s,{},&skill);
    Check(skill.precisionHits==1 && skill.defenseVictorySeconds>=10 && skill.defenseVictorySeconds<10.5f,
        "commander precision kill must not extend the defense deadline");
    s.precisionTargetID=0;s.troopNoKillSeconds=240;
    ConstructionStats immediate;const auto alreadyEnded=Evaluate(s,{},&immediate);
    Check(immediate.defenseVictorySeconds==0 && alreadyEnded[4]==0,"expired saves must not grant an extra forecast step");
    // 同样亏冰的普通进攻，在共同预测窗内等待将认输时优先比较真实部队击杀；不凭出兵或生产续期。
    Snapshot last;last.rows=1;last.searchVersion=2;last.capacity=3;last.budget=100;last.houseX=-10000;
    last.troopNoKillSeconds=230;
    p.x=880;p.health=p.initialHealth=1;p.reward=0;last.plants={p};
    Option attack;attack.type=74001;attack.cost=10;attack.unit=biter;attack.unit.body.purchaseCost=10;
    last.options={attack};
    const Weights expense{0,0,0,0,0,-1,0,0};
    const auto active=Search(last,expense,42);
    Check(!active.actions.empty() && active.score<0 && active.construction.defenseVictorySeconds<0,
        "a real troop kill avoiding imminent surrender must beat a zero-score waiting loss");
    last.troopNoKillSeconds=170;
    const auto preparing=Search(last,expense,42);
    Check(!preparing.actions.empty() && preparing.score<0 && preparing.construction.defenseVictorySeconds<0,
        "a forecast waiting loss must not defer an effective assault until the final sixty seconds");
    last.troopNoKillSeconds=0;
    Check(Search(last,expense,42).actions.empty(),"without an imminent loss the same uneconomic purchase must still wait");
    last.troopNoKillSeconds=230;last.options[0].unit.biteDps=0;
    Check(Search(last,expense,42).actions.empty(),"merely spawning guards or workers cannot manufacture an escape from surrender");
}
