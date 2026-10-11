#include "Game/AI/ColdStorageFormationSeeds.h"
#include "Game/AI/ColdStorageSearch.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {
/** 经验配方只是完整、合法的候选；此处不以配方外观代替真实战果。 */
void CheckRecipe(bool okay,const char* message) {
    if(!okay) {std::cerr<<"FAILED cannon experience: "<<message<<'\n';std::exit(1);}
}
}

/** 总血更高的普通护卫不能挤掉抗灰角色；新配方保留整队资金与缺角色拒绝规则。 */
void RunColdStorageCannonExperienceTests() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=5;s.netEconomy=true;s.budget=500;s.capacity=64;
    enum Role { Heavy=56000,AshShield,Worker,Clock,Drum,Sniper,Ladder,Runner,Catapult };
    for(int row=0;row<s.rows;++row) for(int role=Heavy;role<=Catapult;++role) {
        Option o;o.type=role;o.row=row;o.cost=12;o.unit.body.row=row;o.unit.body.health=1300;
        o.unit.body.speed=20;o.unit.body.x=1140;o.unit.maximumBody=270;
        if(role==Heavy) o.unit.body.health=5000;
        if(role==AshShield) {o.unit.shieldHealth=1030;o.unit.shieldedAshCap=320;}
        if(role==Worker) {o.unit.body.economic=true;o.cost=24;}
        if(role==Clock) {o.unit.clock.present=true;o.cost=18;}
        if(role==Drum) o.unit.drum.enabled=true;
        if(role==Sniper) o.unit.sniper.enabled=true;
        if(role==Ladder) o.unit.ladder.present=true;
        if(role==Runner) o.unit.body.speed=50;
        if(role==Catapult) o.unit.catapult.present=true;
        s.options.push_back(o);
    }
    const auto plans=BuildExperiencedFormations(s,64,500);
    bool income=false,access=false,ranged=false,ordinary=false;
    for(const auto& plan:plans) {
        std::set<int> types,shieldRows;int cost=0;bool later=false;
        for(const auto& a:plan) {
            const auto& o=s.options[a.option];types.insert(o.type);cost+=o.cost;later|=a.delay>0;
            if(o.type==AshShield) shieldRows.insert(o.row);
        }
        CheckRecipe(cost<=500 && plan.size()<=64,"new seeds use the existing complete wallet and member limit");
        ordinary|=types.contains(Heavy) && types.contains(Worker) && !types.contains(AshShield);
        if(shieldRows.size()<2 || !later) continue;
        income|=types.contains(Worker)&&types.contains(Clock)&&types.contains(Sniper);
        access|=types.contains(Ladder)&&types.contains(Runner)&&types.contains(Drum);
        ranged|=types.contains(Catapult)&&types.contains(Clock)&&types.contains(Sniper);
    }
    CheckRecipe(income && access && ranged && ordinary,
        "ash-capped shields keep multi-lane income, ladder and ranged alternatives beside the existing heavy-guard recipes");
    auto noShield=s;
    for(auto& o:noShield.options) o.unit.shieldedAshCap=0;
    for(const auto& plan:BuildExperiencedFormations(noShield,64,500))
        CheckRecipe(std::none_of(plan.begin(),plan.end(),[&](const Action& a){return noShield.options[a.option].type==AshShield;}),
            "raw shield health alone does not pretend to have a per-ash damage cap");
    auto disabled=s;disabled.experiencedFormations=false;
    CheckRecipe(BuildExperiencedFormations(disabled,64,500).empty(),"the existing experience ablation disables the new recipes too");
    for(const auto& plan:BuildExperiencedFormations(s,3,35)) {
        int cost=0;for(const auto& a:plan) cost+=s.options[a.option].cost;
        CheckRecipe(plan.size()<=3 && cost<=35,"unaffordable complete experience groups are not partially admitted");
    }
    CheckRecipe(s.budget==500 && s.options[1].unit.shieldHealth==1030,
        "candidate generation neither prepays units nor modifies their combat profile");
    std::cout<<"Cannon experience preserves ash roles, complete multi-lane groups and optional free competition\n";
}
