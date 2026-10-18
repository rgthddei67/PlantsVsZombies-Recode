#include "Game/AI/ColdStorageSearch.h"
#include "Game/AI/ColdStorageHealerForecast.h"
#include "Game/AI/ColdStorageFormationSeeds.h"
#include "Game/Zombie/PressureGuardRules.h"
#include <algorithm>
#include <set>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <cstring>

namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << std::endl; throw std::runtime_error(message); }
}
bool Near(float a, float b) { return std::abs(a-b)<.01f; }
/** 同位置静止护卫与后排夹具，隔离价格、移动和随机攻击速度。 */
ColdStorageSearch::Unit Guard(int id, int row, bool ability=true) {
    ColdStorageSearch::Unit u;
    u.id=id; u.body.row=row; u.body.x=700; u.body.health=PressureGuardRules::BodyHealth+PressureGuardRules::ShieldHealth; u.helmHealth=u.maximumHelm=PressureGuardRules::ShieldHealth;
    u.maximumBody=600; u.body.value=u.body.purchaseCost=35; u.pressureGuard=ability;
    return u;
}

/** 普通前排在血价比和总血量上都胜出，仍必须给保护光环与合法攻击角色比较协同的机会。 */
void CheckGuardFormations() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=3;s.netEconomy=true;s.searchVersion=2;s.budget=3000;s.capacity=64;s.cellWidth=80;
    for(int row=0;row<s.rows;++row) {
        Plant wall;wall.row=row;wall.x=400;wall.health=1000;wall.edible=true;s.plants.push_back(wall);
        for(int role=0;role<9;++role) {
            Option o;o.type=78000+role;o.row=row;o.cost=8;
            auto& u=o.unit;u.body.row=row;u.body.x=1000;u.body.speed=20;u.biteDps=30;
            u.body.health=u.maximumBody=270;
            if(role==0) {u=Guard(0,row);u.body.x=1000;u.body.speed=10;u.biteDps=30;o.cost=35;}
            if(role==1) {u.body.health=1370;u.helmHealth=u.maximumHelm=1100;}
            if(role==2) {u.body.economic=true;o.cost=24;}
            if(role==3) {u.dance.leader=true;o.cost=12;}
            if(role==4) {u.body.health=u.maximumBody=10000;o.cost=1;}
            if(role==5) {u.pressure=true;o.cost=20;}
            if(role==6) {u.floodMortar=true;o.cost=30;}
            if(role==7) {u.ladder.present=true;u.ladder.placementSeconds=2;o.cost=12;}
            if(role==8) {u.body.speed=50;o.cost=4;}
            u.body.value=u.body.purchaseCost=static_cast<float>(o.cost);s.options.push_back(o);
        }
    }
    const auto hasGuard=[&](const auto& plan) {
        return std::any_of(plan.begin(),plan.end(),[&](const auto& a){return s.options[a.option].unit.pressureGuard;});
    };
    const auto full=BuildExperiencedFormations(s,64,s.budget);
    Check(full.size()<=96 && full.size()>1 && !hasGuard(full.front()) && hasGuard(full[1]),
        "independent guard recipe must enter early without replacing the original first seed");
    std::set<int> partners;bool split=false,delayed=false;int guardPlans=0;
    for(const auto& plan:full) if(hasGuard(plan)) {
        ++guardPlans;std::set<int> routes;
        for(const auto& a:plan) {
            const auto& o=s.options[a.option];routes.insert(o.row);
            if(!o.unit.pressureGuard) {partners.insert(o.type-78000);delayed|=a.delay>0;}
        }
        split|=routes.size()==2;
    }
    Check(guardPlans>=19 && partners==std::set<int>({1,2,3,5,6,7,8}) && split && delayed,
        "guard candidates must cover basic, ranged, mortar, ladder and separated-route cooperation");
    for(int limit:{3,4,8,16}) for(int budget:{34,58,59,118,300}) {
        const auto plans=BuildExperiencedFormations(s,limit,budget);
        for(const auto& plan:plans) {
            int cost=0;for(const auto& a:plan) cost+=s.options[a.option].cost;
            Check(cost<=budget && plan.size()<=static_cast<size_t>(limit),"all recipes must respect full cash and deployment limits");
            if(hasGuard(plan)) Check(plan.size()>=3,"unaffordable guard recipes must not degrade into an isolated guard");
        }
    }
    // 删除装甲伙伴后仍允许独立远程组合，但不能把缺失的基础成员静默跳过。
    auto withoutArmor=s;
    for(auto& o:withoutArmor.options) if(o.type==78001) o.unit.helmHealth=o.unit.maximumHelm=0;
    const auto specialized=BuildExperiencedFormations(withoutArmor,64,s.budget);
    int specializedGuards=0;
    for(const auto& plan:specialized) if(hasGuard(plan)) ++specializedGuards;
    Check(specializedGuards>=9,"missing armored role must retain specialist and coverage teams");
    auto renamed=s;for(auto& o:renamed.options) o.type+=12345;
    const auto renamedPlans=BuildExperiencedFormations(renamed,64,s.budget);
    Check(renamedPlans.size()==full.size(),"guard role detection must not depend on zombie enum values");
    for(size_t i=0;i<full.size();++i) {
        Check(renamedPlans[i].size()==full[i].size(),"renamed roles must keep composition");
        for(size_t j=0;j<full[i].size();++j)
            Check(renamedPlans[i][j].option==full[i][j].option && Near(renamedPlans[i][j].delay,full[i][j].delay),
                "renamed roles must keep candidate order and timing");
    }
    for(auto& o:s.options) o.unit.pressureGuard=false;
    const auto disabled=BuildExperiencedFormations(s,64,s.budget);
    for(const auto& plan:disabled) for(const auto& a:plan)
        Check(s.options[a.option].type!=78000,"ordinary armor must not impersonate a missing protection ability");
    s.experiencedFormations=false;
    Check(BuildExperiencedFormations(s,64,s.budget).empty(),"experienced candidate switch must still disable all families");
}

/** 验证多路投篮掩护能进入紧预算候选，且缺角色、缺钱或缺名额时不会产生残缺配方。 */
void CheckGuardCatapultFormations() {
    using namespace ColdStorageSearch;
    Snapshot s;s.rows=5;s.netEconomy=true;s.searchVersion=2;s.budget=295;s.capacity=64;
    for(int row=0;row<s.rows;++row) {
        Option guard;guard.type=801;guard.row=row;guard.cost=35;guard.unit=Guard(0,row);s.options.push_back(guard);
        Option car;car.type=802;car.row=row;car.cost=12;car.unit.catapult.present=true;
        car.unit.body.row=row;car.unit.body.health=850;s.options.push_back(car);
    }
    const auto isConvoy=[&](const auto& plan,int rows,float gap) {
        if(plan.size()!=static_cast<size_t>(rows*3)) return false;
        std::array<int,6> guards{},cars{};std::set<int> routes;
        for(const auto& a:plan) {
            const auto& o=s.options[a.option];routes.insert(o.row);
            if(!Near(a.delay,o.row*gap)) return false;
            if(o.unit.pressureGuard) ++guards[o.row];else if(o.unit.catapult.present) ++cars[o.row];else return false;
        }
        if(routes.size()!=static_cast<size_t>(rows)) return false;
        for(int row:routes) if(guards[row]!=1 || cars[row]!=2) return false;
        return true;
    };
    const auto plans=BuildExperiencedFormations(s,64,s.budget);
    Check(plans.size()>1 && isConvoy(plans[1],5,8),"all-route staggered convoy must enter the early search candidates");
    Check(std::any_of(plans.begin(),plans.end(),[&](const auto& p){return isConvoy(p,3,8);}),
        "separated routes must retain a complete staggered convoy");
    for(int limit:{8,9,14,15}) for(int budget:{58,176,177,294,295}) {
        for(const auto& plan:BuildExperiencedFormations(s,limit,budget)) {
            int cost=0;for(const auto& a:plan) cost+=s.options[a.option].cost;
            Check(cost<=budget && plan.size()<=static_cast<size_t>(limit),"convoys must fit the full wallet and capacity");
            Check(std::all_of(plan.begin(),plan.end(),[](const auto& a){return a.delay>=0 && a.delay<=60;}),
                "convoys must respect the paid queue delay range");
        }
    }
    for(auto& o:s.options) if(o.row!=0) o.unit.catapult.present=false;
    const auto oneRoute=BuildExperiencedFormations(s,64,s.budget);
    Check(std::none_of(oneRoute.begin(),oneRoute.end(),[&](const auto& p){return isConvoy(p,5,8);}),
        "missing legal catapult roles must not be fabricated");
}
}

/** 检查真正滚动窗口、保存余时以及完整战斗预测中的保护反事实。 */
void RunColdStoragePressureGuardTests()
{
    using namespace ColdStorageSearch;
    CheckGuardFormations();
    CheckGuardCatapultFormations();
    // 邻行护卫共同保护单次打击；只给队友计价，以终点资产独立核对其剩余伤害。
    for (int count : {1,2,3,8}) {
        Snapshot stacked; stacked.rows=3; stacked.houseX=-10000;
        Unit ally; ally.id=1; ally.body.row=0; ally.body.x=700;
        ally.body.health=ally.maximumBody=3000; ally.body.purchaseCost=20;
        stacked.current.push_back(ally);
        float remaining=1000;
        for(int i=0; i<count; ++i) {
            auto g=Guard(10+i,1);g.body.x=680+i;g.body.value=g.body.purchaseCost=0;stacked.current.push_back(g);
            const float transferred=remaining*(i==0 ? .8f : .5f);
            remaining-=transferred;
        }
        const float expected=20*(1-remaining/3000);
        Plant source; source.id=100;source.row=2;source.x=300;source.health=10000;
        stacked.plants={source};stacked.rowStrikes={{100,0,10000,1000,0,0}};
        Check(Near(Evaluate(stacked,{})[3],expected),
            "ordinary forecast must apply every guard's diminishing protection to the ally");
    }
    // 两版预测中，车道内的护卫与受保护队友都被处决；相邻未被碾到的护卫不能替它们承伤。
    for (int version : {1,2}) {
        Snapshot mower; mower.rows=3; mower.houseX=-10000; mower.searchVersion=version;
        auto protector=Guard(1,1); protector.body.x=680;
        auto peer=Guard(2,0);
        Unit ally; ally.id=3; ally.body.row=2; ally.body.x=700;
        ally.body.health=ally.maximumBody=3000; ally.body.value=ally.body.purchaseCost=20;
        for (bool exhausted : {false,true}) {
            if (exhausted) protector.pressureWindow.Consume(600,0);
            mower.current={protector,peer,ally};
            mower.mowers={{0,650,100,230,false,true},{2,650,100,230,false,true}};
            auto control=mower; control.current={protector}; control.mowers.clear();
            Check(Evaluate(mower,{})==Evaluate(control,{}),
                "mowers must execute protected peers and allies without charging the adjacent guard");
            mower.mowers.push_back({1,650,100,230,false,true});
            Check(Near(Evaluate(mower,{})[3],0),"mower execution must ignore the guard's own full or spent damage window");
        }
    }
    PressureGuardRules::DamageWindow window;
    Check(Near(window.Consume(200,0),200) && Near(window.Consume(200,.25),150),"pressure shared budget must cap at 350");
    Check(Near(window.Consume(3000,.99),0),"pressure cap cannot reset at a frame or fixed boundary");
    auto saved=window.RelativeTo(.5);
    Check(Near(saved.Used(0),350) && Near(saved.Consume(600,.5),200),"saved pressure window must keep independent hit expiries");
    Check(Near(saved.Consume(600,.75),150),"rolling pressure window must release only expired damage");

    Snapshot s; s.rows=3; s.columns=9; s.cellWidth=80; s.gridLeft=242;
    s.current={Guard(1,1)};
    Unit worker; worker.id=2; worker.body.row=0; worker.body.x=730; worker.body.health=worker.maximumBody=3000;
    worker.body.economic=true; worker.body.value=worker.body.purchaseCost=20; s.current.push_back(worker);
    Plant p; p.row=0; p.x=300; p.health=100000; p.dps=1000; p.hitDamage=1000; p.range=1000;
    s.plants.push_back(p);
    const auto protectedResult=Evaluate(s,{});
    s.current[0].pressureGuard=false;
    const auto exposedResult=Evaluate(s,{});
    Check(protectedResult[4]>exposedResult[4],"pressure guard must extend the adjacent worker's actual forecast income");
    s.current[0].pressureGuard=true; s.current[0].body.stopped=300;
    const auto controlledResult=Evaluate(s,{});
    Check(controlledResult==protectedResult,"hard-controlled guard must keep protecting adjacent allies");
    s.current[0].ferryPassengerEligible=false;
    Check(Evaluate(s,{})==protectedResult,"kelp-bound guard must keep protecting adjacent allies");

    // 用完整爆炸事务检查护卫先死、保护不叠加、下一爆失去保护及防具先承伤。
    Snapshot ash; ash.rows=3; ash.columns=9; ash.cellWidth=80; ash.houseX=-10000; ash.traceCombat=true;
    auto guard=Guard(10,1); guard.body.health=300; guard.helmHealth=100;
    Unit ally; ally.id=11; ally.body.row=0; ally.body.x=730; ally.body.health=ally.maximumBody=1000;
    ash.current={guard,ally};
    Counter blast; blast.source=0; blast.blast.committed=true; blast.blast.ready=1;
    blast.blast.x=700; blast.blast.reach.fill(1000); blast.blast.damage=1800;
    ash.counters={blast};
    const auto impacts=[&](const Snapshot& world, int target) {
        ConstructionStats trace; Evaluate(world,{},&trace);
        std::vector<float> result;
        for(const auto& event:trace.combatTrace) if(std::strcmp(event.event,"counter_impact")==0)
            for(const auto& hit:event.targets) if(hit.id==target) result.push_back(hit.after);
        return result;
    };
    auto protectedHits=impacts(ash,11);
    Check(!protectedHits.empty() && Near(protectedHits[0],640),"same-blast guard death must still reduce ally ash to 360");
    auto guardHits=impacts(ash,10);
    Check(!guardHits.empty() && Near(guardHits[0],0),"allowed ash must still kill a guard with less than 350 total durability");
    auto secondGuard=guard; secondGuard.id=12; ash.current.insert(ash.current.begin(),secondGuard);
    Check(Near(impacts(ash,11)[0],820),"second guard must halve the remaining ash even when both die in that blast");
    for (int count : {3,4,8}) {
        auto stacked=ash; stacked.current.clear();
        for(int i=0; i<count; ++i) {auto g=guard;g.id=100+i;stacked.current.push_back(g);}
        stacked.current.push_back(ally);
        Check(Near(impacts(stacked,11)[0],1000-PressureGuardRules::ReduceAsh(1800,count)),
            "all valid guards must contribute to the frozen ash count without a two-guard ceiling");
    }
    ash.current={guard,ally}; ash.current[0].body.stopped=300;
    Check(Near(impacts(ash,11)[0],640),"hard control must preserve ash protection");
    ash.current[0].ferryPassengerEligible=false;
    Check(Near(impacts(ash,11)[0],640),"kelp binding must preserve ash protection");
    ash.current[0]=guard; ash.current[0].helmHealth=0;
    Check(Near(impacts(ash,11)[0],0),"broken guard must not protect from ash");
    ash.current[0]=guard; ash.current[1].body.x=781;
    Check(Near(impacts(ash,11)[0],0),"out-of-range allies must take full ash");
    ash.current={guard,ally}; auto later=blast; later.source=1; later.blast.ready=2; ash.counters.push_back(later);
    const auto repeated=impacts(ash,11);
    Check(repeated.size()==2 && Near(repeated[0],640) && Near(repeated[1],0),"a later blast must not reuse dead guard protection");
    ash.counters={blast}; ash.current[1].helmHealth=1100; ash.current[1].body.health=1370; ash.current[1].ashBodyThreshold=true;
    Check(Near(impacts(ash,11)[0],1010),"protected bucket must keep its 270 body and lose only 360 helmet health");
    ash.current[1].helmHealth=200; ash.current[1].body.health=470;
    Check(Near(impacts(ash,11)[0],110),"broken helmet must pass only its 160 overflow to the body");
    ash.current[1].helmHealth=90; ash.current[1].body.health=360;
    Check(Near(impacts(ash,11)[0],0),"ash overflow equal to remaining body must still kill");
    ash.current[1].helmHealth=1100; ash.current[1].body.health=1370; ash.current[0].helmHealth=0;
    ash.counters[0].blast.damage=500;
    Check(Near(impacts(ash,11)[0],0),"unprotected ash must retain its ordinary armor-bypassing char threshold");
    ash.current[0]=guard; ash.counters[0].blast.damage=1800;
    ash.current[1].helmHealth=0; ash.current[1].shieldHealth=1100;
    Check(Near(impacts(ash,11)[0],1010),"protected screen door must absorb ash before its body too");

    // 大额灰烬也只消费护卫自己的共享滚动额度，不因同次多爆或化灰快路径绕过。
    Snapshot capped=ash; capped.current={Guard(10,1)};
    auto huge=blast; huge.blast.damage=10000; huge.blast.ready=0;
    auto overlap=huge; overlap.source=1; overlap.blast.ready=.5f;
    auto expired=huge; expired.source=2; expired.blast.ready=1;
    capped.counters={huge,overlap,expired};
    const auto cappedHits=impacts(capped,10);
    const float guardHealth=PressureGuardRules::BodyHealth+PressureGuardRules::ShieldHealth;
    Check(cappedHits.size()==3 && Near(cappedHits[0],guardHealth-350) && Near(cappedHits[1],guardHealth-350) && Near(cappedHits[2],guardHealth-700),
        "ash must share a rolling 350 budget and release only expired damage");
    capped.current[0].body.health=guardHealth-200; capped.current[0].helmHealth=PressureGuardRules::ShieldHealth-200;
    capped.current[0].pressureWindow.Consume(200,0);
    Check(impacts(capped,10)==cappedHits,"ordinary damage already paid must reduce the same ash allowance");

    // 没有可治本体损伤时，急救员不为不可修复的装置施法。
    std::vector<Unit> units{Guard(1,1)}; units[0].helmHealth=1000; units[0].body.health=1600;
    Unit healer; healer.id=2; healer.body.row=1; healer.body.x=700; healer.body.health=healer.maximumBody=800;
    healer.healer.present=true; healer.healer.cooldown=0; units.push_back(healer);
    ConstructionStats stats; std::vector<float> initial{guardHealth,800};
    AdvanceHealers(s,0,units,initial,stats);
    Check(units[1].healer.phase==HealerRules::Forecast::Phase::IDLE && Near(units[0].helmHealth,1000),
        "healer forecast must not repair pressure armor or spend a cast on it");
}
