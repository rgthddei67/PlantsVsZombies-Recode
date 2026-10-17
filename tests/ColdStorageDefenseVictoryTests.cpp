#include "Game/AI/ColdStorageSearch.h"
#include <cmath>
#include <stdexcept>

namespace {
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}

/** 对照同一局面的撤军截止、零返冰部队击杀与指挥官狙击，禁止认输后继续虚构收益。 */
void RunColdStorageDefenseVictoryTests() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=1;s.searchVersion=2;s.houseX=-10000;s.troopNoKillSeconds=355;
    Unit worker;worker.id=1;worker.body.row=0;worker.body.x=900;
    worker.body.health=worker.maximumBody=5000;worker.body.economic=true;worker.body.purchaseCost=24;
    worker.biteDps=0;worker.productionRemaining=10;s.current={worker};
    ConstructionStats ended;const auto cutoff=Evaluate(s,{},&ended);
    Check(ended.defenseVictorySeconds>=5 && ended.defenseVictorySeconds<5.5f && cutoff[4]==0 && cutoff[3]==0,
        "defense victory must cut off future production and remove remaining troop assets without refunds");
    auto unrestricted=s;unrestricted.troopNoKillSeconds=-1;
    Check(Evaluate(unrestricted,{})[4]>0,"counterfactual worker must otherwise produce beyond the deadline");
    Plant p;p.id=7;p.row=0;p.x=880;p.health=p.initialHealth=1;p.edible=true;p.reward=0;s.plants={p};
    Unit biter=worker;biter.id=2;biter.body.economic=false;biter.biteDps=100;s.current.push_back(biter);
    ConstructionStats extended;const auto wonTime=Evaluate(s,{},&extended);
    Check(extended.defenseVictorySeconds<0 && wonTime[4]>0,"even zero-reward troop kills must reset the whole defense clock");
    s.current.pop_back();s.precisionTargetID=7;s.troopNoKillSeconds=350;
    ConstructionStats skill;Evaluate(s,{},&skill);
    Check(skill.precisionHits==1 && skill.defenseVictorySeconds>=10 && skill.defenseVictorySeconds<10.5f,
        "commander precision kill must not extend the defense deadline");
    s.precisionTargetID=0;s.troopNoKillSeconds=360;
    ConstructionStats immediate;const auto alreadyEnded=Evaluate(s,{},&immediate);
    Check(immediate.defenseVictorySeconds==0 && alreadyEnded[4]==0,"expired saves must not grant an extra forecast step");
}
